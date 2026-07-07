#pragma once

#include "osgSlug/Drawable/ShapeDrawable.hpp"

#include <functional>

namespace osgSlug {

// SubdividedDrawable - generalized mesh drawable for the slug pipeline.
//
// Subclasses (or direct users) supply a position function:
//
// (slug_t u, slug_t v) -> Vec3
//
// The subdivider handles em-coord mapping, index stitching, and vertex attribute binding. The slug
// pipeline sees exactly the same data as ShapeDrawable, just with more triangles and non-flat
// positions. compile() is the only override -- setLayerColor/EffectId/EffectParam/
// GradientTransform, updateLayer, dirtyLayers, and getLayerBuffer are all inherited from
// ShapeDrawable unchanged, since they operate on the same _layers[i].buffer regardless of mesh
// shape.
//
// Single-layer: uses _layers[0] for shape/color/effectId. The position function owns all geometric
// decisions; the base class owns all slug plumbing.
class SubdividedDrawable: public ShapeDrawable {
public:
	using PositionCallback = std::function<Vec3(slug_t u, slug_t v)>;

	SubdividedDrawable() = default;

	void setStepsU(index_element_type s) { _stepsU = s; }
	void setStepsV(index_element_type s) { _stepsV = s; }

	// Set the position callback; called once per vertex with u,v in [0, 1].
	void setPositionCallback(PositionCallback cb) { _positionCallback = std::move(cb); }

	// When true, each cell gets its own 4 vertices (no boundary sharing). Cost: 4x vertex
	// count. Benefit: gl_FrontFacing is reliable per-cell, enabling per-tile rotation/flip
	// effects without affecting neighbors.
	void setIsolatedVertices(bool isolated) { _isolatedVertices = isolated; }
	bool getIsolatedVertices() const { return _isolatedVertices; }

	void compile() override;

protected:
	index_element_type _stepsU = 64;
	index_element_type _stepsV = 64;

	bool _isolatedVertices = false;

	PositionCallback _positionCallback;
};

}
