#pragma once

#include "osgSlug/Drawable.hpp"

#include <initializer_list>

namespace osgSlug {

// ShapeDrawable renders slughorn::Layer and slughorn::CompositeShape instances.
//
// compile() emits only two vertex attribute arrays (a_position at loc 0, a_emCoord at loc 1) and
// packs all per-shape data into a GL_SHADER_STORAGE_BUFFER indexed by a_position.w. Requires GL 4.3+.
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

		// Null means "no mask, nothing to bind." See applyMask() in ShapeDrawable.cpp.
		osg::ref_ptr<RenderMask> mask;
	};

	// Consolidated per-layer working data: the authoring-time Layer plus its GPU-packed SSBO
	// slice, kept together so compile()/mutators never risk the two drifting out of index sync.
	struct RenderShape {
		slughorn::Layer layer;
		osg::ref_ptr<osgx::Vec4Array> buffer;

		// Shared across every layer copied from the same CompositeShape by addCompositeShape();
		// null if that composite had no mask. Pointer identity (not value) is what marks two
		// layers as "masked together" -- see ai/context-todo-mask.md, "Step 2 design".
		osg::ref_ptr<RenderMask> mask;
	};

	ShapeDrawable() = default;

	// Both overridden by subclasses that need to track additional per-layer state in lockstep --
	// _layerMasks below always grows alongside _layers regardless of which entry point is used.
	virtual void addLayer(const slughorn::Layer& layer);
	virtual void addCompositeShape(const slughorn::CompositeShape& composite);

	const auto& getLayers() const { return _layers; }

	void clear() { _layers.clear(); }

	void compile() override;

	// Fine-grained mutation; write the relevant SSBO slot(s) and keep _layers in sync.
	// Call dirtyLayers() once after all mutations in a frame.
	virtual void setLayerColor(size_t index, const slughorn::Color& color);
	virtual void setLayerEffectId(size_t index, uint32_t effectId);
	virtual void setLayerEffectParam(size_t index, slug_t param);
	virtual void setLayerShapeIndex(size_t index, size_t shapeIndex);
	virtual void setLayerGradientTransform(size_t index, const slughorn::Matrix& m);

	// Full re-pack from a Layer struct. Re-runs gradient packing internally.
	virtual void updateLayer(size_t index, const slughorn::Layer& layer);

	// Flush all accumulated writes to the GPU; all layers or just one.
	virtual void dirtyLayers();
	virtual void dirtyLayers(size_t index);

	void dirtyLayers(std::initializer_list<size_t> indices) {
		for(size_t i : indices) dirtyLayers(i);
	}

	// Per-layer SSBO slice. Valid after compile(); nullptr if index is out of range.
	osgx::Vec4Array* getLayerBuffer(size_t index) const {
		return index < _renderShapes.size() ? _renderShapes[index].buffer.get() : nullptr;
	}

	// Per-layer mask, shared across every layer from the same addCompositeShape() call; nullptr
	// if that layer's composite had no mask. Valid immediately (unlike getLayerBuffer(), no
	// compile() required) since RenderMask identity is created eagerly -- see _layerMasks.
	RenderMask* getLayerMask(size_t index) const {
		return index < _layerMasks.size() ? _layerMasks[index].get() : nullptr;
	}

	osg::BoundingBox computeBoundingBox() const override;

	// Per-group blend state dispatch. Calls base drawImplementation() directly when all groups
	// are SrcOver (the common case) so there is no overhead for normal usage.
	void drawImplementation(osg::RenderInfo& renderInfo) const override;

protected:
	std::vector<slughorn::Layer> _layers;
	std::vector<RenderGroup> _groups;

	// Bind the 2 SSBO vertex attrib slots (attrs 0-1) in one call.
	void bindSSBOAttribs(osgx::Vec4Array* verts, osgx::Vec4Array* emCoords);

private:
	std::vector<RenderShape> _renderShapes;

	// Parallel to _layers. Real RenderMask identity is created immediately in addLayer()/
	// addCompositeShape() -- RenderMask's constructor doesn't need an Atlas, only repack() does
	// (called from compile(), where an Atlas is guaranteed) -- so there's no deferred-lookup
	// bookkeeping here, just the ref_ptr itself, one per layer.
	std::vector<osg::ref_ptr<RenderMask>> _layerMasks;
};

}
