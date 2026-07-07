#pragma once

#include "osgSlug/Drawable/SubdividedDrawable.hpp"

#include <cmath>

namespace osgSlug {

class SphereDrawable: public SubdividedDrawable {
public:
	SphereDrawable(
		slug_t radius=1_cv,
		index_element_type stacks=64,
		index_element_type slices=128
	) {
		_stepsU = slices;
		_stepsV = stacks;

		setPositionCallback([radius](slug_t u, slug_t v) -> Vec3 {
			const slug_t PI = M_PIf;
			const slug_t TAU = 2_cv * PI;
			const slug_t lat = PI * v - PI * 0.5_cv;
			const slug_t lon = TAU * u;

			return Vec3(
				std::cos(lat) * std::cos(lon),
				std::sin(lat),
				std::cos(lat) * std::sin(lon)
			) * radius;
		});
	}
};

}
