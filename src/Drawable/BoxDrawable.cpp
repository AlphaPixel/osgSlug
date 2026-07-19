#include "osgSlug/Drawable/BoxDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>

namespace osgSlug {

namespace {

static void pushEmptySlot(osgx::Vec4Array& buf) {
	buf.append_range({
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv),
		Vec4(0_cv, 0_cv, 0_cv, 0_cv)
	});
}

}

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

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<index_type>();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	// Layer SSBO (binding 1): one Vec4Array per face, each holding 5 vec4s -- see
	// ShapeDrawable::compile() for the slot layout this mirrors. Always reserves a slot per
	// face, even if the shape lookup fails, so lidx == index+1 always addresses the right slot.
	auto addFace = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, size_t index) {
		auto& rs = _layers[index];
		const auto& layer = rs.layer;
		const auto shape = atlas->getShape(layer.key);
		auto layerBuf = osgx::make_ref<osgx::Vec4Array>();

		if(shape) {
			const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape);
			const slug_t lidx = cv(index + 1);

			auto base = static_cast<index_element_type>(vertices->size());

			Vec3 ps[4] = {p0, p1, p2, p3};
			slug_t uvs[4][2] = {{0_cv, 0_cv}, {1_cv, 0_cv}, {1_cv, 1_cv}, {0_cv, 1_cv}};

			for(size_t i = 0; i < 4; i++) {
				vertices->push_back(Vec4(ps[i].x(), ps[i].y(), ps[i].z(), lidx));

				slug_t emX = emX0 + uvs[i][0] * (emX1 - emX0);
				slug_t emY = emY0 + uvs[i][1] * (emY1 - emY0);

				emCoords->push_back({emX, emY, uvs[i][0], uvs[i][1]});
			}

			indices->append_range({
				base, index_element_type(base + 1), index_element_type(base + 2),
				base, index_element_type(base + 2), index_element_type(base + 3)
			});

			const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
			const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

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
			Vec3 eU = p1 - p0;
			Vec3 eV = p3 - p0;
			const slug_t spanU = emX1 - emX0;
			const slug_t spanV = emY1 - emY0;
			const slug_t rateU = spanU > 0_cv ? cv(eU.length()) / spanU : 1_cv;
			const slug_t rateV = spanV > 0_cv ? cv(eV.length()) / spanV : 1_cv;

			eU.normalize();
			eV.normalize();

			layerBuf->push_back({cv(eU.x()), cv(eU.y()), cv(eU.z()), rateU});
			layerBuf->push_back({cv(eV.x()), cv(eV.y()), cv(eV.z()), rateV});
		}
		else {
			pushEmptySlot(*layerBuf);
		}

		layerBuf->setBufferObject(ssbo);
		rs.buffer = layerBuf;
	};

	auto s = _size * 0.5_cv;

	addFace({-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}, 0); // +Z front
	addFace({ s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s}, 1); // -Z back
	addFace({ s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s}, 2); // +X right
	addFace({-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s}, 3); // -X left
	addFace({-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s}, 4); // +Y top
	addFace({-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s}, 5); // -Y bottom

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	addPrimitiveSet(indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layers.size() * 7 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layers[0].buffer, 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

}
