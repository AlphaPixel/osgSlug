#pragma once

#include "osgSlug/Drawable.hpp"

namespace osgSlug {

enum class PathMode {
	Miter, // mitered quads + analytical fwidth SDF (fast, no atlas needed)
	Sluggit, // mitered quads + Slug SDF pipeline against a caller-provided shape (setShapeKey)
	Stamp // one shape per point, rotated by points[i].z, Slug SDF pipeline (setShapeKey)
};

// PathDrawable - instanced per-segment quad stroke renderer driven by an SSBO of path points.
//
// Typical usage:
// 1. Call atlas->build() then atlas->packTextures().
// 2. For Sluggit/Stamp: define a shape in the atlas and call setShapeKey() before compile().
// 3. Call setPoints() then add under atlas via atlas->addChild() (triggers compile).
class PathDrawable: public Drawable {
public:
	PathDrawable(PathMode mode = PathMode::Sluggit);

	// setMode() triggers recompile. Other setters take effect on the next compile() call,
	// except setRevealCount() and setPoints() which are dynamic.
	void setMode(PathMode mode);
	void setHalfWidth(slug_t w) { _halfWidth = w; }
	void setShapeKey(const slughorn::Key& key) { _shapeKey = key; } // Sluggit/Stamp: shape to render
	void setColor(const Vec4& c) { _color = c; } // Sluggit mode only

	// Dynamic: updates the DrawArrays instance count immediately (no recompile needed).
	void setRevealCount(size_t n);

	// Dynamic: replaces point data and rebuilds the SSBO binding. xy = position.
	// If called before compile(), the data is staged. If called after, the SSBO is updated live.
	void setPoints(std::vector<Vec4> pts);

	void compile() override;
	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

private:
	PathMode _mode = PathMode::Sluggit;
	slug_t _halfWidth = 0.45_cv;
	size_t _revealCount = 0;
	Vec4 _color = {1.0f, 1.0f, 1.0f, 1.0f};
	slughorn::Key _shapeKey;

	osg::ref_ptr<osgx::Vec4Array> _points;
	osg::ref_ptr<osg::ShaderStorageBufferBinding> _ssboBinding;
	osg::ref_ptr<osg::DrawArrays> _drawArrays;
};

}
