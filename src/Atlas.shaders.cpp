#include "osgSlug/Drawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/Shader>

OSGSLUG_ENABLE_WARNINGS

namespace {

std::string_view::size_type caseInsensitiveFind(
	std::string_view haystack,
	std::string_view needle,
	std::string_view::size_type pos=0
) {
	if(pos > haystack.size()) return std::string_view::npos;

	const auto equalIgnoringCase = [](char lhs, char rhs) noexcept {
		const auto lhsUnsigned = static_cast<unsigned char>(lhs);
		const auto rhsUnsigned = static_cast<unsigned char>(rhs);

		return std::tolower(lhsUnsigned) == std::tolower(rhsUnsigned);
	};

	const std::string_view tail = haystack.substr(pos);

	const auto it = std::search(
		tail.begin(),
		tail.end(),
		needle.begin(),
		needle.end(),
		equalIgnoringCase
	);

	if(it == tail.end()) return std::string_view::npos;

	return pos + static_cast<std::string_view::size_type>(
		std::distance(tail.begin(), it)
	);
}

std::string resolveLib(std::string src, const std::string& pragma, const std::string& lib) {
	size_t pos = 0;

	while((pos = caseInsensitiveFind(src, pragma, pos)) != std::string::npos) {
		src.replace(pos, pragma.size(), lib);

		pos += lib.size();
	}

	return src;
}

// Replaces #pragma osgSlug lib_vertex / lib_fragment with the actual library source.
// Used by SHADER_FRAG / SHADER_NOOP_* static initializers AND by createDefaultStateSet.
std::string resolveLibs(std::string src) {
	src = resolveLib(
		std::move(src),
		"#pragma osgSlug lib_vertex",
		osgSlug::Atlas::SHADER_LIB_VERTEX
	);

	src = resolveLib(
		std::move(src),
		"#pragma osgSlug lib_fragment",
		osgSlug::Atlas::SHADER_LIB_FRAGMENT
	);

	src = resolveLib(
		std::move(src),
		"#pragma osgSlug lib_scanline",
		osgSlug::Atlas::SHADER_LIB_SCANLINE
	);

	src = resolveLib(
		std::move(src),
		"#pragma osgSlug lib_mask",
		osgSlug::Atlas::SHADER_LIB_MASK
	);

	return src;
}

// Prepend `types` immediately after the #version directive in `src`, then resolve libs.
osg::Shader* makeVertShader(const std::string& src, const std::string& types) {
	const std::string resolved = resolveLibs(src);
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
	vec4 effectData; // x = effectId, y = shapeIndex (into AtlasShapeBuffer), z = 0, w = effectParam
	vec4 transformData; // xy = layer.transform.xy (canvas-space origin); zw = unused
};

layout(std430, binding = 1) buffer LayerBuffer {
	osgSlug_LayerData layers[];
};
)";

const std::string Atlas::SHADER_LIB_VERTEX = R"(
// All per-vertex data the osgSlug_Vertex hook receives.
// Use data.pos, data.emCoord, data.origin, data.effectParam, etc.
struct osgSlug_VertexData {
	vec3 pos; // world-space position (xyz of a_position)
	vec2 emCoord; // em-space coordinate
	vec2 uv; // normalized [0,1] UV
	int effectId; // per-layer effect selector (set via setLayerEffectId)
	vec2 origin; // shape origin in em-space (used by Rotate/Scale helpers)
	float effectParam; // per-layer float (set via setLayerEffectParam)
	float time; // osg_SimulationTime
};

// Helper prototypes (implementations live in the main vertex shader only - one definition per program).
vec3 osgSlug_Vertex_Rotate(vec3 pos, vec2 emCoord, vec2 origin, float angle);
vec3 osgSlug_Vertex_Scale(vec3 pos, vec2 emCoord, vec2 origin, float scale);

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
// type: 0=MSDF 1=Circle 2=Rect 3=Capsule 4=Arc 5=ArcBand
// params: SDF [0..3]; MSDF stores cx,cy,r,range here (bbox derived in shader).
// params2: SDF overflow [4,5]; Arc: angle_end; ArcBand: angle_end + stroke_hw.
// msdfLayer/debug are MSDF-only fields; ignored for analytical types.
// MSDF sampling reuses osgSlug_msdfTexture (unit 3, always bound by the Atlas's own default
// StateSet) -- a mask's MSDF tile lives in the same Texture2DArray every glyph/shape already
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

// No inline layout(binding=N): inline UBO binding syntax is illegal pre-4.20. Bound instead
// via Program::addBindUniformBlock() in createHookStateSet().
layout(std140) uniform osgSlug_MaskBlock {
	osgSlug_MaskData osgSlug_mask;
};
)";

const std::string Atlas::SHADER_VERT = R"(
#version 430 core

#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex_Rotate(vec3 pos, vec2 emCoord, vec2 origin, float angle) {
	float c = cos(angle), s = sin(angle);
	mat2 R = mat2(c, s, -s, c);
	vec2 pivot = pos.xy - emCoord.xy + origin;
	pos.xy = R * (pos.xy - pivot) + pivot;
	return pos;
}

vec3 osgSlug_Vertex_Scale(vec3 pos, vec2 emCoord, vec2 origin, float scale) {
	vec2 pivot = pos.xy - emCoord.xy + origin;
	pos.xy = (pos.xy - pivot) * scale + pivot;
	return pos;
}

layout(location = 0) in vec4 a_position; // xyz = world pos, w = layer index (1-based)
layout(location = 1) in vec4 a_emCoord; // xy = em-coord, zw = UV [0,1]

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

// Defined in the linked effects or noop unit.
vec3 osgSlug_Vertex(osgSlug_VertexData data);

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

	vec3 pos = osgSlug_Vertex(vData);

	geom.emCoord = a_emCoord.xy;
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
		fx.msdfLayer = int(ld.effectData.z) - 1;
		fx.msdfRange = fract(ld.effectData.z);
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

vec3 osgSlug_Vertex_Rotate(vec3 pos, vec2 emCoord, vec2 origin, float angle) {
	float c = cos(angle), s = sin(angle);
	mat2 R = mat2(c, s, -s, c);
	vec2 pivot = pos.xy - emCoord.xy + origin;
	pos.xy = R * (pos.xy - pivot) + pivot;
	return pos;
}

vec3 osgSlug_Vertex_Scale(vec3 pos, vec2 emCoord, vec2 origin, float scale) {
	vec2 pivot = pos.xy - emCoord.xy + origin;
	pos.xy = (pos.xy - pivot) * scale + pivot;
	return pos;
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
vec3 osgSlug_Vertex(osgSlug_VertexData data);

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
	float r = ld.center.w;
	vec3 Te = ld.tangentEast.xyz;
	vec3 Tn = ld.tangentNorth.xyz;

	// Gnomonic (central) projection: project tangent-plane point back onto sphere surface.
	vec3 world = normalize(P + lu * Te + lv * Tn) * r;

	osgSlug_VertexData vData;

	vData.pos = world;
	vData.emCoord = a_emCoord.xy;
	vData.uv = a_emCoord.zw;
	vData.effectId = effectId;
	vData.origin = sd.originData.xy;
	vData.effectParam = ld.effectData.w;
	vData.time = osg_SimulationTime;

	vec3 pos = osgSlug_Vertex(vData);

	geom.emCoord = a_emCoord.xy;
	geom.uv = a_emCoord.zw;
	geom.layerIndex = a_position.w;
	geom.color = ld.color;
	fx.bandXform = sd.bandXform;
	fx.shapeData = sd.shapeData;
	fx.effectId = effectId;
	fx.gradientId = int(ld.gradientMeta.x + 0.5);
	fx.msdfLayer = -1;
	fx.msdfRange = 0.0;
	fx.effectParam = ld.effectData.w;
	geom.gradientMeta = ld.gradientMeta;
	geom.gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
)";

// Main fragment shader. Stored pre-resolved so PathDrawable.cpp can use it directly.
// The #pragma osgSlug lib_fragment is expanded at static init time - SHADER_FRAG always
// contains the fully substituted SHADER_LIB_FRAGMENT content (struct defs + effect helpers).
const std::string Atlas::SHADER_FRAG = resolveLibs(R"(
#version 330 core

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
	if(msdfSd < 0.0) return flatNormal;

	float bevel = 1.0 - clamp((msdfSd - 0.5) / bevelWidth, 0.0, 1.0);

	if(bevel <= 0.0001) return flatNormal;

	// Gradient points toward the interior; the bevel wants interior->edge, hence the negation.
	vec2 grad = -osgSlug_MSDFGradient(emCoord);
	float gradLen = length(grad);

	if(gradLen <= 0.0001) return flatNormal;

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

#ifndef SLUG_NO_MSAA
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
// main() premultiplies internally to combine this with the normal fill result, then
// unpremultiplies once before writing color; do not pre-weight rgb by alpha yourself.
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
	// Layer mask: 0 = all visible (default). Non-zero: discard if the layer's bit is clear.
	if(osgSlug_layerMask != 0 && (osgSlug_layerMask & (1 << int(geom.layerIndex + 0.5))) == 0) discard;

	ivec2 glyphLoc = ivec2(fx.shapeData.xy);
	ivec2 bandMax = ivec2(fx.shapeData.zw);

	// fwidth on the raw varying, no discontinuities. osgSlug_FragEmCoord may scale it for
	// effects like tiling (where fract would make fwidth unreliable at tile boundaries).
	vec2 emsPerPixel = fwidth(geom.emCoord);

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

		if(fill < 0.001 && onEdge < 0.01) discard;

		vec4 fillColor = osgSlug_Fragment(fData);

		vec4 borderColor = vec4(
			fract(fx.bandXform.x * 127.1),
			fract(fx.bandXform.y * 311.7),
			fract(fx.bandXform.z * 74.3 + fx.bandXform.w * 19.1),
			1.0
		);

		color = mix(fillColor, borderColor, onEdge);

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
	if(fill < 0.001 && extColor.a < 0.001) {
		if(osgSlug_debugMode == 6) color = vec4(0.5, 0.5, 0.5, 0.5);

		else discard;
	}

	// No ext contribution (the overwhelmingly common case) - identical to pre-hook behavior.
	else if(extColor.a < 0.001) {
		color = (osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
			? osgSlug_Fragment(fData)
			: slug_ApplyDebug(fill, geom.emCoord, effectiveColor, glyphLoc, fx.bandXform, iterations)
		;
	}

	else {
		vec4 fillColor = fill < 0.001
			? vec4(0.0)
			: (
				(osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
					? osgSlug_Fragment(fData)
					: slug_ApplyDebug(fill, geom.emCoord, effectiveColor, glyphLoc, fx.bandXform, iterations
				)
			)
		;

		// Combine in premultiplied space - the only space where alpha compositing/addition
		// is well-defined - then unpremultiply back to the straight-alpha convention main()
		// has always used. Skipping the unpremultiply hands the GL blend func
		// (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) an already alpha-weighted color, which it
		// then weights by alpha AGAIN - alpha gets squared, visibly distorting the falloff
		// right at the edge (reads as jagged/banded, not blurry).
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

		color = vec4(outAlpha > 0.0001 ? outPremul / outAlpha : vec3(0.0), outAlpha);
	}

	// TODO: This line is required when using PREMULTIPLIED ALPHA!
	// color.rgb *= color.a;
}
)");

const std::string Atlas::SHADER_NOOP_VERTEX_HOOK = resolveLibs(R"(
#version 430 core

#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex(osgSlug_VertexData data) {
	return data.pos;
}
)");

const std::string Atlas::SHADER_NOOP_FRAGMENT_HOOK = resolveLibs(R"(
#version 330 core

#pragma osgSlug lib_fragment

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
}
)");

const std::string Atlas::SHADER_NOOP_FRAGMENT_EXT_HOOK = resolveLibs(R"(
#version 330 core

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

// Pre-resolved at static-init time so ScanlineDrawable can use it without re-resolving.
const std::string Atlas::SHADER_SCANLINE_FRAG = resolveLibs(R"(
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
// which osgSlug_Mask_Evaluate needs and osgSlug_MaskData deliberately does not carry (see its
// comment in SHADER_LIB_FRAGMENT).
struct osgSlug_LayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 transformData;
};

layout(std430, binding = 1) readonly buffer LayerBuffer {
	osgSlug_LayerData layers[];
};

// Private re-declaration of osgSlug_msdfTexture (see SHADER_TYPES/SHADER_FRAG): same reason as
// LayerBuffer above -- this shader object never gets SHADER_FRAG prepended. GLSL shares the
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

// Apply osgSlug_mask.invert + alpha-gate. Returns premultiplied fragment color - the comment
// always said so, but the code never actually multiplied rgb by alpha until now. Latent since
// this function was written: the SrcOver fast path in ShapeDrawable::drawImplementation()
// used to skip applyBlendMode() entirely for a single masked group, so the mismatch between
// this straight-alpha output and applyBlendMode()'s premultiplied GL_ONE blend func was never
// exercised. Adding mask-awareness to that fast path's condition (RenderGroup.mask) exposed
// it: GL_ONE doesn't scale src by alpha, so any nonzero straight alpha saturated to near-full
// foreground brightness, making the antialiased ramp look like a hard step.
vec4 osgSlug_Mask_Apply(osgSlug_FragmentData data, float maskFill) {
	if(osgSlug_mask.invert) maskFill = 1.0 - maskFill;
	if(maskFill < 0.001) discard;

	float alpha = data.fill * maskFill * data.layerColor.a;

	return vec4(data.layerColor.rgb * alpha, alpha);
}

// Full mask evaluation: reads osgSlug_mask, returns the masked fragment color.
vec4 osgSlug_Mask_Evaluate(osgSlug_FragmentData data) {
	vec2 layerOrigin = layers[int(geom.layerIndex + 0.5) - 1].transformData.xy;
	vec2 canvasCoord = data.emCoord + layerOrigin;
	float maskFill;

	if(osgSlug_mask.type == 0) { // MSDF - baked tile sample
		if(osgSlug_mask.msdfLayer < 0) discard;
		float cx = osgSlug_mask.params.x, cy = osgSlug_mask.params.y;
		float r = osgSlug_mask.params.z, rng = osgSlug_mask.params.w;
		vec4 bbox = vec4(cx - r - rng, cy - r - rng, cx + r + rng, cy + r + rng);
		vec2 tileUV = (canvasCoord - bbox.xy) / (bbox.zw - bbox.xy);
		if(any(lessThan(tileUV, vec2(0.0))) || any(greaterThan(tileUV, vec2(1.0)))) discard;
		vec3 msd = texture(osgSlug_msdfTexture, vec3(tileUV, float(osgSlug_mask.msdfLayer))).rgb;
		if(osgSlug_mask.debug) return vec4(msd.r, msd.g, msd.b, 1.0);
		float maskSd = max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
		float pxRange = max(2.0 * rng / max(data.emsPerPixel.x, data.emsPerPixel.y), 1.0);
		maskFill = clamp((maskSd - 0.5) * pxRange + 0.5, 0.0, 1.0);
	}

	else if(osgSlug_mask.type == 1) { // Circle
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Circle(canvasCoord, osgSlug_mask.params.xy, osgSlug_mask.params.z),
			data.emsPerPixel);
	}

	else if(osgSlug_mask.type == 2) { // Rect
		vec2 center = osgSlug_mask.params.xy + osgSlug_mask.params.zw * 0.5;
		vec2 halfExt = osgSlug_mask.params.zw * 0.5;
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Box(canvasCoord, center, halfExt),
			data.emsPerPixel);
	}

	else if(osgSlug_mask.type == 3) { // Capsule
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Capsule(canvasCoord,
				osgSlug_mask.params.xy, osgSlug_mask.params.zw,
				osgSlug_mask.params2.x),
			data.emsPerPixel);
	}

	else if(osgSlug_mask.type == 4) { // Arc - filled pie sector
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_Pie(canvasCoord, osgSlug_mask.params.xy,
				osgSlug_mask.params.z, osgSlug_mask.params.w,
				osgSlug_mask.params2.x),
			data.emsPerPixel);
	}

	else { // ArcBand - stroked arc
		maskFill = osgSlug_Mask_Coverage(
			osgSlug_SDF_ArcBand(canvasCoord, osgSlug_mask.params.xy,
				osgSlug_mask.params.z, osgSlug_mask.params.w,
				osgSlug_mask.params2.x, osgSlug_mask.params2.y),
			data.emsPerPixel);
	}

	return osgSlug_Mask_Apply(data, maskFill);
}
)";

// ================================================================================================
// State-set builders
// ================================================================================================

osg::StateSet* Atlas::createDefaultStateSet(HookList hooks) const {
	const std::string* vertEffects = &SHADER_NOOP_VERTEX_HOOK;
	const std::string* fragEffects = &SHADER_NOOP_FRAGMENT_HOOK;
	const std::string* fragExt = &SHADER_NOOP_FRAGMENT_EXT_HOOK;

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
	}

	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragExt)));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
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
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	// TODO: This is premultiplied alpha, and needs to be synchronized with the shader!
	// ss->setAttributeAndModes(new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
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

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
	}

	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragExt)));

	// osgSlug_MaskBlock has no inline layout(binding=N) (illegal pre-GL4.20); bound instead via
	// glUniformBlockBinding here, matching where RenderMask::apply() binds at draw time.
	program->addBindUniformBlock("osgSlug_MaskBlock", RENDER_MASK_UBO_BINDING);

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);

	return ss;
}

osg::Program* Atlas::createDecalProgram(HookList hooks) const {
	const std::string* vertEffects = &SHADER_NOOP_VERTEX_HOOK;
	const std::string* fragEffects = &SHADER_NOOP_FRAGMENT_HOOK;
	const std::string* fragExt = &SHADER_NOOP_FRAGMENT_EXT_HOOK;

	for(const auto& [hook, src] : hooks) {
		if(hook == VertexHook) vertEffects = &src;
		else if(hook == FragmentHook) fragEffects = &src;
		else if(hook == FragmentExtHook) fragExt = &src;
	}

	auto* program = new osg::Program();

	// Inject only SHADER_ATLAS_TYPES (binding 0); the decal shader defines osgSlug_DecalLayerData
	// and binding 1 itself, avoiding conflict with the standard osgSlug_LayerData / binding 1.
	program->addShader(makeVertShader(SHADER_VERT_DECAL, SHADER_ATLAS_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_ATLAS_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragExt)));

	return program;
}

}
