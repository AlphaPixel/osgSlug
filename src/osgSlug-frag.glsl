#version 330 core

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
uniform sampler2D osgSlug_effectTexture;
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
//
// O(1) band index lookup via the per-shape indirection tables written by packTextures().
//
// bandTransform.xy maps em-coords to [0, SLUG_INDIRECTION_SIZE) space. The quantized slot
// indexes directly into the Y (or X) indirection table; each table entry's R channel holds the
// band index for that slot. Two texelFetches total per axis, regardless of band count.
//
// Band texture layout per shape (from glyphLoc):
//
// [0 .. INDIRECTION_SIZE-1] = Y indirection table
// [INDIRECTION_SIZE .. 2*IS-1] = X indirection table
// [2*IS .. 2*IS + numHBands - 1] = hband headers
// [2*IS + numHBands .. ...] = vband headers
//
// ...followed by curve index lists, row-wrapped via slug_CalcBandLoc.
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
//
// Core single-sample Slug coverage. pixelsPerEm is passed from the caller (computed via fwidth
// at uniform control flow in main) so sub-pixel MSAA samples share the same pixel-frequency
// estimate.
//
// Returns coverage in [0..1] and, via totalIterations, the total curve-fetch loop iterations
// executed. Use with osgSlug_debugMode 4 (heatmap) to visualise band efficiency.
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

	// Former "& 0x00FF" mask removed; was an artifact of the original HLSL packing where bandMaxY
	// and a flags byte shared one field. In slughorn these are separate values and bandMaxY is
	// always small (numBands - 1).
	//
	// TODO: Investigate WHY the HLSL reference shaders did this! Am I missing something?

	int bandY = slug_BandY(glyphLoc, bandTransform, renderCoord);
	int bandX = slug_BandX(glyphLoc, bandTransform, renderCoord);

	float xcov = 0.0;
	float xwgt = 0.0;
	int iters = 0;

	// hband header at glyphLoc + 2*IS + bandY; vband header at glyphLoc + 2*IS + numHBands + bandX.
	// numHBands = bandMax.y + 1 (bandMaxY = numBandsY - 1, stored in v_shapeData.w).
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
//
// Text-quality wrapper around slug_Render. Adds 4x rotated-grid MSAA below 16 ppem, blending
// toward the supersampled result via smoothstep(16, 8, ppem). Fully supersampled at 8 ppem;
// large text pays nothing (branch never taken above 16 ppem).
//
// Both emsPerPixel and pixelsPerEm are passed from main() (computed once via fwidth at uniform
// control flow) so all 5 samples share the same pixel-frequency estimate.
//
// Define SLUG_NO_MSAA to compile out the MSAA path for perf profiling.
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
//
// Maps a normalised value t in [0..1] to a blue -> green -> red gradient:
//
// t = 0.0 : blue (cheap / few iterations)
// t = 0.5 : green (moderate)
// t = 1.0 : red (expensive / many iterations)
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
//
// Converts a raw em-space coordinate (as interpolated in v_emCoord) into a normalized [0..1] UV
// within the shape's tight bounding box.
//
// bandXform.xy = SLUG_INDIRECTION_SIZE / range, so range = SLUG_INDIRECTION_SIZE / bandXform.xy.
// ------------------------------------------------------------------------------------------------
vec2 slug_EmToUV(vec2 emCoord, vec4 bandXform) {
	vec2 emOrigin = -bandXform.zw / bandXform.xy;
	vec2 emSize = float(SLUG_INDIRECTION_SIZE) / bandXform.xy;

	return (emCoord - emOrigin) / emSize;
}

// ================================================================================================
// slug_ApplyDebug
//
// Resolves osgSlug_debugMode into a diagnostic fragment color. Called when osgSlug_debugMode != 0.
// ================================================================================================
vec4 slug_ApplyDebug(
	float fill,
	vec2 emCoord,
	vec4 layerColor,
	ivec2 glyphLoc,
	vec4 bandXform,
	int iterations
) {
	// bandCoord is in [0, SLUG_INDIRECTION_SIZE) space on both axes.
	vec2 bandCoord = emCoord * bandXform.xy + bandXform.zw;

	// O(1) band index via indirection tables.
	int qY = clamp(int(bandCoord.y), 0, SLUG_INDIRECTION_SIZE - 1);
	int qX = clamp(int(bandCoord.x), 0, SLUG_INDIRECTION_SIZE - 1);

	ivec2 bandIdx = ivec2(
		int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + qX, glyphLoc.y), 0).r),
		int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + qY, glyphLoc.y), 0).r)
	);

	if(osgSlug_debugMode == 1) {
		// ----------------------------------------------------------------------------------------
		// Mode 1: Checkerboard; alternating dark/light per band cell. Useful for confirming band
		// count and grid alignment.
		// ----------------------------------------------------------------------------------------
		bool checker = ((bandIdx.x + bandIdx.y) & 1) == 0;
		vec3 altColor = 1.0 - layerColor.rgb;

		return vec4(checker ? layerColor.rgb : altColor, fill * layerColor.a);
	}

	// Edge detection via indirection table: scan adjacent slots; a boundary exists where adjacent
	// slots map to different band indices. Uses fwidth/smoothstep for 1px anti-aliased lines.
	int bY_prev = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + max(qY - 1, 0), glyphLoc.y), 0).r);
	int bY_next = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + min(qY + 1, SLUG_INDIRECTION_SIZE - 1), glyphLoc.y), 0).r);
	int bX_prev = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + max(qX - 1, 0), glyphLoc.y), 0).r);
	int bX_next = int(texelFetch(osgSlug_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + min(qX + 1, SLUG_INDIRECTION_SIZE - 1), glyphLoc.y), 0).r);

	float fracY = fract(bandCoord.y);
	float dY	= fwidth(bandCoord.y);
	float edgeY = 0.0;
	if(bandIdx.y != bY_prev) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, fracY));
	if(bandIdx.y != bY_next) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY, 1.0 - fracY));

	float fracX = fract(bandCoord.x);
	float dX	= fwidth(bandCoord.x);
	float edgeX = 0.0;
	if(bandIdx.x != bX_prev) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, fracX));
	if(bandIdx.x != bX_next) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX, 1.0 - fracX));

	float atEdge = max(edgeY, edgeX);

	if(osgSlug_debugMode == 2) {
		// ----------------------------------------------------------------------------------------
		// Mode 2: Band edge lines; invert color at band boundaries. 1px anti-aliased via
		// fwidth/smoothstep.
		// ----------------------------------------------------------------------------------------
		return vec4(mix(layerColor.rgb, 1.0 - layerColor.rgb, atEdge), fill * layerColor.a);
	}

	// TODO: Mode 3 is handled in main(); move here when discard/early-return can be avoided.

	// Heatmap shared by modes 4 and 5.
	const int maxIterations = 24;
	float t = float(iterations) / float(maxIterations);
	vec3 heatColor = slug_Heatmap(t);

	if(osgSlug_debugMode == 4) {
		// ----------------------------------------------------------------------------------------
		// Mode 4: Iteration heatmap; blue (cheap) -> green -> red (expensive).
		//
		// maxIterations is the count that maps to full red. With numBands=12 and 14 curves,
		// theoretical worst case is ~28 (14 x 2 passes), but band culling means most fragments see
		// far fewer. Tune as needed. With MSAA active, iters is multiplied by 5 so the heatmap
		// remains a useful cost signal at small sizes.
		// ----------------------------------------------------------------------------------------
		return vec4(heatColor, fill * layerColor.a);
	}

	if(osgSlug_debugMode == 5) {
		// ----------------------------------------------------------------------------------------
		// Mode 5: Heatmap + 1px band grid overlay.
		//
		// Heatmap shows iteration cost; white grid lines at real band boundaries (adaptive or
		// uniform), anti-aliased to exactly 1 fragment via fwidth/smoothstep.
		// ----------------------------------------------------------------------------------------
		return vec4(mix(heatColor, vec3(1.0), atEdge), fill * layerColor.a);
	}

	// Fallback
	return vec4(1.0, 0.0, 1.0, fill * layerColor.a);
}

float slug_StemDarken(float coverage, float brightness, float ppem) {
	float k = mix(pow(2.0, brightness - 0.5), 1.0, smoothstep(8.0, 48.0, ppem));

	return pow(coverage, k);
}

// Sample the MSDF atlas for the current fragment.
// Computes tileUV from v_emCoord + v_bandXform, accounting for the range margin baked
// into the tile by renderMSDFTile (tile covers tight bbox expanded by v_msdfRange on all sides).
// Returns 0.5 (neutral) when no MSDF tile is registered for this shape (v_msdfLayer < 0).
// Callable from any fragment hook linked into the same program.
#define SLUG_INDIRECTION_SIZE 32
float osgSlug_SampleMSDF() {
	if(v_msdfLayer < 0) return 0.5;

	vec2 emOrigin = -v_bandXform.zw / v_bandXform.xy;
	vec2 emSize   = float(SLUG_INDIRECTION_SIZE) / v_bandXform.xy;
	vec2 tileUV   = (v_emCoord - emOrigin + v_msdfRange) / (emSize + 2.0 * v_msdfRange);

	vec3 msd = texture(osgSlug_msdfTexture, vec3(tileUV, float(v_msdfLayer))).rgb;

	return max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
}

// Returns the per-layer float set via setLayerEffectParam().
// Defaults to 0 unless the caller sets it after compile().
float osgSlug_EffectParam() { return v_effectParam; }

// Defined in the linked effects or noop unit.
vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time);

vec4 osgSlug_Fragment(
	float fill,
	vec2 emCoord,
	vec2 uv,
	vec4 layerColor,
	int effectId,
	float time
);

void main() {
	// Layer mask: 0 = all visible (default). Non-zero: discard if the layer's bit is clear.
	// Decode 1-based layer index (packed as float in a_position.w by Drawable::compile()).
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
	// Skipped entirely for non-text layers (osgSlug_textMode == false).
	// Applied only to partially-covered fragments; fully covered interiors are untouched.
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
			// t = length(B * (emCoord - center)) - r0_norm
			// For circular: B = invDR * I (degenerate ellipse); for affine: full 2x2.
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

	// Draws a pixel-perfect border around the quad using true [0,1] UV coords from v_uv.
	if(osgSlug_debugMode == 3) {
		vec2 uv = v_uv;

		vec2 distToEdge = min(uv, 1.0 - uv);
		float dist = min(distToEdge.x, distToEdge.y);

		vec2 fw = fwidth(uv);
		float px = min(fw.x, fw.y);

		float onEdge = step(dist, px);

		if(fill < 0.001 && onEdge < 0.01) discard;

		vec4 fillColor = osgSlug_Fragment(fill, v_emCoord, v_uv, effectiveColor, v_effectId, osg_SimulationTime);

		vec4 borderColor = vec4(
			fract(v_bandXform.x * 127.1),
			fract(v_bandXform.y * 311.7),
			fract(v_bandXform.z * 74.3 + v_bandXform.w * 19.1),
			1.0
		);

		color = mix(fillColor, borderColor, onEdge);

		return;
	}

	// Using the "half white" line helps show 3D shapes, so... leaving it in for now.
	if(fill < 0.001) {
		if(osgSlug_debugMode == 6) color = vec4(0.5, 0.5, 0.5, 0.5);

		else discard;
	}

	else {
		color = (osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
			? osgSlug_Fragment(fill, v_emCoord, v_uv, effectiveColor, v_effectId, osg_SimulationTime)
			: slug_ApplyDebug(fill, v_emCoord, effectiveColor, glyphLoc, v_bandXform, iterations)
		;
	}

	// TODO: This line is required when using PREMULTIPLIED ALPHA!
	// color.rgb *= color.a;
}
