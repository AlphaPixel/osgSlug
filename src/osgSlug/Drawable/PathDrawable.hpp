#pragma once

#include "osgSlug/Drawable.hpp"

#include "slughorn/canvas.hpp"

#include <limits>

namespace osgSlug {

enum class PathMode {
	Miter, // mitered quads + analytical fwidth SDF (fast, no atlas needed)
	Sluggit, // mitered quads + Slug SDF pipeline against a caller-provided shape (setShapeKey)
	Stamp // one shape per point, rotated by points[i].z, Slug SDF pipeline (setShapeKey/setShapeKeys)
};

// PathDrawable - instanced per-segment quad stroke renderer driven by an SSBO of path points.
//
// Typical usage:
// 1. Call atlas->build() then atlas->packTextures().
// 2. For Sluggit/Stamp: define a shape in the atlas and call setShapeKey() before compile().
//    Stamp mode alone also accepts setShapeKeys() for a per-point shape table - see its comment.
// 3. Call setPoints() then add under atlas via atlas->addChild() (triggers compile).
class PathDrawable: public Drawable {
public:
	PathDrawable(PathMode mode = PathMode::Sluggit);

	// setMode() triggers recompile. Other setters take effect on the next compile() call,
	// except setHalfWidth(), setColor(), setJoin(), setMiterLimit(), setCap(), setEffectId(),
	// setEffectParam(), setRevealCount() and setPoints(), which are dynamic.
	void setMode(PathMode mode);

	// Dynamic: the uniform is updated live once compiled and the bound is dirtied, no recompile
	// needed.
	void setHalfWidth(slug_t w);
	slug_t getHalfWidth() const { return _halfWidth; }
	void setShapeKey(const slughorn::Key& key) { _shapeKey = key; } // Sluggit/Stamp: shape to render

	// Stamp mode only: an indexed table of shapes instead of one fixed shape. points[i].w
	// becomes a 0-based index into `keys`, so each instance can stamp a different shape - e.g.
	// a random ASCII glyph per particle in a text-particle demo, or a mix of icon shapes along a
	// route. Overrides setShapeKey() for Stamp; each key's em-space bounds are looked up once
	// here (atlas->getShape()) and uploaded to a private SSBO (the osgSlug::path.shapes slot),
	// since AtlasShapeData has no room for per-shape em bounds. Triggers recompile, same as
	// setShapeKey().
	void setShapeKeys(std::vector<slughorn::Key> keys);

	// Sluggit mode only. Dynamic: the uniform is updated live once compiled, no recompile needed.
	void setColor(const Vec4& c);
	const Vec4& getColor() const { return _color; }

	// Sluggit mode only: how a subpath's interior points are joined, with the same meaning as
	// slughorn::canvas stroking - a Miter join whose miter ratio exceeds the miter limit is
	// beveled. Dynamic: the uniforms are updated live once compiled (and the bound dirtied for the
	// miter limit), no recompile needed.
	void setJoin(slughorn::canvas::LineJoin join);
	slughorn::canvas::LineJoin getJoin() const { return _join; }
	void setMiterLimit(slug_t limit);
	slug_t getMiterLimit() const { return _miterLimit; }

	// Sluggit mode only: how each subpath's two open ends are capped, with the same meaning as
	// slughorn::canvas stroking. Dynamic: the uniform is updated live once compiled.
	void setCap(slughorn::canvas::LineCap cap);
	slughorn::canvas::LineCap getCap() const { return _cap; }

	// The osgSlug_VertexData.effectId / .effectParam a linked vertex hook reads - the same two
	// fields ShapeDrawable feeds per-layer via setLayerEffectId()/setLayerEffectParam(), so a hook
	// written against one drawable reads them identically here. PathDrawable has no per-layer
	// SSBO, so these are one value shared by every instance; per-instance variety comes from
	// gl_InstanceID, which a hook can read directly.
	//
	// Dynamic: the uniform is updated live once compiled, no recompile needed.
	void setEffectId(int id);
	void setEffectParam(slug_t param);

	// Recompiles, since compile() is what links the hook units. See Drawable::setHooks().
	void setHooks(Atlas::HookList hooks) override;

	// Dynamic: updates the DrawArrays instance count immediately (no recompile needed). The count
	// is clamped to the instances the points provide (one per segment, or one per point for
	// Stamp), so the default reveals everything and keeps doing so as setPoints() grows the path;
	// 0 draws nothing.
	void setRevealCount(size_t n);

	// Dynamic: replaces point data and rebuilds the SSBO binding. xy = position.
	// If called before compile(), the data is staged. If called after, the SSBO is updated live.
	void setPoints(std::vector<Vec4> pts);

	// Multi-subpath variant: concatenates N independent, disconnected point sequences into one
	// SSBO, tagging each point's w component with its subpath index. The Miter/Sluggit shader
	// checks that tag before treating a neighboring point as a real miter neighbor or a real
	// segment - a boundary between two subpaths degenerates to a zero-area quad instead of
	// drawing a spurious connecting segment or computing a nonsense cross-subpath miter. Lets
	// one PathDrawable render many independent paths (e.g. a tile's worth of roads) in one
	// instanced draw call instead of one PathDrawable per path. Incoming w components (if any)
	// are discarded; z is preserved (Stamp mode's per-point rotation, though Stamp doesn't use
	// this method's boundary logic - see computeQuad() in the .cpp).
	void setPaths(const std::vector<std::vector<Vec4>>& subpaths);

	void compile() override;
	void drawImplementation(osg::RenderInfo& renderInfo) const override;
	osg::BoundingBox computeBoundingBox() const override;

private:
	// The DrawArrays instance count: _revealCount clamped to what the points provide.
	GLsizei _instanceCount() const;

	PathMode _mode = PathMode::Sluggit;
	slug_t _halfWidth = 0.45_cv;
	size_t _revealCount = std::numeric_limits<size_t>::max();
	int _effectId = 0;
	slug_t _effectParam = 0.0_cv;
	Vec4 _color = {1.0f, 1.0f, 1.0f, 1.0f};
	slughorn::canvas::LineJoin _join = slughorn::canvas::LineJoin::Miter;
	slughorn::canvas::LineCap _cap = slughorn::canvas::LineCap::Butt;
	slug_t _miterLimit = 4.0_cv;
	slughorn::Key _shapeKey;
	std::vector<slughorn::Key> _shapeKeys; // Stamp mode multi-shape path; see setShapeKeys()

	osg::ref_ptr<osgx::Vec4Array> _points;
	osg::ref_ptr<osg::ShaderStorageBufferBinding> _ssboBinding;
	osg::ref_ptr<osg::ShaderStorageBufferBinding> _shapeTableBinding;
	osg::ref_ptr<osg::DrawArrays> _drawArrays;
};

}
