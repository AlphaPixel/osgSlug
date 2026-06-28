#pragma once

#include "osgSlug/Drawable/SSBOSubdividedDrawable.hpp"

namespace osgSlug {

// SSBODecalDrawable
//
// Extends SSBOSubdividedDrawable for sphere-surface decals. Each layer is placed at a lat/lon
// position using a tangent-frame SSBO (7 Vec4s per layer instead of 4). The vertex shader
// reconstructs world-space position from the tangent frame - no per-vertex sphere math on CPU.
class SSBODecalDrawable: public SSBOSubdividedDrawable {
public:
	SSBODecalDrawable(slug_t radius=1_cv): _radius(radius) {}

	void setRadius(slug_t radius) { _radius = radius; }

	// Add a decal layer at the given lat/lon on the sphere.
	// halfHeightDeg < 0 uses halfWidthDeg for both dimensions.
	void addDecal(
		const slughorn::Layer& layer,
		slug_t latDeg,
		slug_t lonDeg,
		slug_t halfWidthDeg,
		slug_t halfHeightDeg=-1_cv
	);

	// Reposition an existing decal without recompiling.
	void updateDecalPosition(
		size_t index,
		slug_t latDeg,
		slug_t lonDeg,
		slug_t halfWidthDeg,
		slug_t halfHeightDeg=-1_cv
	);

	// Reposition + rotate an existing decal without recompiling.
	void setDecalTransform(
		size_t index,
		slug_t latDeg,
		slug_t lonDeg,
		slug_t halfWidthDeg,
		slug_t halfHeightDeg=-1_cv,
		slug_t rotationAngle=0_cv
	);

	void updateLayer(size_t index, const slughorn::Layer& layer) override;
	void compile() override;

private:
	struct Anchor { slug_t latDeg, lonDeg, halfWidthDeg, halfHeightDeg; };
	struct DecalEntry { slughorn::Layer layer; Anchor anchor; };

	std::vector<DecalEntry> _decalEntries;
	slug_t _radius = 1_cv;
};

}
