#pragma once

#include "osgSlug/Drawable/ShapeDrawable.hpp"

namespace osgSlug {

class BoxDrawable: public ShapeDrawable {
public:
	BoxDrawable() = default;

	void setLayer(const slughorn::Layer& layer) {
		clear();

		addLayer(layer);
	}

	void compile() override;

private:
	slug_t _size = 1_cv;

	// One SSBO slice per face (see ShapeDrawable/SubdividedDrawable) -- must outlive
	// compile() since only the first entry is retained by the ShaderStorageBufferBinding.
	std::vector<osg::ref_ptr<osgx::Vec4Array>> _layerBuffers;
};

}
