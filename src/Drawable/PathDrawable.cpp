#include "osgSlug/Drawable/PathDrawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <osg/Depth>
#include <osgUtil/CullVisitor>

OSGSLUG_ENABLE_WARNINGS

#include "slughorn/canvas.hpp"

#include <bit>

namespace osgSlug {

namespace {

// Per-cull update of PATH_SLUGGIT_MAIN's osgSlug_viewport uniform -- same pattern as
// Atlas.cpp's ViewportUniformCallback, duplicated here since PathDrawable's StateSet is
// standalone (setAtlas(), not atlas->addChild() -- see Drawable::getAtlas()'s comment) and
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
//   through osgSlug_VertexData -- ShapeDrawable's non-instanced draws just read 0 there.
// - osgSlug_Vertex_Rotate/_Scale reconstruct their pivot assuming one em maps to one world unit,
//   which is not true for Stamp mode (its rate is u_halfWidth * 2). Rotate about
//   points[gl_InstanceID].xy by hand instead. See SHADER_LIB_VERTEX_IMPL's note.
static const std::string PATH_COMMON = R"GLSL(
	#version 430 core

	#pragma osgSlug lib_vertex,lib_vertex_impl

	uniform float u_halfWidth;
	uniform vec4 u_color;
	uniform int u_effectId; // PathDrawable::setEffectId() -- the hook's selector, as on a layer
	uniform float u_effectParam; // PathDrawable::setEffectParam() -- the hook's float knob
	uniform vec2 u_origin; // shape origin in em-space (Sluggit/Stamp); zero for Miter
	uniform float osg_SimulationTime;

	layout(std430, binding = 0) buffer PathData {
		vec4 points[];
	};

	// Defined by the linked hook or noop unit -- see Atlas::createProgram().
	osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data);

	// Fills the per-corner hook input. Everything a PathDrawable corner genuinely varies
	// (pos/emCoord/uv, its em<->world frame, and -- for the multi-shape Stamp table -- the shape
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
		d.bleed = 0.0; // PathDrawable bakes its margin on the CPU (PATH_EXPAND), no GPU push
		d.axisX = axisX;
		d.axisY = axisY;

		return d;
	}
)GLSL";

// Mitered per-segment quad geometry; used by Miter and Sluggit. Append PATH_MITER_MAIN or
// PATH_SLUGGIT_MAIN to PATH_COMMON + this to form a complete shader.
static const std::string PATH_MITER_GEOM = R"GLSL(
	uniform int u_N;

	const float MAX_MITER = 4.0;

	vec2 perpOf(vec2 dir) { return vec2(-dir.y, dir.x); }

	vec2 miterOffset(vec2 perp_in, vec2 perp_out, float side) {
		vec2 m = normalize(perp_in + perp_out);
		float scale = u_halfWidth / max(dot(m, perp_in), 1.0 / MAX_MITER);
		return m * scale * side;
	}

	// Returns sign vector s (s.x in [0,1]: segment start/end; s.y in {-1,+1}: sides).
	//
	// points[].w carries a subpath id (see PathDrawable::setPaths()). A single PathDrawable
	// can hold many independent, disconnected paths concatenated into one buffer -- w lets
	// this function tell a real neighbor from an unrelated path's data without a separate
	// index buffer. setPoints()-authored single paths give every point the SAME w (whatever
	// value the caller supplied, e.g. 1.0), so every comparison below trivially matches and
	// this is a complete no-op unless setPaths() was actually used to build a multi-id buffer.
	//
	// perp (out): unit segment-perpendicular direction, exposed so PATH_SLUGGIT_MAIN can
	// derive an analytic screen-space scale in the width direction (see its
	// pathSluggitEmsPerPixel()) instead of trusting fwidth() on the pinned emCoord varying.
	vec2 computeQuad(out vec2 base, out vec2 offset, out vec2 perp) {
		vec4 pt0 = points[gl_InstanceID ];
		vec4 pt1 = points[gl_InstanceID + 1];
		vec2 p0 = pt0.xy;
		vec2 p1 = pt1.xy;

		const vec2 signs[4] = vec2[4](
			vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
		);

		vec2 s = signs[gl_VertexID % 4];

		// This instance straddles two different subpaths -- it isn't a real segment, it's the
		// gap between one path's last point and the next path's first. Degenerate to a
		// zero-area quad instead of drawing a spurious connecting segment.
		if(pt0.w != pt1.w) {
			base = p0;
			offset = vec2(0.0);
			perp = vec2(1.0, 0.0); // arbitrary -- quad is zero-area, never visible

			return s;
		}

		vec2 dir = normalize(p1 - p0);
		perp = perpOf(dir);

		base = mix(p0, p1, s.x);

		if(s.x < 0.5) {
			if(gl_InstanceID > 0 && points[gl_InstanceID - 1].w == pt0.w)
				offset = miterOffset(perpOf(normalize(p0 - points[gl_InstanceID - 1].xy)), perp, s.y);
			else
				offset = perp * (u_halfWidth * s.y);
		}
		else {
			if(gl_InstanceID < u_N - 2 && points[gl_InstanceID + 2].w == pt1.w)
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

// Sluggit
//
// em-X is pinned to the shape midpoint so fwidth(emCoord.x) stays near zero. Without this the
// 0.99->0.01 discontinuity at quad boundaries causes the 2x2 derivative group to compute
// fwidth ~ 0.98, giving pixelsPerEm.x ~ 1 and dropping coverage to ~0.48 (dark stripe).
//
// Separately, emCoord.y's fwidth() (computed unconditionally by Atlas.shaders.cpp main()) is
// the AA stair-stepping bug: road-segment quads are small and often screen-rotated, so the
// GPU's 2x2-pixel derivative block frequently straddles a triangle/instance boundary where the
// interpolation basis differs on each side -- a well-known fwidth() unreliability on
// thin/rotated primitives. pathSluggitEmsPerPixel() below computes the width-direction rate
// analytically instead (stroke half-width vs. the MVP's screen-space projection scale), and
// PATH_SLUGGIT_FRAG_HOOK's osgSlug_FragEmCoord() overrides emsPerPixel with it -- bypassing the
// unreliable derivative without touching the shared default fragment path every other
// Slug-rendered shape (text, general shapes, decals) depends on.
static const std::string PATH_SLUGGIT_MAIN = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform; // x=bandScaleX, y=bandScaleY, z=bandOffsetX, w=bandOffsetY
	uniform vec4 u_shapeData; // x=bandTexX, y=bandTexY, z=bandMaxX, w=bandMaxY
	uniform vec2 osgSlug_viewport; // live viewport size (see PathDrawable::compile()'s cull callback)

	// Analytic em-units-per-screen-pixel along `perp`, evaluated at `pos`. Mirrors
	// Atlas.shaders.cpp's osgSlug_WorldPerPixel()/osgSlug_viewport machinery (same math), scaled
	// by the shape's own em<->world rate -- PathDrawable's Sluggit shaders don't link that shader
	// unit (no a_position/layers SSBO), so the small amount of math is reproduced here rather
	// than pulled in via a much larger, incompatible shared vertex main().
	float pathSluggitEmsPerPixel(vec2 pos, vec2 perp, float emPerWorld) {
		vec4 c = gl_ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
		vec4 d = gl_ModelViewProjectionMatrix * vec4(perp, 0.0, 0.0);
		float cw2 = max(c.w * c.w, 1e-12);
		vec2 ndcPerWorld = (d.xy * c.w - c.xy * d.w) / cw2;
		float pixelsPerWorld = length(ndcPerWorld * osgSlug_viewport * 0.5);
		float worldPerPixel = 1.0 / max(pixelsPerWorld, 1e-8);

		return worldPerPixel * emPerWorld;
	}

	out osgSlug_PathEmScaleBlock {
		flat float emsPerPixel;
	} pathEmScale;

	void main() {
		vec2 base, offset, perp;
		vec2 s = computeQuad(base, offset, perp);

		float emMidX = (u_emCorners.x + u_emCorners.z) * 0.5;
		float emY = (s.y < 0.0) ? u_emCorners.y : u_emCorners.w;

		// em-units per world-unit across the stroke's width -- uniform-derived only, no
		// derivative involved.
		float emPerWorld = (u_emCorners.w - u_emCorners.y) / max(2.0 * u_halfWidth, 1e-8);
		float worldPerEm = 1.0 / max(emPerWorld, 1e-8);
		vec2 dir = vec2(perp.y, -perp.x); // undo perpOf()
		vec2 uv = vec2(s.x, (s.y + 1.0) * 0.5);

		osgSlug_VertexResult r = osgSlug_Vertex(pathVertexData(
			vec3(base + offset, 0.0),
			vec2(emMidX, emY),
			uv,
			u_origin,
			// em-X is pinned constant by design (see this shader's header comment), so it has no
			// rate of its own; reuse the width-direction rate rather than invent a second one.
			vec4(dir, 0.0, worldPerEm),
			vec4(perp, 0.0, worldPerEm)
		));

		geom.emCoord = r.emCoord;
		geom.uv = uv;
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientXform = vec4(0.0);
		fx.bandXform = u_bandXform;
		fx.shapeData = u_shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
		fx.effectParam = u_effectParam;

		// Evaluated at the POST-hook position and frame: a hook that displaces or reorients the
		// stroke changes its screen-space footprint, and this rate is what the fragment stage
		// antialiases with. Using the pre-hook values would silently mis-tune the AA under any
		// hook that actually moves something.
		pathEmScale.emsPerPixel = pathSluggitEmsPerPixel(
			r.pos.xy,
			r.axisY.xy,
			1.0 / max(r.axisY.w, 1e-8)
		);

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
	}
)GLSL";

// Sluggit-only fragment hook: overrides osgSlug_FragEmCoord's emsPerPixel with the analytic
// value PATH_SLUGGIT_MAIN computed (pathEmScale), instead of the unreliable
// fwidth(geom.emCoord) Atlas.shaders.cpp main() computes by default -- see PATH_SLUGGIT_MAIN's
// header comment. Isotropic: emCoord.x is pinned constant by design (a different, earlier fix),
// so it carries no real per-pixel rate of its own -- reusing the analytic width-direction value
// keeps pixelsPerEm.x finite/stable for slug_Render without introducing a second, unrelated
// derivative source.
//
// This is Sluggit's per-FLAVOR default for the FragmentHook slot (Atlas::ProgramSpec::fragHook),
// not a user hook: a caller's own FragmentHook still substitutes it, and in doing so gives up the
// analytic emsPerPixel. A Sluggit FragmentHook that cares about AA quality should define its own
// osgSlug_FragEmCoord the same way this one does.
//
// osgSlug_FragmentData used to be hand-copied here, because the #pragma below is expanded by
// Atlas.shaders.cpp's own resolver and this file couldn't reach it. Routing PathDrawable's
// program construction through Atlas::createProgram() closed that gap, so the struct now has
// exactly one definition again.
static const char* PATH_SLUGGIT_FRAG_HOOK = R"GLSL(
	#version 430 core

	#pragma osgSlug lib_fragment

	in osgSlug_PathEmScaleBlock {
		flat float emsPerPixel;
	} pathEmScale;

	vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
		emsPerPixel = vec2(pathEmScale.emsPerPixel);

		return emCoord;
	}

	vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
		return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
	}
)GLSL";

// Stamp mode: one instanced quad per point, centered at points[i].xy and rotated by points[i].z.
// Uses the full Slug SDF pipeline against a caller-defined shape (setShapeKey).
// emCoords vary fully over both axes - unlike Sluggit, which pins emX to the midpoint.
static const char* PATH_STAMP_VERT = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform;
	uniform vec4 u_shapeData;

	const vec2 CORNERS[4] = vec2[4](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
	);

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
		geom.gradientXform = vec4(0.0);
		fx.bandXform = u_bandXform;
		fx.shapeData = u_shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
		fx.effectParam = u_effectParam;

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
	}
)GLSL";

// Stamp mode, multi-shape variant: like PATH_STAMP_VERT, but each instance looks its shape up in
// a ShapeTable SSBO instead of one fixed uniform triple -- see PathDrawable::setShapeKeys().
// points[gl_InstanceID].w (unused by single-shape Stamp) is the 0-based index into that table.
// A separate shader from PATH_STAMP_VERT rather than a branch inside it: AtlasShapeData (the
// Atlas's own shared per-shape SSBO) has no em-bounds field, so per-instance shape variety needs
// its own table with a wider record (bandXform/shapeData/originData/emCorners) that AtlasShapeData
// doesn't have room for -- see ai/context-todo-pathdrawable.md, "Future: Extract StampDrawable +
// user-configurable SSBO bindings". Binding numbers below must match
// PATH_POINTS_SSBO_BINDING/PATH_SHAPE_TABLE_SSBO_BINDING (PathDrawable.hpp).
static const char* PATH_STAMP_TABLE_VERT = R"GLSL(
	// points[].w is the 0-based index into this table; PATH_COMMON declares the points SSBO
	// itself (binding 0).
	struct osgSlug_ShapeTableData {
		vec4 bandXform;
		vec4 shapeData;
		vec4 originData;
		vec4 emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	};

	layout(std430, binding = 1) readonly buffer ShapeTable {
		osgSlug_ShapeTableData shapes[];
	};

	const vec2 CORNERS[4] = vec2[4](
		vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
	);

	void main() {
		vec4 pt = points[gl_InstanceID];
		osgSlug_ShapeTableData sd = shapes[int(pt.w + 0.5)];

		vec2 center = pt.xy;
		float angle = pt.z;
		vec2 q = CORNERS[gl_VertexID % 4];

		// One shared em->world scale for every shape in the table (u_halfWidth is "world units
		// per half an em"), applied to each corner's TRUE em coordinate -- not normalized by
		// this glyph's own bounding box. Normalizing per-glyph (an earlier version of this code
		// did: emSize / max(longest(emSize), eps)) preserves aspect ratio but destroys relative
		// SIZE across glyphs -- a period's tiny bbox would get scaled up to fill the exact same
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
		geom.gradientXform = vec4(0.0);
		fx.bandXform = sd.bandXform;
		fx.shapeData = sd.shapeData;
		fx.effectId = u_effectId;
		fx.gradientId = 0;
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
		fx.effectParam = u_effectParam;

		gl_Position = gl_ModelViewProjectionMatrix * vec4(r.pos, 1.0);
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

static constexpr slug_t PATH_EXPAND = 0.01_cv;

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

void PathDrawable::setRevealCount(size_t n) {
	_revealCount = n;

	if(_drawArrays) _drawArrays->setNumInstances(static_cast<GLsizei>(n));
}

void PathDrawable::setPoints(std::vector<Vec4> pts) {
	if(!_points) _points = new osgx::Vec4Array();

	const bool sizeChanged = pts.size() != _points->size();

	_points->assign(pts.begin(), pts.end());
	_points->dirty();

	// _ssboBinding already exists once compile() has run (it owns the live GL buffer
	// object). Reuse it rather than rebuilding it: OSG's GLBufferObject already tracks
	// dirty state per-array and re-uploads only the changed byte range via
	// glBufferSubData on the next apply() - rebuilding the binding every call discarded
	// that buffer object wholesale and forced a full glBufferData reallocation on every
	// setPoints(), every frame. _size is only captured once at binding construction, so
	// it still needs an explicit refresh when the point count itself changes.
	if(_ssboBinding) {
		if(sizeChanged) _ssboBinding->setSize(_points->getTotalDataSize());

		if(_drawArrays) _drawArrays->setNumInstances(
			static_cast<GLsizei>(_mode == PathMode::Stamp ? _points->size() : _points->size() - 1)
		);
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

	const size_t N = _points->size();

	// (Re)build SSBO for point data.
	auto* ssbo = new osg::ShaderStorageBufferObject();
	_points->setBufferObject(ssbo);

	_ssboBinding = new osg::ShaderStorageBufferBinding(
		PATH_POINTS_SSBO_BINDING, _points, 0, _points->getTotalDataSize()
	);

	// Stamp uses one instance per point; all other modes use one per segment (N-1).
	_drawArrays = new osg::DrawArrays(
		GL_TRIANGLE_FAN, 0, 4,
		static_cast<GLsizei>(_revealCount)
	);

	removePrimitiveSet(0, getNumPrimitiveSets());
	addPrimitiveSet(_drawArrays);
	setInitialBound(computeBoundingBox());
	setUseVertexBufferObjects(true);

	// Built into the drawable's OWN StateSet rather than a fresh one, so anything a caller
	// attached (a custom uniform, a render bin) survives compile(). Shader hooks are the one
	// thing that must NOT come in that way -- compile() picks the program, so hooks arrive via
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
			prog = Atlas::createProgram(
				{
					.vertMain = PATH_COMMON + (multiShape ? PATH_STAMP_TABLE_VERT : PATH_STAMP_VERT),
					.fragMain = Atlas::SHADER_FRAG
				},
				_hooks
			);
		}

		else {
			// PATH_SLUGGIT_FRAG_HOOK is this mode's DEFAULT for the FragmentHook slot, not a user
			// hook -- a caller's own FragmentHook still wins. See the string's own comment.
			prog = Atlas::createProgram(
				{
					.vertMain = PATH_COMMON + PATH_MITER_GEOM + PATH_SLUGGIT_MAIN,
					.fragMain = Atlas::SHADER_FRAG,
					.fragHook = PATH_SLUGGIT_FRAG_HOOK
				},
				_hooks
			);

			ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));

			// Live viewport size for pathSluggitEmsPerPixel()'s analytic scale (see
			// PATH_SLUGGIT_MAIN) -- kept in sync per-cull the same way Atlas::ViewportUniformCallback
			// does for the shared osgSlug_viewport uniform (Atlas.cpp), since PathDrawable's
			// StateSet is standalone and doesn't inherit an Atlas ancestor's copy.
			ss->addUniform(new osg::Uniform("osgSlug_viewport", osg::Vec2(1280.0f, 720.0f)));
			setCullCallback(new PathDrawableViewportCallback());
		}

		// createProgram() already bound osgSlug_MaskBlock to RENDER_MASK_UBO_BINDING, but this
		// StateSet can be entirely standalone (PathDrawable also supports setAtlas() rather than
		// atlas->addChild() -- see Drawable::getAtlas()'s comment), so unlike ShapeDrawable it may
		// NOT inherit the null-mask UBO binding from an Atlas ancestor's StateSet. Bind it
		// explicitly or osgSlug_FragmentMask() reads through an unbound osgSlug_MaskBlock, which
		// is undefined behavior the moment this shader links against SHADER_FRAG.
		ss->setAttributeAndModes(atlas->getNullMask()->getBinding(), osg::StateAttribute::ON);

		ss->setTextureAttributeAndModes(0, atlas->getCurveTexture(), osg::StateAttribute::ON);
		ss->setTextureAttributeAndModes(1, atlas->getBandTexture(), osg::StateAttribute::ON);

		ss->addUniform(new osg::Uniform("osgSlug_texWidth",
			static_cast<int>(std::countr_zero(atlas->getTextureWidth()))
		));
		ss->addUniform(new osg::Uniform("osgSlug_curveTexture", 0));
		ss->addUniform(new osg::Uniform("osgSlug_bandTexture", 1));
		ss->addUniform(new osg::Uniform("osgSlug_effectTexture", 2));
		ss->addUniform(new osg::Uniform("osgSlug_gradientTexture", 3));
		ss->addUniform(new osg::Uniform("osgSlug_gradientCount", 0));
		ss->addUniform(new osg::Uniform("osgSlug_debugMode", 0));
		ss->addUniform(new osg::Uniform("osgSlug_textMode", false));
		ss->addUniform(new osg::Uniform("osgSlug_stemDarken", false));
		ss->addUniform(new osg::Uniform("osgSlug_gamma", 1.0f));
		ss->addUniform(new osg::Uniform("osgSlug_layerMask", 0));
		ss->addUniform(new osg::Uniform("u_color", _color));

		if(multiShape) {
			// One osgSlug_ShapeTableData record (4 vec4s -- see PATH_STAMP_TABLE_VERT) per key,
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
				table.emplace_back(
					shape->bearingX + PATH_EXPAND,
					(shape->bearingY - shape->height) - PATH_EXPAND,
					(shape->bearingX + shape->width) - PATH_EXPAND,
					shape->bearingY + PATH_EXPAND
				);
			}

			auto* shapeTable = new osgx::Vec4Array();
			shapeTable->assign(table.begin(), table.end());

			auto* shapeTableSSBO = new osg::ShaderStorageBufferObject();
			shapeTable->setBufferObject(shapeTableSSBO);

			_shapeTableBinding = new osg::ShaderStorageBufferBinding(
				PATH_SHAPE_TABLE_SSBO_BINDING, shapeTable, 0, shapeTable->getTotalDataSize()
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

			const slug_t emX0 = shape->bearingX + PATH_EXPAND;
			const slug_t emY0 = (shape->bearingY - shape->height) - PATH_EXPAND;
			const slug_t emX1 = (shape->bearingX + shape->width) - PATH_EXPAND;
			const slug_t emY1 = shape->bearingY + PATH_EXPAND;

			// The hook contract's data.origin. Uploaded per-shape in the multiShape branch above
			// (osgSlug_ShapeTableData.originData), which is why that branch doesn't set this.
			ss->addUniform(new osg::Uniform("u_origin", osg::Vec2(
				static_cast<float>(shape->originX),
				static_cast<float>(shape->originY)
			)));
			ss->addUniform(new osg::Uniform("u_emCorners", Vec4(emX0, emY0, emX1, emY1)));
			ss->addUniform(new osg::Uniform("u_bandXform", Vec4(
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

		ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));
		ss->addUniform(new osg::Uniform("u_color", _color));
		ss->addUniform(new osg::Uniform("u_origin", osg::Vec2(0.0f, 0.0f))); // Miter has no shape
	}

	ss->addUniform(new osg::Uniform("u_halfWidth", _halfWidth));
	ss->addUniform(new osg::Uniform("u_effectId", _effectId));
	ss->addUniform(new osg::Uniform("u_effectParam", _effectParam));
	ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
	ss->setAttributeAndModes(_ssboBinding, osg::StateAttribute::ON);
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, false));

	_compiled = true;
}

void PathDrawable::drawImplementation(osg::RenderInfo& renderInfo) const {
	osg::Geometry::drawImplementation(renderInfo);
}

osg::BoundingBox PathDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	if(!_points || _points->empty()) return bb;

	for(const auto& p : *_points) bb.expandBy(Vec3(p.x(), p.y(), 0.0f));

	const slug_t hw = _halfWidth;

	bb.expandBy(Vec3(bb.xMin() - hw, bb.yMin() - hw, -0.1f));
	bb.expandBy(Vec3(bb.xMax() + hw, bb.yMax() + hw, 0.1f));

	return bb;
}

}
