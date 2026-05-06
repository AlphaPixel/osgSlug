#version 330 core

in vec2 v_emCoord;
in vec4 v_color;

flat in vec4 v_bandXform;
flat in vec4 v_shapeData;
flat in int v_effectId;

uniform float osg_SimulationTime;

uniform sampler2D osgSlug_curveTexture;
uniform usampler2D osgSlug_bandTexture;
uniform sampler2D osgSlug_effectTexture;
uniform int osgSlug_debugMode;

out vec4 color;

// TODO: This needs to match slughorn::Atlas::TEX_WIDTH, and for NOW is hardcoded to 512 (1 << 9).
#define TEX_WIDTH 9

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

	bandLoc.y += bandLoc.x >> TEX_WIDTH;
	bandLoc.x &= (1 << TEX_WIDTH) - 1;

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
//   [0 .. INDIRECTION_SIZE-1]		   Y indirection table
//   [INDIRECTION_SIZE .. 2*IS-1]		X indirection table
//   [2*IS .. 2*IS + numHBands - 1]	  hband headers
//   [2*IS + numHBands .. ...]		   vband headers
//   (followed by curve index lists, row-wrapped via slug_CalcBandLoc)
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
// Returns Slug coverage in [0..1] and, via totalIterations, the total number of curve-fetch loop
// iterations executed for this fragment. The iteration count is the primary cost signal: more
// iterations = more texture fetches = more expensive fragment. Use with osgSlug_debugMode 3
// (heatmap) to visualise band efficiency.
// ------------------------------------------------------------------------------------------------
float slug_Render(
	vec2 renderCoord,
	vec4 bandTransform,
	ivec2 glyphLoc,
	ivec2 bandMax,
	out int totalIterations
) {
	int curveIndex;

	vec2 emsPerPixel = fwidth(renderCoord);
	vec2 pixelsPerEm = 1.0 / emsPerPixel;

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

// =============================================================================
// slug_ApplyEffect
//
// Resolves the per-layer effectId into a final fragment color.
// Called in normal rendering mode (osgSlug_debugMode == 0).
// =============================================================================
vec4 slug_ApplyEffect(
	float fill,
	vec2 emCoord,
	vec4 layerColor,
	int effectId,
	vec4 bandXform
) {
	if(effectId == 1) {
		// ----------------------------------------------------------------------------------------
		// Effect 1: checkerboard.
		//
		// Hard two-tone grid. Uses raw em-coords scaled to a visible frequency; not a true UV, just
		// a diagnostic pattern. kScale is empirical: 300 gives ~37 cells across a 100px /
		// 800px-canvas tile.
		// ----------------------------------------------------------------------------------------
		const float kScale = 300.0;

		vec2 emScaled = emCoord * kScale;
		float check = mod(floor(emScaled.x) + floor(emScaled.y), 2.0);
		vec3 colorA = layerColor.rgb;
		vec3 colorB = min(layerColor.rgb + vec3(0.25), vec3(1.0));

		return vec4(mix(colorA, colorB, check), fill * layerColor.a);
	}

	if(effectId == 2) {
		// ----------------------------------------------------------------------------------------
		// Effect 2: pixel grid.
		//
		// Anti-aliased 1px grid lines in em-space using fwidth/smoothstep; lines stay exactly 1
		// fragment wide at any zoom or rotation. Cell interiors are slightly lighter than the
		// layer color; lines are noticeably darker, giving a clean "pixel art graph paper" look.
		// kGridScale is empirical: 200 gives a fine grid at typical zoom.
		// ----------------------------------------------------------------------------------------
		const float kGridScale = 200.0;

		vec2 emScaled = emCoord * kGridScale;
		vec2 emFrac = fract(emScaled);
		vec2 edgeDist = min(emFrac, 1.0 - emFrac); // 0 at lines, 0.5 at centres
		vec2 fw = fwidth(emScaled); // ~1 fragment in em-scaled space

		vec2 lineMask = smoothstep(fw, vec2(0.0), edgeDist);
		float atLine = max(lineMask.x, lineMask.y);

		vec3 cellColor = min(layerColor.rgb + vec3(0.12), vec3(1.0));
		vec3 lineColor = layerColor.rgb * 0.55;

		return vec4(mix(cellColor, lineColor, atLine), fill * layerColor.a);
	}

	if(effectId == 3) {
		// ----------------------------------------------------------------------------------------
		// Effect 3: texture fill.
		//
		// slug_EmToUV() gives a true 0..1 UV across the shape's bounding box. The texture is
		// sampled directly; bind any osg::Texture2D to unit 2 via the state set.
		// ----------------------------------------------------------------------------------------
		vec2 uv01 = slug_EmToUV(emCoord, bandXform);
		vec4 s = texture(osgSlug_effectTexture, uv01);

		vec3 blended = mix(layerColor.rgb, s.rgb, s.a);
		return vec4(blended, fill * layerColor.a);
	}

	if(effectId == 4) {
		vec2 uv01 = slug_EmToUV(emCoord, bandXform);

		// Scroll right-to-left by offsetting X with time
		float scrolled = uv01.x + float(osg_SimulationTime) * 0.3; // speed

		// Sine wave Y position of the wave at this X; 3 cycles, amplitude 0.15, centred at 0.5
		float wave = sin(scrolled * 6.28 * 3.0) * 0.15 + 0.5;

		// Stroke thickness
		float dist = abs(uv01.y - wave);
		float stroke = smoothstep(0.04, 0.01, dist); // ~1px soft edge

		// Two-tone split at the wave centre
		vec3 colorTop = vec3(1.0, 0.6, 0.1); // orange
		vec3 colorBottom = vec3(0.1, 0.5, 1.0); // blue
		vec3 fill3 = uv01.y > wave ? colorTop : colorBottom;

		// Blend stroke color (white) over the fill
		vec3 final = mix(fill3, vec3(1.0), stroke);

		return vec4(final, fill * layerColor.a);
	}

	// A kind of "paper burning away" effect; very cool. :)
	if(effectId == 5) {
		vec2 uv01 = slug_EmToUV(emCoord, bandXform);

		float t = osg_SimulationTime;

		// ------------------------------------------------------------
		// DOMAIN WARP (subtle, safe)
		// ------------------------------------------------------------
		vec2 emWarp = emCoord;
		emWarp.x += sin(emCoord.y * 6.0 + t * 2.0) * 0.02;
		emWarp.y += sin(emCoord.x * 4.0 + t * 1.5) * 0.015;

		// NOTE: we do NOT recompute fill — this is just for effects
		// (important: keeps banding intact)

		// ------------------------------------------------------------
		// DRAW-ON (UV space)
		// ------------------------------------------------------------
		float draw = fract(t * 0.25); // slow loop 0→1

		float wave = sin(uv01.y * 10.0 + t * 3.0) * 0.05;
		float mask = smoothstep(draw - 0.1, draw, uv01.x + wave);

		// ------------------------------------------------------------
		// COLOR
		// ------------------------------------------------------------
		vec3 base = layerColor.rgb;
		vec3 glow = vec3(1.0, 0.8, 0.2);

		vec3 final = mix(base * 0.3, glow, mask);

		// return vec4(final, fill * mask * layerColor.a);
		float alpha = fill * layerColor.a;

		// soften leading edge
		float edge = smoothstep(draw - 0.2, draw, uv01.x);

		alpha *= edge;

		return vec4(final, alpha);
	}

	// Effect 0 (default): standard Slug coverage fill.
	return vec4(layerColor.rgb, fill * layerColor.a);
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
	if(bandIdx.y != bY_prev) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY * 2.0, fracY));
	if(bandIdx.y != bY_next) edgeY = max(edgeY, 1.0 - smoothstep(0.0, dY * 2.0, 1.0 - fracY));

	float fracX = fract(bandCoord.x);
	float dX	= fwidth(bandCoord.x);
	float edgeX = 0.0;
	if(bandIdx.x != bX_prev) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX * 2.0, fracX));
	if(bandIdx.x != bX_next) edgeX = max(edgeX, 1.0 - smoothstep(0.0, dX * 2.0, 1.0 - fracX));

	float atEdge = max(edgeY, edgeX);

	if(osgSlug_debugMode == 2) {
		// ----------------------------------------------------------------------------------------
		// Mode 2: Band edge lines; invert color at band boundaries. 1px anti-aliased via
		// fwidth/smoothstep.
		// ----------------------------------------------------------------------------------------
		return vec4(mix(layerColor.rgb, 1.0 - layerColor.rgb, atEdge), fill * layerColor.a);
	}

	// TODO: Mode 3 is handled in `main()`, but ... stupidly.

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
		// far fewer. Tune as needed.
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

// TODO: Borrowed from Behdad/Harfbuzz. It must operate on raw coverage, and thus can't just be
// thrown into `osgSlug_Render`. We need to decide WHERE.
//
// vec2 emsPerPixel = fwidth(v_emCoord);
// float ppem = 1.0 / max(emsPerPixel.x, emsPerPixel.y);
//
// crude brightness estimate from layer color
// float brightness = dot(v_color.rgb, vec3(0.299, 0.587, 0.114));
//
// fill = slug_StemDarken(fill, brightness, ppem);
float slug_StemDarken(float coverage, float brightness, float ppem) {
	float k = mix(pow(2.0, brightness - 0.5), 1.0, smoothstep(8.0, 48.0, ppem));

	return pow(coverage, k);
}

// TODO: premultiplied alpha
void main() {
	ivec2 glyphLoc = ivec2(v_shapeData.xy);
	ivec2 bandMax = ivec2(v_shapeData.zw);

	int iterations;

	float fill = slug_Render(v_emCoord, v_bandXform, glyphLoc, bandMax, iterations);

	// Draws a border around the quad; however, because `slugEmToUV` returns the "metrics-based"
	// size--and due to the mandatory "expand" value used in order to allow antialiasing--it's
	// impossible to get a CONSISTENTLY-sized "1 pixel" border. To do that, we'd need the
	// actual/traditional UV values in the range 0-1.
	if(osgSlug_debugMode == 3) {
		vec2 uv = slug_EmToUV(v_emCoord, v_bandXform);

		vec2 distToEdge = min(uv, 1.0 - uv);
		float dist = min(distToEdge.x, distToEdge.y);

		vec2 fw = fwidth(uv);
		float px = min(fw.x, fw.y);

		float onEdge = step(dist, px);

		/* vec2 uv = slug_EmToUV(v_emCoord, v_bandXform);

		// detect where actual coverage starts
		float edgeThreshold = 0.001;

		// find distance to first "filled" region
		vec2 distToEdge = min(uv, 1.0 - uv);
		float dist = min(distToEdge.x, distToEdge.y);

		// pixel size
		vec2 fw = fwidth(uv);
		float px = min(fw.x, fw.y);

		// --- KEY IDEA ---
		// shrink edge by coverage instead of guessing padding
		float effectiveDist = dist - (1.0 - fill) * px;

		float onEdge = step(effectiveDist, px); */

		if(fill < 0.001 && onEdge < 0.01) discard;

		vec4 fillColor = slug_ApplyEffect(fill, v_emCoord, v_color, v_effectId, v_bandXform);

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
		/* if(osgSlug_debugMode == 10) {
			vec2 emsPerPixel = fwidth(v_emCoord);
			float ppem = 1.0 / max(emsPerPixel.x, emsPerPixel.y);

			// crude brightness estimate from layer color
			float brightness = dot(v_color.rgb, vec3(0.299, 0.587, 0.114));

			fill = slug_StemDarken(fill, brightness, ppem);
		} */

		// color = (osgSlug_debugMode == 0 || osgSlug_debugMode == 6 || osgSlug_debugMode == 10)
		color = (osgSlug_debugMode == 0 || osgSlug_debugMode == 6)
			? slug_ApplyEffect(fill, v_emCoord, v_color, v_effectId, v_bandXform)
			: slug_ApplyDebug(fill, v_emCoord, v_color, glyphLoc, v_bandXform, iterations)
		;
	}
}
