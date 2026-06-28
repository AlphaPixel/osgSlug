#include "osgSlug/Drawable/SSBOShapeDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferIndexBinding>
#include <osg/BufferObject>

namespace osgSlug {

void SSBOShapeDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN
			<< "SSBOShapeDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl
		;

		return;
	}

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices});
	};

	// Layer SSBO (binding 1): one Vec4Array per layer, each holding 4 vec4s.
	// All arrays share a single ShaderStorageBufferObject so the GPU sees one contiguous buffer,
	// but each array has its own modifiedCount, enabling per-layer dirty uploads.
	//
	// [0] color: RGBA
	// [1] gradientMeta: x=gradientId, yz=center, w=r0_norm
	// [2] gradientXform
	// [3] effectData: x=effectId, y=shapeIndex, z=msdfData, w=effectParam
	_layerBuffers.clear();

	auto ssbo = osgx::make_ref<osg::ShaderStorageBufferObject>();

	index_element_type base = 0;
	size_t index = 0;

	for(const auto& layer : _layers) {
		if(layer.drawMode != slughorn::DrawMode::Visible) continue;

		const auto shape = atlas->getShape(layer.key);

		if(!shape) { index++; continue; }

		if(!groupIndices || layer.blendMode != groupBlend) {
			flushGroup();
			groupIndices = osgx::make_ref<index_type>();
			groupBlend = layer.blendMode;
		}

		const slug_t expand = layer.expand;
		const auto q = shape->computeQuad(layer.transform, layer.scale, expand);
		const slug_t lidx = cv(index + 1);
		const slug_t z = cv(layer.transform.z);

		vertices->append_range({
			{q.x0, q.y0, z, lidx},
			{q.x1, q.y0, z, lidx},
			{q.x1, q.y1, z, lidx},
			{q.x0, q.y1, z, lidx}
		});

		const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape, expand);

		emCoords->append_range({
			{emX0, emY0, 0_cv, 0_cv},
			{emX1, emY0, 1_cv, 0_cv},
			{emX1, emY1, 1_cv, 1_cv},
			{emX0, emY1, 0_cv, 1_cv}
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
		layerBuf->setBufferObject(ssbo);

		_layerBuffers.push_back(std::move(layerBuf));

		groupIndices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});

		base += 4;
		index++;
	}

	flushGroup();

	if(vertices->empty()) return;

	bindSSBOAttribs(vertices, emCoords);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	const auto totalSize = static_cast<GLsizeiptr>(_layerBuffers.size() * 4 * sizeof(Vec4));

	getOrCreateStateSet()->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, _layerBuffers[0], 0, totalSize),
		osg::StateAttribute::ON
	);

	_compiled = true;
}

void SSBOShapeDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layerBuffers.size()) {
		if(!_compiled)
			OSG_WARN << "SSBOShapeDrawable::setLayerColor(): called before compile() -- call ignored" << std::endl;
		return;
	}

	ssboSetLayerColor(_layerBuffers[index].get(), _layers, index, color);
}

void SSBOShapeDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layerBuffers.size()) {
		if(!_compiled)
			OSG_WARN << "SSBOShapeDrawable::setLayerEffectId(): called before compile() -- call ignored" << std::endl;
		return;
	}

	ssboSetLayerEffectId(_layerBuffers[index].get(), _layers, index, effectId);
}

void SSBOShapeDrawable::setLayerEffectParam(size_t index, slug_t param) {
	if(index >= _layerBuffers.size()) {
		if(!_compiled)
			OSG_WARN << "SSBOShapeDrawable::setLayerEffectParam(): called before compile() -- call ignored" << std::endl;
		return;
	}

	ssboSetLayerEffectParam(_layerBuffers[index].get(), _layers, index, param);
}

void SSBOShapeDrawable::setLayerShapeIndex(size_t index, size_t shapeIndex) {
	if(index >= _layerBuffers.size()) return;

	(*_layerBuffers[index])[3].y() = cv(shapeIndex);
}

void SSBOShapeDrawable::setLayerGradientTransform(size_t index, const slughorn::Matrix& m) {
	auto* atlas = getAtlas();

	if(index >= _layerBuffers.size() || !atlas) return;

	const auto& layer = _layers[index];

	if(layer.gradientId <= 0) return;

	slughorn::GradientInfo tmp = atlas->getGradients()[layer.gradientId - 1];
	tmp.transform = m;

	const auto [gmeta, gxform] = buildGradientDataFromInfo(layer.gradientId, tmp);
	auto& buf = *_layerBuffers[index];

	buf[1] = gmeta;
	buf[2] = gxform;
}

void SSBOShapeDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	auto* atlas = getAtlas();

	if(index >= _layerBuffers.size() || !atlas) return;

	_layers[index] = layer;

	const auto [gmeta, gxform] = buildGradientData(*atlas, layer);
	const slug_t shapeIdx = cv(atlas->getShapeIndex(layer.key));

	auto& buf = *_layerBuffers[index];

	buf[0] = Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	buf[1] = gmeta;
	buf[2] = gxform;
	buf[3] = Vec4(cv(layer.effectId), shapeIdx, buf[3].z(), buf[3].w());
}

void SSBOShapeDrawable::dirtyLayers() {
	for(auto& buf : _layerBuffers) if(buf) buf->dirty();
}

void SSBOShapeDrawable::dirtyLayers(size_t index) {
	if(index < _layerBuffers.size() && _layerBuffers[index]) _layerBuffers[index]->dirty();
}

}
