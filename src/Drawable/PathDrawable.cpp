#include "osgSlug/Drawable/PathDrawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/BufferIndexBinding>
#include <osg/BufferObject>
#include <osg/Depth>

OSGSLUG_ENABLE_WARNINGS

#include "slughorn/canvas.hpp"

#include <bit>

namespace osgSlug {

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Shared miter geometry; append PATH_MITER_MAIN or PATH_SLUGGIT_MAIN to form a complete shader.
static const std::string PATH_MITER_COMMON = R"GLSL(
	#version 430 core

	uniform int u_N;
	uniform float u_halfWidth;

	layout(std430, binding = 0) buffer PathData {
		vec4 points[];
	};

	const float MAX_MITER = 4.0;

	vec2 perpOf(vec2 dir) { return vec2(-dir.y, dir.x); }

	vec2 miterOffset(vec2 perp_in, vec2 perp_out, float side) {
		vec2 m = normalize(perp_in + perp_out);
		float scale = u_halfWidth / max(dot(m, perp_in), 1.0 / MAX_MITER);
		return m * scale * side;
	}

	// Returns sign vector s (s.x in [0,1]: segment start/end; s.y in {-1,+1}: sides).
	vec2 computeQuad(out vec2 base, out vec2 offset) {
		vec2 p0 = points[gl_InstanceID ].xy;
		vec2 p1 = points[gl_InstanceID + 1].xy;
		vec2 dir = normalize(p1 - p0);
		vec2 perp = perpOf(dir);

		const vec2 signs[4] = vec2[4](
			vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
		);

		vec2 s = signs[gl_VertexID % 4];
		base = mix(p0, p1, s.x);

		if(s.x < 0.5) {
			if(gl_InstanceID > 0)
				offset = miterOffset(perpOf(normalize(p0 - points[gl_InstanceID - 1].xy)), perp, s.y);
			else
				offset = perp * (u_halfWidth * s.y);
		}
		else {
			if(gl_InstanceID < u_N - 2)
				offset = miterOffset(perp, perpOf(normalize(points[gl_InstanceID + 2].xy - p1)), s.y);
			else
				offset = perp * (u_halfWidth * s.y);
		}

		return s;
	}
)GLSL";

// Miter mode: analytical fwidth SDF coverage via v_uv.
// Private pipeline (PATH_SDF_FRAG only) — not connected to Atlas::SHADER_FRAG.
static const char* PATH_MITER_MAIN = R"GLSL(
	out vec2 v_uv;

	void main() {
		vec2 base, offset;
		vec2 s = computeQuad(base, offset);

		v_uv = s;
		gl_Position = gl_ModelViewProjectionMatrix * vec4(base + offset, 0.0, 1.0);
	}
)GLSL";

// Sluggit
//
// em-X is pinned to the shape midpoint so fwidth(emCoord.x) stays near zero. Without this the
// 0.99->0.01 discontinuity at quad boundaries causes the 2x2 derivative group to compute
// fwidth ~ 0.98, giving pixelsPerEm.x ~ 1 and dropping coverage to ~0.48 (dark stripe).
static const char* PATH_SLUGGIT_MAIN = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform; // x=bandScaleX, y=bandScaleY, z=bandOffsetX, w=bandOffsetY
	uniform vec4 u_shapeData; // x=bandTexX, y=bandTexY, z=bandMaxX, w=bandMaxY
	uniform vec4 u_color;

	out osgSlug_GeomBlock {
		vec2 emCoord;
		vec2 uv;
		vec4 color;
		float layerIndex;
		vec4 gradientMeta;
		vec4 gradientXform;
	} geom;

	out osgSlug_FxBlock {
		flat int   effectId;
		flat int   gradientId;
		flat int   msdfLayer;
		flat float msdfRange;
		flat float effectParam;
		flat vec4  bandXform;
		flat vec4  shapeData;
	} fx;

	void main() {
		vec2 base, offset;
		vec2 s = computeQuad(base, offset);

		float emMidX = (u_emCorners.x + u_emCorners.z) * 0.5;
		float emY = (s.y < 0.0) ? u_emCorners.y : u_emCorners.w;

		geom.emCoord = vec2(emMidX, emY);
		geom.uv = vec2(s.x, (s.y + 1.0) * 0.5);
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientXform = vec4(0.0);
		fx.bandXform = u_bandXform;
		fx.shapeData = u_shapeData;
		fx.effectId = 0;
		fx.gradientId = 0;
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
		fx.effectParam = 0.0;

		gl_Position = gl_ModelViewProjectionMatrix * vec4(base + offset, 0.0, 1.0);
	}
)GLSL";

// Stamp mode: one instanced quad per point, centered at points[i].xy and rotated by points[i].z.
// Uses the full Slug SDF pipeline against a caller-defined shape (setShapeKey).
// emCoords vary fully over both axes - unlike Sluggit, which pins emX to the midpoint.
static const char* PATH_STAMP_VERT = R"GLSL(
	#version 430 core

	uniform float u_halfWidth;
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform;
	uniform vec4 u_shapeData;
	uniform vec4 u_color;

	layout(std430, binding = 0) buffer PathData {
		vec4 points[];
	};

	out osgSlug_GeomBlock {
		vec2 emCoord;
		vec2 uv;
		vec4 color;
		float layerIndex;
		vec4 gradientMeta;
		vec4 gradientXform;
	} geom;

	out osgSlug_FxBlock {
		flat int   effectId;
		flat int   gradientId;
		flat int   msdfLayer;
		flat float msdfRange;
		flat float effectParam;
		flat vec4  bandXform;
		flat vec4  shapeData;
	} fx;

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

		geom.emCoord = mix(u_emCorners.xy, u_emCorners.zw, q);
		geom.uv = q;
		geom.color = u_color;
		geom.layerIndex = 0.0;
		geom.gradientMeta = vec4(0.0);
		geom.gradientXform = vec4(0.0);
		fx.bandXform = u_bandXform;
		fx.shapeData = u_shapeData;
		fx.effectId = 0;
		fx.gradientId = 0;
		fx.msdfLayer = -1;
		fx.msdfRange = 0.0;
		fx.effectParam = 0.0;

		gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
	}
)GLSL";

// Analytical SDF fragment shader - used by Overlap and Miter modes.
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

void PathDrawable::compile() {
	if(_compiled) return;
	if(!_points || _points->size() < 2) return;

	auto* atlas = getAtlas();

	if((_mode == PathMode::Sluggit || _mode == PathMode::Stamp) && (!atlas || !atlas->isBuilt())) return;

	const size_t N = _points->size();

	// (Re)build SSBO for point data at binding 0.
	auto* ssbo = new osg::ShaderStorageBufferObject();
	_points->setBufferObject(ssbo);

	_ssboBinding = new osg::ShaderStorageBufferBinding(
		0, _points, 0, _points->getTotalDataSize()
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

	auto* ss = new osg::StateSet();
	auto* prog = new osg::Program();

	if(_mode == PathMode::Stamp || _mode == PathMode::Sluggit) {
		const auto shape = atlas->getShape(_shapeKey);

		if(!shape) {
			OSG_WARN << "PathDrawable: " << (_mode == PathMode::Stamp ? "Stamp" : "Sluggit")
				<< " mode requires a valid shape key - call setShapeKey() before compile()\n";
			return;
		}

		if(!atlas->getCurveTexture()) {
			OSG_WARN << "PathDrawable: curve texture is null - was atlas->packTextures() called?\n";
			return;
		}

		const slug_t emX0 = shape->bearingX + PATH_EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - PATH_EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) - PATH_EXPAND;
		const slug_t emY1 = shape->bearingY + PATH_EXPAND;

		if(_mode == PathMode::Stamp) {
			prog->addShader(new osg::Shader(osg::Shader::VERTEX, PATH_STAMP_VERT));
		}

		else {
			prog->addShader(new osg::Shader(osg::Shader::VERTEX, PATH_MITER_COMMON + PATH_SLUGGIT_MAIN));
			ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));
		}

		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_FRAG));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_HOOK));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_EXT_HOOK));

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
		ss->addUniform(new osg::Uniform("u_emCorners", Vec4(emX0, emY0, emX1, emY1)));
		ss->addUniform(new osg::Uniform("u_bandXform", Vec4(
			shape->bandScaleX, shape->bandScaleY,
			shape->bandOffsetX, shape->bandOffsetY
		)));
		ss->addUniform(new osg::Uniform("u_shapeData", Vec4(
			cv(shape->bandTexX), cv(shape->bandTexY),
			cv(shape->bandMaxX), cv(shape->bandMaxY)
		)));
		ss->addUniform(new osg::Uniform("u_color", _color));
	}

	else { // Miter - analytical fwidth SDF, no atlas
		prog->addShader(new osg::Shader(osg::Shader::VERTEX, PATH_MITER_COMMON + PATH_MITER_MAIN));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, PATH_SDF_FRAG));
		ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));
	}

	ss->addUniform(new osg::Uniform("u_halfWidth", _halfWidth));
	ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
	ss->setAttributeAndModes(_ssboBinding, osg::StateAttribute::ON);
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, false));

	_compiled = true;

	setStateSet(ss);
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
