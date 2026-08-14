#include "osgSlug/Drawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/Shader>

OSGSLUG_ENABLE_WARNINGS

namespace {

// SHADER_LIB_VERTEX and SHADER_LIB_FRAGMENT are initialized before the first call to
// resolveShaderLibs() below. The later scanline/mask catalogs register immediately after their
// source strings are initialized, before any shader asks to expand them.
void registerOsgSlugCoreShaderLibs() {
	static const bool registered = [] {
		// "lib_fragment" is struct/interface content only (osgSlug_FragmentData, geom/fx blocks,
		// etc.) -- it MUST stay body-free, since SHADER_FRAG, SHADER_MASK_FRAGMENT_HOOK, and
		// whichever FragmentHook is active all pull it in and get linked into the same Program;
		// GLSL only allows ONE of several linked shader objects to provide a given function's
		// body. "lib_fragment_em" (a real default osgSlug_FragEmCoord body) is therefore a
		// SEPARATE, opt-in pragma -- safe only because exactly one shader object (the active
		// FragmentHook) ever chooses to pull it in.
		const osgx::ShaderLib libs[] = {
			{"lib_vertex", {}, osgSlug::Atlas::SHADER_LIB_VERTEX},
			{"lib_fragment", {}, osgSlug::Atlas::SHADER_LIB_FRAGMENT},
			{"lib_fragment_em", {}, osgSlug::Atlas::SHADER_LIB_FRAGMENT_EM}
		};

		osgx::registerShaderLibs("osgSlug", libs);

		return true;
	}();

	(void)registered;
}

// Resolves both osgSlug's registered hook libraries and osgx's PBR/IBL catalogs.
std::string resolveShaderLibs(std::string src) {
	registerOsgSlugCoreShaderLibs();
	osgx::pbr::registerShaderLibs();
	osgx::ibl::registerShaderLibs();

	return osgx::resolveShaderLibs(std::move(src));
}

// Prepend `types` immediately after the #version directive in `src`, then resolve libs.
osg::Shader* makeVertShader(const std::string& src, const std::string& types) {
	const std::string resolved = resolveShaderLibs(src);
	const auto vp = resolved.find("#version");
	const auto nl = (vp != std::string::npos) ? resolved.find('\n', vp) : std::string::npos;

	const std::string full = (nl != std::string::npos)
		? resolved.substr(0, nl + 1) + types + resolved.substr(nl + 1)
		: types + resolved
	;

	return new osg::Shader(osg::Shader::VERTEX, full);
}

}

namespace osgSlug {

// ================================================================================================
// Shader string constants
// ================================================================================================

const std::string Atlas::SHADER_ATLAS_TYPES = R"(
struct osgSlug_AtlasShapeData {
	vec4 bandXform; // xy = bandScaleX/Y, zw = bandOffsetX/Y
	vec4 shapeData; // xy = glyphLoc (ivec2), zw = bandMax (ivec2)
	vec4 originData; // xy = originX/Y, zw = unused
};

layout(std430, binding = 0) readonly buffer AtlasShapeBuffer {
	osgSlug_AtlasShapeData atlasShapes[];
};
)";

const std::string Atlas::SHADER_TYPES = SHADER_ATLAS_TYPES + R"(
// Per-layer data: one entry per layer in each drawable, indexed by (a_position.w - 1).
// Slot order: slughorn contract first (color, gradient), osgSlug machinery last (effectData).
struct osgSlug_LayerData {
	vec4 color; // RGBA flat color
	vec4 gradientMeta; // x = gradientId (1-based), yz = gradient center, w = r0_norm
	vec4 gradientXform;// gradient transform (B matrix / direction / sweep)
	vec4 effectData; // x = effectId, y = shapeIndex (into AtlasShapeBuffer), z = packed msdfLayer+msdfRange (see packMSDFData()) or -1 if no MSDF tile, w = effectParam
	vec4 transformData; // xy = layer.transform.xy (canvas-space origin); z = layer.bleed (em); w = unused
	vec4 axisX; // xyz = model-space direction of +1 em along the quad's X axis, w = worldPerEm rate
	vec4 axisY; // xyz = model-space direction of +1 em along the quad's Y axis, w = worldPerEm rate
};

layout(std430, binding = 1) buffer LayerBuffer {
	osgSlug_LayerData layers[];
};
)";

const std::string Atlas::SHADER_LIB_VERTEX = R"(
// All per-vertex data the osgSlug_Vertex hook receives.
// Use data.pos, data.emCoord, data.origin, data.effectParam, etc.
//
// pos/emCoord are the TRUE authored values - never padded. The AA margin (a ~pixel,
// pixel-denominated) and layer.bleed are pushed outward by main() AFTER the hook runs, at the
// last moment before rasterization, using the em<->world frame the hook returns - so the
// coordinates a hook computes (and the coordinates the user authored) are always exact.
struct osgSlug_VertexData {
	vec3 pos; // model-space position (xyz of a_position) - TRUE corner, no padding
	vec2 emCoord; // em-space coordinate - TRUE bounds, no padding
	vec2 uv; // normalized [0,1] UV
	int effectId; // per-layer effect selector (set via setLayerEffectId)
	vec2 origin; // shape origin in em-space (used by Rotate/Scale helpers)
	float effectParam; // per-layer float (set via setLayerEffectParam)
	float time; // osg_SimulationTime
	float bleed; // layer.bleed: em-space CONTENT margin (glow/shadow room), NOT an AA knob
	vec4 axisX; // xyz = model-space direction of +1 em along the quad's X axis, w = worldPerEm
	vec4 axisY; // xyz = model-space direction of +1 em along the quad's Y axis, w = worldPerEm
};

// What the osgSlug_Vertex hook returns. A rigid hook fills this via osgSlug_VertexDefault()
// and edits pos alone. A hook that changes the local em<->world relationship (non-uniform
// stretch, rotation, scale) must also keep emCoord and the axes truthful: emCoord is what the
// fragment stage samples the curve/band field with, and the axes are what main() uses to push
// the AA margin/bleed outward at a matched rate. Keeping these honest is exactly what makes
// data.fill trustworthy under any deformation - anchored shapes can never drift.
struct osgSlug_VertexResult {
	vec3 pos; // TRUE model-space corner position (post-hook)
	vec2 emCoord; // TRUE em coordinate for that corner (post-hook)
	vec4 axisX; // xyz = +1 em X direction (unit), w = worldPerEm rate (post-hook)
	vec4 axisY; // xyz = +1 em Y direction (unit), w = worldPerEm rate (post-hook)
};

// Helper prototypes (implementations live in the main vertex shader only - one definition per program).
osgSlug_VertexResult osgSlug_VertexDefault(osgSlug_VertexData data);
osgSlug_VertexResult osgSlug_Vertex_Rotate(osgSlug_VertexData data, float angle);
osgSlug_VertexResult osgSlug_Vertex_Scale(osgSlug_VertexData data, float scale);

// Vertex-to-fragment interface contracts. Declared here so hook vertex shaders share the same
// block definition as the main vertex shader without manual duplication.
out osgSlug_GeomBlock {
	vec2 emCoord; // em-space coordinate
	vec2 uv; // normalized [0,1] UV
	vec4 color; // effective layer color
	flat float layerIndex;// 1-based layer index (for osgSlug_layerMask); flat - used as an array index
	vec4 gradientMeta;
	vec4 gradientXform;
} geom;

out osgSlug_FxBlock {
	flat int effectId;
	flat int gradientId;
	flat int msdfLayer; // -1 = no MSDF tile
	flat float msdfRange;
	flat float effectParam;
	flat vec4 bandXform;
	flat vec4 shapeData;
} fx;
)";

const std::string Atlas::SHADER_LIB_FRAGMENT = R"(
// Vertex-to-fragment interface contracts (matching osgSlug_GeomBlock / osgSlug_FxBlock in lib_vertex).
in osgSlug_GeomBlock {
	vec2 emCoord;
	vec2 uv;
	vec4 color;
	flat float layerIndex;
	vec4 gradientMeta;
	vec4 gradientXform;
} geom;

in osgSlug_FxBlock {
	flat int effectId;
	flat int gradientId;
	flat int msdfLayer;
	flat float msdfRange;
	flat float effectParam;
	flat vec4 bandXform;
	flat vec4 shapeData;
} fx;

// Context structs for hook functions
//
// Defined once here so every shader unit that includes this lib sees the same
// definition - no manual sync across compilation units.

// All per-fragment data the osgSlug_FragmentMask early hook receives. Called before
// slug_Render - only geom.emCoord and its screen-space derivative exist yet; no fill, no
// msdfSd, no layerColor. This is deliberate: osgSlug_FragmentMask's whole point is to let
// main() discard before paying for slug_Render's curve-band loop on fragments the mask has
// already excluded, so it cannot depend on anything slug_Render produces. See
// ai/context-todo-mask.md, "osgSlug_FragmentMask() early hook."
struct osgSlug_FragmentMaskData {
	vec2 emCoord; // em-space coordinate (geom.emCoord, raw/untiled)
	vec2 uv; // normalized [0,1] UV (geom.uv) -- the decal mask hook reads this instead, since a
	// decal quad has no meaningful "canvas em-space"/layer origin of its own to evaluate against.
	vec2 emsPerPixel; // fwidth(geom.emCoord), precomputed in main() before any discard
	float time; // osg_SimulationTime
};

// All per-fragment data the osgSlug_Fragment hook receives.
struct osgSlug_FragmentData {
	float fill; // Slug analytic coverage [0,1]
	vec2 emCoord; // em-space coordinate
	vec2 uv; // normalized [0,1] UV
	vec4 layerColor; // effective layer color (after gradient resolve)
	int effectId; // per-layer effect selector
	float time; // osg_SimulationTime
	float msdfSd; // MSDF signed distance: 0.5=edge, >0.5=interior; -1.0=no tile
	float effectParam; // per-layer float (set via setLayerEffectParam)
	// fwidth(geom.emCoord), precomputed in main() before any discard - see
	// osgSlug_FragmentExtData.emsPerPixel for why: derivatives computed later, in a nested
	// hook call that follows a discard, are unreliable on some drivers for axis-aligned
	// geometry specifically (osgSlug_Mask_Coverage hit exactly this). Use this instead of
	// calling fwidth() yourself inside a hook.
	vec2 emsPerPixel;
};

// All per-fragment data the osgSlug_FragmentExt pre-discard hook receives.
struct osgSlug_FragmentExtData {
	float fill; // Slug analytic coverage [0,1]
	float msdfSd; // MSDF signed distance: 0.5=edge, >0.5=interior; -1.0=no tile
	int msdfLayer; // MSDF atlas layer index (-1=no tile)
	float msdfRange; // MSDF range (em-space half-bandwidth of the distance tile)
	vec2 emCoord; // em-space coordinate
	vec2 uv; // normalized [0,1] UV
	vec4 layerColor; // effective layer color (after gradient resolve)
	int effectId; // per-layer effect selector
	float effectParam; // per-layer float (set via setLayerEffectParam)
	float time; // osg_SimulationTime
	vec2 emsPerPixel; // fwidth(emCoord) - em-space per screen pixel
};

// Effect texture (unit 4). Bind any osg::Texture2D to unit 4 in the StateSet.
uniform sampler2D osgSlug_effectTexture;

// Effect helper prototypes (implementations live in the main fragment shader only)
vec4 osgSlug_Effect_Checkerboard(float fill, vec2 emCoord, vec4 layerColor);
vec4 osgSlug_Effect_PixelGrid(float fill, vec2 emCoord, vec4 layerColor);
vec4 osgSlug_Effect_TextureFill(float fill, vec2 uv, vec4 layerColor);
vec4 osgSlug_Effect_Wave(float fill, vec2 uv, vec4 layerColor, float time);
vec4 osgSlug_Effect_Glow(float fill, vec2 uv, vec4 layerColor, float time, float circleR);
vec4 osgSlug_Effect_GlowMSDF(float msdfSd, int msdfLayer, float msdfRange, vec4 layerColor, float effectParam, out int blendMode);

// MSDF field helpers (implementations live in the main fragment shader only).
//
// osgSlug_MSDFSd: median-of-three MSDF reconstruction at an ARBITRARY em-space coordinate --
// same tile mapping as the msdfSd the hooks already receive (0.5=edge, >0.5=interior);
// returns -1.0 if this shape has no MSDF tile registered.
//
// osgSlug_MSDFGradient: em-space gradient of the MSDF field (d(sd)/d(em), points toward the
// interior), by central differences one tile texel wide; vec2(0.0) if no tile. Use THIS --
// never dFdx/dFdy(msdfSd) - when a hook needs the field's direction: screen-space derivatives
// are constant per 2x2 hardware quad, so anything built from them (a bevel normal, a
// reflection vector) is quantized into pixel-scale blocks that sharp downstream lookups
// amplify into crunchy edges (see BUG.md, 2026-07-06). The tile texture is float (GL_RGB32F),
// so a texel-baseline difference of it is smooth per pixel.
float osgSlug_MSDFSd(vec2 emCoord);
vec2 osgSlug_MSDFGradient(vec2 emCoord);

// osgSlug_MSDFBevelNormal: tilts flatNormal toward the shape's edge as msdfSd approaches 0.5,
// giving a smoothly-curved "dome"/bevel look across any MSDF-registered shape (badge, glyph,
// whatever) instead of a flat facet. tangentU/tangentV are the world-space axes the em-space
// gradient's x/y map onto (e.g. camRight/camUp for a camera-facing, un-tilted shape).
// bevelWidth is in msdfSd units (0.5=edge..1.0=deep interior); bevelStrength scales how far the
// normal tilts at the rim. Returns flatNormal unchanged where there's no MSDF tile or no bevel.
vec3 osgSlug_MSDFBevelNormal(
	vec2 emCoord,
	float msdfSd,
	vec3 flatNormal,
	vec3 tangentU,
	vec3 tangentV,
	float bevelWidth,
	float bevelStrength
);

// Mask descriptor - populated by osgSlug::RenderMask, bound via RenderGroup/applyMask() at
// draw time (see ShapeDrawable.cpp). Field order matches RenderMask::PackedData exactly
// (largest-alignment-first: minimal std140 padding) - keep the two in sync if either changes.
// type: 0=MSDF 1=Circle 2=Rect 3=Capsule 4=Arc 5=ArcBand 6=Hexagon 7=Octagon 8=Star
// params: SDF [0..3]; MSDF stores cx,cy,r,range here (bbox derived in shader).
// params2: SDF overflow [4,5]; Arc: angle_end; ArcBand: angle_end + stroke_hw.
// msdfLayer/debug are MSDF-only fields; ignored for analytical types.
// MSDF sampling reuses osgSlug_msdfTexture (unit 3, always bound by the Atlas's own default
// StateSet) - a mask's MSDF tile lives in the same Texture2DArray every glyph/shape already
// samples, so SHADER_LIB_MASK re-declares that uniform rather than binding a second texture
// unit to the same data (see SHADER_LIB_MASK's LayerBuffer re-declaration for why re-declaring
// instead of importing is the normal, required pattern for a separately-linked shader object).
//
// NOTE: no contentOrigin field here (deliberately removed) - it was a per-MASK value shared
// across every layer in a masked RenderGroup, but "canvas bbox min" is fundamentally a
// per-LAYER property (each layer has its own transform.xy). A single shared value only
// happened to work for single-layer masked composites; multi-layer composites where layers
// sit at different canvas positions (e.g. this file's 2-rect demo, or the paragraph-of-text/
// COLRv1-emoji demos this whole feature targets) need it per-layer. See osgSlug_Mask_Evaluate
// below - it reads transformData.xy from the per-layer LayerBuffer SSBO instead.
struct osgSlug_MaskData {
	vec4 params;
	vec2 params2;
	int type;
	int msdfLayer;
	bool invert;
	bool debug;
};

// No inline layout(binding=N): inline UBO binding syntax is illegal pre-4.20. Bound instead via
// Program::addBindUniformBlock() - every Program that links SHADER_FRAG does this now (see
// createDefaultStateSet()/createHookStateSet()/createDecalProgram()/PathDrawable.cpp), since
// osgSlug_FragmentMask() below reads this block unconditionally, not just when a mask-aware
// hook opts in.
layout(std140) uniform osgSlug_MaskBlock {
	osgSlug_MaskData osgSlug_mask;
};
)";

// Opt-in via #pragma osgSlug lib_fragment_em -- see Atlas.hpp's SHADER_LIB_FRAGMENT_EM comment.
const std::string Atlas::SHADER_LIB_FRAGMENT_EM = R"(
vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}
)";

const std::string Atlas::SHADER_VERT = R"(
#version 430 core

#pragma osgSlug lib_vertex

// AA margin in PIXELS, pushed outward at the last moment before rasterization (see main()).
// Deliberately an internal constant, not an authoring value: the margin exists only so the
// rasterizer has fragments to shade slightly past the true edge - it is an implementation
// detail of rasterization, never part of the user's coordinates.
const float OSGSLUG_MARGIN_PX = 1.5;

osgSlug_VertexResult osgSlug_VertexDefault(osgSlug_VertexData data) {
	osgSlug_VertexResult r;
	r.pos = data.pos;
	r.emCoord = data.emCoord;
	r.axisX = data.axisX;
	r.axisY = data.axisY;
	return r;
}

// NOTE: the pivot reconstruction (pos - emCoord + origin) is only correct at layer.scale = 1
// (a pre-existing, documented limitation - baked curves are the workaround for glyphs).
osgSlug_VertexResult osgSlug_Vertex_Rotate(osgSlug_VertexData data, float angle) {
	float c = cos(angle), s = sin(angle);
	mat2 R = mat2(c, s, -s, c);
	vec2 pivot = data.pos.xy - data.emCoord.xy + data.origin;
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);
	r.pos.xy = R * (data.pos.xy - pivot) + pivot;
	r.axisX.xy = R * r.axisX.xy; // rotate the margin-push frame along with the quad
	r.axisY.xy = R * r.axisY.xy;
	return r;
}

osgSlug_VertexResult osgSlug_Vertex_Scale(osgSlug_VertexData data, float scale) {
	vec2 pivot = data.pos.xy - data.emCoord.xy + data.origin;
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);
	r.pos.xy = (data.pos.xy - pivot) * scale + pivot;
	r.axisX.w *= scale; // uniform scale changes the em<->world rate, not the directions
	r.axisY.w *= scale;
	return r;
}

layout(location = 0) in vec4 a_position; // xyz = world pos (TRUE corner), w = layer index (1-based)
layout(location = 1) in vec4 a_emCoord; // xy = em-coord (TRUE bounds), zw = UV [0,1]

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;
uniform vec2 osgSlug_viewport; // live viewport size (Atlas::ViewportUniformCallback)

// Defined in the linked effects or noop unit.
osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data);

// +/-1 at quad-boundary corners (uv is exactly 0 or 1 there), 0 for interior grid vertices
// (SubdividedDrawable) so they are never pushed.
vec2 osgSlug_CornerDir(vec2 uv) {
	return vec2(
		uv.x <= 0.0 ? -1.0 : (uv.x >= 1.0 ? 1.0 : 0.0),
		uv.y <= 0.0 ? -1.0 : (uv.y >= 1.0 ? 1.0 : 0.0)
	);
}

// World units per screen pixel at pos, moving along the (unit) model-space direction axis.
// Exact under an orthographic projection; first-order (excellent) under perspective.
float osgSlug_WorldPerPixel(vec3 pos, vec3 axis) {
	vec4 c = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
	vec4 d = osg_ModelViewProjectionMatrix * vec4(axis, 0.0);
	float cw2 = max(c.w * c.w, 1e-12);
	vec2 ndcPerWorld = (d.xy * c.w - c.xy * d.w) / cw2;
	float pixelsPerWorld = length(ndcPerWorld * osgSlug_viewport * 0.5);
	return 1.0 / max(pixelsPerWorld, 1e-8);
}

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	osgSlug_LayerData ld = layers[layerIdx];
	osgSlug_AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

	int effectId = int(ld.effectData.x + 0.5);

	osgSlug_VertexData vData;

	vData.pos = a_position.xyz;
	vData.emCoord = a_emCoord.xy;
	vData.uv = a_emCoord.zw;
	vData.effectId = effectId;
	vData.origin = sd.originData.xy;
	vData.effectParam = ld.effectData.w;
	vData.time = osg_SimulationTime;
	vData.bleed = ld.transformData.z;
	vData.axisX = ld.axisX;
	vData.axisY = ld.axisY;

	osgSlug_VertexResult r = osgSlug_Vertex(vData);

	// Rasterization-space fudge, applied at the LAST possible moment: push boundary corners
	// outward by the pixel AA margin plus any authored layer.bleed, and push emCoord by the
	// exactly-matching em amount, so the em<->world rate across the margin band equals the
	// quad's own (post-hook) rate BY CONSTRUCTION. The authored/logical coordinates in r are
	// never disturbed - anchored shapes cannot drift, by design rather than by tuning.
	vec2 dir = osgSlug_CornerDir(a_emCoord.zw);
	float rateX = max(r.axisX.w, 1e-8);
	float rateY = max(r.axisY.w, 1e-8);
	float pushX = dir.x * (OSGSLUG_MARGIN_PX * osgSlug_WorldPerPixel(r.pos, r.axisX.xyz) + vData.bleed * rateX);
	float pushY = dir.y * (OSGSLUG_MARGIN_PX * osgSlug_WorldPerPixel(r.pos, r.axisY.xyz) + vData.bleed * rateY);
	vec3 pos = r.pos + r.axisX.xyz * pushX + r.axisY.xyz * pushY;

	geom.emCoord = r.emCoord + vec2(pushX / rateX, pushY / rateY);
	geom.uv = a_emCoord.zw;
	geom.layerIndex = a_position.w;
	geom.color = ld.color;
	fx.bandXform = sd.bandXform;
	fx.shapeData = sd.shapeData;
	fx.effectId = effectId;
	if(ld.effectData.z < 0.0) {
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
	}
	else {
		// See packMSDFData() (Drawable/Util.hpp): payload lives entirely in the mantissa (bits
		// 0-22); sign+exponent are pinned to 0x3F800000 so the bit pattern is always a normal
		// float, never a subnormal a GPU might flush to zero on load.
		uint msdfPacked = floatBitsToUint(ld.effectData.z);

		fx.msdfLayer = int(bitfieldExtract(msdfPacked, 12, 11)) - 1;
		fx.msdfRange = float(bitfieldExtract(msdfPacked, 0, 12)) / 256.0;
	}
	fx.effectParam = ld.effectData.w;
	fx.gradientId = int(ld.gradientMeta.x + 0.5);
	geom.gradientMeta = ld.gradientMeta;
	geom.gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
)";

const std::string Atlas::SHADER_VERT_DECAL = R"(
#version 430 core

#pragma osgSlug lib_vertex

// Same helper implementations as SHADER_VERT (each program links exactly one main vertex
// unit, so the duplication across the two strings is intentional and safe).
osgSlug_VertexResult osgSlug_VertexDefault(osgSlug_VertexData data) {
	osgSlug_VertexResult r;
	r.pos = data.pos;
	r.emCoord = data.emCoord;
	r.axisX = data.axisX;
	r.axisY = data.axisY;
	return r;
}

osgSlug_VertexResult osgSlug_Vertex_Rotate(osgSlug_VertexData data, float angle) {
	float c = cos(angle), s = sin(angle);
	mat2 R = mat2(c, s, -s, c);
	vec2 pivot = data.pos.xy - data.emCoord.xy + data.origin;
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);
	r.pos.xy = R * (data.pos.xy - pivot) + pivot;
	r.axisX.xy = R * r.axisX.xy;
	r.axisY.xy = R * r.axisY.xy;
	return r;
}

osgSlug_VertexResult osgSlug_Vertex_Scale(osgSlug_VertexData data, float scale) {
	vec2 pivot = data.pos.xy - data.emCoord.xy + data.origin;
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);
	r.pos.xy = (data.pos.xy - pivot) * scale + pivot;
	r.axisX.w *= scale;
	r.axisY.w *= scale;
	return r;
}

// Vertex layout (set by DecalDrawable::compile()):
//
// a_position: xy = (lu, lv) normalized grid in [0,1], z = 0, w = layer index (1-based)
// a_emCoord: xy = shape em-space coordinate, zw = (lu, lv) UV

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_emCoord;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

// Defined in the linked effects or noop unit.
osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data);

struct osgSlug_DecalLayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 center;
	vec4 tangentEast;
	vec4 tangentNorth;
};

layout(std430, binding = 1) buffer DecalLayerBuffer {
	osgSlug_DecalLayerData decalLayers[];
};

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	osgSlug_DecalLayerData ld = decalLayers[layerIdx];
	osgSlug_AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

	int effectId = int(ld.effectData.x + 0.5);

	// Map normalized grid [0,1] to centered [-0.5, 0.5], then displace along tangent frame.
	float lu = a_position.x - 0.5;
	float lv = a_position.y - 0.5;

	vec3 P = ld.center.xyz;
	float sphereR = ld.center.w;
	vec3 Te = ld.tangentEast.xyz;
	vec3 Tn = ld.tangentNorth.xyz;

	vec3 planePos = P + lu * Te + lv * Tn;

	// sphereR == 0 is the planar sentinel (DecalDrawable::addPlanarDecal()): use the tangent-plane
	// point directly, no gnomonic reprojection. sphereR > 0 keeps the original sphere-surface
	// projection back onto the sphere.
	vec3 world = sphereR > 0.0 ? normalize(planePos) * sphereR : planePos;

	osgSlug_VertexData vData;

	vData.pos = world;
	vData.emCoord = a_emCoord.xy;
	vData.uv = a_emCoord.zw;
	vData.effectId = effectId;
	vData.origin = sd.originData.xy;
	vData.effectParam = ld.effectData.w;
	vData.time = osg_SimulationTime;
	// TODO(expand-removal): decals still bake their AA margin on the CPU (DecalDrawable.cpp's
	// DECAL_EXPAND) rather than using the GPU-live margin push, so hooks get a neutral frame
	// here and main() applies no push. Adapt like SHADER_VERT when decals get the full
	// treatment (the tangent frame Te/Tn is the natural axis source).
	vData.bleed = 0.0;
	vData.axisX = vec4(1.0, 0.0, 0.0, 1.0);
	vData.axisY = vec4(0.0, 1.0, 0.0, 1.0);

	osgSlug_VertexResult r = osgSlug_Vertex(vData);
	vec3 pos = r.pos;

	geom.emCoord = r.emCoord;
	geom.uv = a_emCoord.zw;
	geom.layerIndex = a_position.w;
	geom.color = ld.color;
	fx.bandXform = sd.bandXform;
	fx.shapeData = sd.shapeData;
	fx.effectId = effectId;
	fx.gradientId = int(ld.gradientMeta.x + 0.5);
	// DecalDrawable::compile() already packs this the same way SHADER_VERT does (see
	// packMSDFData()) - just never got unpacked here until now.
	if(ld.effectData.z < 0.0) {
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
	}
	else {
		uint msdfPacked = floatBitsToUint(ld.effectData.z);

		fx.msdfLayer = int(bitfieldExtract(msdfPacked, 12, 11)) - 1;
		fx.msdfRange = float(bitfieldExtract(msdfPacked, 0, 12)) / 256.0;
	}
	fx.effectParam = ld.effectData.w;
	geom.gradientMeta = ld.gradientMeta;
	geom.gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
)";

// Main fragment shader. Stored pre-resolved so PathDrawable.cpp can use it directly.
// The #pragma osgSlug lib_fragment is expanded at static init time - SHADER_FRAG always
// contains the fully substituted SHADER_LIB_FRAGMENT content (struct defs + effect helpers).
const std::string Atlas::SHADER_FRAG = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_fragment

vec4 osgSlug_Effect_Checkerboard(float fill, vec2 emCoord, vec4 layerColor) {
	const float SCALE = 300.0;
	vec2 s = emCoord * SCALE;
	float check = mod(floor(s.x) + floor(s.y), 2.0);
	vec3 colorA = layerColor.rgb;
	vec3 colorB = min(layerColor.rgb + vec3(0.25), vec3(1.0));
	return vec4(mix(colorA, colorB, check), fill * layerColor.a);
}

vec4 osgSlug_Effect_PixelGrid(float fill, vec2 emCoord, vec4 layerColor) {
	const float SCALE = 200.0;
	vec2 s = emCoord * SCALE;
	vec2 f = fract(s);
	vec2 edgeDist = min(f, 1.0 - f);
	vec2 fw = fwidth(s);
	vec2 lineMask = smoothstep(fw, vec2(0.0), edgeDist);
	float atLine = max(lineMask.x, lineMask.y);
	vec3 cellColor = min(layerColor.rgb + vec3(0.12), vec3(1.0));
	vec3 lineColor = layerColor.rgb * 0.55;
	return vec4(mix(cellColor, lineColor, atLine), fill * layerColor.a);
}

vec4 osgSlug_Effect_TextureFill(float fill, vec2 uv, vec4 layerColor) {
	vec4 s = texture(osgSlug_effectTexture, uv);
	return vec4(mix(layerColor.rgb, s.rgb, s.a), fill * layerColor.a);
}

vec4 osgSlug_Effect_Wave(float fill, vec2 uv, vec4 layerColor, float time) {
	float scrolled = uv.x + time * 0.3;
	float wave = sin(scrolled * 6.28318 * 3.0) * 0.15 + 0.5;
	float dist = abs(uv.y - wave);
	float edge = smoothstep(0.04, 0.01, dist);
	vec3 colorTop = vec3(1.0, 0.6, 0.1);
	vec3 colorBottom = vec3(0.1, 0.5, 1.0);
	vec3 waveFill = uv.y > wave ? colorTop : colorBottom;
	return vec4(mix(waveFill, vec3(1.0), edge), fill * layerColor.a);
}

vec4 osgSlug_Effect_Glow(float fill, vec2 uv, vec4 layerColor, float time, float circleR) {
	vec2 p = uv * 2.0 - 1.0;
	float dist = abs(length(p) - circleR);

	vec2 lightDir = normalize(vec2(sin(time), cos(time)));
	float spotlight = dot(p, lightDir) + 1.0;

	float glow = 0.09 / (dist + 0.008);

	const float GLOW_SIGMA = 0.22;
	float fade = exp(-dist * dist / (GLOW_SIGMA * GLOW_SIGMA));

	return vec4(layerColor.rgb * spotlight * glow, fill * layerColor.a * fade);
}

vec4 osgSlug_Effect_GlowMSDF(float msdfSd, int msdfLayer, float msdfRange, vec4 layerColor, float effectParam, out int blendMode) {
	blendMode = 0;

	if(msdfLayer < 0) return vec4(0.0);

	float dist = 0.5 - msdfSd;
	float distEm = dist * 2.0 * msdfRange;

	const float SEAM_HALF_EM = 0.005;
	float glowMask = smoothstep(-SEAM_HALF_EM, SEAM_HALF_EM, distEm);

	const float EASE_WIDTH_EM = 0.02;
	float ease = smoothstep(0.0, EASE_WIDTH_EM, distEm);

	float outerFadeEm = effectParam > 0.0 ? effectParam : msdfRange * 0.5;
	float alpha = (1.0 - smoothstep(0.0, outerFadeEm, distEm)) * glowMask * ease;

	if(alpha < 0.001) return vec4(0.0);

	return vec4(layerColor.rgb, alpha);
}

uniform float osg_SimulationTime;

uniform sampler2D osgSlug_curveTexture;
uniform usampler2D osgSlug_bandTexture;
uniform sampler2D osgSlug_gradientTexture;
uniform sampler2DArray osgSlug_msdfTexture;
uniform int osgSlug_gradientCount;
uniform int osgSlug_debugMode;
uniform bool osgSlug_textMode; // enables MSAA, stem darkening, and gamma for text layers
uniform bool osgSlug_stemDarken; // requires osgSlug_textMode; true = apply stem darkening to edge
uniform float osgSlug_gamma; // 1.0 = off, 2.2 = dark-on-light, ~0.454 = light-on-dark
// Bitmask controlling which layers are visible. Each bit corresponds to a 1-based layer index.
// 0 (default) = all layers visible. Non-zero = apply filter; discard if bit (1 << layerIndex) is clear.
uniform int osgSlug_layerMask;

out vec4 color;

// log2(atlas.getTextureWidth()); set by Atlas::createDefaultStateSet() via __builtin_ctz.
uniform int osgSlug_texWidth;

// Must match slughorn::Atlas::INDIRECTION_SIZE.
#define SLUG_INDIRECTION_SIZE 32

// ================================================================================================
// MSDF field helpers (prototypes + usage contract in SHADER_LIB_FRAGMENT)
// ================================================================================================

float osgSlug_MSDFSd(vec2 emCoord) {
	if(fx.msdfLayer < 0) return -1.0;

	vec2 emOrigin = -fx.bandXform.zw / fx.bandXform.xy;
	vec2 emSize = float(SLUG_INDIRECTION_SIZE) / fx.bandXform.xy;
	vec2 tileUV = (emCoord - emOrigin + fx.msdfRange) / (emSize + 2.0 * fx.msdfRange);
	vec3 msd = texture(osgSlug_msdfTexture, vec3(tileUV, float(fx.msdfLayer))).rgb;

	return max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
}

vec2 osgSlug_MSDFGradient(vec2 emCoord) {
	if(fx.msdfLayer < 0) return vec2(0.0);

	// One tile texel expressed in em units - the tile spans the shape's em bbox plus
	// msdfRange of padding on every side (the same denominator as tileUV above).
	vec2 emSpan = float(SLUG_INDIRECTION_SIZE) / fx.bandXform.xy + 2.0 * fx.msdfRange;
	vec2 dEm = emSpan / vec2(textureSize(osgSlug_msdfTexture, 0).xy);

	return vec2(
		osgSlug_MSDFSd(emCoord + vec2(dEm.x, 0.0)) - osgSlug_MSDFSd(emCoord - vec2(dEm.x, 0.0)),
		osgSlug_MSDFSd(emCoord + vec2(0.0, dEm.y)) - osgSlug_MSDFSd(emCoord - vec2(0.0, dEm.y))
	) / (2.0 * dEm);
}

vec3 osgSlug_MSDFBevelNormal(
	vec2 emCoord,
	float msdfSd,
	vec3 flatNormal,
	vec3 tangentU,
	vec3 tangentV,
	float bevelWidth,
	float bevelStrength
) {
	const float EPSILON = 0.0001;

	if(msdfSd < 0.0) return flatNormal;

	float bevel = 1.0 - clamp((msdfSd - 0.5) / bevelWidth, 0.0, 1.0);

	if(bevel <= EPSILON) return flatNormal;

	// Gradient points toward the interior; the bevel wants interior->edge, hence the negation.
	vec2 grad = -osgSlug_MSDFGradient(emCoord);
	float gradLen = length(grad);

	if(gradLen <= EPSILON) return flatNormal;

	vec2 edgeDir = grad / gradLen;

	return normalize(flatNormal + (tangentU * edgeDir.x + tangentV * edgeDir.y) * bevel * bevelStrength);
}

// ================================================================================================
// Slug core
// ================================================================================================

uint slug_CalcRootCode(float y1, float y2, float y3) {
	uint i1 = floatBitsToUint(y1) >> 31u;
	uint i2 = floatBitsToUint(y2) >> 30u;
	uint i3 = floatBitsToUint(y3) >> 29u;

	uint shift = (i2 & 2u) | (i1 & ~2u);
	shift = (i3 & 4u) | (shift & ~4u);

	return ((0x2E74u >> shift) & 0x0101u);
}

vec2 slug_SolveHorizPoly(vec4 p12, vec2 p3) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.y;
	float rb = 0.5 / b.y;

	float d = sqrt(max(b.y * b.y - a.y * p12.y, 0.0));
	float t1 = (b.y - d) * ra;
	float t2 = (b.y + d) * ra;

	if(abs(a.y) < 1.0 / 65536.0) { t1 = p12.y * rb; t2 = t1; }

	return vec2(
		(a.x * t1 - b.x * 2.0) * t1 + p12.x,
		(a.x * t2 - b.x * 2.0) * t2 + p12.x
	);
}

vec2 slug_SolveVertPoly(vec4 p12, vec2 p3) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float ra = 1.0 / a.x;
	float rb = 0.5 / b.x;

	float d = sqrt(max(b.x * b.x - a.x * p12.x, 0.0));
	float t1 = (b.x - d) * ra;
	float t2 = (b.x + d) * ra;

	if(abs(a.x) < 1.0 / 65536.0) { t1 = p12.x * rb; t2 = t1; }

	return vec2(
		(a.y * t1 - b.y * 2.0) * t1 + p12.y,
		(a.y * t2 - b.y * 2.0) * t2 + p12.y
	);
}

ivec2 slug_CalcBandLoc(ivec2 glyphLoc, uint offset) {
	ivec2 bandLoc = ivec2(glyphLoc.x + int(offset), glyphLoc.y);

	bandLoc.y += bandLoc.x >> osgSlug_texWidth;
	bandLoc.x &= (1 << osgSlug_texWidth) - 1;

	return bandLoc;
}

float slug_CalcCoverage(float xcov, float ycov, float xwgt, float ywgt) {
	float coverage = max(
		abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0),
		min(abs(xcov), abs(ycov))
	);

	return clamp(coverage, 0.0, 1.0);
}

// ------------------------------------------------------------------------------------------------
// slug_BandY / slug_BandX
// ------------------------------------------------------------------------------------------------
int slug_BandY(ivec2 glyphLoc, vec4 bandTransform, vec2 renderCoord) {
	int q = clamp(int(renderCoord.y * bandTransform.y + bandTransform.w), 0, SLUG_INDIRECTION_SIZE - 1);

	return int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + q, glyphLoc.y), 0).r);
}

int slug_BandX(ivec2 glyphLoc, vec4 bandTransform, vec2 renderCoord) {
	int q = clamp(int(renderCoord.x * bandTransform.x + bandTransform.z), 0, SLUG_INDIRECTION_SIZE - 1);

	return int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + q, glyphLoc.y), 0).r);
}

// ------------------------------------------------------------------------------------------------
// slug_Render
// ------------------------------------------------------------------------------------------------
float slug_Render(
	vec2 renderCoord,
	vec2 pixelsPerEm,
	vec4 bandTransform,
	ivec2 glyphLoc,
	ivec2 bandMax,
	out int totalIterations
) {
	int curveIndex;

	int bandY = slug_BandY(glyphLoc, bandTransform, renderCoord);
	int bandX = slug_BandX(glyphLoc, bandTransform, renderCoord);

	float xcov = 0.0;
	float xwgt = 0.0;
	int iters = 0;

	uvec2 hbandData = texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + 2 * SLUG_INDIRECTION_SIZE + bandY, glyphLoc.y), 0).xy;
	ivec2 hbandLoc = slug_CalcBandLoc(glyphLoc, hbandData.y);

	for(curveIndex = 0; curveIndex < int(hbandData.x); curveIndex++) {
		iters++;

		ivec2 curveLoc = ivec2(texelFetch(osgSlug_bandTexture, ivec2(hbandLoc.x + curveIndex, hbandLoc.y), 0).xy);

		vec4 p12 = texelFetch(osgSlug_curveTexture, curveLoc, 0) - vec4(renderCoord, renderCoord);
		vec2 p3 = texelFetch(osgSlug_curveTexture, ivec2(curveLoc.x + 1, curveLoc.y), 0).xy - renderCoord;

		if(max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -0.5) break;

		uint code = slug_CalcRootCode(p12.y, p12.w, p3.y);

		if(code != 0u) {
			vec2 r = slug_SolveHorizPoly(p12, p3) * pixelsPerEm.x;

			if((code & 1u) != 0u) {
				xcov += clamp(r.x + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}

			if(code > 1u) {
				xcov -= clamp(r.y + 0.5, 0.0, 1.0);
				xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	float ycov = 0.0;
	float ywgt = 0.0;

	uvec2 vbandData = texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + 2 * SLUG_INDIRECTION_SIZE + bandMax.y + 1 + bandX, glyphLoc.y), 0).xy;
	ivec2 vbandLoc = slug_CalcBandLoc(glyphLoc, vbandData.y);

	for(curveIndex = 0; curveIndex < int(vbandData.x); curveIndex++) {
		iters++;

		ivec2 curveLoc = ivec2(texelFetch(osgSlug_bandTexture, ivec2(vbandLoc.x + curveIndex, vbandLoc.y), 0).xy);

		vec4 p12 = texelFetch(osgSlug_curveTexture, curveLoc, 0) - vec4(renderCoord, renderCoord);
		vec2 p3 = texelFetch(osgSlug_curveTexture, ivec2(curveLoc.x + 1, curveLoc.y), 0).xy - renderCoord;

		if(max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -0.5) break;

		uint code = slug_CalcRootCode(p12.x, p12.z, p3.x);

		if(code != 0u) {
			vec2 r = slug_SolveVertPoly(p12, p3) * pixelsPerEm.y;

			if((code & 1u) != 0u) {
				ycov -= clamp(r.x + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0));
			}

			if(code > 1u) {
				ycov += clamp(r.y + 0.5, 0.0, 1.0);
				ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0));
			}
		}
	}

	totalIterations = iters;

	return slug_CalcCoverage(xcov, ycov, xwgt, ywgt);
}

// ------------------------------------------------------------------------------------------------
// slug_RenderText
// ------------------------------------------------------------------------------------------------
float slug_RenderText(
	vec2 renderCoord,
	vec2 emsPerPixel,
	vec2 pixelsPerEm,
	vec4 bandTransform,
	ivec2 glyphLoc,
	ivec2 bandMax,
	out int totalIterations
) {
	float ppem = 1.0 / max(emsPerPixel.x, emsPerPixel.y);

	int iters;
	float c = slug_Render(renderCoord, pixelsPerEm, bandTransform, glyphLoc, bandMax, iters);

#ifndef OSGSLUG_NO_MSAA
	if(ppem < 16.0) {
		vec2 d = emsPerPixel * (1.0 / 3.0);
		int i1, i2, i3, i4;
		float msaa = 0.25 * (
			slug_Render(renderCoord + vec2(-d.x, -d.y), pixelsPerEm, bandTransform, glyphLoc, bandMax, i1) +
			slug_Render(renderCoord + vec2( d.x, -d.y), pixelsPerEm, bandTransform, glyphLoc, bandMax, i2) +
			slug_Render(renderCoord + vec2(-d.x, d.y), pixelsPerEm, bandTransform, glyphLoc, bandMax, i3) +
			slug_Render(renderCoord + vec2( d.x, d.y), pixelsPerEm, bandTransform, glyphLoc, bandMax, i4)
		);
		float msaaAmount = 1.0 - smoothstep(8.0, 16.0, ppem);

		c = mix(c, msaa, msaaAmount);

		iters += i1 + i2 + i3 + i4;
	}
#endif

	totalIterations = iters;

	return c;
}

// ------------------------------------------------------------------------------------------------
// slug_Heatmap
// ------------------------------------------------------------------------------------------------
vec3 slug_Heatmap(float t) {
	t = clamp(t, 0.0, 1.0);

	return t < 0.5
		? mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), t * 2.0)
		: mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), t * 2.0 - 1.0)
	;
}

// ------------------------------------------------------------------------------------------------
// slug_EmToUV
// ------------------------------------------------------------------------------------------------
vec2 slug_EmToUV(vec2 emCoord, vec4 bandXform) {
	vec2 emOrigin = -bandXform.zw / bandXform.xy;
	vec2 emSize = float(SLUG_INDIRECTION_SIZE) / bandXform.xy;

	return (emCoord - emOrigin) / emSize;
}

// ================================================================================================
// slug_ApplyDebug
// ================================================================================================
vec4 slug_ApplyDebug(
	float fill,
	vec2 emCoord,
	vec4 layerColor,
	ivec2 glyphLoc,
	vec4 bandXform,
	int iterations
) {
	vec2 bandCoord = emCoord * bandXform.xy + bandXform.zw;

	int qY = clamp(int(bandCoord.y), 0, SLUG_INDIRECTION_SIZE - 1);
	int qX = clamp(int(bandCoord.x), 0, SLUG_INDIRECTION_SIZE - 1);

	ivec2 bandIdx = ivec2(
		int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + qX, glyphLoc.y), 0).r),
		int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + qY, glyphLoc.y), 0).r)
	);

	if(osgSlug_debugMode == 1) {
		bool checker = ((bandIdx.x + bandIdx.y) & 1) == 0;
		vec3 altColor = 1.0 - layerColor.rgb;

		return vec4(checker ? layerColor.rgb : altColor, fill * layerColor.a);
	}

	int bY_prev = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + max(qY - 1, 0), glyphLoc.y), 0).r);
	int bY_next = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + min(qY + 1, SLUG_INDIRECTION_SIZE - 1), glyphLoc.y), 0).r);
	int bX_prev = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + max(qX - 1, 0), glyphLoc.y), 0).r);
	int bX_next = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + min(qX + 1, SLUG_INDIRECTION_SIZE - 1), glyphLoc.y), 0).r);

	// dY/dX is fwidth(bandCoord) ~= INDIRECTION_SIZE / (shape's screen size in pixels) --
	// meant to antialias this highlight over ~1 screen pixel. For a shape small enough on
	// screen that this exceeds a single slot's width, smoothstep's edge1 lands outside
	// fracY/fracX's own [0,1] range and never fully decays back to 0, washing the entire
	// shape in highlight instead of a thin line at the true boundary. Clamp it to a sane
	// fraction of a slot so small shapes still get a highlight, just not one that eats the
	// whole shape.
	// smoothstep(edge0, edge1, x) is UNDEFINED per the GLSL spec when edge0 >= edge1 --
	// internally it divides by (edge1-edge0). fwidth() legitimately returns exactly 0.0
	// wherever the screen-space derivative underflows float32 precision (more likely, not
	// less, at high zoom - the true analytic change between adjacent pixels shrinks
	// continuously), which made dY/dX == 0.0 here and fed a 0/0 into smoothstep below --
	// NaN or driver-dependent garbage, then straight into mix()'s blend factor, reading as
	// per-pixel noise along the boundary. Floor it away from 0 so edge0 < edge1 always.
	float fracY = fract(bandCoord.y);
	float dY = clamp(fwidth(bandCoord.y), 1e-5, 0.15);
	float edgeY = 0.0;
	if(bandIdx.y != bY_prev) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, fracY));
	if(bandIdx.y != bY_next) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, 1.0 - fracY));

	float fracX = fract(bandCoord.x);
	float dX = clamp(fwidth(bandCoord.x), 1e-5, 0.15);
	float edgeX = 0.0;
	if(bandIdx.x != bX_prev) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, fracX));
	if(bandIdx.x != bX_next) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, 1.0 - fracX));

	float atEdge = max(edgeY, edgeX);

	// atEdge highlights a thin axis-aligned strip (the band grid line); fill's own
	// antialiasing highlights a thin strip along the shape's (generally diagonal/curved)
	// silhouette. Wherever the shape's boundary happens to run near-tangent to a grid
	// line, the grid's axis-aligned strip intersects the diagonal silhouette strip over a
	// long run instead of a point, and blending both signals at once there reads as a
	// jagged/stepped edge - even though fill itself stays perfectly smooth underneath.
	// This only ever happens where fill is already mid-antialiasing (fully inside/outside
	// pixels can't show a silhouette notch), so suppress the grid highlight there and let
	// fill's own smooth edge show through unmodified; the grid remains fully visible
	// everywhere else, including right up to the edge.
	atEdge *= abs(fill - 0.5) * 2.0;

	if(osgSlug_debugMode == 2) {
		return vec4(mix(layerColor.rgb, 1.0 - layerColor.rgb, atEdge), fill * layerColor.a);
	}

	const int maxIterations = 24;
	float t = float(iterations) / float(maxIterations);
	vec3 heatColor = slug_Heatmap(t);

	if(osgSlug_debugMode == 4) {
		return vec4(heatColor, fill * layerColor.a);
	}

	if(osgSlug_debugMode == 5) {
		return vec4(mix(heatColor, vec3(1.0), atEdge), fill * layerColor.a);
	}

	return vec4(1.0, 0.0, 1.0, fill * layerColor.a);
}

float slug_StemDarken(float coverage, float brightness, float ppem) {
	float k = mix(pow(2.0, brightness - 0.5), 1.0, smoothstep(8.0, 48.0, ppem));

	return pow(coverage, k);
}

// Defined in the linked effects or noop unit.
vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time);
vec4 osgSlug_Fragment(osgSlug_FragmentData data);

// Early mask hook: called BEFORE slug_Render (see main() below), so it can discard fragments
// outside the mask without ever paying for Slug's curve-band loop on them. Defined in its own
// always-linked unit (see Atlas::SHADER_MASK_FRAGMENT_HOOK) whose DEFAULT implementation is the
// real mask evaluation (not a no-op like every other hook's default) - masking is automatic,
// no user-authored hook required. Returns coverage in [0,1] (invert already applied); main()
// discards below threshold and folds the surviving value into the final alpha after
// osgSlug_Fragment/osgSlug_FragmentExt run, completely unaware masking exists.
float osgSlug_FragmentMask(osgSlug_FragmentMaskData data);

// Pre-discard hook: fires for EVERY quad fragment, even where fill < 0.001 (outside Slug's
// own coverage). This is what lets exterior-fragment effects (glow, halos) attach to a
// standard Slug drawable without a separate MSDF-only program - contrast with
// osgSlug_Fragment(), which only ever sees fragments Slug already considers covered.
// Defined in its own always-linked unit (see Atlas::SHADER_NOOP_FRAGMENT_EXT), independent
// of osgSlug_Fragment's effects unit, so existing custom effects units never need to know
// this hook exists.
//
// data.msdfSd: median-of-three MSDF reconstruction for this fragment - 0.5=edge, >0.5=interior,
// or -1.0 if this shape has no MSDF tile registered.
//
// Return value: STRAIGHT (non-premultiplied) alpha - rgb is the true color, alpha is coverage.
// main() premultiplies internally (osgSlug's fragment output + blend func are premultiplied
// universally) to combine this with the normal fill result; do not pre-weight rgb by alpha
// yourself.
//
// blendMode (out, callee MUST set it): how main() combines the returned color with fill.
//
// 0 = "over" - composited behind fillColor; correct for backdrops, drop shadows,
// anything that should be occluded normally by solid fill.
//
// 1 = "additive" - added directly into the result; correct for glow/bloom effects
// that should brighten the result even where fill already covers it.
//
// The noop default sets blendMode = 0 and returns vec4(0.0) - contributes nothing.
//
// data.emsPerPixel: fwidth(geom.emCoord) in the raw (untiled) coordinate space. Multiply a
// target em-space width by emsPerPixel to get a constant screen-size effect at any zoom.
vec4 osgSlug_FragmentExt(osgSlug_FragmentExtData data, out int blendMode);

void main() {
	// Below this, fill/alpha/coverage values are treated as fully transparent.
	const float COVERAGE_EPSILON = 0.001;

	// Layer mask: 0 = all visible (default). Non-zero: discard if the layer's bit is clear.
	if(osgSlug_layerMask != 0 && (osgSlug_layerMask & (1 << int(geom.layerIndex + 0.5))) == 0) discard;

	ivec2 glyphLoc = ivec2(fx.shapeData.xy);
	ivec2 bandMax = ivec2(fx.shapeData.zw);

	// fwidth on the raw varying, no discontinuities. osgSlug_FragEmCoord may scale it for
	// effects like tiling (where fract would make fwidth unreliable at tile boundaries).
	vec2 emsPerPixel = fwidth(geom.emCoord);

	// Early-out BEFORE slug_Render's curve-band loop: a mask that only reveals e.g. 10% of a
	// shape would otherwise still pay the full band-loop cost on the other 90% of fragments,
	// since the old design only gated osgSlug_Fragment's output at the very end of main(). Mask
	// coverage never depends on Slug's own fill, so it can (and now does) run first. Fragments
	// on the mask's AA boundary survive here (maskFill > 0 but < 1) and still need slug_Render's
	// real coverage - osgSlug_maskFill is folded into the final alpha further down, once
	// osgSlug_Fragment/osgSlug_FragmentExt have run. See ai/context-todo-mask.md.
	float osgSlug_maskFill = osgSlug_FragmentMask(
		osgSlug_FragmentMaskData(geom.emCoord, geom.uv, emsPerPixel, osg_SimulationTime)
	);

	if(osgSlug_maskFill < COVERAGE_EPSILON) discard;

	// Allow effects to remap em-coords (e.g. fract-based GPU tiling). Gradients and debug
	// visualisation stay on the raw geom.emCoord; only coverage sampling uses renderCoord.
	vec2 renderCoord = osgSlug_FragEmCoord(geom.emCoord, emsPerPixel, fx.effectId, osg_SimulationTime);

	vec2 pixelsPerEm = 1.0 / emsPerPixel;

	int iterations;

	float fill = osgSlug_textMode
		? slug_RenderText(renderCoord, emsPerPixel, pixelsPerEm, fx.bandXform, glyphLoc, bandMax, iterations)
		: slug_Render(renderCoord, pixelsPerEm, fx.bandXform, glyphLoc, bandMax, iterations)
	;

	// Edge-only coverage adjustment for text: stem darkening and gamma correction.
	if (osgSlug_textMode && fill > 0.0 && fill < 1.0) {
		float ppem = 1.0 / max(emsPerPixel.x, emsPerPixel.y);
		float adj = fill;

		if (osgSlug_stemDarken) {
			float brightness = dot(geom.color.rgb, vec3(0.299, 0.587, 0.114));

			adj = slug_StemDarken(adj, brightness, ppem);
		}

		if(osgSlug_gamma != 1.0) adj = pow(adj, osgSlug_gamma);

		fill = adj;
	}

	vec4 effectiveColor = geom.color;

	if(fx.gradientId > 0 && osgSlug_gradientCount > 0) {
		float t;

		if(geom.gradientXform.w == 0.0) {
			// Linear: xform = (dirX, dirY, offset, 0)
			t = dot(geom.emCoord, geom.gradientXform.xy) + geom.gradientXform.z;
		}

		else if(geom.gradientXform.w > 0.0) {
			// Radial/AffineRadial: xform = B matrix (column-major mat2); meta.yz = center; meta.w = r0_norm
			vec2 d = geom.emCoord - geom.gradientMeta.yz;
			mat2 B = mat2(geom.gradientXform);

			t = length(B * d) - geom.gradientMeta.w;
		}

		else {
			// Sweep: xform = (cx, cy, startAngle, -invArcSpan)
			float angle = atan(
				geom.emCoord.y - geom.gradientXform.y,
				geom.emCoord.x - geom.gradientXform.x
			);

			t = (angle - geom.gradientXform.z) * (-geom.gradientXform.w);
		}

		t = clamp(t, 0.0, 1.0);

		float gv = (float(fx.gradientId) - 0.5) / float(osgSlug_gradientCount);
		vec4 gc = texture(osgSlug_gradientTexture, vec2(t, gv));

		effectiveColor = vec4(gc.rgb, gc.a * geom.color.a);
	}

	// Compute msdfSd once; -1.0 means no tile registered. Shared by fData and feData.
	float msdfSd = osgSlug_MSDFSd(geom.emCoord);

	// Build osgSlug_FragmentData once; shared by all osgSlug_Fragment call sites below.
	osgSlug_FragmentData fData;

	fData.fill = fill;
	fData.emCoord = geom.emCoord;
	fData.uv = geom.uv;
	fData.layerColor = effectiveColor;
	fData.effectId = fx.effectId;
	fData.time = osg_SimulationTime;
	fData.msdfSd = msdfSd;
	fData.effectParam = fx.effectParam;
	fData.emsPerPixel = emsPerPixel;

	// Draws a pixel-perfect border around the quad using true [0,1] UV coords.
	if(osgSlug_debugMode == 3) {
		vec2 uv = geom.uv;

		vec2 distToEdge = min(uv, 1.0 - uv);
		float dist = min(distToEdge.x, distToEdge.y);

		vec2 fw = fwidth(uv);
		float px = min(fw.x, fw.y);

		float onEdge = step(dist, px);

		if(fill < COVERAGE_EPSILON && onEdge < 0.01) discard;

		vec4 fillColor = osgSlug_Fragment(fData);

		vec4 borderColor = vec4(
			fract(fx.bandXform.x * 127.1),
			fract(fx.bandXform.y * 311.7),
			fract(fx.bandXform.z * 74.3 + fx.bandXform.w * 19.1),
			1.0
		);

		color = mix(fillColor, borderColor, onEdge);

		// Fold in the mask coverage computed early in main() (see osgSlug_maskFill above), then
		// premultiply: osgSlug_Fragment returns straight alpha, but every draw call now uses the
		// premultiplied SrcOver blend func, mask or no mask, so this conversion always happens.
		color.a *= osgSlug_maskFill;
		color.rgb *= color.a;

		return;
	}

	// Build osgSlug_FragmentExtData and call the pre-discard hook.
	osgSlug_FragmentExtData feData;

	feData.fill = fill;
	feData.msdfSd = msdfSd;
	feData.msdfLayer = fx.msdfLayer;
	feData.msdfRange = fx.msdfRange;
	feData.emCoord = geom.emCoord;
	feData.uv = geom.uv;
	feData.layerColor = effectiveColor;
	feData.effectId = fx.effectId;
	feData.effectParam = fx.effectParam;
	feData.time = osg_SimulationTime;
	feData.emsPerPixel = emsPerPixel;

	int extBlendMode;

	vec4 extColor = osgSlug_FragmentExt(feData, extBlendMode);

	// Using the "half white" line helps show 3D shapes, so... leaving it in for now.
	if(fill < COVERAGE_EPSILON && extColor.a < COVERAGE_EPSILON) {
		if(osgSlug_debugMode == 6) {
			color = vec4(0.5, 0.5, 0.5, 0.5);
			color.rgb *= color.a;
		}

		else discard;
	}

	// No ext contribution (the overwhelmingly common case) - identical to pre-hook behavior.
	else if(extColor.a < COVERAGE_EPSILON) {
		color = (osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
			? osgSlug_Fragment(fData)
			: slug_ApplyDebug(fill, geom.emCoord, effectiveColor, glyphLoc, fx.bandXform, iterations)
		;

		// See the debug-mode-3 branch above for why this always premultiplies.
		color.a *= osgSlug_maskFill;
		color.rgb *= color.a;
	}

	else {
		vec4 fillColor = fill < COVERAGE_EPSILON
			? vec4(0.0)
			: (
				(osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
					? osgSlug_Fragment(fData)
					: slug_ApplyDebug(fill, geom.emCoord, effectiveColor, glyphLoc, fx.bandXform, iterations
				)
			)
		;

		// Combine in premultiplied space - the only space where alpha compositing/addition
		// is well-defined - and stay there: every draw call uses the premultiplied SrcOver
		// blend func now, so outPremul/outAlpha below ARE the final color, no unpremultiply/
		// re-premultiply round trip needed (osgSlug_maskFill folds straight into both).
		vec3 fillPremul = fillColor.rgb * fillColor.a;
		vec3 extPremul = extColor.rgb * extColor.a;

		vec3 outPremul;
		float outAlpha;

		// blendMode 1 = additive: brightens through fillColor too (glow/bloom).
		if(extBlendMode == 1) {
			outPremul = fillPremul + extPremul;
			outAlpha = clamp(fillColor.a + extColor.a, 0.0, 1.0);
		}

		// blendMode 0 = standard "over": fillColor occludes extColor normally.
		else {
			outPremul = fillPremul + extPremul * (1.0 - fillColor.a);
			outAlpha = fillColor.a + extColor.a * (1.0 - fillColor.a);
		}

		color = vec4(outPremul * osgSlug_maskFill, outAlpha * osgSlug_maskFill);
	}
}
)");

const std::string Atlas::SHADER_NOOP_VERTEX_HOOK = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	return osgSlug_VertexDefault(data);
}
)");

const std::string Atlas::SHADER_NOOP_FRAGMENT_HOOK = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_fragment

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
}
)");

const std::string Atlas::SHADER_NOOP_FRAGMENT_EXT_HOOK = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_fragment

vec4 osgSlug_FragmentExt(osgSlug_FragmentExtData data, out int blendMode) {
	blendMode = 0;

	return vec4(0.0);
}
)");

// ================================================================================================
// Scanline Sweeper library + shaders
// ================================================================================================

// Pure-math GLSL library. Include with #pragma osgSlug lib_scanline.
// Translated from the HLSL reference implementation in Rook & Possum (2026) ?8.
const std::string Atlas::SHADER_LIB_SCANLINE = R"(
vec2 scanline_evaluate_bezier(vec2 p0, vec2 p1, vec2 p2, float t) {
	vec2 a = mix(p0, p1, t);
	vec2 b = mix(p1, p2, t);
	return mix(a, b, t);
}

// Preconditions (caller-enforced): c0 <= target <= c2; curve is monotonic (c0 <= c1 <= c2).
// qa: second-degree coefficient of the 1-D quadratic in the chosen dimension.
float scanline_intersect_monotonic(float qa, float c0, float c1, float c2, float target) {
	if (abs(qa) < 1e-3) return (target - c0) / (c2 - c0);
	float qb = fma(2.0, c1, -2.0 * c0);
	float qc = c0 - target;
	float d = fma(qb, qb, -4.0 * qa * qc);
	float sqrtd = d < 0.0 ? 0.0 : sqrt(d);
	float inv2a = 0.5 / qa;
	return fma(-qb, inv2a, sign(c2 - c0) * sqrtd * inv2a);
}

// Returns the signed swept-area contribution of one monotonic quadratic Bezier curve.
// size: pixel-window size in em-space (from dFdx/dFdy of v_emCoord)
// offset: lower-left corner of the pixel window in em-space
// p0..p2: monotonic quadratic control points - all(p0 <= p1) and all(p1 <= p2) in both axes
// Accumulate the return values for all curves, then divide by (size.x * size.y) -> coverage [0,1].
float scanline_sweep(vec2 size, vec2 offset, vec2 p0, vec2 p1, vec2 p2) {
	// Discard curves entirely above or below the scanline.
	if (max(p0.y, p2.y) <= offset.y || min(p0.y, p2.y) >= offset.y + size.y) return 0.0;

	vec2 delta = p2 - p0;

	// Horizontal curves (all y equal) contribute zero signed area.
	if (abs(delta.y) < 1e-6) return 0.0;

	// Shift to a coordinate system with the window at the origin.
	p0 -= offset;
	p1 -= offset;
	p2 -= offset;

	// Fast path: strictly vertical segments (common in many fonts).
	if (p0.x == p1.x && p0.x == p2.x) {
		if (p0.x >= size.x) return 0.0;
		float vTop = min(max(p0.y, p2.y), size.y);
		float vBot = max(min(p0.y, p2.y), 0.0);
		float h = vTop - vBot;
		float base = min(size.x, size.x - p0.x);
		return sign(delta.y) * base * h;
	}

	// Second-degree coefficient for the y quadratic; find t at top and bottom of window.
	float qa = fma(-2.0, p1.y, p0.y + p2.y);
	float bt = scanline_intersect_monotonic(qa, p0.y, p1.y, p2.y, 0.0);
	float tt = scanline_intersect_monotonic(qa, p0.y, p1.y, p2.y, size.y);

	// v_min_t/v_max_t: t-values where the curve enters and exits the y window.
	float v_min_t = delta.y > 0.0 ? bt : tt;
	float v_max_t = delta.y > 0.0 ? tt : bt;

	vec2 v_min = scanline_evaluate_bezier(p0, p1, p2, clamp(v_min_t, 0.0, 1.0));
	vec2 v_max = scanline_evaluate_bezier(p0, p1, p2, clamp(v_max_t, 0.0, 1.0));

	// Fast paths for curves entirely left or right of the window within the scanline.
	if (max(v_min.x, v_max.x) <= 0.0) return (v_max.y - v_min.y) * size.x;
	if (min(v_min.x, v_max.x) >= size.x) return 0.0;

	// Solve for horizontal window crossings.
	qa = fma(-2.0, p1.x, p0.x + p2.x);

	float h_min_t;
	float h_max_t;

	// Packed check vector: (lower_x_bound, upper_x_bound, target_x, t_at_lower_x_bound).
	// Components depend on the horizontal direction of travel.
	vec4 h_check = delta.x > 0.0
		? vec4(p0.x, p2.x, 0.0, 0.0)
		: vec4(p2.x, p0.x, size.x, 1.0);

	if (h_check.x >= h_check.z) {
		h_min_t = h_check.w;
	} else if (h_check.y <= h_check.z) {
		h_min_t = 1.0 - h_check.w;
	} else {
		h_min_t = scanline_intersect_monotonic(qa, p0.x, p1.x, p2.x, h_check.z);
	}

	h_check.z = size.x - h_check.z;

	if (h_check.x >= h_check.z) {
		h_max_t = h_check.w;
	} else if (h_check.y <= h_check.z) {
		h_max_t = 1.0 - h_check.w;
	} else {
		h_max_t = scanline_intersect_monotonic(qa, p0.x, p1.x, p2.x, h_check.z);
	}

	// Combined t-range clipped to [0, 1].
	float min_t = clamp(max(v_min_t, h_min_t), 0.0, 1.0);
	float max_t = clamp(min(v_max_t, h_max_t), 0.0, 1.0);

	// Reuse precomputed boundary evaluations where possible.
	vec2 q0 = v_min_t >= h_min_t ? v_min : scanline_evaluate_bezier(p0, p1, p2, min_t);
	vec2 q1 = v_max_t <= h_max_t ? v_max : scanline_evaluate_bezier(p0, p1, p2, max_t);

	float coverage = 0.0;

	if (min_t > 0.0 && delta.x > 0.0) {
		// Curve enters from the left edge: integrate the left rectangle below entry.
		float h = delta.y > 0.0
			? q0.y - max(0.0, p0.y)
			: min(size.y, p0.y) - q0.y;
		coverage = sign(delta.y) * h * size.x;
	}

	if (max_t < 1.0 && delta.x < 0.0) {
		// Curve exits on the left edge: integrate the left rectangle above exit.
		float h = delta.y > 0.0
			? min(size.y, p2.y) - q1.y
			: q1.y - max(0.0, p2.y);
		coverage += sign(delta.y) * h * size.x;
	}

	// Trapezoidal approximation for the curve segment inside the window.
	float h = q1.y - q0.y;
	float b = fma(-0.5, q0.x + q1.x, size.x);
	coverage += b * h;

	return coverage;
}
)";

const std::string Atlas::SHADER_SCANLINE_VERT = R"(
#version 430 core

layout(location = 0) in vec4 a_position; // xy = world-space 2D position
layout(location = 1) in vec4 a_emCoord; // xy = em-space coordinate (glyph units)
layout(location = 2) in vec4 a_curveRange; // x = curveStart, y = curveCount (as floats)
layout(location = 3) in vec4 a_layerColor; // per-layer RGBA

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 v_emCoord;
flat out vec2 v_curveRange;
flat out vec4 v_layerColor;

void main() {
	v_emCoord = a_emCoord.xy;
	v_curveRange = a_curveRange.xy;
	v_layerColor = a_layerColor;
	gl_Position = osg_ModelViewProjectionMatrix * vec4(a_position.xy, 0.0, 1.0);
}
)";

const bool REGISTER_SCANLINE_SHADER_LIB = [] {
	const osgx::ShaderLib libs[] = {
		{"lib_scanline", {}, Atlas::SHADER_LIB_SCANLINE}
	};

	osgx::registerShaderLibs("osgSlug", libs);

	return true;
}();

// Pre-resolved at static-init time so ScanlineDrawable can use it without re-resolving.
const std::string Atlas::SHADER_SCANLINE_FRAG = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_scanline

in vec2 v_emCoord;
flat in vec2 v_curveRange;
flat in vec4 v_layerColor;

uniform sampler2D u_scanlineTex;
uniform int u_texWidth;

out vec4 fragColor;

void main() {
	int curveStart = int(v_curveRange.x);
	int curveCount = int(v_curveRange.y);

	vec2 emCoord = v_emCoord;
	vec2 size = fwidth(emCoord);
	vec2 offset = emCoord - size * 0.5;
	float winArea = size.x * size.y;
	float coverage = 0.0;

	for (int i = 0; i < curveCount; i++) {
		int base = curveStart + i * 2;
		vec4 t0 = texelFetch(u_scanlineTex, ivec2(base % u_texWidth, base / u_texWidth), 0);
		vec4 t1 = texelFetch(u_scanlineTex, ivec2((base + 1) % u_texWidth, (base + 1) / u_texWidth), 0);
		vec2 p0 = t0.xy, p1 = t0.zw, p2 = t1.xy;

		// AABB y-cull (scanline_sweep also does this; saved here as an early-out).
		if (max(p0.y, p2.y) <= offset.y || min(p0.y, p2.y) >= offset.y + size.y) continue;
		// Right-side x-cull only; curves to the left still contribute swept area.
		if (min(p0.x, p2.x) >= offset.x + size.x) continue;

		coverage += scanline_sweep(size, offset, p0, p1, p2);
	}

	float alpha = clamp(coverage / winArea, 0.0, 1.0);
	fragColor = vec4(v_layerColor.rgb, v_layerColor.a * alpha);
}
)");

// osgSlug_SDF_* - closed-form signed distance functions (negative = inside).
// osgSlug_Mask_* - coverage helpers + full osgSlug_mask dispatcher.
// Opt-in via: #pragma osgSlug lib_mask
// Prerequisites: #pragma osgSlug lib_fragment (for osgSlug_MaskData / osgSlug_FragmentData).
// Requires #version 430: the LayerBuffer re-declaration below is a `buffer` (SSBO) block,
// illegal pre-4.30 - something any hook using lib_mask must declare correctly.
const std::string Atlas::SHADER_LIB_MASK = R"(

// Private re-declaration of LayerBuffer (see SHADER_TYPES): needed here because this fragment
// hook's shader object never gets SHADER_TYPES prepended (that only happens for the vertex
// shader - see makeVertShader() in createHookStateSet()). GLSL requires each shader
// object/stage to redeclare the buffer blocks it uses; this is normal, not a hack. Only used
// to recover transformData.xy (each layer's own canvas-space origin) via geom.layerIndex,
// which osgSlug_Mask_CoverageFor's callers need and osgSlug_MaskData deliberately does not
// carry (see its comment in SHADER_LIB_FRAGMENT).
struct osgSlug_LayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 transformData;
	vec4 axisX; // MUST stay member-identical to SHADER_TYPES' declaration (GL links by block
	vec4 axisY; // layout) - see the expand-removal / GPU-live margin work
};

layout(std430, binding = 1) readonly buffer LayerBuffer {
	osgSlug_LayerData layers[];
};

// This fragment's own layer's canvas-space origin. Exposed as a function (not just the
// LayerBuffer declaration above) so OTHER hooks that only need this one value - e.g. a custom
// debug/visualization hook - can forward-declare and call it without redeclaring LayerBuffer
// themselves. Redeclaring LayerBuffer a second time would be harmless (declarations, unlike
// function bodies, may repeat verbatim across shader objects linked into one Program), but
// pulling in the whole mask pragma library an entire second time to get it is NOT harmless:
// that library also pulls in every osgSlug_SDF_*/osgSlug_Mask_* function BODY below, and GLSL
// rejects the same function being defined twice across linked shader objects of one stage --
// exactly the trap osgslug-mask.cpp's --debug-msdf hook hit before this helper existed.
vec2 osgSlug_Mask_LayerOrigin() {
	return layers[int(geom.layerIndex + 0.5) - 1].transformData.xy;
}

// Private re-declaration of osgSlug_msdfTexture (see SHADER_TYPES/SHADER_FRAG): same reason as
// LayerBuffer above - this shader object never gets SHADER_FRAG prepended. GLSL shares the
// binding automatically across shader objects when the uniform name+type match (the Atlas's own
// default StateSet already binds this to unit 3 unconditionally, since every glyph/shape's own
// MSDF sampling depends on it too), so a mask's MSDF tile needs no separate texture unit.
uniform sampler2DArray osgSlug_msdfTexture;

// --- Signed distance primitives ---

float osgSlug_SDF_Circle(vec2 p, vec2 center, float r) {
	return length(p - center) - r;
}

float osgSlug_SDF_Box(vec2 p, vec2 center, vec2 halfExt) {
	vec2 d = abs(p - center) - halfExt;
	return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
}

float osgSlug_SDF_Capsule(vec2 p, vec2 a, vec2 b, float r) {
	vec2 pa = p - a, ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return length(pa - ba * h) - r;
}

// Filled pie sector. a0/a1 in radians, standard math convention (0=+X, CCW positive).
float osgSlug_SDF_Pie(vec2 p, vec2 center, float r, float a0, float a1) {
	vec2 q = p - center;
	float midAngle = (a0 + a1) * 0.5;
	float halfSpan = (a1 - a0) * 0.5;
	vec2 sc = vec2(sin(halfSpan), cos(halfSpan));
	float cosM = cos(-midAngle), sinM = sin(-midAngle);
	vec2 rp = vec2(q.x * cosM - q.y * sinM, q.x * sinM + q.y * cosM);
	rp.x = abs(rp.x);
	float l = length(rp) - r;
	float m = length(rp - sc * clamp(dot(rp, sc), 0.0, r));
	return max(l, m * sign(sc.y * rp.x - sc.x * rp.y));
}

// Stroked arc (annular band along an arc). rb = stroke half-width.
float osgSlug_SDF_ArcBand(vec2 p, vec2 center, float ra, float a0, float a1, float rb) {
	vec2 q = p - center;
	float midAngle = (a0 + a1) * 0.5;
	float halfSpan = (a1 - a0) * 0.5;
	float cosM = cos(-midAngle), sinM = sin(-midAngle);
	vec2 rp = vec2(q.x * cosM - q.y * sinM, q.x * sinM + q.y * cosM);
	rp.y = abs(rp.y);
	vec2 sc_x = vec2(cos(halfSpan), sin(halfSpan));
	float k = (sc_x.x * rp.y > sc_x.y * rp.x) ? dot(rp, sc_x) : length(rp);
	return sqrt(max(dot(rp, rp) + ra * ra - 2.0 * ra * k, 0.0)) - rb;
}

// Rotates p by angle a (CCW, radians). Used by every rotatable mask primitive below to pre-
// rotate the query point by -rotation into the shape's own unrotated local frame - same trick
// for all of them, not worth a dedicated per-shape variant.
vec2 osgSlug_SDF_Rotate(vec2 p, float a) {
	float c = cos(a), s = sin(a);
	return vec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// Regular hexagon (flat-top at rotation=0). Exact SDF (Inigo Quilez, iquilezles.org/articles/distfunctions2d).
float osgSlug_SDF_Hexagon(vec2 p, vec2 center, float r, float rotation) {
	vec2 q = abs(osgSlug_SDF_Rotate(p - center, -rotation));
	const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);

	q -= 2.0 * min(dot(k.xy, q), 0.0) * k.xy;
	q -= vec2(clamp(q.x, -k.z * r, k.z * r), r);

	return length(q) * sign(q.y);
}

// Regular octagon. Exact SDF (Inigo Quilez, iquilezles.org/articles/distfunctions2d).
float osgSlug_SDF_Octagon(vec2 p, vec2 center, float r, float rotation) {
	vec2 q = abs(osgSlug_SDF_Rotate(p - center, -rotation));
	const vec3 k = vec3(-0.9238795325, 0.3826834323, 0.4142135623);

	q -= 2.0 * min(dot(vec2(k.x, k.y), q), 0.0) * vec2(k.x, k.y);
	q -= 2.0 * min(dot(vec2(-k.x, k.y), q), 0.0) * vec2(-k.x, k.y);
	q -= vec2(clamp(q.x, -k.z * r, k.z * r), r);

	return length(q) * sign(q.y);
}

// General n-pointed star (Inigo Quilez, iquilezles.org/articles/distfunctions2d). r = outer
// radius, points = point count (rounded to the nearest integer >= 3), innerRatio in [0,1] maps
// to IQ's "m" shape parameter (0 = sharpest spikes, 1 = regular n-gon).
float osgSlug_SDF_Star(vec2 p, vec2 center, float r, float points, float innerRatio, float rotation) {
	vec2 q = osgSlug_SDF_Rotate(p - center, -rotation);
	float n = max(round(points), 3.0);
	float m = mix(2.0, n, clamp(innerRatio, 0.0, 1.0));

	float an = 3.14159265 / n;
	float en = 3.14159265 / m;
	vec2 acs = vec2(cos(an), sin(an));
	vec2 ecs = vec2(cos(en), sin(en));

	float bn = mod(atan(q.x, q.y), 2.0 * an) - an;
	q = length(q) * vec2(cos(bn), abs(sin(bn)));
	q -= r * acs;
	q += ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);

	return length(q) * sign(q.x);
}

// --- Mask helpers ---

// 1-pixel AA ramp from a signed distance (dist < 0 = inside). emsPerPixel must be
// data.emsPerPixel (precomputed in main() before any discard) - NOT a fresh fwidth() call
// here: derivatives computed this deep in a hook call chain, following a discard elsewhere in
// the shader, produced degenerate (zero) results for axis-aligned geometry on at least one
// driver (NVIDIA) - see osgSlug_FragmentData.emsPerPixel's comment.
float osgSlug_Mask_Coverage(float dist, vec2 emsPerPixel) {
	float px = max(emsPerPixel.x, emsPerPixel.y);
	return clamp(0.5 - dist / px, 0.0, 1.0);
}

// Debug-only: raw baked MSDF tile RGB (the msd.r/g/b channels, before median-of-three
// reconstruction) at a canvas-space coordinate, or vec3(-1.0) if there's no tile or
// canvasCoord falls outside its baked extent. NOT called by the automatic
// osgSlug_FragmentMask() pipeline below - osgSlug_FragmentMask returns a coverage float, which
// has no room for a raw-tile preview. Call this instead from your own FragmentExt/Fragment hook
// when you want to visualize a baked mask's tile directly (e.g. the --debug-msdf flag in
// osgslug-mask.cpp). Forward-declare it (`vec3 osgSlug_Mask_DebugMSDF(vec2 canvasCoord);`)
// plus the ordinary fragment-data pragma - do NOT also pull in the mask pragma library in that
// hook: this function's BODY is already linked in via the always-present MaskHook shader object, and
// GLSL rejects the same function being defined twice across shader objects linked into one
// Program. See osgslug-mask.cpp's HOOK_DEBUG_MSDF for the working pattern.
vec3 osgSlug_Mask_DebugMSDF(vec2 canvasCoord) {
	if(osgSlug_mask.type != 0 || osgSlug_mask.msdfLayer < 0) return vec3(-1.0);

	float cx = osgSlug_mask.params.x, cy = osgSlug_mask.params.y;
	float r = osgSlug_mask.params.z, rng = osgSlug_mask.params.w;
	vec4 bbox = vec4(cx - r - rng, cy - r - rng, cx + r + rng, cy + r + rng);
	vec2 tileUV = (canvasCoord - bbox.xy) / (bbox.zw - bbox.xy);

	if(any(lessThan(tileUV, vec2(0.0))) || any(greaterThan(tileUV, vec2(1.0)))) return vec3(-1.0);

	return texture(osgSlug_msdfTexture, vec3(tileUV, float(osgSlug_mask.msdfLayer))).rgb;
}

// Coverage-only mask evaluation: reads osgSlug_mask, returns maskFill in [0,1] (invert already
// applied). No discard, no color - callers decide what to do with the result. This is what
// lets it run BEFORE slug_Render (see osgSlug_FragmentMask below, called early in main()) as
// well as feed the post-slug_Render alpha gate at the end of main() - one computation, two
// call sites, instead of the old osgSlug_Mask_Evaluate/osgSlug_Mask_Apply pair that both
// recomputed coverage AND replaced osgSlug_Fragment's color output outright.
float osgSlug_Mask_CoverageFor(vec2 canvasCoord, vec2 emsPerPixel) {
	// Null sentinel (Atlas::getNullMask(), bound whenever a RenderGroup has no real mask) --
	// always fully unmasked. Checked BEFORE invert: a "no mask" state must never invert to
	// "hide everything."
	if(osgSlug_mask.type < 0) return 1.0;

	float maskFill;

	if(osgSlug_mask.type == 0) { // MSDF - baked tile sample
		if(osgSlug_mask.msdfLayer < 0) {
			// No tile baked at all - treat as "definitely outside," not a discard: an
			// unconditional discard here would run before invert is ever applied below,
			// silently making invert a no-op. maskFill = 0.0 lets invert flip it correctly.
			maskFill = 0.0;
		}
		else {
			float cx = osgSlug_mask.params.x, cy = osgSlug_mask.params.y;
			float r = osgSlug_mask.params.z, rng = osgSlug_mask.params.w;
			vec4 bbox = vec4(cx - r - rng, cy - r - rng, cx + r + rng, cy + r + rng);
			vec2 tileUV = (canvasCoord - bbox.xy) / (bbox.zw - bbox.xy);

			// Outside the baked tile's extent: no SDF data exists there, but the tile is padded
			// by rng beyond the shape's true bounds, so "outside" reliably means "outside the
			// shape." Same reasoning as msdfLayer<0 above - maskFill = 0.0, not discard, so
			// invert still applies below. Procedural types (Circle/Rect/etc.) never hit this at
			// all: their SDF formulas are closed-form and valid everywhere, so they never needed
			// this distinction - this brings MSDF's invert behavior in line with them instead
			// of being a discard-shaped exception.
			if(any(lessThan(tileUV, vec2(0.0))) || any(greaterThan(tileUV, vec2(1.0)))) {
				maskFill = 0.0;
			}
			else {
				vec3 msd = texture(osgSlug_msdfTexture, vec3(tileUV, float(osgSlug_mask.msdfLayer))).rgb;
				float maskSd = max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
				float pxRange = max(2.0 * rng / max(emsPerPixel.x, emsPerPixel.y), 1.0);
				maskFill = clamp((maskSd - 0.5) * pxRange + 0.5, 0.0, 1.0);
			}
		}
	}

	else if(osgSlug_mask.type == 1) { // Circle
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Circle(canvasCoord, osgSlug_mask.params.xy, osgSlug_mask.params.z),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 2) { // Rect
		vec2 center = osgSlug_mask.params.xy + osgSlug_mask.params.zw * 0.5;
		vec2 halfExt = osgSlug_mask.params.zw * 0.5;
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Box(canvasCoord, center, halfExt),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 3) { // Capsule
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Capsule(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.zw,
				osgSlug_mask.params2.x
			),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 4) { // Arc - filled pie sector
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Pie(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.z,
				osgSlug_mask.params.w,
				osgSlug_mask.params2.x
			),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 5) { // ArcBand - stroked arc
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_ArcBand(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.z,
				osgSlug_mask.params.w,
				osgSlug_mask.params2.x,
				osgSlug_mask.params2.y
			),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 6) { // Hexagon
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Hexagon(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.z,
				osgSlug_mask.params.w
			),
			emsPerPixel
		);
	}

	else if(osgSlug_mask.type == 7) { // Octagon
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Octagon(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.z,
				osgSlug_mask.params.w
			),
			emsPerPixel);
	}

	else { // Star (type == 8)
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Star(
				canvasCoord,
				osgSlug_mask.params.xy,
				osgSlug_mask.params.z,
				osgSlug_mask.params.w,
				osgSlug_mask.params2.x,
				osgSlug_mask.params2.y
			),
			emsPerPixel);
	}

	if(osgSlug_mask.invert) maskFill = 1.0 - maskFill;

	return maskFill;
}
)";

// Default (always-linked, NOT opt-in) implementation of the osgSlug_FragmentMask early hook --
// see main()'s call site in SHADER_FRAG and osgSlug_FragmentMaskData's comment. Unlike
// SHADER_NOOP_FRAGMENT_HOOK/SHADER_NOOP_FRAGMENT_EXT_HOOK, this default is NOT a no-op: it IS
// the real mask coverage evaluation, so masking works automatically the moment
// CompositeShape.mask is set, without any user-authored hook. A power user can still override
// MaskHook (e.g. for custom clip logic) exactly like FragmentHook/FragmentExtHook.
//
// Requires #version 430: lib_mask's private LayerBuffer redeclaration is a `buffer` (SSBO)
// block, illegal pre-4.30. (All other fragment-stage shader strings in this file are also
// 430 now, post-GL3-removal, but this is the one that actually needs it.)
const bool REGISTER_MASK_SHADER_LIB = [] {
	const osgx::ShaderLib libs[] = {
		{"lib_mask", {}, Atlas::SHADER_LIB_MASK}
	};

	osgx::registerShaderLibs("osgSlug", libs);

	return true;
}();

const std::string Atlas::SHADER_MASK_FRAGMENT_HOOK = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_fragment
#pragma osgSlug lib_mask

float osgSlug_FragmentMask(osgSlug_FragmentMaskData data) {
	// osgSlug_Mask_CoverageFor() also checks this internally, but checking here first skips the
	// LayerBuffer read below (see lib_mask's private redeclaration) whenever nothing is masked -
	// the common case for most drawables most of the time.
	if(osgSlug_mask.type < 0) return 1.0;

	vec2 canvasCoord = data.emCoord + osgSlug_Mask_LayerOrigin();

	return osgSlug_Mask_CoverageFor(canvasCoord, data.emsPerPixel);
}
)");

// createDecalProgram()'s default MaskHook (see Atlas.hpp's declaration comment for the full
// LayerBuffer/DecalLayerBuffer mismatch reasoning). data.uv is the decal quad's own [0,1]
// tangent-plane position (see SHADER_VERT_DECAL/geom.uv) - centering it to match the vertex
// shader's own lu-0.5/lv-0.5 convention gives exactly the coordinate space a face-outline mask
// (built via Canvas::mask() in that same centered [-0.5,0.5] space) should be authored in.
const std::string Atlas::SHADER_MASK_FRAGMENT_HOOK_DECAL = resolveShaderLibs(R"(
#version 430 core

#pragma osgSlug lib_fragment
#pragma osgSlug lib_mask

float osgSlug_FragmentMask(osgSlug_FragmentMaskData data) {
	return osgSlug_Mask_CoverageFor(data.uv - vec2(0.5), data.emsPerPixel);
}
)");

// ================================================================================================
// State-set builders
// ================================================================================================

osg::StateSet* Atlas::createDefaultStateSet(HookList hooks) const {
	const std::string* vertEffects = &SHADER_NOOP_VERTEX_HOOK;
	const std::string* fragEffects = &SHADER_NOOP_FRAGMENT_HOOK;
	const std::string* fragExt = &SHADER_NOOP_FRAGMENT_EXT_HOOK;
	const std::string* maskHook = &SHADER_MASK_FRAGMENT_HOOK;

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
		else if(hook == MaskHook) maskHook = &src;
	}

	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragExt)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*maskHook)));

	// osgSlug_MaskBlock has no inline layout(binding=N) (illegal pre-GL4.20); bound instead via
	// glUniformBlockBinding here, matching where RenderMask::apply() binds at draw time.
	program->addBindUniformBlock("osgSlug_MaskBlock", RENDER_MASK_UBO_BINDING);

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);

	// Ambient default: the null sentinel, so every fragment shader linking SHADER_FRAG can read
	// osgSlug_mask unconditionally. Child drawables that bind a real RenderMask (ShapeDrawable's
	// slow path) override this per-group at draw time and restore it afterward - see
	// ShapeDrawable::drawImplementation(). Attached here (not just imperatively) so drawables
	// that never touch masking at all (the fast path, DecalDrawable, etc.) still have something
	// valid bound via ordinary StateSet inheritance.
	ss->setAttributeAndModes(getNullMask()->getBinding(), osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("osgSlug_curveTexture", 0));
	ss->addUniform(new osg::Uniform("osgSlug_bandTexture", 1));
	ss->addUniform(new osg::Uniform("osgSlug_gradientTexture", 2));
	ss->addUniform(new osg::Uniform("osgSlug_msdfTexture", 3));
	ss->addUniform(new osg::Uniform("osgSlug_effectTexture", 4));
	ss->addUniform(new osg::Uniform(
		"osgSlug_gradientCount",
		static_cast<int>(getGradients().size())
	));
	ss->addUniform(new osg::Uniform(
		"osgSlug_texWidth",
		static_cast<int>(std::countr_zero(getTextureWidth()))
	));
	ss->addUniform(new osg::Uniform("osgSlug_emTile", osg::Vec2(1.0f, 1.0f)));
	// Pre-first-cull fallback only; kept live per-frame by Atlas's ViewportUniformCallback
	// (see Atlas::packTextures()). SHADER_VERT's pixel-denominated AA margin reads this.
	ss->addUniform(new osg::Uniform("osgSlug_viewport", osg::Vec2(1280.0f, 720.0f)));
	ss->setTextureAttributeAndModes(0, _curveTexture, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(1, _bandTexture, osg::StateAttribute::ON);

	if(_gradientTexture.valid()) ss->setTextureAttributeAndModes(
		2,
		_gradientTexture,
		osg::StateAttribute::ON
	);

#ifdef SLUGHORN_HAS_MSDF
	if(_msdfTexture.valid()) ss->setTextureAttributeAndModes(
		3,
		_msdfTexture,
		osg::StateAttribute::ON
	);
#endif

	if(_shapeBuffer.valid() && _shapeBuffer->getTotalDataSize() > 0) {
		ss->setAttributeAndModes(
			new osg::ShaderStorageBufferBinding(
				0,
				_shapeBuffer,
				0,
				_shapeBuffer->getTotalDataSize()
			),
			osg::StateAttribute::ON
		);
	}

	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	// Premultiplied SrcOver, universally: every draw call (masked or not) writes premultiplied
	// color out of main() now, so there is exactly one blend func for the whole pipeline. See
	// main()'s tail for where the conversion from osgSlug_Fragment's straight-alpha return value
	// actually happens.
	ss->setAttributeAndModes(new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	if(auto dm = getEnv<int>("DEBUG", 0); dm) ss->addUniform(new osg::Uniform(
		"osgSlug_debugMode",
		dm
	));

	return ss;
}

osg::StateSet* Atlas::createHookStateSet(HookList hooks) const {
	const std::string* vertEffects = &SHADER_NOOP_VERTEX_HOOK;
	const std::string* fragEffects = &SHADER_NOOP_FRAGMENT_HOOK;
	const std::string* fragExt = &SHADER_NOOP_FRAGMENT_EXT_HOOK;
	const std::string* maskHook = &SHADER_MASK_FRAGMENT_HOOK;

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
		else if(hook == MaskHook) maskHook = &src;
	}

	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragExt)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*maskHook)));

	// osgSlug_MaskBlock has no inline layout(binding=N) (illegal pre-GL4.20); bound instead via
	// glUniformBlockBinding here, matching where RenderMask::apply() binds at draw time. The
	// actual buffer bound to that index (real mask, or the null sentinel) is inherited from the
	// Atlas parent's own StateSet (createDefaultStateSet()) - see that function's comment.
	program->addBindUniformBlock("osgSlug_MaskBlock", RENDER_MASK_UBO_BINDING);

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);

	return ss;
}

osg::Program* Atlas::createDecalProgram(HookList hooks) const {
	const std::string* vertEffects = &SHADER_NOOP_VERTEX_HOOK;
	const std::string* fragEffects = &SHADER_NOOP_FRAGMENT_HOOK;
	const std::string* fragExt = &SHADER_NOOP_FRAGMENT_EXT_HOOK;
	const std::string* maskHook = &SHADER_MASK_FRAGMENT_HOOK_DECAL;

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
		else if(hook == MaskHook) maskHook = &src;
	}

	auto* program = new osg::Program();

	// Inject only SHADER_ATLAS_TYPES (binding 0); the decal shader defines osgSlug_DecalLayerData
	// and binding 1 itself, avoiding conflict with the standard osgSlug_LayerData / binding 1.
	program->addShader(makeVertShader(SHADER_VERT_DECAL, SHADER_ATLAS_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_ATLAS_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*fragExt)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveShaderLibs(*maskHook)));

	program->addBindUniformBlock("osgSlug_MaskBlock", RENDER_MASK_UBO_BINDING);

	return program;
}

}
