#include "osgSlug/Atlas.hpp"

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
struct AtlasShapeData {
	vec4 bandXform; // xy = bandScaleX/Y, zw = bandOffsetX/Y
	vec4 shapeData; // xy = glyphLoc (ivec2), zw = bandMax (ivec2)
	vec4 originData; // xy = originX/Y, zw = unused
};

layout(std430, binding = 0) readonly buffer AtlasShapeBuffer {
	AtlasShapeData atlasShapes[];
};
)";

const std::string Atlas::SHADER_TYPES = SHADER_ATLAS_TYPES + R"(
// Per-layer data: one entry per layer in each drawable, indexed by (a_position.w - 1).
// Slot order: slughorn contract first (color, gradient), osgSlug machinery last (effectData).
struct LayerData {
	vec4 color; // RGBA flat color
	vec4 gradientMeta; // x = gradientId (1-based), yz = gradient center, w = r0_norm
	vec4 gradientXform;// gradient transform (B matrix / direction / sweep)
	vec4 effectData; // x = effectId, y = shapeIndex (into AtlasShapeBuffer), z = 0, w = effectParam
};

layout(std430, binding = 1) buffer LayerBuffer {
	LayerData layers[];
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
)";

const std::string Atlas::SHADER_LIB_FRAGMENT = R"(
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

out vec2 v_emCoord;
out vec2 v_uv;
out vec4 v_color;
out float v_layerIndex;

flat out vec4 v_bandXform;
flat out vec4 v_shapeData;
flat out int v_effectId;
flat out int v_gradientId;
flat out int v_msdfLayer;
flat out float v_msdfRange;
flat out float v_effectParam;
out vec4 v_gradientMeta;
out vec4 v_gradientXform;

// Defined in the linked effects or noop unit.
vec3 osgSlug_Vertex(osgSlug_VertexData data);

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	LayerData ld = layers[layerIdx];
	AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

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

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_color = ld.color;
	v_bandXform = sd.bandXform;
	v_shapeData = sd.shapeData;
	v_effectId = effectId;
	if(ld.effectData.z < 0.0) {
		v_msdfLayer = -1;
		v_msdfRange = 0.0;
	}
	else {
		v_msdfLayer = int(ld.effectData.z) - 1;
		v_msdfRange = fract(ld.effectData.z);
	}
	v_effectParam = ld.effectData.w;
	v_gradientId = int(ld.gradientMeta.x + 0.5);
	v_gradientMeta = ld.gradientMeta;
	v_gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
)";

const std::string Atlas::SHADER_VERT_GL3 = R"(
#version 330 core

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

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec4 a_emCoord;
layout(location = 3) in vec4 a_bandXform;
layout(location = 4) in vec4 a_shapeData;

// .x = effectId (integer packed as float)
// .yz = origin (shape.originX, shape.originY)
// .w = effectParam (user-settable via setLayerEffectParam)
layout(location = 5) in vec4 a_effectData;

layout(location = 6) in vec4 a_gradientMeta;
layout(location = 7) in vec4 a_gradientXform;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

out vec2 v_emCoord;
out vec2 v_uv;
out vec4 v_color;
out float v_layerIndex;

flat out vec4 v_bandXform;
flat out vec4 v_shapeData;
flat out int v_effectId;
flat out int v_gradientId;
flat out int v_msdfLayer;
flat out float v_msdfRange;
flat out float v_effectParam;
out vec4 v_gradientMeta;
out vec4 v_gradientXform;

// Defined in the linked effects or noop unit.
vec3 osgSlug_Vertex(osgSlug_VertexData data);

void main() {
	int effectId = int(a_effectData.x + 0.5);

	osgSlug_VertexData vData;

	vData.pos = a_position.xyz;
	vData.emCoord = a_emCoord.xy;
	vData.uv = a_emCoord.zw;
	vData.effectId = effectId;
	vData.origin = a_effectData.yz;
	vData.effectParam = a_effectData.w;
	vData.time = osg_SimulationTime;

	vec3 pos = osgSlug_Vertex(vData);

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_color = a_color;
	v_bandXform = a_bandXform;
	v_shapeData = a_shapeData;
	v_effectId = effectId;
	v_msdfLayer = -1;
	v_msdfRange = 0.0;
	v_effectParam = a_effectData.w;
	v_gradientId = int(a_gradientMeta.x + 0.5);
	v_gradientMeta = a_gradientMeta;
	v_gradientXform = a_gradientXform;

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

// Vertex layout (set by SSBODecalDrawable::compile()):
//
// a_position: xy = (lu, lv) normalized grid in [0,1], z = 0, w = layer index (1-based)
// a_emCoord: xy = shape em-space coordinate, zw = (lu, lv) UV

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_emCoord;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

out vec2 v_emCoord;
out vec2 v_uv;
out vec4 v_color;
out float v_layerIndex;

flat out vec4 v_bandXform;
flat out vec4 v_shapeData;
flat out int v_effectId;
flat out int v_gradientId;
out vec4 v_gradientMeta;
out vec4 v_gradientXform;

// Defined in the linked effects or noop unit.
vec3 osgSlug_Vertex(osgSlug_VertexData data);

struct DecalLayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 center;
	vec4 tangentEast;
	vec4 tangentNorth;
};

layout(std430, binding = 1) buffer DecalLayerBuffer {
	DecalLayerData decalLayers[];
};

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	DecalLayerData ld = decalLayers[layerIdx];
	AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

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

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_color = ld.color;
	v_bandXform = sd.bandXform;
	v_shapeData = sd.shapeData;
	v_effectId = effectId;
	v_gradientId = int(ld.gradientMeta.x + 0.5);
	v_gradientMeta = ld.gradientMeta;
	v_gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
)";

// Main fragment shader. Stored pre-resolved so InkDrawable.cpp can use it directly.
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

in vec2 v_emCoord;
in vec2 v_uv;
in vec4 v_color;
in float v_layerIndex;

flat in vec4 v_bandXform;
flat in vec4 v_shapeData;
flat in int v_effectId;
flat in int v_gradientId;
flat in int v_msdfLayer;
flat in float v_msdfRange;
flat in float v_effectParam;
in vec4 v_gradientMeta;
in vec4 v_gradientXform;

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

	float fracY = fract(bandCoord.y);
	float dY = fwidth(bandCoord.y);
	float edgeY = 0.0;
	if(bandIdx.y != bY_prev) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, fracY));
	if(bandIdx.y != bY_next) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, 1.0 - fracY));

	float fracX = fract(bandCoord.x);
	float dX = fwidth(bandCoord.x);
	float edgeX = 0.0;
	if(bandIdx.x != bX_prev) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, fracX));
	if(bandIdx.x != bX_next) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, 1.0 - fracX));

	float atEdge = max(edgeY, edgeX);

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
// data.emsPerPixel: fwidth(v_emCoord) in the raw (untiled) coordinate space. Multiply a
// target em-space width by emsPerPixel to get a constant screen-size effect at any zoom.
vec4 osgSlug_FragmentExt(osgSlug_FragmentExtData data, out int blendMode);

void main() {
	// Layer mask: 0 = all visible (default). Non-zero: discard if the layer's bit is clear.
	if(osgSlug_layerMask != 0 && (osgSlug_layerMask & (1 << int(v_layerIndex + 0.5))) == 0) discard;

	ivec2 glyphLoc = ivec2(v_shapeData.xy);
	ivec2 bandMax = ivec2(v_shapeData.zw);

	// fwidth on the raw varying, no discontinuities. osgSlug_FragEmCoord may scale it for
	// effects like tiling (where fract would make fwidth unreliable at tile boundaries).
	vec2 emsPerPixel = fwidth(v_emCoord);

	// Allow effects to remap em-coords (e.g. fract-based GPU tiling). Gradients and debug
	// visualisation stay on the raw v_emCoord; only coverage sampling uses renderCoord.
	vec2 renderCoord = osgSlug_FragEmCoord(v_emCoord, emsPerPixel, v_effectId, osg_SimulationTime);

	vec2 pixelsPerEm = 1.0 / emsPerPixel;

	int iterations;

	float fill = osgSlug_textMode
		? slug_RenderText(renderCoord, emsPerPixel, pixelsPerEm, v_bandXform, glyphLoc, bandMax, iterations)
		: slug_Render(renderCoord, pixelsPerEm, v_bandXform, glyphLoc, bandMax, iterations)
	;

	// Edge-only coverage adjustment for text: stem darkening and gamma correction.
	if (osgSlug_textMode && fill > 0.0 && fill < 1.0) {
		float ppem = 1.0 / max(emsPerPixel.x, emsPerPixel.y);
		float adj = fill;

		if (osgSlug_stemDarken) {
			float brightness = dot(v_color.rgb, vec3(0.299, 0.587, 0.114));

			adj = slug_StemDarken(adj, brightness, ppem);
		}

		if(osgSlug_gamma != 1.0) adj = pow(adj, osgSlug_gamma);

		fill = adj;
	}

	vec4 effectiveColor = v_color;

	if(v_gradientId > 0 && osgSlug_gradientCount > 0) {
		float t;

		if(v_gradientXform.w == 0.0) {
			// Linear: xform = (dirX, dirY, offset, 0)
			t = dot(v_emCoord, v_gradientXform.xy) + v_gradientXform.z;
		}

		else if(v_gradientXform.w > 0.0) {
			// Radial/AffineRadial: xform = B matrix (column-major mat2); meta.yz = center; meta.w = r0_norm
			vec2 d = v_emCoord - v_gradientMeta.yz;
			mat2 B = mat2(v_gradientXform);

			t = length(B * d) - v_gradientMeta.w;
		}

		else {
			// Sweep: xform = (cx, cy, startAngle, -invArcSpan)
			float angle = atan(
				v_emCoord.y - v_gradientXform.y,
				v_emCoord.x - v_gradientXform.x
			);

			t = (angle - v_gradientXform.z) * (-v_gradientXform.w);
		}

		t = clamp(t, 0.0, 1.0);

		float gv = (float(v_gradientId) - 0.5) / float(osgSlug_gradientCount);
		vec4 gc = texture(osgSlug_gradientTexture, vec2(t, gv));

		effectiveColor = vec4(gc.rgb, gc.a * v_color.a);
	}

	// Compute msdfSd once; -1.0 means no tile registered. Shared by fData and feData.
	float msdfSd = -1.0;
	if(v_msdfLayer >= 0) {
		vec2 emOrigin = -v_bandXform.zw / v_bandXform.xy;
		vec2 emSize = float(SLUG_INDIRECTION_SIZE) / v_bandXform.xy;
		vec2 tileUV = (v_emCoord - emOrigin + v_msdfRange) / (emSize + 2.0 * v_msdfRange);
		vec3 msd = texture(osgSlug_msdfTexture, vec3(tileUV, float(v_msdfLayer))).rgb;
		msdfSd = max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
	}

	// Build osgSlug_FragmentData once; shared by all osgSlug_Fragment call sites below.
	osgSlug_FragmentData fData;

	fData.fill = fill;
	fData.emCoord = v_emCoord;
	fData.uv = v_uv;
	fData.layerColor = effectiveColor;
	fData.effectId = v_effectId;
	fData.time = osg_SimulationTime;
	fData.msdfSd = msdfSd;
	fData.effectParam = v_effectParam;

	// Draws a pixel-perfect border around the quad using true [0,1] UV coords from v_uv.
	if(osgSlug_debugMode == 3) {
		vec2 uv = v_uv;

		vec2 distToEdge = min(uv, 1.0 - uv);
		float dist = min(distToEdge.x, distToEdge.y);

		vec2 fw = fwidth(uv);
		float px = min(fw.x, fw.y);

		float onEdge = step(dist, px);

		if(fill < 0.001 && onEdge < 0.01) discard;

		vec4 fillColor = osgSlug_Fragment(fData);

		vec4 borderColor = vec4(
			fract(v_bandXform.x * 127.1),
			fract(v_bandXform.y * 311.7),
			fract(v_bandXform.z * 74.3 + v_bandXform.w * 19.1),
			1.0
		);

		color = mix(fillColor, borderColor, onEdge);

		return;
	}

	// Build osgSlug_FragmentExtData and call the pre-discard hook.
	osgSlug_FragmentExtData feData;

	feData.fill = fill;
	feData.msdfSd = msdfSd;
	feData.msdfLayer = v_msdfLayer;
	feData.msdfRange = v_msdfRange;
	feData.emCoord = v_emCoord;
	feData.uv = v_uv;
	feData.layerColor = effectiveColor;
	feData.effectId = v_effectId;
	feData.effectParam = v_effectParam;
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
			: slug_ApplyDebug(fill, v_emCoord, effectiveColor, glyphLoc, v_bandXform, iterations)
		;
	}

	else {
		vec4 fillColor = fill < 0.001
			? vec4(0.0)
			: (
				(osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
					? osgSlug_Fragment(fData)
					: slug_ApplyDebug(fill, v_emCoord, effectiveColor, glyphLoc, v_bandXform, iterations
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
// State-set builders
// ================================================================================================

osg::StateSet* Atlas::createDefaultStateSet(bool useGL3, HookList hooks) const {
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

	// GL3 effect units must be #version 330 core; swap any 430 declaration.
	auto makeGL3EffectShader = [&](std::string src) {
		src = resolveLibs(src);

		const auto vp = src.find("#version");

		if(vp != std::string::npos) {
			const auto nl = src.find('\n', vp);

			if(nl != std::string::npos) src.replace(vp, nl - vp, "#version 330 core");
		}

		return new osg::Shader(osg::Shader::VERTEX, src);
	};

	if(useGL3) {
		program->addShader(new osg::Shader(osg::Shader::VERTEX, resolveLibs(SHADER_VERT_GL3)));
		program->addShader(makeGL3EffectShader(*vertEffects));
	}

	else {
		program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
		program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	}

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

	if(_shapeBuffer.valid()) {
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
	const bool useGL3 = _useGL3;
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

	auto makeGL3EffectShader = [&](std::string src) {
		src = resolveLibs(src);

		const auto vp = src.find("#version");

		if(vp != std::string::npos) {
			const auto nl = src.find('\n', vp);

			if(nl != std::string::npos) src.replace(vp, nl - vp, "#version 330 core");
		}

		return new osg::Shader(osg::Shader::VERTEX, src);
	};

	if(useGL3) {
		program->addShader(new osg::Shader(osg::Shader::VERTEX, resolveLibs(SHADER_VERT_GL3)));
		program->addShader(makeGL3EffectShader(*vertEffects));
	}

	else {
		program->addShader(makeVertShader(SHADER_VERT, SHADER_TYPES));
		program->addShader(makeVertShader(*vertEffects, SHADER_TYPES));
	}

	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragExt)));

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

	// Inject only SHADER_ATLAS_TYPES (binding 0); the decal shader defines DecalLayerData
	// and binding 1 itself, avoiding conflict with the standard LayerData / binding 1.
	program->addShader(makeVertShader(SHADER_VERT_DECAL, SHADER_ATLAS_TYPES));
	program->addShader(makeVertShader(*vertEffects, SHADER_ATLAS_TYPES));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, SHADER_FRAG));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragEffects)));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(*fragExt)));

	return program;
}

}
