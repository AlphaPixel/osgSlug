#pragma once

#include "osgSlug/Drawable/ShapeDrawable.hpp"

namespace osgSlug {

// GL3ShapeDrawable: 8-attribute vertex path, GL 3.x compatible, explicit opt-in.
class GL3ShapeDrawable: public ShapeDrawable {
public:
	GL3ShapeDrawable() = default;

	void compile() override;

	void setLayerColor(size_t index, const slughorn::Color& color) override;
	void setLayerEffectId(size_t index, uint32_t effectId) override;
	void setLayerEffectParam(size_t index, slug_t param) override;
	void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) override;
	void updateLayer(size_t index, const slughorn::Layer& layer) override;
	void dirtyLayers() override;
	// GL3 stores per-layer data interleaved across 4 VBOs; range-dirty is not yet implemented.
	// Fall back to full dirty so callers using the common dirtyLayers(i) API still work correctly.
	void dirtyLayers(size_t) override { dirtyLayers(); }
};

}
