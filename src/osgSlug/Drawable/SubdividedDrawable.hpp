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
// positions. Like ShapeDrawable, compile() emits only two vertex attribute arrays and packs
// per-layer data into a GL_SHADER_STORAGE_BUFFER; requires GL 4.3+.
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

	void setLayerColor(size_t index, const slughorn::Color& color) override;
	void setLayerEffectId(size_t index, uint32_t effectId) override;
	void setLayerEffectParam(size_t index, slug_t param) override;
	void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) override;
	void updateLayer(size_t index, const slughorn::Layer& layer) override;
	void dirtyLayers() override;
	void dirtyLayers(size_t index) override;

	osgx::Vec4Array* getLayerBuffer(size_t index) const {
		return index < _layerBuffers.size() ? _layerBuffers[index].get() : nullptr;
	}

protected:
	index_element_type _stepsU = 64;
	index_element_type _stepsV = 64;

	bool _isolatedVertices = false;

	PositionCallback _positionCallback;

	std::vector<osg::ref_ptr<osgx::Vec4Array>> _layerBuffers;
};

}
