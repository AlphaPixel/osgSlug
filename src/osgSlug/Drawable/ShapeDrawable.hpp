#pragma once

#include "osgSlug/Drawable.hpp"

#include <initializer_list>

namespace osgSlug {

// ShapeDrawable renders slughorn::Layer and slughorn::CompositeShape instances.
//
// Scene placement is NOT handled here; wrap this node in an `osg::MatrixTransform` if you need to
// position it in world space. All geometry is built using `slughorn` as the source of truth.
class ShapeDrawable: public Drawable {
public:
	using index_type = osgx::DrawElementsUShort;
	using index_element_type = index_type::vector_type::value_type;

	struct RenderGroup {
		slughorn::BlendMode blendMode = slughorn::BlendMode::SrcOver;
		slughorn::DrawMode drawMode = slughorn::DrawMode::Visible;

		osg::ref_ptr<index_type> indices;

		// Only ever set by subclasses that build RenderMask (currently just
		// SSBOShapeDrawable); null means "no mask, nothing to bind" for every other
		// subclass. See applyMask() in ShapeDrawable.cpp.
		osg::ref_ptr<RenderMask> mask;
	};

	virtual void addLayer(const slughorn::Layer& layer) {
		_layers.push_back(layer);
	}

	virtual void addCompositeShape(const slughorn::CompositeShape& composite) {
		for(const auto& layer : composite.layers) _layers.push_back(layer);
	}

	const auto& getLayers() const { return _layers; }

	void clear() { _layers.clear(); }

	virtual void setLayerColor(size_t index, const slughorn::Color& color) {}
	virtual void setLayerEffectId(size_t index, uint32_t effectId) {}
	virtual void setLayerEffectParam(size_t index, slug_t param) {}
	virtual void setLayerShapeIndex(size_t index, size_t shapeIndex) {}
	virtual void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) {}
	virtual void updateLayer(size_t index, const slughorn::Layer& layer) {}
	virtual void dirtyLayers() {}
	virtual void dirtyLayers(size_t index) {}

	void dirtyLayers(std::initializer_list<size_t> indices) {
		for(size_t i : indices) dirtyLayers(i);
	}

	osg::BoundingBox computeBoundingBox() const override;

	// Per-group blend state dispatch. Calls base drawImplementation() directly when all groups
	// are SrcOver (the common case) so there is no overhead for normal usage.
	void drawImplementation(osg::RenderInfo& renderInfo) const override;

protected:
	std::vector<slughorn::Layer> _layers;
	std::vector<RenderGroup> _groups;

	// Bind all 8 GL3 vertex attrib slots (attrs 0-7) in one call.
	void bindGL3Attribs(
		osgx::Vec4Array* verts,
		osgx::Vec4Array* colors,
		osgx::Vec4Array* emCoords,
		osgx::Vec4Array* bandXform,
		osgx::Vec4Array* shapeData,
		osgx::Vec4Array* effectData,
		osgx::Vec4Array* gradMeta,
		osgx::Vec4Array* gradXforms
	);

	// Bind the 2 SSBO vertex attrib slots (attrs 0-1) in one call.
	void bindSSBOAttribs(osgx::Vec4Array* verts, osgx::Vec4Array* emCoords);
};

}
