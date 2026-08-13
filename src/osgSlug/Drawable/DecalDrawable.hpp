#pragma once

#include "osgSlug/Drawable/SubdividedDrawable.hpp"

namespace osgSlug {

// DecalDrawable
//
// Extends SubdividedDrawable for tangent-plane decals, on either a sphere (lat/lon anchored,
// gnomonic reprojection) or an arbitrary flat surface (origin + tangent frame, no reprojection --
// see addPlanarDecal()). Each layer is placed via a tangent-frame SSBO (7 Vec4s per layer instead
// of 4). The vertex shader reconstructs world-space position from the tangent frame - no
// per-vertex sphere math on CPU. Sphere vs planar is distinguished in the shader by the SSBO's
// center.w (radius>0 = sphere, radius==0 = planar) - see Atlas::SHADER_VERT_DECAL.
class DecalDrawable: public SubdividedDrawable {
public:
	DecalDrawable(slug_t radius=1_cv): _radius(radius) {}

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

	// Add a decal layer on a flat surface: `origin` is the decal's center in world space,
	// `tangentU`/`tangentV` are the FULL width/height vectors (not half-extents) spanning the
	// patch - e.g. a cube face's edge vectors. No reprojection: world = origin + lu*tangentU +
	// lv*tangentV directly, lu/lv in [-0.5, 0.5].
	void addPlanarDecal(
		const slughorn::Layer& layer,
		const Vec3& origin,
		const Vec3& tangentU,
		const Vec3& tangentV
	);

	// Reposition an existing decal without recompiling.
	void updateDecalPosition(
		size_t index,
		slug_t latDeg,
		slug_t lonDeg,
		slug_t halfWidthDeg,
		slug_t halfHeightDeg=-1_cv
	);

	// Reposition an existing planar decal without recompiling.
	void updatePlanarDecalPosition(
		size_t index,
		const Vec3& origin,
		const Vec3& tangentU,
		const Vec3& tangentV
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

	// Required: world positions are computed in the vertex shader from the SSBO tangent frame,
	// not from the CPU-side (lu, lv, 0, layerIndex) grid ShapeDrawable::computeBoundingBox()
	// would otherwise read - the inherited version returns a bogus [0,1]^2x{0} box, which the
	// OSG frustum culler (CPU-data-only) would wrongly cull against.
	osg::BoundingBox computeBoundingBox() const override;

private:
	struct Anchor {
		bool planar = false;
		// Sphere case: computeDecalTangentFrame() re-derives the frame from these, using the
		// CURRENT _radius, on every compile()/updateDecalPosition() call - so setRadius() after
		// the fact still applies to already-added decals.
		slug_t latDeg = 0_cv, lonDeg = 0_cv, halfWidthDeg = 0_cv, halfHeightDeg = -1_cv;
		// Planar case: the resolved frame itself, no re-derivation needed.
		Vec3 origin, tangentU, tangentV;
	};

	struct DecalEntry { slughorn::Layer layer; Anchor anchor; };

	std::vector<DecalEntry> _decalEntries;
	slug_t _radius = 1_cv;
};

}
