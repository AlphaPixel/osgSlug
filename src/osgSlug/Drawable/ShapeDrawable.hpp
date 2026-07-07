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

	// The single pre-compile source of truth for a layer: the authoring-time Layer, its mask
	// identity (set immediately by addLayer()/addCompositeShape(), no Atlas required), and its
	// GPU-packed SSBO slice (null until compile() fills it in). Keeping all three together means
	// there is exactly one array to index and no risk of parallel arrays drifting out of sync.
	struct RenderShape {
		slughorn::Layer layer;
		osg::ref_ptr<osgx::Vec4Array> buffer;

		// Shared across every layer copied from the same CompositeShape by addCompositeShape();
		// null if that composite had no mask. Pointer identity (not value) is what marks two
		// layers as "masked together" -- see ai/context-todo-mask.md, "Step 2 design".
		osg::ref_ptr<RenderMask> mask;
	};

	ShapeDrawable() = default;

	// Virtual so subclasses can hook additional per-layer bookkeeping if ever needed; no current
	// subclass overrides either.
	virtual void addLayer(const slughorn::Layer& layer);
	virtual void addCompositeShape(const slughorn::CompositeShape& composite);

	// Snapshot of the authored Layer data (by value -- _layers itself holds the richer
	// RenderShape, not exposed here).
	std::vector<slughorn::Layer> getLayers() const;

	void clear() { _layers.clear(); }

	void compile() override;

	// Fine-grained mutation; write the relevant SSBO slot(s) and keep the Layer in sync.
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
		return index < _layers.size() ? _layers[index].buffer.get() : nullptr;
	}

	// Per-layer mask, shared across every layer from the same addCompositeShape() call; nullptr
	// if that layer's composite had no mask. Valid immediately (unlike getLayerBuffer(), no
	// compile() required) since RenderMask identity is created eagerly in addLayer()/
	// addCompositeShape().
	RenderMask* getLayerMask(size_t index) const {
		return index < _layers.size() ? _layers[index].mask.get() : nullptr;
	}

	osg::BoundingBox computeBoundingBox() const override;

	// Per-group blend state dispatch. Calls base drawImplementation() directly when all groups
	// are SrcOver (the common case) so there is no overhead for normal usage.
	void drawImplementation(osg::RenderInfo& renderInfo) const override;

protected:
	// Pre-compile source of truth: one RenderShape per addLayer()/addCompositeShape() call, in
	// authoring order. `buffer` starts null and is filled in by compile(); `mask` is set
	// immediately. Subclasses (SubdividedDrawable, BoxDrawable, DecalDrawable) index this
	// directly rather than keeping their own parallel per-layer arrays.
	std::vector<RenderShape> _layers;

	// Post-compile source of truth: one RenderGroup per contiguous run of layers sharing a
	// blend mode and mask, each owning the index buffer for one draw call.
	std::vector<RenderGroup> _groups;

	// Bind the 2 SSBO vertex attrib slots (attrs 0-1) in one call.
	void bindSSBOAttribs(osgx::Vec4Array* verts, osgx::Vec4Array* emCoords);
};

}
