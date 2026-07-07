#pragma once

#include "osgSlug/Drawable/ShapeDrawable.hpp"

namespace osgSlug {

// SSBOShapeDrawable: SSBO-backed ShapeDrawable.
//
// compile() emits only two vertex attribute arrays (a_position at loc 0, a_emCoord at loc 1) and
// packs all per-shape data into a GL_SHADER_STORAGE_BUFFER indexed by a_position.w.
//
// Requires GL 4.3+
class SSBOShapeDrawable: public ShapeDrawable {
public:
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

	SSBOShapeDrawable() = default;

	// Both overridden (not just addCompositeShape()) so _layerMasks -- see below -- always
	// grows in lockstep with _layers, regardless of which entry point a caller uses.
	void addLayer(const slughorn::Layer& layer) override;
	void addCompositeShape(const slughorn::CompositeShape& composite) override;

	void compile() override;

	// Fine-grained mutation; write the relevant SSBO slot(s) and keep _layers in sync.
	// Call dirtyLayers() once after all mutations in a frame.
	void setLayerColor(size_t index, const slughorn::Color& color) override;
	void setLayerEffectId(size_t index, uint32_t effectId) override;
	void setLayerEffectParam(size_t index, slug_t param) override;
	void setLayerShapeIndex(size_t index, size_t shapeIndex) override;
	void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) override;

	// Full re-pack from a Layer struct. Re-runs gradient packing internally.
	void updateLayer(size_t index, const slughorn::Layer& layer) override;

	// Flush all accumulated writes to the GPU; all layers or just one.
	void dirtyLayers() override;
	void dirtyLayers(size_t index) override;

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

private:
	std::vector<RenderShape> _renderShapes;

	// Parallel to the base class's _layers, scoped to this class only (not every ShapeDrawable
	// subclass needs masking today). Real RenderMask identity is created immediately in
	// addLayer()/addCompositeShape() -- RenderMask's constructor doesn't need an Atlas, only
	// repack() does (called from compile(), where an Atlas is guaranteed) -- so there's no
	// deferred-lookup bookkeeping here, just the ref_ptr itself, one per layer.
	std::vector<osg::ref_ptr<RenderMask>> _layerMasks;
};

}
