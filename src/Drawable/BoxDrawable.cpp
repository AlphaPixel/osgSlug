#include "osgSlug/Drawable/BoxDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <osg/Depth>

namespace osgSlug {

void BoxDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN
			<< "BoxDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl
		;

		return;
	}

	if(!atlas->isBuilt()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	_layers.clear();
	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;
	osg::ref_ptr<RenderMask> groupMask;
	RenderMask* lastRepacked = nullptr; // dedupes repack() across consecutive shared-mask layers

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices, groupMask});
	};

	struct FaceGeom { Vec3 p0, p1, p2, p3; };

	auto s = _size * 0.5_cv;

	const FaceGeom faceGeoms[6] = {
		{{-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}}, // +Z front
		{{ s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s}}, // -Z back
		{{ s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s}}, // +X right
		{{-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s}}, // -X left
		{{-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s}}, // +Y top
		{{-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s}}  // -Y bottom
	};

	// Small outward push per within-face layer index -- avoids z-fighting between layers
	// stacked on the exact same plane (e.g. a background plate under a pip/numeral overlay).
	const slug_t NORMAL_PUSH_STEP = 0.0005_cv * _size;

	for(size_t face = 0; face < 6; face++) {
		const auto& fg = faceGeoms[face];
		const auto& composite = _faces[face];

		if(composite.layers.empty()) continue;

		Vec3 eU = fg.p1 - fg.p0;
		Vec3 eV = fg.p3 - fg.p0;
		Vec3 normal = eU ^ eV;

		normal.normalize();

		osg::ref_ptr<RenderMask> mask;

		if(composite.mask) mask = new RenderMask(*composite.mask, RENDER_MASK_UBO_BINDING);

		for(size_t li = 0; li < composite.layers.size(); li++) {
			const auto& layer = composite.layers[li];
			const auto shape = atlas->getShape(layer.key);

			if(!shape) continue;

			if(!groupIndices || layer.blendMode != groupBlend || mask.get() != groupMask.get()) {
				flushGroup();
				groupIndices = osgx::make_ref<index_type>();
				groupBlend = layer.blendMode;
				groupMask = mask;
			}

			const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape);
			const slug_t lidx = cv(_layers.size() + 1);
			const Vec3 push = normal * (NORMAL_PUSH_STEP * cv(li));

			Vec3 ps[4] = {fg.p0 + push, fg.p1 + push, fg.p2 + push, fg.p3 + push};
			slug_t uvs[4][2] = {{0_cv, 0_cv}, {1_cv, 0_cv}, {1_cv, 1_cv}, {0_cv, 1_cv}};

			auto base = static_cast<index_element_type>(vertices->size());

			for(size_t i = 0; i < 4; i++) {
				vertices->push_back(Vec4(ps[i].x(), ps[i].y(), ps[i].z(), lidx));

				slug_t emX = emX0 + uvs[i][0] * (emX1 - emX0);
				slug_t emY = emY0 + uvs[i][1] * (emY1 - emY0);

				emCoords->push_back({emX, emY, uvs[i][0], uvs[i][1]});
			}

			groupIndices->append_range({
				base, index_element_type(base + 1), index_element_type(base + 2),
				base, index_element_type(base + 2), index_element_type(base + 3)
			});

			const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
			const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

			auto layerBuf = osgx::make_ref<osgx::Vec4Array>();

			layerBuf->push_back({layer.color.r, layer.color.g, layer.color.b, layer.color.a});
			layerBuf->push_back(gmeta);
			layerBuf->push_back(gxform);
			layerBuf->push_back({
				cv(layer.effectId),
				shapeIdx,
				cv(packMSDFData(shape->msdfLayer, shape->msdfRange)),
				layer.effectParam
			});
			layerBuf->push_back({layer.transform.x, layer.transform.y, layer.bleed, 0_cv});

			// [5]/[6] quad frame for SHADER_VERT's live margin push: each box face lies in its
			// own 3D plane, so the em axes are the face's actual edge directions (NOT world XY),
			// and worldPerEm is edge length over em span.
			Vec3 axisU = eU;
			Vec3 axisV = eV;
			const slug_t spanU = emX1 - emX0;
			const slug_t spanV = emY1 - emY0;
			const slug_t rateU = spanU > 0_cv ? cv(eU.length()) / spanU : 1_cv;
			const slug_t rateV = spanV > 0_cv ? cv(eV.length()) / spanV : 1_cv;

			axisU.normalize();
			axisV.normalize();

			layerBuf->push_back({cv(axisU.x()), cv(axisU.y()), cv(axisU.z()), rateU});
			layerBuf->push_back({cv(axisV.x()), cv(axisV.y()), cv(axisV.z()), rateV});

			layerBuf->setBufferObject(ssbo);

			if(mask && mask.get() != lastRepacked) {
				mask->repack(*atlas);
				lastRepacked = mask.get();
			}

			_layers.push_back({layer, layerBuf, mask});
		}
	}

	flushGroup();

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layers.size() * 7 * sizeof(Vec4));

	auto* ss = getOrCreateStateSet();

	// Only when hooks were actually requested -- otherwise inherit the Atlas parent's own program
	// rather than duplicating it per child. Same rule as ShapeDrawable::compile().
	if(!_hooks.empty()) ss->setAttributeAndModes(
		Atlas::createDefaultProgram(_hooks),
		osg::StateAttribute::ON
	);

	ss->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layers[0].buffer, 0, totalSize),
		osg::StateAttribute::ON
	);

	// Atlas::createDefaultStateSet() turns depth testing OFF ambiently (right for slughorn's
	// usual 2D/HUD layering, where draw order -- not the z-buffer -- decides composite order).
	// A BoxDrawable is real opaque 3D solid geometry with 6 mutually-occluding faces, so it needs
	// both the test AND the write back on -- unlike PathDrawable/ScanlineDrawable's depth-test-
	// only override, which stay translucent overlays that must not occlude each other.
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, true), osg::StateAttribute::ON);

	_compiled = true;
}

}
