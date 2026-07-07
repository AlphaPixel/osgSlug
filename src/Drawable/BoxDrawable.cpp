#include "osgSlug/Drawable/BoxDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>

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

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto indices = osgx::make_ref<index_type>();

	_layerBuffers.clear();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	// Layer SSBO (binding 1): one Vec4Array per face, each holding 5 vec4s -- see
	// ShapeDrawable::compile() for the slot layout this mirrors.
	auto addFace = [&](Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, size_t index) {
		const auto& layer = _layers[index];
		const auto shape = atlas->getShape(layer.key);

		if(!shape) return;

		const slug_t expand = layer.expand;
		const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape, expand);
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
		layerBuf->push_back({layer.transform.x, layer.transform.y, 0_cv, 0_cv});
		layerBuf->setBufferObject(ssbo);

		_layerBuffers.push_back(std::move(layerBuf));
	};

	auto s = _size * 0.5_cv;

	addFace({-s,-s, s}, { s,-s, s}, { s, s, s}, {-s, s, s}, 0); // +Z front
	addFace({ s,-s,-s}, {-s,-s,-s}, {-s, s,-s}, { s, s,-s}, 1); // -Z back
	addFace({ s,-s, s}, { s,-s,-s}, { s, s,-s}, { s, s, s}, 2); // +X right
	addFace({-s,-s,-s}, {-s,-s, s}, {-s, s, s}, {-s, s,-s}, 3); // -X left
	addFace({-s, s, s}, { s, s, s}, { s, s,-s}, {-s, s,-s}, 4); // +Y top
	addFace({-s,-s,-s}, { s,-s,-s}, { s,-s, s}, {-s,-s, s}, 5); // -Y bottom

	if(vertices->empty() || _layerBuffers.empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	addPrimitiveSet(indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layerBuffers.size() * 5 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layerBuffers[0], 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

}
