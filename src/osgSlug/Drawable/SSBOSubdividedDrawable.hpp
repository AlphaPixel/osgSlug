#pragma once

#include "osgSlug/Drawable/SubdividedDrawable.hpp"

namespace osgSlug {

// SSBOSubdividedDrawable: SSBO-backed SubdividedDrawable.
//
// Like SSBOShapeDrawable but generates a subdivided mesh via the position callback.
// Requires GL 4.3+
class SSBOSubdividedDrawable: public SubdividedDrawable {
public:
	SSBOSubdividedDrawable() = default;

	void compile() override;

	void setLayerColor(size_t index, const slughorn::Color& color) override;
	void setLayerEffectId(size_t index, uint32_t effectId) override;
	void setLayerEffectParam(size_t index, slug_t param) override;
	void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) override;
	void updateLayer(size_t index, const slughorn::Layer& layer) override;
	void dirtyLayers() override;
	void dirtyLayers(size_t index) override;

	osgx::Vec4Array* getLayerBuffer(size_t index) const {
		return index < _layerBuffers.size() ? _layerBuffers[index].get() : nullptr;
	}

protected:
	std::vector<osg::ref_ptr<osgx::Vec4Array>> _layerBuffers;
};

}
