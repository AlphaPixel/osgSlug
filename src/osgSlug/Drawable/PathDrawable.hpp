#pragma once

#include "osgSlug/Drawable.hpp"

namespace osgSlug {

enum class PathMode {
	Miter, // mitered quads + analytical fwidth SDF (fast, no atlas needed)
	Sluggit, // mitered quads + Slug SDF pipeline against a caller-provided shape (setShapeKey)
	Stamp // one shape per point, rotated by points[i].z, Slug SDF pipeline (setShapeKey/setShapeKeys)
};

// Private SSBO binding points for Stamp mode's multi-shape path (setShapeKeys() below). These
// have nothing to do with osgSlug::Atlas's AtlasShapeBuffer/LayerBuffer convention (bindings 0/1
// in Atlas.shaders.cpp) -- PathDrawable's Program never links those, so there is no cross-
// convention collision. Named here only so PathDrawable.cpp's two SSBOs (points + shape table)
// don't silently end up sharing a binding again as this class grows. See
// ai/context-todo-pathdrawable.md, "Future: Extract StampDrawable + user-configurable SSBO
// bindings" for the longer-term plan to generalize this.
constexpr unsigned PATH_POINTS_SSBO_BINDING = 0;
constexpr unsigned PATH_SHAPE_TABLE_SSBO_BINDING = 1;

// PathDrawable - instanced per-segment quad stroke renderer driven by an SSBO of path points.
//
// Typical usage:
// 1. Call atlas->build() then atlas->packTextures().
// 2. For Sluggit/Stamp: define a shape in the atlas and call setShapeKey() before compile().
//    Stamp mode alone also accepts setShapeKeys() for a per-point shape table -- see its comment.
// 3. Call setPoints() then add under atlas via atlas->addChild() (triggers compile).
class PathDrawable: public Drawable {
public:
	PathDrawable(PathMode mode = PathMode::Sluggit);

	// setMode() triggers recompile. Other setters take effect on the next compile() call,
	// except setRevealCount() and setPoints() which are dynamic.
	void setMode(PathMode mode);
	void setHalfWidth(slug_t w) { _halfWidth = w; }
	void setShapeKey(const slughorn::Key& key) { _shapeKey = key; } // Sluggit/Stamp: shape to render

	// Stamp mode only: an indexed table of shapes instead of one fixed shape. points[i].w
	// becomes a 0-based index into `keys`, so each instance can stamp a different shape -- e.g.
	// a random ASCII glyph per particle in a text-particle demo, or a mix of icon shapes along a
	// route. Overrides setShapeKey() for Stamp; each key's em-space bounds are looked up once
	// here (atlas->getShape()) and uploaded to a private SSBO at PATH_SHAPE_TABLE_SSBO_BINDING,
	// since AtlasShapeData has no room for per-shape em bounds. Triggers recompile, same as
	// setShapeKey().
	void setShapeKeys(std::vector<slughorn::Key> keys);

	void setColor(const Vec4& c) { _color = c; } // Sluggit mode only

	// Dynamic: updates the DrawArrays instance count immediately (no recompile needed).
	void setRevealCount(size_t n);

	// Dynamic: replaces point data and rebuilds the SSBO binding. xy = position.
	// If called before compile(), the data is staged. If called after, the SSBO is updated live.
	void setPoints(std::vector<Vec4> pts);

	// Multi-subpath variant: concatenates N independent, disconnected point sequences into one
	// SSBO, tagging each point's w component with its subpath index. The Miter/Sluggit shader
	// checks that tag before treating a neighboring point as a real miter neighbor or a real
	// segment -- a boundary between two subpaths degenerates to a zero-area quad instead of
	// drawing a spurious connecting segment or computing a nonsense cross-subpath miter. Lets
	// one PathDrawable render many independent paths (e.g. a tile's worth of roads) in one
	// instanced draw call instead of one PathDrawable per path. Incoming w components (if any)
	// are discarded; z is preserved (Stamp mode's per-point rotation, though Stamp doesn't use
	// this method's boundary logic -- see computeQuad() in the .cpp).
	void setPaths(const std::vector<std::vector<Vec4>>& subpaths);

	void compile() override;
	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

private:
	PathMode _mode = PathMode::Sluggit;
	slug_t _halfWidth = 0.45_cv;
	size_t _revealCount = 0;
	Vec4 _color = {1.0f, 1.0f, 1.0f, 1.0f};
	slughorn::Key _shapeKey;
	std::vector<slughorn::Key> _shapeKeys; // Stamp mode multi-shape path; see setShapeKeys()

	osg::ref_ptr<osgx::Vec4Array> _points;
	osg::ref_ptr<osg::ShaderStorageBufferBinding> _ssboBinding;
	osg::ref_ptr<osg::ShaderStorageBufferBinding> _shapeTableBinding;
	osg::ref_ptr<osg::DrawArrays> _drawArrays;
};

}
