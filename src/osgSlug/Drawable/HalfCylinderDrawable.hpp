#pragma once

#include "osgSlug/Drawable/SubdividedDrawable.hpp"

#include <cmath>

namespace osgSlug {

// HalfCylinderDrawable
//
// Inside surface of a partial cylinder; a "curved monitor". The arc sweeps arcAngle radians
// centred on the Z axis (viewer looks in -Z). Y is linear. UV maps naturally: u along arc, v along
// height.
class HalfCylinderDrawable: public SubdividedDrawable {
public:
	HalfCylinderDrawable(
		slug_t radius=2_cv,
		slug_t height=1_cv,
		// 120 degrees default
		slug_t arcAngle=osg::PIf * (2_cv / 3_cv),
		index_element_type stepsU=64,
		// height needs far fewer steps
		index_element_type stepsV=8
	) {
		_stepsU = stepsU;
		_stepsV = stepsV;

		setPositionCallback([radius, height, arcAngle](slug_t u, slug_t v) -> Vec3 {
			const slug_t angle = (u - 0.5_cv) * arcAngle;
			return {
				radius * std::sin(angle),
				v * height - height * 0.5_cv,
				radius * std::cos(angle)
			};
		});
	}
};

}
