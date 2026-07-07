#pragma once

#include "Atlas.hpp"

OSGSLUG_DISABLE_WARNINGS

// #include <osg/BufferIndexBinding>
// #include <osg/BufferObject>
#include <osg/Geometry>

OSGSLUG_ENABLE_WARNINGS

namespace osgSlug {

// UBO binding index used for osgSlug_mask. Independent namespace from the SSBO bindings used
// elsewhere (atlas shape data = 0, layer data = 1 -- see ShapeDrawable.cpp) since GL keeps
// GL_UNIFORM_BUFFER and GL_SHADER_STORAGE_BUFFER binding points separate.
constexpr unsigned RENDER_MASK_UBO_BINDING = 0;

// Render-side wrapper around a slughorn::Mask. Owns the packed GPU-ready representation
// (mirroring the std140 layout of osgSlug_MaskData in Atlas.shaders.cpp) plus the UBO it
// lives in, and gives the mask reference-counted identity so RenderShape/RenderGroup can
// group layers by pointer equality instead of comparing Mask values (see
// ai/context-todo-mask.md, "Step 2 design").
//
// NOT an osg::StateAttribute: RenderGroup already bypasses StateSet for per-group state
// (see applyBlendMode() in ShapeDrawable.cpp) because state must change multiple times
// within a single drawImplementation() call, which StateSet application can't express.
// apply() below follows the same pattern -- it calls UniformBufferBinding::apply() directly,
// imperatively, reusing OSG's buffer-compile/upload machinery without ever attaching
// anything to a StateSet.
class RenderMask: public osg::Referenced {
public:
	// Construction is deliberately Atlas-independent -- callers need real identity (this
	// object, not just its eventual data) before an Atlas is guaranteed resolvable (e.g.
	// ShapeDrawable::addCompositeShape() may run before this drawable is parented under one).
	// bindingPoint is the UBO binding index this mask will be bound to in apply(). msdfLayer
	// (the one field that genuinely needs an Atlas -- see repack()) is packed as "none" (-1)
	// until repack() is called.
	//
	// No contentOrigin parameter: canvas-space origin is a per-LAYER property (each layer has
	// its own transform.xy), not a per-mask one -- a single shared value here only happens to
	// be correct for single-layer masked composites. The shader reads each fragment's own
	// layer origin directly from the LayerBuffer SSBO (osgSlug_LayerData.transformData) via
	// geom.layerIndex instead. See osgSlug_Mask_Evaluate() in SHADER_LIB_MASK.
	RenderMask(const slughorn::Mask& mask, unsigned bindingPoint);

	const slughorn::Mask& mask() const { return _mask; }
	slughorn::Mask& mask() { return _mask; }

	// MSDF-only: show the raw baked tile RGB instead of evaluating coverage. Not part of
	// slughorn::Mask (a rendering/debug concern, not authoring data) -- repack() to upload.
	void setDebug(bool debug) { _debug = debug; }

	// Re-derives the packed GPU data from the current mask()/contentOrigin and marks it for
	// re-upload. Call once an Atlas is known (compile() always has one) and again after
	// mutating mask() in place (e.g. animating params[] frame to frame for a growing-stroke/
	// radial-wipe/arc-sweep mask) if that mask is type MSDF and its msdfLayer could have
	// changed; harmless to call unconditionally otherwise.
	void repack(const Atlas& atlas);

	// Binds this mask's UBO to the constructor's bindingPoint. See class comment: this is a
	// direct, StateSet-free StateAttribute::apply() call, not a scene-graph state change.
	void apply(osg::State& state) const;

private:
	// CPU mirror of osgSlug_MaskData's std140 layout (48 bytes, zero padding -- see the
	// field-order note in ai/context-todo-mask.md, "params/params2 split"). Real int/bool
	// bit patterns are required here, NOT numeric-cast-to-float (unlike osgSlug_LayerData's
	// all-float convention) -- osgSlug_MaskData declares actual GLSL int/bool members.
	struct alignas(16) PackedData {
		slug_t params[4] = {};
		slug_t params2[2] = {};
		int32_t type = 0;
		int32_t msdfLayer = -1;
		int32_t invert = 0;
		int32_t debug = 0;
	};

	static_assert(sizeof(PackedData) == 48);

	// atlas == nullptr packs msdfLayer as -1 (no MSDF tile resolved yet); see the constructor.
	void pack(const Atlas* atlas);

	slughorn::Mask _mask;
	bool _debug = false;

	osg::ref_ptr<osg::UByteArray> _data;
	osg::ref_ptr<osg::UniformBufferBinding> _binding;
};

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
