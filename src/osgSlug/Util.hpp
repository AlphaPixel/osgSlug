#pragma once

#include "osgSlug/Types.hpp"
#include "slughorn/nanosvg.hpp"

namespace osgSlug {
namespace util {

// Returns the matrix that maps SVG canvas coordinates (Y-down, width normalized to 1.0)
// into OSG world space (Y-up). By default the SVG top-left sits at world origin and the
// image extends downward. When bottomAtOrigin=true, the SVG bottom sits at world origin
// and the image extends upward by cfg.heightEm.
//
// Apply to a MatrixTransform that wraps both the CompositeShape drawable and any sibling
// PathDrawable nodes so they share the same rectified frame.
inline Matrix svgToOSG(const slughorn::nanosvg::LoadConfig& cfg, bool bottomAtOrigin=false) {
	if(bottomAtOrigin) return
		Matrix::scale(1_cv, -1_cv, 1_cv) *
		Matrix::translate(0_cv, cfg.heightEm, 0_cv)
	;

	return Matrix::scale(1_cv, -1_cv, 1_cv);
}

// Returns the translation matrix that brings a GeometryOnly shape's sampled curves from
// local bbox-origin space into SVG canvas space. Apply as a MatrixTransform wrapping a
// PathDrawable (or any node whose points were sampled from atlas->getShape(key)->curves).
//
// Must be a child of the svgToOSG() transform so it operates in the same pre-flip frame.
// Throws std::out_of_range if key is not present in cs.
inline Matrix svgShapeTransform(const slughorn::CompositeShape& cs, const slughorn::Key& key) {
	const auto& layer = cs.layer(key);

	return Matrix::translate(layer.transform.x, layer.transform.y, 0_cv);
}

// Returns the matrix that maps any Y-down canvas (Skia, Cairo, etc.) into OSG world space (Y-up).
// Analogous to svgToOSG(); apply as a MatrixTransform wrapping the drawable.
//
// heightEm: total canvas height in em-space (= canvasHeight * scale).
// Default (0): flip around y=0; content occupies negative-Y world space. If > 0, also translate so
// the canvas bottom sits at world y=0; content at y=[0, heightEm].
inline Matrix yDownToOSG(slug_t heightEm=0_cv) {
	if(heightEm > 0_cv) return
		Matrix::scale(1_cv, -1_cv, 1_cv) *
		Matrix::translate(0_cv, heightEm, 0_cv)
	;

	return Matrix::scale(1_cv, -1_cv, 1_cv);
}

}
}
