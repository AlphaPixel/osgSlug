#include "osgSlug/Drawable/PathDrawable.hpp"

#include "Drawable/Util.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <osg/Depth>
#include <osgUtil/CullVisitor>

OSGSLUG_ENABLE_WARNINGS

#include "slughorn/canvas.hpp"

#include <algorithm>

#include <bit>

namespace osgSlug {

namespace {

// Per-cull update of PATH_EM_SCALE's osgSlug_viewport uniform - same pattern as
// Atlas.cpp's ViewportUniformCallback, duplicated here since PathDrawable's StateSet is
// standalone (setAtlas(), not atlas->addChild() - see Drawable::getAtlas()'s comment) and
// doesn't inherit an Atlas ancestor's copy of the uniform.
struct PathDrawableViewportCallback: public osg::NodeCallback {
	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		auto* cv = nv ? nv->asCullVisitor() : nullptr;
		const auto* vp = cv && cv->getCurrentCamera() ? cv->getCurrentCamera()->getViewport() : nullptr;

		if(vp) {
			auto* ss = node->getStateSet();

			if(auto* u = ss ? ss->getUniform("osgSlug_viewport") : nullptr; u) u->set(osg::Vec2(
				static_cast<float>(vp->width()),
				static_cast<float>(vp->height())
			));
		}

		traverse(node, nv);
	}
};

}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Preamble shared by all four PathDrawable vertex mains: the points SSBO, the uniforms every
// mode carries, and the plumbing that puts PathDrawable on the SAME osgSlug_Vertex() hook
// contract as ShapeDrawable/SubdividedDrawable/DecalDrawable.
//
// That contract is the whole point of this string. PathDrawable was built standalone from day one
// and had no vertex hook at all, so animation that comes almost free on ShapeDrawable (see
// pyosgslug-zora.py's VERT_EFFECTS) was simply unreachable here, and every attempt to add a
// specific motion ended up proposing another bespoke uniform pair. A hook unit written against
// osgSlug_VertexData now links against any of these mains unchanged.
//
// Two notes for hook authors targeting PathDrawable specifically:
//
// - gl_InstanceID is readable directly inside the hook (it is an ordinary vertex shader unit in
//   the same Program), and is the per-particle/per-segment identity. Nothing needs to plumb it
//   through osgSlug_VertexData - ShapeDrawable's non-instanced draws just read 0 there.
// - osgSlug_Vertex_Rotate/_Scale reconstruct their pivot assuming one em maps to one world unit,
//   which is not true for Stamp mode (its rate is u_halfWidth * 2). Rotate about
//   points[gl_InstanceID].xy by hand instead. See SHADER_VERTEX_MAIN's note.
static const std::string PATH_COMMON = R"GLSL(
	#version 430 core

	#pragma osgSlug vertex,vertex_lib,vertex_main

	uniform float u_halfWidth;
	uniform vec4 u_color;
	uniform int u_effectId; // PathDrawable::setEffectId() - the hook's selector, as on a layer
	uniform float u_effectParam; // PathDrawable::setEffectParam() - the hook's float knob
	uniform vec2 u_origin; // shape origin in em-space (Sluggit/Stamp); zero for Miter
	uniform float osg_SimulationTime;

	layout(std430, binding = @osgSlug::path.points@) buffer PathData {
		vec4 points[];
	};

	// Defined by the linked hook or noop unit - see Atlas::createProgram().
	osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data);

	// Fills the per-corner hook input. Everything a PathDrawable corner genuinely varies
	// (pos/emCoord/uv, its em<->world frame, and - for the multi-shape Stamp table - the shape
	// origin) is an argument; the rest come from uniforms and are identical for every instance,
	// since PathDrawable has no per-layer SSBO to read them from the way SHADER_VERT does.
	osgSlug_VertexData pathVertexData(
		vec3 pos,
		vec2 emCoord,
		vec2 uv,
		vec2 origin,
		vec4 axisX,
		vec4 axisY
	) {
		osgSlug_VertexData d;

		d.pos = pos;
		d.emCoord = emCoord;
		d.uv = uv;
		d.effectId = u_effectId;
		d.origin = origin;
		d.effectParam = u_effectParam;
		d.time = osg_SimulationTime;
		d.bleed = 0.0; // no layer.bleed; Sluggit pushes its pixel margin after the hook, Stamp bakes PATH_EXPAND
		d.axisX = axisX;
		d.axisY = axisY;

		return d;
	}
)GLSL";

// Mitered per-segment quad geometry; used by Miter and Sluggit. Append PATH_MITER_MAIN or
// PATH_SLUGGIT_MAIN to PATH_COMMON + this to form a complete shader.
static const std::string PATH_MITER_GEOM = R"GLSL(
	const float MAX_MITER = 4.0; // matches PATH_MAX_MITER (C++, computeBoundingBox())

	vec2 perpOf(vec2 dir) { return vec2(-dir.y, dir.x); }

	// Subpath topology of this instance's segment, from points[].w (see computeQuad()'s comment).
	// pathIsGap(): the instance spans two different subpaths and is not a real segment.
	// pathHasPrev()/pathHasNext(): the segment's start/end joins a neighbor segment of the same
	// subpath; when false, that end is a cap.
	bool pathIsGap() { return points[gl_InstanceID].w != points[gl_InstanceID + 1].w; }
	bool pathHasPrev() { return gl_InstanceID > 0 && points[gl_InstanceID - 1].w == points[gl_InstanceID].w; }
	bool pathHasNext() { return gl_InstanceID < points.length() - 2 && points[gl_InstanceID + 2].w == points[gl_InstanceID + 1].w; }

	vec2 miterOffset(vec2 perp_in, vec2 perp_out, float side) {
		vec2 m = normalize(perp_in + perp_out);
		float scale = u_halfWidth / max(dot(m, perp_in), 1.0 / MAX_MITER);
		return m * scale * side;
	}

	// Returns sign vector s (s.x in [0,1]: segment start/end; s.y in {-1,+1}: sides).
	//
	// points[].w carries a subpath id (see PathDrawable::setPaths()). A single PathDrawable
	// can hold many independent, disconnected paths concatenated into one buffer - w lets
	// this function tell a real neighbor from an unrelated path's data without a separate
	// index buffer. setPoints()-authored single paths give every point the SAME w (whatever
	// value the caller supplied, e.g. 1.0), so every comparison below trivially matches and
	// this is a complete no-op unless setPaths() was actually used to build a multi-id buffer.
	//
	// perp (out): unit segment-perpendicular direction, exposed so PATH_SLUGGIT_MAIN can
	// derive its em<->world frame and analytic screen-space scale (PATH_EM_SCALE's
	// pathEmsPerPixel()) instead of trusting fwidth() on the emCoord varying.
	vec2 computeQuad(out vec2 base, out vec2 offset, out vec2 perp) {
		vec4 pt0 = points[gl_InstanceID ];
		vec4 pt1 = points[gl_InstanceID + 1];
		vec2 p0 = pt0.xy;
		vec2 p1 = pt1.xy;

		const vec2 signs[4] = vec2[4](
			vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
		);

		vec2 s = signs[gl_VertexID % 4];

		// This instance straddles two different subpaths - it isn't a real segment, it's the
		// gap between one path's last point and the next path's first. Degenerate to a
		// zero-area quad instead of drawing a spurious connecting segment.
		if(pathIsGap()) {
			base = p0;
			offset = vec2(0.0);
			perp = vec2(1.0, 0.0); // arbitrary - quad is zero-area, never visible

			return s;
		}

		vec2 dir = normalize(p1 - p0);
		perp = perpOf(dir);

		base = mix(p0, p1, s.x);

		if(s.x < 0.5) {
			if(pathHasPrev())
				offset = miterOffset(perpOf(normalize(p0 - points[gl_InstanceID - 1].xy)), perp, s.y);
			else
				offset = perp * (u_halfWidth * s.y);
		}
		else {
			if(pathHasNext())
				offset = miterOffset(perp, perpOf(normalize(points[gl_InstanceID + 2].xy - p1)), s.y);
			else
				offset = perp * (u_halfWidth * s.y);
		}

		return s;
	}
)GLSL";

// Miter mode: analytical fwidth SDF coverage via v_uv.
// Private pipeline (PATH_SDF_FRAG only) - not connected to Atlas::SHADER_FRAG. The vertex hook
// still runs: it is a vertex-stage contract, independent of which fragment stage is linked.
//
// Miter has no atlas shape and therefore no real em space, so the hook sees emCoord == uv. The
// frame it gets is the honest one anyway: axisX along the segment, axisY across its width, with
// each w carrying that direction's true world-units-per-uv-unit rate.
static const char* PATH_MITER_MAIN = R"GLSL(
	out vec2 v_uv;

	void main() {
		vec2 base, offset, perp;
		vec2 s = computeQuad(base, offset, perp);

		vec2 uv = vec2(s.x, (s.y + 1.0) * 0.5);
		vec2 dir = vec2(perp.y, -perp.x); // undo perpOf()
		float segLen = distance(points[gl_InstanceID].xy, points[gl_InstanceID + 1].xy);

		osgSlug_VertexResult r = osgSlug_Vertex(pathVertexData(
			vec3(base + offset, 0.0),
			uv,
			uv,
			u_origin,
			vec4(dir, 0.0, segLen),
			vec4(perp, 0.0, u_halfWidth * 2.0)
		));

		v_uv = s;
		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
	}
)GLSL";

// Analytic em-per-pixel rate, shared by Sluggit (its vertex-side margin push) and both Stamp
// variants (their fragment AA rate, replacing the shared fragment pipeline's default
// `emsPerPixel = fwidth(geom.emCoord)` via PATH_STAMP_FRAG_HOOK). osgSlug_viewport is the live viewport size, updated
// per-cull (see PathDrawableViewportCallback). Mirrors Atlas.shaders.cpp's
// osgSlug_WorldPerPixel() math; PathDrawable doesn't link that shader unit (no a_position/layers
// SSBO), so the small amount of math is reproduced here.
//
// The rate is the magnitude of each em coordinate's SCREEN-SPACE GRADIENT, from the full 2x2
// screen/world Jacobian - not the per-axis 1 / |screen image of one world unit along the axis|.
// The two agree only while the projected axes stay perpendicular on screen (ortho, any
// rotation). Under perspective the axes shear toward each other, the per-axis length overstates
// pixels-per-world, and the AA ramp comes out too sharp by 1 / sin(screen angle between the
// axes). fwidth() is a true screen gradient, so ShapeDrawable never had this problem.
static const std::string PATH_EM_SCALE = R"GLSL(
	uniform vec2 osgSlug_viewport;

	// Screen-pixel image of a 1-world-unit step along world-space direction `dir` from `pos`: the
	// directional derivative of NDC position (quotient rule through the perspective divide),
	// converted to pixels.
	vec2 pathPixelsPerWorld(vec2 pos, vec2 dir) {
		vec4 c = gl_ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
		vec4 d = gl_ModelViewProjectionMatrix * vec4(dir, 0.0, 0.0);
		float cw2 = max(c.w * c.w, 1e-12);

		return (d.xy * c.w - c.xy * d.w) / cw2 * osgSlug_viewport * 0.5;
	}

	// Em-per-pixel along each em axis at `pos`. axisX/axisY: xy = unit world direction, w =
	// worldPerEm along it (the osgSlug_VertexResult frame convention). With world coordinates
	// (a, b) along (axisX, axisY) and screen s = a * vx + b * vy, a = cross(s, vy) / cross(vx, vy),
	// so |grad a| = |vy| / |det| (and symmetrically for b).
	vec2 pathEmsPerPixel(vec2 pos, vec4 axisX, vec4 axisY) {
		vec2 vx = pathPixelsPerWorld(pos, axisX.xy);
		vec2 vy = pathPixelsPerWorld(pos, axisY.xy);
		float det = max(abs(vx.x * vy.y - vx.y * vy.x), 1e-8);

		return vec2(
			length(vy) / (det * max(axisX.w, 1e-8)),
			length(vx) / (det * max(axisY.w, 1e-8))
		);
	}
)GLSL";

// Sluggit
//
// The stroke's WIDTH maps onto the shape's em-Y extent; em-X carries only the end caps. A
// fragment's em-X is its distance from the nearest real cap (a subpath end with no neighbor
// segment), measured at the same em rate as the width and clamped at the shape's X midpoint: near
// a cap the SDF sees that edge of the shape, everywhere else - every interior joint included - it
// reads the shape's center column. A per-segment 0..1 em-X would instead put the shape's left and
// right edges at both ends of every segment, i.e. a ~0.5-coverage stripe at every joint.
//
// The two cap distances travel as separate non-flat varyings (pathCap) and are resolved in
// PATH_SLUGGIT_FRAG_HOOK: min() of two affine functions isn't affine, so it can't be interpolated
// as one value. Each distance is affine in world position, so it interpolates exactly across both
// triangles of the mitered (non-parallelogram) quad. em-Y is affine too: the corner's signed
// distance across this segment's centerline, so a miter corner clamped by MAX_MITER gets its true
// inset em-Y rather than the edge's.
//
// Same contract as SHADER_VERT/ShapeDrawable: the hook sees the TRUE corner and em values, and
// main() pushes the quad outward by PATH_MARGIN_PX afterwards, in the hook's returned frame,
// adding the exactly-matching em amounts - so the coverage ramp always has rasterized geometry to
// fall off into, and the stroke's width maps onto the shape's real em-Y extent with no padding.
// Side corners move along their miter offset rather than along perp: neighboring segments compute
// the same offset at a shared joint, so both quads' corner lands on the same point and the
// bisector split stays watertight. Cap corners additionally move along the segment direction.
//
// The analytic rate (PATH_EM_SCALE) sizes that vertex-side push; the fragment AA rate is per-pixel
// fwidth() of the em varyings. A per-instance (flat) analytic rate is wrong under perspective - it
// is exact only at the vertex it was evaluated at, and a long segment's screen scale changes along
// its length.
static const std::string PATH_SLUGGIT_MAIN = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandTransform; // x=bandScaleX, y=bandScaleY, z=bandOffsetX, w=bandOffsetY
	uniform vec4 u_shapeData; // x=bandTexX, y=bandTexY, z=bandMaxX, w=bandMaxY

	// Matches OSGSLUG_MARGIN_PX (Atlas.shaders.cpp), which PathDrawable doesn't link.
	const float PATH_MARGIN_PX = 1.5;

	// Cap distance for a segment end that has no cap: beyond any shape half-width in em units, so
	// the fragment hook's clamp resolves it to the shape's X midpoint.
	const float PATH_NO_CAP = 1.0e6;

	// Em-space distances from this segment's start/end cap along the segment (negative inside the
	// cap margin), or PATH_NO_CAP where that end joins a neighbor. Resolved into em-X by
	// PATH_SLUGGIT_FRAG_HOOK - see this shader's header comment.
	out osgSlug_PathCapBlock {
		float startDist;
		float endDist;
	} pathCap;

	void main() {
		vec2 base, offset, perp;
		vec2 s = computeQuad(base, offset, perp);

		float emMidX = (u_emCorners.x + u_emCorners.z) * 0.5;
		float emMidY = (u_emCorners.y + u_emCorners.w) * 0.5;

		// em-units per world-unit across the stroke's width - uniform-derived only, no
		// derivative involved. The cap distances use this same rate, so em-X and em-Y share it.
		float emPerWorld = (u_emCorners.w - u_emCorners.y) / max(2.0 * u_halfWidth, 1e-8);
		float worldPerEm = 1.0 / max(emPerWorld, 1e-8);
		vec2 dir = vec2(perp.y, -perp.x); // undo perpOf()
		vec2 uv = vec2(s.x, (s.y + 1.0) * 0.5);

		vec2 p0 = points[gl_InstanceID].xy;
		vec2 p1 = points[gl_InstanceID + 1].xy;
		bool startCap = !pathIsGap() && !pathHasPrev();
		bool endCap = !pathIsGap() && !pathHasNext();

		// True (unpushed) corner and em values. base lies on the centerline, so offset alone gives
		// the corner's signed distance across it.
		vec2 pos = base + offset;
		float emY = emMidY + dot(offset, perp) * emPerWorld;

		// emCoord.x is a placeholder (the shape's X midpoint): PATH_SLUGGIT_FRAG_HOOK replaces it
		// with the cap-derived value, so a hook's change to emCoord.x has no effect.
		osgSlug_VertexResult r = osgSlug_Vertex(pathVertexData(
			vec3(pos, 0.0),
			vec2(emMidX, emY),
			uv,
			u_origin,
			vec4(dir, 0.0, worldPerEm),
			vec4(perp, 0.0, worldPerEm)
		));

		// Margin push in world units along (axisX, axisY), applied in the hook's returned frame.
		// The subpath-gap quad has a zero offset and stays zero-area.
		vec2 push = vec2(0.0);

		if(dot(offset, offset) > 0.0) {
			// Side: scale the miter offset itself, so a joint shared with a neighbor segment moves
			// to the same point from both quads. offset's across-width component is u_halfWidth at
			// every unclamped corner, so this pushes PATH_MARGIN_PX across the edge; a clamped
			// miter corner is inset, and moves proportionally less.
			vec2 u = normalize(offset);
			vec3 uPost = r.axisX.xyz * dot(u, dir) + r.axisY.xyz * dot(u, perp);
			float worldPerPixel = 1.0 / max(length(pathPixelsPerWorld(r.pos.xy, uPost.xy)), 1e-8);

			push = vec2(dot(offset, dir), dot(offset, perp)) *
				(PATH_MARGIN_PX * worldPerPixel / max(u_halfWidth, 1e-8));

			// Cap: also push past the segment's end. With a worldPerEm of 1.0, pathEmsPerPixel().x
			// is world units along axisX per screen pixel measured across the cap edge, so the
			// margin stays PATH_MARGIN_PX wide on screen under perspective shear too.
			if(s.x < 0.5 ? startCap : endCap) {
				float capMargin = PATH_MARGIN_PX * pathEmsPerPixel(
					r.pos.xy,
					vec4(r.axisX.xy, 0.0, 1.0),
					vec4(r.axisY.xy, 0.0, 1.0)
				).x;

				push.x += s.x < 0.5 ? -capMargin : capMargin;
			}
		}

		float rateX = max(r.axisX.w, 1e-8);
		float rateY = max(r.axisY.w, 1e-8);
		float startDist = dot(pos - p0, dir) * emPerWorld + push.x / rateX;
		float endDist = dot(p1 - pos, dir) * emPerWorld - push.x / rateX;

		pathCap.startDist = startCap ? startDist : PATH_NO_CAP;
		pathCap.endDist = endCap ? endDist : PATH_NO_CAP;

		geom.emCoord = r.emCoord + vec2(0.0, push.y / rateY);
		geom.uv = uv;
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientTransform = vec4(0.0);
		fx.bandTransform = u_bandTransform;
		fx.shapeData = u_shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.sdfTile = -1;
		fx.effectParam = u_effectParam;

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos + r.axisX.xyz * push.x + r.axisY.xyz * push.y, 1.0);
	}
)GLSL";

// Sluggit-only fragment hook: resolves em-X from the two interpolated cap distances (pathCap) -
// the nearer cap's distance, offset from that cap's own shape edge (start = left, end = right)
// and clamped at the shape's X midpoint, so shapes that aren't left/right symmetric still cap
// correctly at both ends - and sets emsPerPixel.x to that distance's fwidth(), since the incoming
// fwidth(geom.emCoord).x is of the placeholder midpoint (zero). emsPerPixel.y is the incoming
// fwidth(geom.emCoord).y, unchanged. See PATH_SLUGGIT_MAIN's header comment.
//
// This is Sluggit's per-FLAVOR default for the FragmentHook slot (Atlas::ProgramSpec::fragHook),
// not a user hook: a caller's own FragmentHook still substitutes it, and in doing so loses cap
// coverage entirely (em-X stays at the placeholder midpoint). A Sluggit FragmentHook should define
// its own osgSlug_FragmentEmCoord the same way this one does.
//
// osgSlug_FragmentData used to be hand-copied here, because the #pragma below is expanded by
// Atlas.shaders.cpp's own resolver and this file couldn't reach it. Routing PathDrawable's
// program construction through Atlas::createProgram() closed that gap, so the struct now has
// exactly one definition again.
static const char* PATH_SLUGGIT_FRAG_HOOK = R"GLSL(
	#version 430 core

	#pragma osgSlug fragment_emcoord

	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1; the shape's true em extent

	in osgSlug_PathCapBlock {
		float startDist;
		float endDist;
	} pathCap;

	vec2 osgSlug_FragmentEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
		// Both derivatives are taken unconditionally (uniform control flow), then selected.
		float startRate = fwidth(pathCap.startDist);
		float endRate = fwidth(pathCap.endDist);
		bool fromStart = pathCap.startDist < pathCap.endDist;

		float halfSpan = (u_emCorners.z - u_emCorners.x) * 0.5;
		float emX = fromStart
			? u_emCorners.x + min(pathCap.startDist, halfSpan)
			: u_emCorners.z - min(pathCap.endDist, halfSpan);

		// A PATH_NO_CAP end is constant, so its rate is zero; em-X then sits at the midpoint and
		// its rate only needs to be finite - fall back to the width-direction rate.
		float rateX = fromStart ? startRate : endRate;

		emsPerPixel.x = (rateX > 0.0) ? rateX : emsPerPixel.y;

		return vec2(emX, emCoord.y);
	}

	vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
		return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
	}
)GLSL";

// Stamp mode: one instanced quad per point, centered at points[i].xy and rotated by points[i].z.
// Uses the full Slug SDF pipeline against a caller-defined shape (setShapeKey). emCoords vary
// fully over both axes; emsPerPixel comes from PATH_EM_SCALE's pathEmsPerPixel(), evaluated on
// the hook RESULT's post-hook frame - a hook that displaces or reorients the stamp changes its
// screen-space footprint (see DEPTH_DRIFT_HOOK in pyosgslug-particles.py).
static const char* PATH_STAMP_VERT = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandTransform;
	uniform vec4 u_shapeData;

	const vec2 CORNERS[4] = vec2[4](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
	);

	out osgSlug_PathStampEmScaleBlock {
		flat vec2 emsPerPixel;
	} pathStampEmScale;

	void main() {
		vec2 center = points[gl_InstanceID].xy;
		float angle = points[gl_InstanceID].z;
		vec2 q = CORNERS[gl_VertexID % 4];
		vec2 local = (q - 0.5) * (u_halfWidth * 2.0);
		float cosA = cos(angle);
		float sinA = sin(angle);
		vec2 pos = center + vec2(local.x * cosA - local.y * sinA, local.x * sinA + local.y * cosA);

		// The quad's em<->world frame, rotated with the stamp and carrying each axis's true rate:
		// the quad spans u_halfWidth * 2 world units across the shape's whole em extent.
		vec2 emSpan = max(abs(u_emCorners.zw - u_emCorners.xy), vec2(1e-8));
		vec2 rate = vec2(u_halfWidth * 2.0) / emSpan;

		osgSlug_VertexResult r = osgSlug_Vertex(pathVertexData(
			vec3(pos, 0.0),
			mix(u_emCorners.xy, u_emCorners.zw, q),
			q,
			u_origin,
			vec4(cosA, sinA, 0.0, rate.x),
			vec4(-sinA, cosA, 0.0, rate.y)
		));

		geom.emCoord = r.emCoord;
		geom.uv = q;
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientTransform = vec4(0.0);
		fx.bandTransform = u_bandTransform;
		fx.shapeData = u_shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.sdfTile = -1;
		fx.effectParam = u_effectParam;

		pathStampEmScale.emsPerPixel = pathEmsPerPixel(r.pos.xy, r.axisX, r.axisY);

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
	}
)GLSL";

// Stamp mode, multi-shape variant: like PATH_STAMP_VERT, but each instance looks its shape up in
// a ShapeTable SSBO instead of one fixed uniform triple - see PathDrawable::setShapeKeys().
// points[gl_InstanceID].w (unused by single-shape Stamp) is the 0-based index into that table.
// A separate shader from PATH_STAMP_VERT rather than a branch inside it: AtlasShapeData (the
// Atlas's own shared per-shape SSBO) has no em-bounds field, so per-instance shape variety needs
// its own table with a wider record (bandTransform/shapeData/originData/emCorners) that AtlasShapeData
// doesn't have room for.
static const char* PATH_STAMP_TABLE_VERT = R"GLSL(
	// points[].w is the 0-based index into this table; PATH_COMMON declares the points SSBO
	// itself.
	struct osgSlug_ShapeTableData {
		vec4 bandTransform;
		vec4 shapeData;
		vec4 originData;
		vec4 emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	};

	layout(std430, binding = @osgSlug::path.shapes@) readonly buffer ShapeTable {
		osgSlug_ShapeTableData shapes[];
	};

	const vec2 CORNERS[4] = vec2[4](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
	);

	out osgSlug_PathStampEmScaleBlock {
		flat vec2 emsPerPixel;
	} pathStampEmScale;

	void main() {
		vec4 pt = points[gl_InstanceID];
		osgSlug_ShapeTableData sd = shapes[int(pt.w + 0.5)];

		vec2 center = pt.xy;
		float angle = pt.z;
		vec2 q = CORNERS[gl_VertexID % 4];

		// One shared em->world scale for every shape in the table (u_halfWidth is "world units
		// per half an em"), applied to each corner's TRUE em coordinate - not normalized by
		// this glyph's own bounding box. Normalizing per-glyph (an earlier version of this code
		// did: emSize / max(longest(emSize), eps)) preserves aspect ratio but destroys relative
		// SIZE across glyphs - a period's tiny bbox would get scaled up to fill the exact same
		// box as a capital M's. Em-space bounds are already normalized to one shared font em
		// square (see slughorn::Atlas::Shape's comment), so a single scale factor is correct
		// here the same way it would be for ordinary baseline-aligned text.
		vec2 emCenter = (sd.emCorners.xy + sd.emCorners.zw) * 0.5; // this glyph's own bbox center
		vec2 emPos = mix(sd.emCorners.xy, sd.emCorners.zw, q);
		vec2 local = (emPos - emCenter) * (u_halfWidth * 2.0);

		float cosA = cos(angle);
		float sinA = sin(angle);
		vec2 pos = center + vec2(local.x * cosA - local.y * sinA, local.x * sinA + local.y * cosA);

		// One shared em->world rate for every shape in the table, by the same reasoning as the
		// scale above: u_halfWidth * 2 world units per em, rotated with the stamp.
		osgSlug_VertexResult r = osgSlug_Vertex(pathVertexData(
			vec3(pos, 0.0),
			emPos,
			q,
			sd.originData.xy,
			vec4(cosA, sinA, 0.0, u_halfWidth * 2.0),
			vec4(-sinA, cosA, 0.0, u_halfWidth * 2.0)
		));

		geom.emCoord = r.emCoord;
		geom.uv = q;
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientTransform = vec4(0.0);
		fx.bandTransform = sd.bandTransform;
		fx.shapeData = sd.shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.sdfTile = -1;
		fx.effectParam = u_effectParam;

		pathStampEmScale.emsPerPixel = pathEmsPerPixel(r.pos.xy, r.axisX, r.axisY);

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
	}
)GLSL";

// Stamp-only fragment hook: overrides osgSlug_FragmentEmCoord's emsPerPixel with the analytic,
// per-instance value PATH_STAMP_VERT/PATH_STAMP_TABLE_VERT computed (pathStampEmScale), instead
// of the fwidth(geom.emCoord) Atlas.shaders.cpp main() computes by default - see
// PATH_EM_SCALE's header comment. Mirrors PATH_SLUGGIT_FRAG_HOOK's emsPerPixel override;
// Stamp's emCoord needs no other adjustment.
//
// This is Stamp's per-FLAVOR default for the FragmentHook slot (Atlas::ProgramSpec::fragHook),
// not a user hook: a caller's own FragmentHook still substitutes it, and in doing so gives up the
// analytic emsPerPixel. A Stamp FragmentHook that cares about AA quality should define its own
// osgSlug_FragmentEmCoord the same way this one does.
static const char* PATH_STAMP_FRAG_HOOK = R"GLSL(
	#version 430 core

	#pragma osgSlug fragment_emcoord

	in osgSlug_PathStampEmScaleBlock {
		flat vec2 emsPerPixel;
	} pathStampEmScale;

	vec2 osgSlug_FragmentEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
		emsPerPixel = pathStampEmScale.emsPerPixel;

		return emCoord;
	}

	vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
		return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
	}
)GLSL";

// Analytical SDF fragment shader - Miter mode's private fragment stage. Not connected to
// Atlas::SHADER_FRAG, so Miter links no fragment hook units (Atlas::ProgramSpec::fragMain empty);
// its vertex hook still runs.
static const char* PATH_SDF_FRAG = R"GLSL(
	#version 430 core

	in vec2 v_uv;
	out vec4 fragColor;

	void main() {
		float d = abs(v_uv.y);
		float aa = fwidth(d);
		float alpha = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, d);
		fragColor = vec4(1.0, 1.0, 1.0, alpha);
	}
)GLSL";

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

// Stamp's em-space margin, baked into its quad bounds on the CPU (both axes). Sluggit pushes a
// pixel margin on the GPU instead and uses the shape's true em extent.
static constexpr slug_t PATH_EXPAND = 0.01_cv;

// Matches MAX_MITER in PATH_MITER_GEOM.
static constexpr slug_t PATH_MAX_MITER = 4_cv;

PathDrawable::PathDrawable(PathMode mode) : _mode(mode) {
}

void PathDrawable::setMode(PathMode mode) {
	_mode = mode;
	_compiled = false;

	if(getAtlas()) compile();
}

void PathDrawable::setHooks(Atlas::HookList hooks) {
	Drawable::setHooks(std::move(hooks));

	_compiled = false;

	if(getAtlas()) compile();
}

// Live uniform updates rather than a recompile: these feed a hook that is already linked, and
// changing an animation's speed or selector mid-flight shouldn't rebuild a Program.
void PathDrawable::setHalfWidth(slug_t w) {
	_halfWidth = w;

	if(auto* u = getStateSet() ? getStateSet()->getUniform("u_halfWidth") : nullptr; u) u->set(
		static_cast<float>(w)
	);

	dirtyBound();
}

void PathDrawable::setColor(const Vec4& c) {
	_color = c;

	if(auto* u = getStateSet() ? getStateSet()->getUniform("u_color") : nullptr; u) u->set(c);
}

void PathDrawable::setEffectId(int id) {
	_effectId = id;

	if(auto* u = getStateSet() ? getStateSet()->getUniform("u_effectId") : nullptr; u) u->set(id);
}

void PathDrawable::setEffectParam(slug_t param) {
	_effectParam = param;

	if(auto* u = getStateSet() ? getStateSet()->getUniform("u_effectParam") : nullptr; u) u->set(
		static_cast<float>(param)
	);
}

void PathDrawable::setShapeKeys(std::vector<slughorn::Key> keys) {
	_shapeKeys = std::move(keys);
	_compiled = false;

	if(getAtlas()) compile();
}

GLsizei PathDrawable::_instanceCount() const {
	if(!_points || _points->empty()) return 0;

	// Stamp uses one instance per point; all other modes use one per segment (N-1).
	const size_t available = _mode == PathMode::Stamp ? _points->size() : _points->size() - 1;

	return static_cast<GLsizei>(std::min(_revealCount, available));
}

void PathDrawable::setRevealCount(size_t n) {
	_revealCount = n;

	if(_drawArrays) _drawArrays->setNumInstances(_instanceCount());
}

void PathDrawable::setPoints(std::vector<Vec4> pts) {
	if(!_points) _points = new osgx::Vec4Array();

	const bool sizeChanged = pts.size() != _points->size();

	_points->assign(pts.begin(), pts.end());
	_points->dirty();

	dirtyBound();

	// _ssboBinding already exists once compile() has run (it owns the live GL buffer
	// object). Reuse it rather than rebuilding it: OSG's GLBufferObject already tracks
	// dirty state per-array and re-uploads only the changed byte range via
	// glBufferSubData on the next apply() - rebuilding the binding every call discarded
	// that buffer object wholesale and forced a full glBufferData reallocation on every
	// setPoints(), every frame. _size is only captured once at binding construction, so
	// it still needs an explicit refresh when the point count itself changes.
	if(_ssboBinding) {
		if(sizeChanged) _ssboBinding->setSize(_points->getTotalDataSize());

		if(_drawArrays) _drawArrays->setNumInstances(_instanceCount());
	}
}

void PathDrawable::setPaths(const std::vector<std::vector<Vec4>>& subpaths) {
	size_t total = 0;

	for(const auto& sub : subpaths) total += sub.size();

	std::vector<Vec4> combined;
	combined.reserve(total);

	for(size_t id = 0; id < subpaths.size(); id++) {
		for(const auto& p : subpaths[id]) {
			combined.emplace_back(p.x(), p.y(), p.z(), static_cast<float>(id));
		}
	}

	setPoints(std::move(combined));
}

void PathDrawable::compile() {
	if(_compiled) return;
	if(!_points || _points->size() < 2) return;

	auto* atlas = getAtlas();

	if((_mode == PathMode::Sluggit || _mode == PathMode::Stamp) && (!atlas || !atlas->isBuilt())) return;

	// (Re)build SSBO for point data.
	auto* ssbo = new osg::ShaderStorageBufferObject();
	_points->setBufferObject(ssbo);

	auto& slots = osgx::Library::instance().bindings();

	_ssboBinding = new osg::ShaderStorageBufferBinding(
		slots.get("osgSlug::path.points"), _points, 0, _points->getTotalDataSize()
	);

	_drawArrays = new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4, _instanceCount());

	removePrimitiveSet(0, getNumPrimitiveSets());
	addPrimitiveSet(_drawArrays);
	setInitialBound(computeBoundingBox());
	setUseVertexBufferObjects(true);

	// Built into the drawable's OWN StateSet rather than a fresh one, so anything a caller
	// attached (a custom uniform, a render bin) survives compile(). Shader hooks are the one
	// thing that must NOT come in that way - compile() picks the program, so hooks arrive via
	// setHooks() and are linked below. Recompiles (setMode/setShapeKeys) drop the previous mode's
	// program and shape table explicitly; every other key this function writes is simply
	// overwritten in place.
	auto* ss = getOrCreateStateSet();

	ss->removeAttribute(osg::StateAttribute::PROGRAM);

	if(_shapeTableBinding) {
		ss->removeAttribute(_shapeTableBinding);

		_shapeTableBinding = nullptr;
	}

	osg::Program* prog = nullptr;

	// Stamp is the only mode setShapeKeys() applies to; Sluggit always uses the single-shape
	// (setShapeKey()) path below.
	const bool multiShape = _mode == PathMode::Stamp && !_shapeKeys.empty();

	setCullCallback(nullptr);

	if(_mode == PathMode::Stamp || _mode == PathMode::Sluggit) {
		if(!atlas->getCurveTexture()) {
			OSG_WARN << "PathDrawable: curve texture is null - was atlas->packTextures() called?\n";
			return;
		}

		if(_mode == PathMode::Stamp) {
			// PATH_STAMP_FRAG_HOOK is this mode's DEFAULT for the FragmentHook slot, not a user
			// hook - a caller's own FragmentHook still wins. See the string's own comment.
			prog = Atlas::createProgram(
				{
					.vertMain = PATH_COMMON + PATH_EM_SCALE +
						(multiShape ? PATH_STAMP_TABLE_VERT : PATH_STAMP_VERT),
					.fragMain = Atlas::SHADER_FRAG,
					.fragHook = PATH_STAMP_FRAG_HOOK
				},
				_hooks
			);
		}

		else {
			// PATH_SLUGGIT_FRAG_HOOK is this mode's DEFAULT for the FragmentHook slot, not a user
			// hook - a caller's own FragmentHook still wins. See the string's own comment.
			prog = Atlas::createProgram(
				{
					.vertMain = PATH_COMMON + PATH_EM_SCALE + PATH_MITER_GEOM + PATH_SLUGGIT_MAIN,
					.fragMain = Atlas::SHADER_FRAG,
					.fragHook = PATH_SLUGGIT_FRAG_HOOK
				},
				_hooks
			);
		}

		// Live viewport size for pathEmsPerPixel()'s analytic scale (see PATH_EM_SCALE) - kept in
		// sync per-cull the same way
		// Atlas::ViewportUniformCallback does for the shared osgSlug_viewport uniform (Atlas.cpp),
		// since PathDrawable's StateSet is standalone and doesn't inherit an Atlas ancestor's copy.
		// Both Stamp and Sluggit need this now; Miter doesn't reach this branch at all.
		ss->addUniform(new osg::Uniform("osgSlug_viewport", osg::Vec2(1280.0f, 720.0f)));
		setCullCallback(new PathDrawableViewportCallback());

		// SHADER_FRAG declares osgSlug_MaskBlock at the "osgSlug::mask" slot, but this
		// StateSet can be entirely standalone (PathDrawable also supports setAtlas() rather than
		// atlas->addChild() - see Drawable::getAtlas()'s comment), so unlike ShapeDrawable it may
		// NOT inherit the null-mask UBO binding from an Atlas ancestor's StateSet. Bind it
		// explicitly or osgSlug_FragmentMask() reads through an unbound osgSlug_MaskBlock, which
		// is undefined behavior the moment this shader links against SHADER_FRAG.
		ss->setAttributeAndModes(atlas->getNullMask()->getBinding(), osg::StateAttribute::ON);

		ss->setTextureAttributeAndModes(
			slots.get("osgSlug::atlas.curve"),
			atlas->getCurveTexture(),
			osg::StateAttribute::ON
		);

		ss->setTextureAttributeAndModes(
			slots.get("osgSlug::atlas.band"),
			atlas->getBandTexture(),
			osg::StateAttribute::ON
		);

		ss->addUniform(new osg::Uniform("osgSlug_texWidth",
			static_cast<int>(std::countr_zero(atlas->getTextureWidth()))
		));
		addSamplerUniforms(ss);
		ss->addUniform(new osg::Uniform("osgSlug_gradientCount", 0));

		// Deliberately NOT setting a local osgSlug_debugMode uniform here (unlike every uniform
		// around it): DebugModeHandler (pyosgslug_example.py) sets this on the camera's own
		// StateSet, and a local copy here - even a "just a default" 0 - shadows that ancestor
		// value per ordinary OSG StateSet inheritance, permanently disabling F1-F7 debug modes
		// for anything drawn through PathDrawable. Atlas::createDefaultStateSet() only adds its
		// own copy conditionally (gated on the DEBUG env var) for exactly this reason - mirror
		// that by adding none at all rather than an unconditional dead 0.
		ss->addUniform(new osg::Uniform("osgSlug_textMode", false));
		ss->addUniform(new osg::Uniform("osgSlug_stemDarken", false));
		ss->addUniform(new osg::Uniform("osgSlug_gamma", 1.0f));
		ss->addUniform(new osg::Uniform("osgSlug_layerMask", 0));
		ss->addUniform(new osg::Uniform("u_color", _color));

		if(multiShape) {
			// One osgSlug_ShapeTableData record (4 vec4s - see PATH_STAMP_TABLE_VERT) per key,
			// looked up once here rather than per-instance on the GPU: atlas->getShape() is a
			// CPU-side map lookup, not something worth re-doing every vertex shader invocation.
			std::vector<Vec4> table;
			table.reserve(_shapeKeys.size() * 4);

			for(const auto& key : _shapeKeys) {
				const auto shape = atlas->getShape(key);

				if(!shape) {
					OSG_WARN << "PathDrawable: Stamp mode shape key not found in atlas - "
						"call setShapeKeys() with valid keys before compile()\n";
					return;
				}

				table.emplace_back(shape->bandScaleX, shape->bandScaleY, shape->bandOffsetX, shape->bandOffsetY);
				table.emplace_back(cv(shape->bandTexX), cv(shape->bandTexY), cv(shape->bandMaxX), cv(shape->bandMaxY));
				table.emplace_back(shape->originX, shape->originY, 0_cv, 0_cv);

				// PATH_EXPAND pushes every edge OUTWARD by the same amount on both axes. X is a
				// real rendered extent for Stamp, so pushing it inward would shave PATH_EXPAND off
				// the shape's own left/right edges (clipped corners on wide/flat glyphs, e.g. the
				// feet of a capital A).
				table.emplace_back(
					shape->bearingX - PATH_EXPAND,
					(shape->bearingY - shape->height) - PATH_EXPAND,
					(shape->bearingX + shape->width) + PATH_EXPAND,
					shape->bearingY + PATH_EXPAND
				);
			}

			auto* shapeTable = new osgx::Vec4Array();
			shapeTable->assign(table.begin(), table.end());

			auto* shapeTableSSBO = new osg::ShaderStorageBufferObject();
			shapeTable->setBufferObject(shapeTableSSBO);

			_shapeTableBinding = new osg::ShaderStorageBufferBinding(
				slots.get("osgSlug::path.shapes"), shapeTable, 0, shapeTable->getTotalDataSize()
			);

			ss->setAttributeAndModes(_shapeTableBinding, osg::StateAttribute::ON);
		}

		else {
			const auto shape = atlas->getShape(_shapeKey);

			if(!shape) {
				OSG_WARN << "PathDrawable: " << (_mode == PathMode::Stamp ? "Stamp" : "Sluggit")
					<< " mode requires a valid shape key - call setShapeKey() before compile()\n";
				return;
			}

			// Stamp: see the multiShape branch above for why this is a symmetric outward push on
			// both axes.
			const slug_t expand = _mode == PathMode::Stamp ? PATH_EXPAND : 0_cv;
			const slug_t emX0 = shape->bearingX - expand;
			const slug_t emY0 = (shape->bearingY - shape->height) - expand;
			const slug_t emX1 = (shape->bearingX + shape->width) + expand;
			const slug_t emY1 = shape->bearingY + expand;

			// The hook contract's data.origin. Uploaded per-shape in the multiShape branch above
			// (osgSlug_ShapeTableData.originData), which is why that branch doesn't set this.
			ss->addUniform(new osg::Uniform("u_origin", osg::Vec2(
				static_cast<float>(shape->originX),
				static_cast<float>(shape->originY)
			)));
			ss->addUniform(new osg::Uniform("u_emCorners", Vec4(emX0, emY0, emX1, emY1)));
			ss->addUniform(new osg::Uniform("u_bandTransform", Vec4(
				shape->bandScaleX, shape->bandScaleY,
				shape->bandOffsetX, shape->bandOffsetY
			)));
			ss->addUniform(new osg::Uniform("u_shapeData", Vec4(
				cv(shape->bandTexX), cv(shape->bandTexY),
				cv(shape->bandMaxX), cv(shape->bandMaxY)
			)));
		}
	}

	else { // Miter - analytical fwidth SDF, no atlas
		// An empty fragMain means createProgram() links NO osgSlug fragment stack, leaving the
		// private one below as this Program's only fragment unit. The vertex hook still applies.
		prog = Atlas::createProgram({.vertMain = PATH_COMMON + PATH_MITER_GEOM + PATH_MITER_MAIN}, _hooks);

		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, PATH_SDF_FRAG));

		ss->addUniform(new osg::Uniform("u_color", _color));
		ss->addUniform(new osg::Uniform("u_origin", osg::Vec2(0.0f, 0.0f))); // Miter has no shape
	}

	ss->addUniform(new osg::Uniform("u_halfWidth", _halfWidth));
	ss->addUniform(new osg::Uniform("u_effectId", _effectId));
	ss->addUniform(new osg::Uniform("u_effectParam", _effectParam));
	ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
	ss->setAttributeAndModes(_ssboBinding, osg::StateAttribute::ON);
	// Matches Atlas::createDefaultStateSet() (ShapeDrawable's setup): the shared fragment main()
	// (Atlas.shaders.cpp) unconditionally premultiplies osgSlug_Fragment's straight-alpha return
	// value (color.rgb *= color.a) before every draw call, PathDrawable included - GL_SRC_ALPHA
	// here double-multiplied that already-premultiplied color by alpha again, darkening every
	// partial-coverage AA pixel (alpha^2 instead of alpha). Invisible blending toward a plain
	// background, but a visible dark seam wherever that AA ramp overlapped already-opaque content.
	ss->setAttributeAndModes(new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, false));

	// Matches Atlas::createDefaultStateSet() (ShapeDrawable's setup): depth TEST off (not just
	// depth WRITE, set above) and the transparent bin, not the default opaque one. PathDrawable's
	// StateSet is standalone and never went through createDefaultStateSet(), so it never picked
	// either of these up - no confirmed link to the overlap-seam artifact yet, but this closes a
	// real, confirmed gap from the known-correct setup regardless.
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	_compiled = true;
}

void PathDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	// osg::DrawArrays treats 0 instances as a plain non-instanced draw (gl_InstanceID 0, so the
	// first segment), not as nothing.
	if(_drawArrays && _drawArrays->getNumInstances() == 0) return;

	osg::Geometry::drawImplementation(renderInfo);
}

osg::BoundingBox PathDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	if(!_points || _points->empty()) return bb;

	for(const auto& p : *_points) bb.expandBy(Vec3(p.x(), p.y(), 0.0f));

	// A miter corner reaches up to PATH_MAX_MITER half-widths from its point. The GPU's pixel
	// margin has no CPU-side size; that padding covers it everywhere but the sharpest joints.
	const slug_t pad = _mode == PathMode::Stamp ? _halfWidth : _halfWidth * PATH_MAX_MITER;

	bb.expandBy(Vec3(bb.xMin() - pad, bb.yMin() - pad, -0.1f));
	bb.expandBy(Vec3(bb.xMax() + pad, bb.yMax() + pad, 0.1f));

	return bb;
}

}
