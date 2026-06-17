#pragma once

#include "Drawable.hpp"

namespace osgSlug {

// Three meaningful rendering modes for InkDrawable. SDF/MSDF reserved for future use.
enum class InkMode {
    Overlap,  // non-mitered quads + analytical SDF coverage (fast, notches at high curvature)
    Miter,    // mitered quads    + analytical SDF coverage (clean joins)
    Sluggit,  // mitered quads    + internal unit-square slug shape (resolution-independent AA)
    Stamp,    // one caller-defined shape per point (set via setShapeKey); supports path rotation
    // SDF,
    // MSDF,
};

// InkDrawable — instanced per-segment quad stroke renderer driven by an SSBO of path points.
//
// Typical usage:
//   1. Construct BEFORE atlas->build() (Sluggit injects a unit-square shape into the atlas).
//   2. Call atlas->build() then atlas->packTextures().
//   3. Call setPoints() then compile().
//   4. Optionally animate via setRevealCount() (show first n of N-1 segments).
//
// _layers (inherited from ShapeDrawable) is unused; it is negligible cost and a future
// AtlasDrawable base class refactor is the clean fix if it ever becomes annoying.
class InkDrawable : public ShapeDrawable {
public:
    InkDrawable(Atlas* atlas, InkMode mode = InkMode::Sluggit);

    // setMode() triggers recompile. Other setters take effect on the next compile() call,
    // except setRevealCount() and setPoints() which are dynamic.
    void setMode(InkMode mode);
    void setHalfWidth(slug_t w) { _halfWidth = w; }
    void setOverlap(slug_t o) { _overlap = o; }         // Overlap mode only; silently ignored otherwise
    void setShapeKey(const slughorn::Key& key) { _shapeKey = key; }  // Overlap/Miter: user shape
    void setColor(const osg::Vec4f& c) { _color = c; }  // Sluggit mode only

    // Dynamic: updates the DrawArrays instance count immediately (no recompile needed).
    void setRevealCount(size_t n);

    // Dynamic: replaces point data and rebuilds the SSBO binding. xy = position.
    // If called before compile(), the data is staged. If called after, the SSBO is updated live.
    void setPoints(std::vector<osg::Vec4> pts);

    void compile() override;
    osg::BoundingBox computeBoundingBox() const override;

private:
    InkMode   _mode       = InkMode::Sluggit;
    slug_t    _halfWidth  = 0.45_cv;
    slug_t    _overlap    = 0.05_cv;
    size_t    _revealCount = 0;
    osg::Vec4f _color     = {1.0f, 1.0f, 1.0f, 1.0f};
    slughorn::Key _shapeKey;

    osg::ref_ptr<osg::Vec4Array>                  _points;
    osg::ref_ptr<osg::ShaderStorageBufferBinding> _ssboBinding;
    osg::ref_ptr<osg::DrawArrays>                 _drawArrays;
};

}
