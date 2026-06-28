#pragma once

#include "osgSlug/Drawable.hpp"

#include "slughorn/slughorn.hpp"

namespace osgSlug {

// ScanlineDrawable - renders one shape using the Scanline Sweeper algorithm.
//
// Unlike Slug-pipeline drawables, this has no band indirection and no SDF bake step.
// The fragment shader loops over the shape's monotonic quadratic curves stored in the
// atlas scanline texture and computes signed swept area analytically per pixel.
//
// Typical usage:
//
// auto d = new osgSlug::ScanlineDrawable();
// d->setShapeKey(key);
// d->setColor({1.f, 1.f, 1.f, 1.f});
// atlas->addChild(d);
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
	Vec4 _color = {1.0f, 1.0f, 1.0f, 1.0f};
	osg::BoundingBox _bbox;
};

}
