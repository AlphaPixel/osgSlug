#pragma once
// Private implementation helpers shared across Drawable .cpp files.
// Not installed; not part of the public API.

#include "osgSlug/Atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace osgSlug {

// Encode msdfLayer and msdfRange into the mantissa bits of a float whose sign+exponent are
// pinned to 0x3F800000 ("1.0") regardless of payload, unpacked in the shader via
// floatBitsToUint + bitfieldExtract (GLSL 4.00 - unusable while the GL3 path still had to
// compile, see TODO.md's packHalf2x16 note; both were freed up by the GL3 removal).
// The pinned exponent matters: an earlier version of this shifted the raw payload straight
// across all 32 bits, which for small msdfLayer/msdfRange (the common case) left the exponent
// field at all-zero - a subnormal float, which this box's GPU (and many others) flush to zero
// on load, silently corrupting the round trip. Pinning sign+exponent guarantees the bit
// pattern always decodes as a normal float in [1.0, 2.0), so it's never touched by flush-to-
// zero; only the 23 mantissa bits carry data, split 11 bits msdfLayer + 1 (up to 2046 layers)
// / 12 bits msdfRange as Q4.8 fixed point ([0, 16) em units, ~1/256 resolution).
// Sentinel: -1.0f (negative) means "no MSDF" - same contract as before.
inline slug_t packMSDFData(int msdfLayer, slug_t msdfRange) {
	static_assert(sizeof(slug_t) == sizeof(uint32_t));

	if(msdfLayer < 0) return -1.0f;

	const uint32_t layerBits = (static_cast<uint32_t>(msdfLayer + 1) & 0x7FFu) << 12;
	const uint32_t rangeBits = static_cast<uint32_t>(std::clamp(msdfRange, 0.0f, 15.99f) * 256.0f) & 0xFFFu;
	const uint32_t packed = 0x3F800000u | layerBits | rangeBits;
	slug_t result;

	std::memcpy(&result, &packed, sizeof(result));

	return result;
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
