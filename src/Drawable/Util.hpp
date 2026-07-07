#pragma once
// Private implementation helpers shared across Drawable .cpp files.
// Not installed; not part of the public API.

#include "osgSlug/Atlas.hpp"

#include <algorithm>
#include <cmath>

namespace osgSlug {

// Encode msdfLayer and range into a single float: float(layer + 1) + clamp(range, 0, 0.999).
// Shader unpacks with: layer = int(value) - 1, range = fract(value).
// Sentinel: -1.0f means "no MSDF" (effectData.z < 0 in the shader).
inline slug_t packMSDFData(int msdfLayer, slug_t msdfRange) {
	if(msdfLayer < 0) return -1.0f;

	return slug_t(msdfLayer + 1) + std::clamp(msdfRange, 0.0f, 0.999f);
}

struct GradientData {
	Vec4 meta {0.0f, 0.0f, 0.0f, 0.0f};
	Vec4 xform {0.0f, 0.0f, 0.0f, 0.0f};
};

inline GradientData buildGradientDataFromInfo(
	uint32_t gradientId,
	const slughorn::GradientInfo& grad
) {
	GradientData data;

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

inline GradientData buildGradientData(const Atlas& atlas, const slughorn::Layer& layer) {
	if(layer.gradientId <= 0) return {};

	return buildGradientDataFromInfo(layer.gradientId, atlas.getGradients()[layer.gradientId - 1]);
}

// em-coordinate bounding box for a shape with expand margin (quick win A).
struct EmBounds { slug_t x0, y0, x1, y1; };

inline EmBounds computeEmBounds(const slughorn::Atlas::Shape& shape, slug_t expand) {
	return {
		shape.bearingX - expand,
		(shape.bearingY - shape.height) - expand,
		(shape.bearingX + shape.width) + expand,
		shape.bearingY + expand
	};
}

} // namespace osgSlug
