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
};

}
