#include "osgSlug/Drawable/GL3ShapeDrawable.hpp"

#include "Drawable/Util.hpp"

#include <osg/BufferObject>

namespace osgSlug {

void GL3ShapeDrawable::compile() {
	if(_compiled) return;

	auto* atlas = getAtlas();

	if(!atlas) {
		OSG_WARN
			<< "GL3ShapeDrawable::compile(): no Atlas parent -- "
			<< "add to an Atlas node first (e.g. atlas->addChild(drawable))" << std::endl
		;

		return;
	}

	if(!atlas->isBuilt() || _layers.empty()) return;

	auto vertices = osgx::make_ref<osgx::Vec4Array>();
	auto colors = osgx::make_ref<osgx::Vec4Array>();
	auto emCoords = osgx::make_ref<osgx::Vec4Array>();
	auto bandXform = osgx::make_ref<osgx::Vec4Array>();
	auto shapeData = osgx::make_ref<osgx::Vec4Array>();
	auto effectData = osgx::make_ref<osgx::Vec4Array>();
	auto gradientMeta = osgx::make_ref<osgx::Vec4Array>();
	auto gradientXforms = osgx::make_ref<osgx::Vec4Array>();

	_groups.clear();

	osg::ref_ptr<index_type> groupIndices;
	slughorn::BlendMode groupBlend = slughorn::BlendMode::SrcOver;

	auto flushGroup = [&]() {
		if(groupIndices && !groupIndices->empty())
			_groups.push_back({groupBlend, slughorn::DrawMode::Visible, groupIndices});
	};

	index_element_type base = 0;
	size_t index = 0;

	for(const auto& layer : _layers) {
		if(layer.drawMode != slughorn::DrawMode::Visible) continue;

		const auto shape = atlas->getShape(layer.key);

		if(!shape) continue;

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

		colors->append_n<4>({layer.color.r, layer.color.g, layer.color.b, layer.color.a});

		const auto [emX0, emY0, emX1, emY1] = computeEmBounds(*shape, expand);

		emCoords->append_range({
			{emX0, emY0, 0_cv, 0_cv},
			{emX1, emY0, 1_cv, 0_cv},
			{emX1, emY1, 1_cv, 1_cv},
			{emX0, emY1, 0_cv, 1_cv}
		});

		bandXform->append_n<4>({
			shape->bandScaleX, shape->bandScaleY,
			shape->bandOffsetX, shape->bandOffsetY
		});

		shapeData->append_n<4>({
			cv(shape->bandTexX), cv(shape->bandTexY),
			cv(shape->bandMaxX), cv(shape->bandMaxY)
		});

		effectData->append_n<4>(Vec4(
			cv(layer.effectId),
			cv(shape->originX),
			cv(shape->originY),
			layer.effectParam
		));

		const auto [gmeta, gxform] = buildGradientData(*atlas, layer);

		gradientMeta->append_n<4>(gmeta);
		gradientXforms->append_n<4>(gxform);

		groupIndices->append_range({
			base, index_element_type(base + 1), index_element_type(base + 2),
			base, index_element_type(base + 2), index_element_type(base + 3)
		});

		base += 4;
		index++;
	}

	flushGroup();

	if(vertices->empty()) return;

	bindGL3Attribs(vertices, colors, emCoords, bandXform, shapeData, effectData, gradientMeta, gradientXforms);

	for(const auto& g : _groups) addPrimitiveSet(g.indices);

	_compiled = true;
}

void GL3ShapeDrawable::setLayerColor(size_t index, const slughorn::Color& color) {
	if(index >= _layers.size()) return;

	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(1));

	if(!arr || (index + 1) * 4 > arr->size()) return;

	_layers[index].color = color;

	const Vec4 c(color.r, color.g, color.b, color.a);

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v] = c;
}

void GL3ShapeDrawable::setLayerEffectId(size_t index, uint32_t effectId) {
	if(index >= _layers.size()) return;

	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr || (index + 1) * 4 > arr->size()) return;

	_layers[index].effectId = effectId;

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v].x() = cv(effectId);
}

void GL3ShapeDrawable::setLayerEffectParam(size_t index, slug_t param) {
	auto* arr = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));

	if(!arr) {
		OSG_WARN << "GL3ShapeDrawable::setLayerEffectParam(): called before compile() -- call ignored" << std::endl;
		return;
	}

	if((index + 1) * 4 > arr->size()) return;

	_layers[index].effectParam = param;

	for(size_t v = 0; v < 4; v++) (*arr)[index * 4 + v].w() = cv(param);
}

void GL3ShapeDrawable::updateLayer(size_t index, const slughorn::Layer& layer) {
	auto* atlas = getAtlas();

	if(index >= _layers.size() || !atlas) return;

	auto* colors = static_cast<osgx::Vec4Array*>(getVertexAttribArray(1));
	auto* effectData = static_cast<osgx::Vec4Array*>(getVertexAttribArray(5));
	auto* gradMeta = static_cast<osgx::Vec4Array*>(getVertexAttribArray(6));
	auto* gradXforms = static_cast<osgx::Vec4Array*>(getVertexAttribArray(7));

	if(!colors || !effectData || !gradMeta || !gradXforms) return;
	if((index + 1) * 4 > colors->size()) return;

	_layers[index] = layer;

	const auto shape = atlas->getShape(layer.key);
	const auto [gmeta, gxform] = buildGradientData(*atlas, layer);

	const Vec4 c(layer.color.r, layer.color.g, layer.color.b, layer.color.a);
	const Vec4 eid(
		cv(layer.effectId),
		shape ? cv(shape->originX) : 0_cv,
		shape ? cv(shape->originY) : 0_cv,
		0_cv
	);

	for(size_t v = 0; v < 4; v++) {
		(*colors)[index * 4 + v] = c;
		(*effectData)[index * 4 + v] = eid;
		(*gradMeta)[index * 4 + v] = gmeta;
		(*gradXforms)[index * 4 + v] = gxform;
	}
}

void GL3ShapeDrawable::setLayerGradientTransform(size_t index, const slughorn::Matrix& m) {
	auto* atlas = getAtlas();

	if(index >= _layers.size() || !atlas) return;

	auto* gradMeta = static_cast<osgx::Vec4Array*>(getVertexAttribArray(6));
	auto* gradXforms = static_cast<osgx::Vec4Array*>(getVertexAttribArray(7));

	if(!gradMeta || !gradXforms || (index + 1) * 4 > gradMeta->size()) return;

	const auto& layer = _layers[index];

	if(layer.gradientId <= 0) return;

	slughorn::GradientInfo tmp = atlas->getGradients()[layer.gradientId - 1];

	tmp.transform = m;

	const auto [gmeta, gxform] = buildGradientDataFromInfo(layer.gradientId, tmp);

	for(size_t v = 0; v < 4; v++) {
		(*gradMeta)[index * 4 + v] = gmeta;
		(*gradXforms)[index * 4 + v] = gxform;
	}
}

void GL3ShapeDrawable::dirtyLayers() {
	for(unsigned slot : {1u, 5u, 6u, 7u}) {
		if(auto* arr = getVertexAttribArray(slot)) arr->dirty();
	}
}

}
