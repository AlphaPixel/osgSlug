#pragma once

#include "Atlas.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Geometry>

OSGSLUG_ENABLE_WARNINGS

namespace osgSlug {

// Thin base for all osgSlug drawables.
//
// getAtlas() checks _atlas first (set via setAtlas()), then walks up the OSG parent chain.
// Prefer atlas->addChild(drawable) over setAtlas() where possible; setAtlas() exists for
// drawables that live outside the Atlas subtree (e.g. PathDrawable).
class Drawable: public osg::Geometry {
public:
	Drawable();

	// Returns _atlas if set explicitly, otherwise walks the OSG parent chain.
	virtual Atlas* getAtlas() const;

	// Set atlas explicitly when this drawable is not a child of an Atlas node.
	void setAtlas(Atlas* atlas) { _atlas = atlas; }

	virtual void compile() = 0;

	osg::BoundingBox computeBoundingBox() const override = 0;

	// Called by OSG's GLObjectsVisitor (viewer.realize()). Delegates to compile(), which is
	// idempotent - it no-ops if the drawable is already compiled.
	void compileGLObjects(osg::RenderInfo& renderInfo) const override;

	// Optional callback fired by Atlas immediately after compile() succeeds (via addChild or
	// packTextures). Use this for post-compile setup that requires _layerBuffers to exist,
	// e.g. setLayerEffectParam(). Set before adding the drawable to an Atlas.
	std::function<void(Atlas&)> onAtlasAttached;

protected:
	mutable bool _compiled = false;
	osg::ref_ptr<Atlas> _atlas;

	friend class Atlas;
};

}
