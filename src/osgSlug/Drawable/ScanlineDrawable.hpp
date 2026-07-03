#pragma once

#include "osgSlug/Drawable.hpp"

#include "slughorn/slughorn.hpp"

namespace osgSlug {

// https://rookandpossum.com/posts/scanline-sweeper
//
// ScanlineDrawable - renders one shape using the Scanline Sweeper algorithm.
//
// Unlike Slug-pipeline drawables, this has no band indirection and no SDF bake step.
// The fragment shader loops over the shape's monotonic quadratic curves stored in the
// atlas scanline texture and computes signed swept area analytically per pixel.
class ScanlineDrawable: public Drawable {
public:
	ScanlineDrawable() = default;

	void setColor(const Vec4& c) { _color = c; }
	void addCompositeShape(const slughorn::CompositeShape& cs);

	void compile() override;
	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

private:
	slughorn::CompositeShape _composite;
	Vec4 _color = {1_cv, 1_cv, 1_cv, 1_cv};
	osg::BoundingBox _bbox;
};

}
