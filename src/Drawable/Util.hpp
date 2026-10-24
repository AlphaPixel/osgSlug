#pragma once
// Private implementation helpers shared across Drawable .cpp files.
// Not installed; not part of the public API.

#include "osgSlug/Atlas.hpp"

#include <osg/Notify>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace osgSlug {

// Sets each osgSlug sampler uniform to the unit of the osgx::Bindings slot it reads (see
// SHADER_FRAGMENT_EMCOORD's osgSlug_effectTexture comment for why these aren't layout(binding)).
inline void addSamplerUniforms(osg::StateSet* ss) {
	static constexpr std::pair<const char*, const char*> SAMPLERS[] = {
		{"osgSlug_curveTexture", "osgSlug::atlas.curve"},
		{"osgSlug_bandTexture", "osgSlug::atlas.band"},
		{"osgSlug_gradientTexture", "osgSlug::atlas.gradient"},
		{"osgSlug_sdfTexture", "osgSlug::atlas.sdf"},
		{"osgSlug_effectTexture", "osgSlug::effect"}
	};

	auto& slots = osgx::Library::instance().bindings();

	for(const auto& [uniform, slot] : SAMPLERS) {
		ss->addUniform(new osg::Uniform(uniform, static_cast<int>(slots.get(slot))));
	}
}

// A layer's "paint slot": osgSlug_LayerData's [1] (gradientMeta) and [2] (gradientTransform).
// A layer has ONE paint - flat color, a gradient or a pattern - so gradients and patterns share
// the same two vec4s (HTML Canvas / SVG treat all three as one fillStyle):
//
//   gradient: meta.x = gradientId (positive), the rest as buildGradientDataFromInfo() documents.
//   pattern: meta.x = -patternId (negative), meta.yzw = (xx, yx, xy) and xform.xyz = (yy, dx, dy)
//            of PatternInfo::transform, xform.w = reflect bits (1 = X, 2 = Y). The atlas-level
//            half (cell + tile shape) is in the osgSlug::atlas.patterns SSBO.
struct PaintData {
	Vec4 meta {0.0f, 0.0f, 0.0f, 0.0f};
	Vec4 xform {0.0f, 0.0f, 0.0f, 0.0f};
};

inline PaintData buildGradientDataFromInfo(
	uint32_t gradientId,
	const slughorn::GradientInfo& grad
) {
	PaintData data;

	data.meta.x() = cv(gradientId);

	const auto& m = grad.transform;

	if(grad.type == slughorn::GradientInfo::Type::Radial) {
		const slug_t deltaR = cv(m.xx) - cv(grad.innerRadius);
		const slug_t invDR = deltaR > 1e-6f ? 1.0f / deltaR : 0.0f;

		data.xform = {invDR, 0.0f, 0.0f, invDR};
		data.meta = {cv(gradientId), cv(m.dx), cv(m.dy), cv(grad.innerRadius) * invDR};
	}
	else if(grad.type == slughorn::GradientInfo::Type::AffineRadial) {
		slug_t b00 = cv(m.xx), b01 = cv(m.xy), b10 = cv(m.yx), b11 = cv(m.yy);

		if(b11 < 0.0f) { b00 = -b00; b01 = -b01; b10 = -b10; b11 = -b11; }

		data.xform = {b00, b10, b01, b11};
		data.meta = {cv(gradientId), cv(m.dx), cv(m.dy), cv(grad.innerRadius)};
	}
	else if(grad.type == slughorn::GradientInfo::Type::Sweep) {
		const slug_t arcSpan = cv(m.xy);
		const slug_t invArcSpan = arcSpan > 1e-6f ? 1.0f / arcSpan : 0.0f;

		data.xform = {cv(m.dx), cv(m.dy), cv(m.xx), -invArcSpan};
	}
	else data.xform = {cv(m.xx), cv(m.xy), cv(m.dx), 0.0f};

	return data;
}

inline uint32_t patternReflectBits(slughorn::PatternInfo::Repeat repeatX, slughorn::PatternInfo::Repeat repeatY) {
	return
		(repeatX == slughorn::PatternInfo::Repeat::Reflect ? 1u : 0u) |
		(repeatY == slughorn::PatternInfo::Repeat::Reflect ? 2u : 0u)
	;
}

inline PaintData buildPatternDataFromInfo(uint32_t patternId, const slughorn::PatternInfo& pattern) {
	const auto& m = pattern.transform;

	return {
		{-cv(patternId), cv(m.xx), cv(m.yx), cv(m.xy)},
		{cv(m.yy), cv(m.dx), cv(m.dy), cv(patternReflectBits(pattern.repeatX, pattern.repeatY))}
	};
}

inline PaintData buildPaintData(const Atlas& atlas, const slughorn::Layer& layer) {
	if(layer.patternId > 0) {
		const auto& patterns = atlas.getPatterns();

		if(layer.gradientId > 0) OSG_WARN
			<< "osgSlug: layer " << layer.key << " has both a gradient and a pattern; using the pattern"
			<< std::endl
		;

		if(layer.patternId <= patterns.size()) {
			return buildPatternDataFromInfo(layer.patternId, patterns[layer.patternId - 1]);
		}

		OSG_WARN << "osgSlug: layer " << layer.key << " has an unknown patternId " << layer.patternId << std::endl;

		return {};
	}

	if(layer.gradientId <= 0) return {};

	return buildGradientDataFromInfo(layer.gradientId, atlas.getGradients()[layer.gradientId - 1]);
}

// em-coordinate bounding box for a shape. Like Shape::computeQuad(), this is the TRUE authored
// bounds - no padding, no margin, ever. The AA margin (and Layer::bleed) are applied live in
// the vertex stage (see SHADER_VERT), keeping baked coordinates exact so anchored shapes can
// never drift.
struct EmBounds { slug_t x0, y0, x1, y1; };

inline EmBounds computeEmBounds(const slughorn::Atlas::Shape& shape) {
	return {
		shape.bearingX,
		shape.bearingY - shape.height,
		shape.bearingX + shape.width,
		shape.bearingY
	};
}

} // namespace osgSlug
