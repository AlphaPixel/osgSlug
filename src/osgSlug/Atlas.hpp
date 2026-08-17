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
	// above, this default is NOT a no-op -- it IS the real mask coverage evaluation, always
	// linked as its own shader unit so masking works automatically (no user hook required) and
	// main() can discard before slug_Render for fragments outside the mask. See
	// ai/context-todo-mask.md, "osgSlug_FragmentMask() early hook."
	static const std::string SHADER_MASK_FRAGMENT_HOOK;
	// createDecalProgram()'s default MaskHook. Evaluates against data.uv (the decal quad's own
	// [0,1] tangent-plane position) instead of data.emCoord + osgSlug_Mask_LayerOrigin() -- a
	// decal has no per-layer canvas origin to look up, and osgSlug_Mask_LayerOrigin() reads
	// through a LayerBuffer redeclaration that assumes the standard 5-vec4 osgSlug_LayerData
	// layout, which does not match DecalDrawable's own 7-vec4 osgSlug_DecalLayerData at the same
	// SSBO binding. See SHADER_MASK_FRAGMENT_HOOK_DECAL's definition for the full reasoning.
	static const std::string SHADER_MASK_FRAGMENT_HOOK_DECAL;
	static const std::string SHADER_ATLAS_TYPES; // AtlasShapeData + binding 0 only
	static const std::string SHADER_TYPES; // SHADER_ATLAS_TYPES + LayerData + binding 1
	static const std::string SHADER_LIB_VERTEX;
	// Struct/interface content ONLY (osgSlug_FragmentData, geom/fx blocks, etc.) -- MUST stay
	// body-free. SHADER_FRAG, SHADER_MASK_FRAGMENT_HOOK, and whichever FragmentHook is active
	// all pull this in and get linked into the same Program; GLSL only allows one of several
	// linked shader objects to define a given function, so a real function body here would
	// duplicate-define across units. (A 2026-08-10 attempt to bundle a default
	// osgSlug_FragEmCoord in here broke exactly this way -- reverted.)
	static const std::string SHADER_LIB_FRAGMENT;
	// Opt-in default (identity passthrough) osgSlug_FragEmCoord via #pragma osgSlug
	// lib_fragment_em. A custom osgSlug_Fragment hook must always define BOTH
	// osgSlug_FragEmCoord and osgSlug_Fragment (linking fails otherwise -- the hook unit
	// replaces the whole no-op unit, not just one function of it); most hooks don't care about
	// tiling/em-coord remapping and were forgetting this one every time. Safe as a separate
	// opt-in (unlike folding it into SHADER_LIB_FRAGMENT above) because exactly one shader
	// object -- the active FragmentHook -- ever pulls it in. Skip this pragma and write your own
	// osgSlug_FragEmCoord if you DO need custom em-coord behavior (e.g. tiling).
	static const std::string SHADER_LIB_FRAGMENT_EM;
	static const std::string SHADER_LIB_SCANLINE; // evaluate_bezier + intersect_monotonic + scanline_sweep
	static const std::string SHADER_LIB_MASK; // osgSlug_SDF_* + osgSlug_Mask_* impls; opt-in via #pragma osgSlug lib_mask
	static const std::string SHADER_VERT; // main SSBO vertex shader (embedded)
	static const std::string SHADER_VERT_DECAL; // tangent-plane decal vertex shader (embedded)
	static const std::string SHADER_FRAG; // main fragment shader (embedded, resolved)
	static const std::string SHADER_SCANLINE_VERT; // ScanlineDrawable vertex shader
	static const std::string SHADER_SCANLINE_FRAG; // ScanlineDrawable fragment shader (resolved)

	enum Hook { VertexHook, FragmentHook, FragmentExtHook, MaskHook };

	using HookList = std::vector<std::pair<Hook, std::string>>;

	osg::StateSet* createDefaultStateSet(HookList hooks={}) const;

	// Returns a lightweight StateSet carrying only the shader program (with hooks applied).
	// No textures, uniforms, or blend state - those are inherited from the Atlas Group's StateSet.
	// Set this on a child drawable to override just the program (e.g. for vertex animation hooks).
	// Call BEFORE compile() so compile()'s getOrCreateStateSet() merges into the hook StateSet
	// rather than replacing it.
	osg::StateSet* createHookStateSet(HookList hooks={}) const;

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
