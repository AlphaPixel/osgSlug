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
	SSBOShapeDrawable() = default;

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
		return index < _layerBuffers.size() ? _layerBuffers[index].get() : nullptr;
	}

private:
	std::vector<osg::ref_ptr<osgx::Vec4Array>> _layerBuffers;
};

}
