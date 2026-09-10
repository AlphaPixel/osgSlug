#pragma once

#include "Types.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Group>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/Texture2DArray>
#include <osg/BufferObject>
#include <osg/BufferIndexBinding>

OSGSLUG_ENABLE_WARNINGS

#ifdef SLUGHORN_HAS_MSDF
#include "slughorn/render.hpp"
#endif

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace osgSlug {

class Drawable; // forward declaration; full type needed only in Atlas.cpp
class RenderMask; // forward declaration; full type needed only in Atlas.cpp/Atlas.shaders.cpp

// ================================================================================================
// osgSlug::Atlas
//
// Thin OSG adapter over slughorn::Atlas.
//
// Workflow:
//
// 1. Call addShape() -> build() via the slughorn::Atlas base (unchanged).
// 2. Call packTextures() to upload the raw buffers into osg::Texture2D.
// 3. Call getCurveTexture() / getBandTexture() if you need access to the data (unlikely).
//
// The split between build() and packTextures() is intentional: build() is pure C++ and can run on
// any thread; packTextures() touches OSG and should run on the OSG main/draw thread (or before the
// scene-graph is live).
// ================================================================================================
class Atlas: public osg::Group, public slughorn::Atlas {
public:
	enum class State { Empty, Built, Packed };

	// texWidth: texture atlas width; rarely needs changing from the default.
	Atlas(uint32_t texWidth=slughorn::Atlas::DEFAULT_TEXTURE_WIDTH);
	explicit Atlas(const slughorn::Atlas& src);

	static osg::ref_ptr<Atlas> read(std::filesystem::path path);
	static osg::ref_ptr<Atlas> read(std::ifstream& ifs);

	State getState() const { return _state; }

	// Pack the raw TextureData buffers produced by build() into OSG texture objects, and set the
	// Atlas Group's StateSet with the default shader program, all textures, uniforms, blend state,
	// and SSBO binding 0. Must be called after build(). Texture packing is idempotent
	// for the textures; the StateSet program is always refreshed.
	// After packing, _state becomes Packed and any existing osgSlug::Drawable children are
	// compiled automatically.
	void packTextures();

	// Overrides osg::Group::addChild. When the atlas is already Packed, any osgSlug::Drawable
	// child that hasn't been compiled yet is compiled immediately so callers don't need to call
	// setAtlas() + compile() manually.
	bool addChild(osg::Node* child) override;

	// Valid after packTextures().
	osg::Texture2D* getCurveTexture() const { return _curveTexture.get(); }
	osg::Texture2D* getBandTexture() const { return _bandTexture.get(); }
	osg::Texture2D* getGradientTexture() const { return _gradientTexture.get(); }
	osg::Texture2D* getScanlineTexture() const { return _scanlineTexture.get(); }

	// Valid after packTextures(); null when no shapes have MSDF registered.
	osg::Texture2DArray* getMSDFTexture() const { return _msdfTexture.get(); }

	// Atlas-level shape SSBO (binding 0). Valid after packTextures().
	// Returns the 0-based index of key in the shape buffer, or throws if not found.
	uint32_t getShapeIndex(const slughorn::Key& key) const;
	osgx::Vec4Array* getShapeBuffer() const { return _shapeBuffer.get(); }

	// Always-valid "no mask" sentinel (type=-1), created once alongside this Atlas. Bound as an
	// ambient default in createDefaultStateSet() so every fragment shader can read osgSlug_mask
	// unconditionally; ShapeDrawable::drawImplementation() rebinds it after any masked
	// RenderGroup so the next group never sees a stale real mask. See
	// ai/context-todo-mask.md, "null UBO" plan.
	RenderMask* getNullMask() const { return _nullMask.get(); }

	// Vertex hook no-op; osgSlug_Vertex() returns osgSlug_VertexDefault(data) - the TRUE
	// authored pos/emCoord and the layer's baked em<->world frame, all unchanged.
	static const std::string SHADER_NOOP_VERTEX_HOOK;
	// Fragment hook no-op; osgSlug_Fragment() returns coverage unchanged.
	static const std::string SHADER_NOOP_FRAGMENT_HOOK;
	// Default for osgSlug_FragmentExt's pre-discard hook. Always linked as its own shader unit,
	// independent of fragEffects, so existing custom fragEffects units never need to know this hook
	// exists.
	static const std::string SHADER_NOOP_FRAGMENT_EXT_HOOK;
	// Default for osgSlug_FragmentMask's early (pre-slug_Render) hook. Unlike the two NOOP hooks
	// above, this default is NOT a no-op - it IS the real mask coverage evaluation, always
	// linked as its own shader unit so masking works automatically (no user hook required) and
	// main() can discard before slug_Render for fragments outside the mask. See
	// ai/context-todo-mask.md, "osgSlug_FragmentMask() early hook."
	static const std::string SHADER_MASK_FRAGMENT_HOOK;
	// createDecalProgram()'s default MaskHook. Evaluates against data.uv (the decal quad's own
	// [0,1] tangent-plane position) instead of data.emCoord + osgSlug_Mask_LayerOrigin() - a
	// decal has no per-layer canvas origin to look up, and osgSlug_Mask_LayerOrigin() reads
	// through a LayerBuffer redeclaration that assumes the standard 5-vec4 osgSlug_LayerData
	// layout, which does not match DecalDrawable's own 7-vec4 osgSlug_DecalLayerData at the same
	// SSBO binding. See SHADER_MASK_FRAGMENT_HOOK_DECAL's definition for the full reasoning.
	static const std::string SHADER_MASK_FRAGMENT_HOOK_DECAL;
	static const std::string SHADER_ATLAS_TYPES; // AtlasShapeData + binding 0 only
	static const std::string SHADER_TYPES; // SHADER_ATLAS_TYPES + LayerData + binding 1
	// #pragma osgSlug vertex - interface structs/blocks (osgSlug_VertexData/Result, geom/fx
	// out-blocks) PLUS the osgSlug_VertexDefault prototype. Everyone pulls this - it's the whole
	// contract a vertex hook needs to compile, no second pragma to remember. Body-free itself;
	// safe for hook units AND both main vertex units (SHADER_VERT/SHADER_VERT_DECAL) to share.
	static const std::string SHADER_VERTEX;
	// #pragma osgSlug vertex_lib - osgSlug_Vertex_Rotate/_Scale prototypes ONLY. A separate,
	// opt-in pragma purely by convention (unlike SHADER_LIB_FRAGMENT below, nothing here is
	// unsafe to fold into SHADER_VERTEX - these are prototypes, not bodies) - most hooks never
	// rotate/scale, so this stays out of the zero-pragma default the same way mask_lib/
	// scanline_lib are opt-in rather than bundled into every fragment shader.
	static const std::string SHADER_LIB_VERTEX;
	// Real bodies for osgSlug_VertexDefault/_Rotate/_Scale. NOT a hook-facing pragma - pulled
	// only by SHADER_VERT and SHADER_VERT_DECAL, the two (mutually exclusive per Program) main
	// vertex units, via #pragma osgSlug vertex_main. Kept as its own shared string rather than
	// inlined directly into each of those two shaders' source (the way SHADER_FRAG inlines its
	// own effect-helper bodies below) purely to avoid duplicating this text twice - fragment has
	// exactly one "body owner" unit, vertex has two. GLSL allows exactly one of a Program's
	// linked shader objects to define a given function; a hook pulling this by mistake would be
	// a duplicate-definition link error, since every hook links alongside one of these two units.
	static const std::string SHADER_VERTEX_MAIN;
	// #pragma osgSlug fragment_emcoord - struct/interface content ONLY (osgSlug_FragmentData,
	// geom/fx blocks, the mask UBO, etc.), named after the one function this pragma leaves for
	// YOU to define: osgSlug_FragmentEmCoord. MUST stay body-free - SHADER_FRAG, both
	// SHADER_MASK_FRAGMENT_HOOK* units, and SHADER_NOOP_FRAGMENT_EXT_HOOK (and any custom
	// FragmentExtHook/MaskHook) all pull this and get linked alongside whichever FragmentHook is
	// active in the same Program; GLSL only allows one linked shader object to define a given
	// function, so a real osgSlug_FragmentEmCoord body here would duplicate-define against whichever
	// FragmentHook supplies one. (A 2026-08-10 attempt to bundle a default body into this exact
	// content broke exactly this way - reverted; see project_shader_lib memory.) Use this pragma
	// (instead of the merged `fragment` below) when you need custom em-coord behavior, e.g.
	// tiling.
	static const std::string SHADER_FRAGMENT_EMCOORD;
	// #pragma osgSlug fragment - SHADER_FRAGMENT_EMCOORD plus a default (identity passthrough)
	// osgSlug_FragmentEmCoord body, merged into one pragma. This is what most fragment hooks want:
	// a custom osgSlug_Fragment hook must always define BOTH osgSlug_FragmentEmCoord and
	// osgSlug_Fragment (linking fails otherwise - the hook unit replaces the whole no-op unit,
	// not just one function of it), and most hooks don't care about tiling/em-coord remapping -
	// this makes that the zero-extra-pragma default instead of a line that's easy to forget.
	// Safe ONLY because exactly one shader object - the active FragmentHook - may ever pull this
	// (never SHADER_FRAG/SHADER_MASK_FRAGMENT_HOOK*/FragmentExtHook - see SHADER_FRAGMENT_EMCOORD
	// above, which is what those always use instead).
	static const std::string SHADER_FRAGMENT;
	// #pragma osgSlug fragment_lib - osgSlug_Effect_*/osgSlug_Fragment_FadeOut/osgSlug_MSDF*
	// helper prototypes ONLY (their bodies live inline in SHADER_FRAG, the one always-linked
	// main fragment unit, same as before). Separate from SHADER_FRAGMENT(_EMCOORD) purely by
	// convention, matching vertex_lib/mask_lib/scanline_lib: these are genuinely optional extra
	// helpers, not part of the base hook contract, so they stay an opt-in a hook pulls
	// additionally (`#pragma osgSlug fragment,fragment_lib`) rather than being bundled in.
	static const std::string SHADER_LIB_FRAGMENT;
	static const std::string SHADER_LIB_SCANLINE; // evaluate_bezier + intersect_monotonic + scanline_sweep
	static const std::string SHADER_LIB_MASK; // osgSlug_SDF_* + osgSlug_Mask_* impls; opt-in via #pragma osgSlug mask_lib
	// slug_Render/slug_RenderText + their band/curve-texture helpers, and osgSlug_CoverageFill()
	// (Slug's own analytic fill test, including the early mask-hook discard) - always linked via
	// #pragma osgSlug coverage_lib in SHADER_FRAG, and reused verbatim by osgSlug's pick fragment
	// shader so the two can never compute a different answer for the same fragment. See
	// SHADER_FRAG's own #pragma osgSlug coverage_lib pull-in comment and
	// slughorn/ai/context-todo-picking.md.
	static const std::string SHADER_LIB_COVERAGE;
	static const std::string SHADER_VERT; // main SSBO vertex shader (embedded)
	static const std::string SHADER_VERT_DECAL; // tangent-plane decal vertex shader (embedded)
	static const std::string SHADER_FRAG; // main fragment shader (embedded, resolved)
	static const std::string SHADER_SCANLINE_VERT; // ScanlineDrawable vertex shader
	static const std::string SHADER_SCANLINE_FRAG; // ScanlineDrawable fragment shader (resolved)
	// GPU object-ID pick fragment: calls osgSlug_CoverageFill() (coverage_lib) for the real
	// Slug/mask coverage test - the SAME one SHADER_FRAG's main() uses - and on a coverage pass
	// writes a packed pick ID (the `pickID` uniform's per-drawable base, plus this fragment's
	// 0-based layer offset) instead of color; discards otherwise. See createPickProgram() and
	// ai/context-todo-picking.md.
	static const std::string SHADER_PICK_FRAG;

	enum Hook { VertexHook, FragmentHook, FragmentExtHook, MaskHook };

	using HookList = std::vector<std::pair<Hook, std::string>>;

	// Everything the osgSlug Program flavors actually differ by. Same shape as osgx::VertexLayout:
	// a small struct of defaulted fields a caller overrides only where its pipeline really
	// diverges, instead of a Program-building function per flavor that hand-copies the hook
	// dispatch (which is how this started, and how it drifted).
	struct ProgramSpec {
		// The single shader unit defining main() for the vertex stage.
		std::string vertMain = {};

		// GLSL spliced in immediately after #version, into BOTH vertMain and the vertex hook unit.
		// SHADER_TYPES for pipelines reading the per-layer SSBO, SHADER_ATLAS_TYPES for shape-only
		// ones, a #define prefix on either, or empty for a pipeline that declares its own buffers
		// (PathDrawable).
		std::string types = {};

		// The unit defining main() for the fragment stage, normally SHADER_FRAG. Empty links NO
		// fragment stage at all: the caller adds its own private one to the returned Program, and
		// the three fragment hook slots plus the osgSlug_MaskBlock binding are skipped, since a
		// private fragment shader has no osgSlug_Fragment/FragmentExt/FragmentMask entry points to
		// substitute.
		std::string fragMain = {};

		// Defaults for the FragmentHook and MaskHook slots when `hooks` doesn't override them.
		// Empty means SHADER_NOOP_FRAGMENT_HOOK / SHADER_MASK_FRAGMENT_HOOK. Decals pass
		// SHADER_MASK_FRAGMENT_HOOK_DECAL; PathDrawable's Sluggit mode passes a fragHook that
		// replaces fwidth()-derived emsPerPixel with an analytic one. These are per-FLAVOR
		// defaults, not user hooks: an entry in `hooks` still wins over either.
		std::string fragHook = {};
		std::string maskHook = {};
	};

	// The one place any osgSlug Program is assembled. Attaches exactly one shader unit per slot:
	// spec's vertex main, a vertex hook unit, and (unless spec.fragMain is empty) the fragment
	// main plus the fragment/ext/mask hook units. An entry in `hooks` SUBSTITUTES that slot's
	// default rather than being attached alongside it - GLSL permits one body per function.
	//
	// Static because no flavor reads Atlas instance state; PathDrawable's Miter mode needs a
	// Program without an Atlas at all.
	static osg::Program* createProgram(const ProgramSpec& spec, const HookList& hooks={});

	// The standard SHADER_VERT + SHADER_FRAG pipeline every ShapeDrawable/SubdividedDrawable uses.
	static osg::Program* createDefaultProgram(const HookList& hooks={});

	// SHADER_VERT (unmodified - every VertexHook still runs, so a deformed shape picks correctly)
	// + SHADER_PICK_FRAG. Meant to run in a shared osgx::Picking.hpp pick camera pass alongside
	// arbitrary other pickable content, writing into the same object-ID buffer: install on a
	// StateSet as Program+OVERRIDE over that camera's own generic default program (OSG resolves a
	// deeper OVERRIDE over an ancestor's - the shared drawable's own real Program, set without
	// OVERRIDE by createDefaultStateSet(), only wins where no such ancestor exists, i.e. under the
	// normal scene root). Per-layer opt-in: SHADER_PICK_FRAG reads each layer's own pick ID
	// straight from the existing LayerBuffer SSBO (transformData.w - see
	// ShapeDrawable::setLayerPickID()), 0 = not pickable (the default), so individual layers
	// within one CompositeShape can be pickable independently with no extra uniform or wrapper
	// StateSet needed per drawable. `hooks` should normally match whatever
	// VertexHook/FragmentHook/MaskHook the drawable's real Program uses, so a hover/click test
	// lands on the same deformed/remapped/masked silhouette that's actually on screen - NOT the
	// same as the real Program's own hooks by default, since createPickProgram() has no drawable
	// to read them from (static, like createDefaultProgram()); the caller passes them through
	// explicitly.
	//
	// Does NOT consider osgSlug_FragmentExt (glow/halo can make a fragment visible past Slug's own
	// fill; deliberately out of scope - see SHADER_LIB_COVERAGE's own comment and
	// ai/context-todo-picking.md).
	static osg::Program* createPickProgram(const HookList& hooks={});

	osg::StateSet* createDefaultStateSet(HookList hooks={}) const;

	// Returns a Program that uses the tangent-plane decal vertex shader.
	// Set this on an DecalDrawable's StateSet to override the parent Geode's program.
	// (DecalDrawable::compile() calls this automatically.)
	osg::Program* createDecalProgram(HookList hooks={}) const;

	// Returns a Program using the same SHADER_VERT as createDefaultStateSet(), but compiled with
	// OSGSLUG_AXIS_PER_VERTEX defined: main() reads the em<->world tangent frame from per-vertex
	// a_axisX/a_axisY attributes instead of the per-layer LayerBuffer SSBO. Needed by any
	// SubdividedDrawable with a curved custom _positionCallback, where that frame genuinely
	// varies over the surface and a single per-layer constant is wrong (see
	// SubdividedDrawable::compile(), which calls this automatically when a callback is set).
	osg::Program* createSubdividedProgram(HookList hooks={}) const;

	static osg::ref_ptr<Atlas> fromAtlas(const slughorn::Atlas& src) {
		osg::ref_ptr<Atlas> atlas = new osgSlug::Atlas();

		static_cast<slughorn::Atlas&>(*atlas) = src;

		atlas->packTextures();

		return atlas;
	}

protected:
	// Out-of-line (not = default): _nullMask is a ref_ptr<RenderMask>, and RenderMask is only
	// forward-declared here (its full definition, in Drawable.hpp, includes this header).
	virtual ~Atlas();

	template<typename... Args>
	static osg::ref_ptr<Atlas> fromAtlas(Args&&... args) {
		return new osgSlug::Atlas(slughorn::serial::read(std::forward<Args>(args)...));
	}

private:
	static osg::ref_ptr<osg::Texture2D> _makeTexture(const slughorn::Atlas::TextureData& data);

	State _state = State::Empty;

	osg::ref_ptr<osg::Texture2D> _curveTexture;
	osg::ref_ptr<osg::Texture2D> _bandTexture;
	osg::ref_ptr<osg::Texture2D> _gradientTexture; // null when no gradients registered
	osg::ref_ptr<osg::Texture2D> _scanlineTexture; // Scanline Sweeper curve data (RGBA32F)

	osg::ref_ptr<osg::Texture2DArray> _msdfTexture; // null when no shapes have MSDF registered

	osg::ref_ptr<osgx::Vec4Array> _shapeBuffer; // atlas shape SSBO, binding 0
	std::unordered_map<slughorn::Key, uint32_t, slughorn::KeyHash> _shapeIndex;

	osg::ref_ptr<RenderMask> _nullMask; // see getNullMask()
};

}
