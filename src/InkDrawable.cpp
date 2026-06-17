#include "osgSlug/InkDrawable.hpp"

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

// Non-mitered quads; adjacent quads overlap by u_overlap to hide seams.
static const char* INK_OVERLAP_VERT = R"GLSL(
	#version 430 core

	uniform float u_halfWidth;
	uniform float u_overlap;

	layout(std430, binding = 0) buffer PathData {
		vec4 points[];
	};

	out vec2 v_uv;

	void main() {
		vec2 p0 = points[gl_InstanceID ].xy;
		vec2 p1 = points[gl_InstanceID + 1].xy;
		vec2 dir = normalize(p1 - p0);
		vec2 perp = vec2(-dir.y, dir.x);
		vec2 a = p0 - dir * u_overlap;
		vec2 b = p1 + dir * u_overlap;

		const vec2 signs[4] = vec2[4](
			vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
		);

		vec2 s = signs[gl_VertexID % 4];
		vec2 pos = mix(a, b, s.x) + perp * (s.y * u_halfWidth);

		v_uv = s;
		gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
	}
)GLSL";

// Shared miter geometry; append INK_MITER_MAIN or INK_SLUGGIT_MAIN to form a complete shader.
static const std::string INK_MITER_COMMON = R"GLSL(
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
static const char* INK_MITER_MAIN = R"GLSL(
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
// fwidth ? 0.98, giving pixelsPerEm.x ? 1 and dropping coverage to ~0.48 (dark stripe).
static const char* INK_SLUGGIT_MAIN = R"GLSL(
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform; // x=bandScaleX, y=bandScaleY, z=bandOffsetX, w=bandOffsetY
	uniform vec4 u_shapeData; // x=bandTexX, y=bandTexY, z=bandMaxX, w=bandMaxY
	uniform vec4 u_color;

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

	void main() {
		vec2 base, offset;
		vec2 s = computeQuad(base, offset);

		float emMidX = (u_emCorners.x + u_emCorners.z) * 0.5;
		float emY = (s.y < 0.0) ? u_emCorners.y : u_emCorners.w;

		v_emCoord = vec2(emMidX, emY);
		v_uv = vec2(s.x, (s.y + 1.0) * 0.5);
		v_color = u_color;
		v_layerIndex = 0.0;
		v_bandXform = u_bandXform;
		v_shapeData = u_shapeData;
		v_effectId = 0;
		v_gradientId = 0;
		v_gradientMeta = vec4(0.0);
		v_gradientXform = vec4(0.0);

		gl_Position = gl_ModelViewProjectionMatrix * vec4(base + offset, 0.0, 1.0);
	}
)GLSL";

// Stamp mode: one instanced quad per point, centered at points[i].xy and rotated by points[i].z.
// Uses the full Slug SDF pipeline against a caller-defined shape (setShapeKey).
// emCoords vary fully over both axes - unlike Sluggit, which pins emX to the midpoint.
static const char* INK_STAMP_VERT = R"GLSL(
	#version 430 core

	uniform float u_halfWidth;
	uniform vec4 u_emCorners; // x=emX0, y=emY0, z=emX1, w=emY1
	uniform vec4 u_bandXform;
	uniform vec4 u_shapeData;
	uniform vec4 u_color;

	layout(std430, binding = 0) buffer PathData {
		vec4 points[];
	};

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

		v_emCoord = mix(u_emCorners.xy, u_emCorners.zw, q);
		v_uv = q;
		v_color = u_color;
		v_layerIndex = 0.0;
		v_bandXform = u_bandXform;
		v_shapeData = u_shapeData;
		v_effectId = 0;
		v_gradientId = 0;
		v_gradientMeta = vec4(0.0);
		v_gradientXform = vec4(0.0);

		gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 0.0, 1.0);
	}
)GLSL";

// Analytical SDF fragment shader - used by Overlap and Miter modes.
static const char* INK_SDF_FRAG = R"GLSL(
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

static const slughorn::Key INK_UNIT_SQ("_ink_unit_sq");
static constexpr slug_t INK_EXPAND = 0.01_cv;

InkDrawable::InkDrawable(Atlas* atlas, InkMode mode) : _mode(mode) {
	setAtlas(atlas);

	if(mode == InkMode::Sluggit && atlas) {
		slughorn::canvas::Canvas cvs(*atlas);
		cvs.setAutoMetrics(false);
		cvs.rect(0.0_cv, 0.0_cv, 1.0_cv, 1.0_cv);
		cvs.defineShape(INK_UNIT_SQ);
	}
}

void InkDrawable::setMode(InkMode mode) {
	_mode = mode;
	compile();
}

void InkDrawable::setRevealCount(size_t n) {
	_revealCount = n;

	if(_drawArrays) _drawArrays->setNumInstances(static_cast<GLsizei>(n));
}

void InkDrawable::setPoints(std::vector<osg::Vec4> pts) {
	if(!_points) _points = new osg::Vec4Array();

	_points->assign(pts.begin(), pts.end());

	if(_ssboBinding) {
		auto* ssbo = new osg::ShaderStorageBufferObject();
		_points->setBufferObject(ssbo);

		_ssboBinding = new osg::ShaderStorageBufferBinding(
			0, _points, 0, _points->getTotalDataSize()
		);

		getOrCreateStateSet()->setAttributeAndModes(_ssboBinding, osg::StateAttribute::ON);

		if(_drawArrays) _drawArrays->setNumInstances(
			static_cast<GLsizei>(_mode == InkMode::Stamp ? _points->size() : _points->size() - 1)
		);
	}

	_points->dirty();
}

void InkDrawable::compile() {
	if(!_atlas || !_atlas->isBuilt() || !_points || _points->size() < 2) return;

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

	if(_mode == InkMode::Stamp) {
		const auto shape = _atlas->getShape(_shapeKey);

		if(!shape) {
			OSG_WARN << "InkDrawable: Stamp mode requires a valid shape key - call setShapeKey() before compile()\n";
			return;
		}

		if(!_atlas->getCurveTexture()) {
			OSG_WARN << "InkDrawable: curve texture is null - was atlas->packTextures() called?\n";
			return;
		}

		const slug_t emX0 = shape->bearingX + INK_EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - INK_EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) - INK_EXPAND;
		const slug_t emY1 = shape->bearingY + INK_EXPAND;

		prog->addShader(new osg::Shader(osg::Shader::VERTEX, INK_STAMP_VERT));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_FRAG));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_HOOK));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_EXT_HOOK));

		ss->setTextureAttributeAndModes(0, _atlas->getCurveTexture(), osg::StateAttribute::ON);
		ss->setTextureAttributeAndModes(1, _atlas->getBandTexture(), osg::StateAttribute::ON);

		ss->addUniform(new osg::Uniform("osgSlug_texWidth",
			static_cast<int>(std::countr_zero(_atlas->getTextureWidth()))
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
		ss->addUniform(new osg::Uniform("u_emCorners", osg::Vec4f(emX0, emY0, emX1, emY1)));
		ss->addUniform(new osg::Uniform("u_bandXform", osg::Vec4f(
			shape->bandScaleX, shape->bandScaleY,
			shape->bandOffsetX, shape->bandOffsetY
		)));
		ss->addUniform(new osg::Uniform("u_shapeData", osg::Vec4f(
			float(shape->bandTexX), float(shape->bandTexY),
			float(shape->bandMaxX), float(shape->bandMaxY)
		)));
		ss->addUniform(new osg::Uniform("u_color", _color));
	}
	else if(_mode == InkMode::Sluggit) {
		const auto shape = _atlas->getShape(INK_UNIT_SQ);

		if(!shape) {
			OSG_WARN << "InkDrawable: _ink_unit_sq not found - was atlas->build() called after the constructor?\n";
			return;
		}

		if(!_atlas->getCurveTexture()) {
			OSG_WARN << "InkDrawable: curve texture is null - was atlas->packTextures() called?\n";
			return;
		}

		const slug_t emX0 = shape->bearingX + INK_EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - INK_EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) - INK_EXPAND;
		const slug_t emY1 = shape->bearingY + INK_EXPAND;

		prog->addShader(new osg::Shader(osg::Shader::VERTEX, INK_MITER_COMMON + INK_SLUGGIT_MAIN));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_FRAG));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_HOOK));
		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, Atlas::SHADER_NOOP_FRAGMENT_EXT_HOOK));

		ss->setTextureAttributeAndModes(0, _atlas->getCurveTexture(), osg::StateAttribute::ON);
		ss->setTextureAttributeAndModes(1, _atlas->getBandTexture(), osg::StateAttribute::ON);

		// osgSlug_texWidth MUST be set; missing it makes HALF_WIDTH appear to have no effect
		// (the fragment shader silently misbehaves on the emCoord derivative).
		ss->addUniform(new osg::Uniform("osgSlug_texWidth",
			static_cast<int>(std::countr_zero(_atlas->getTextureWidth()))
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
		ss->addUniform(new osg::Uniform("u_emCorners", osg::Vec4f(emX0, emY0, emX1, emY1)));
		ss->addUniform(new osg::Uniform("u_bandXform", osg::Vec4f(
			shape->bandScaleX, shape->bandScaleY,
			shape->bandOffsetX, shape->bandOffsetY
		)));
		ss->addUniform(new osg::Uniform("u_shapeData", osg::Vec4f(
			float(shape->bandTexX), float(shape->bandTexY),
			float(shape->bandMaxX), float(shape->bandMaxY)
		)));
		ss->addUniform(new osg::Uniform("u_color", _color));
		ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));
	}
	else {
		if(_mode == InkMode::Miter) {
			prog->addShader(new osg::Shader(osg::Shader::VERTEX, INK_MITER_COMMON + INK_MITER_MAIN));
			ss->addUniform(new osg::Uniform("u_N", static_cast<int>(N)));
		}
		else {
			prog->addShader(new osg::Shader(osg::Shader::VERTEX, INK_OVERLAP_VERT));
			ss->addUniform(new osg::Uniform("u_overlap", _overlap));
		}

		prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, INK_SDF_FRAG));
	}

	ss->addUniform(new osg::Uniform("u_halfWidth", _halfWidth));
	ss->setAttributeAndModes(prog, osg::StateAttribute::ON);
	ss->setAttributeAndModes(_ssboBinding, osg::StateAttribute::ON);
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setMode(GL_CULL_FACE, osg::StateAttribute::OFF);
	ss->setAttributeAndModes(new osg::Depth(osg::Depth::LESS, 0.0, 1.0, false));

	setStateSet(ss);
}

osg::BoundingBox InkDrawable::computeBoundingBox() const {
	osg::BoundingBox bb;

	if(!_points || _points->empty()) return bb;

	for(const auto& p : *_points) bb.expandBy(osg::Vec3(p.x(), p.y(), 0.0f));

	// Expand by half-width to encompass stroke edges.
	const float hw = _halfWidth;

	bb.expandBy(osg::Vec3(bb.xMin() - hw, bb.yMin() - hw, -0.1f));
	bb.expandBy(osg::Vec3(bb.xMax() + hw, bb.yMax() + hw, 0.1f));

	return bb;
}

}
