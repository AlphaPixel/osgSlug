#pragma once

#include "Atlas.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Geometry>

OSGSLUG_ENABLE_WARNINGS

namespace osgSlug {

// ShapeDrawable renders slughorn::Layer and slughorn::CompositeShape instances.
//
// Scene placement is NOT handled here; wrap this node in an `osg::MatrixTransform` if you need to
// position it in world space. All geometry is built using `slughorn` as the source of "truth."
class ShapeDrawable: public osg::Geometry {
public:
	using index_type = osg::DrawElementsUShort;
	using index_element_type = index_type::vector_type::value_type;

	ShapeDrawable();

	auto* getAtlas() { return _atlas.get(); }
	void setAtlas(Atlas* atlas) { _atlas = atlas; }

	// Add a single layer for rendering.
	void addLayer(const slughorn::Layer& layer) {
		_layers.push_back(layer);
	}

	// Expand a CompositeShape into individual layer draw calls.
	void addCompositeShape(const slughorn::CompositeShape& composite) {
		for(const auto& layer : composite.layers) _layers.push_back(layer);
	}

	// TODO: We probably need/want to add `slughorn::Layers` eventually; for now, we'll just abuse
	// the fact that modern C++ allows `auto` as a return value. :)
	const auto& getLayers() const { return _layers; }

	void clear() { _layers.clear(); }

	virtual void compile() = 0;

	osg::BoundingBox computeBoundingBox() const override;

protected:
	osg::ref_ptr<Atlas> _atlas = nullptr;

	std::vector<slughorn::Layer> _layers;
};

// GL3ShapeDrawable: 8-attribute vertex path, GL 3.x compatible, explicit opt-in.
class GL3ShapeDrawable: public ShapeDrawable {
public:
	GL3ShapeDrawable() = default;

	void compile() override;
};

class BoxDrawable: public ShapeDrawable {
public:
	BoxDrawable() = default;

	void setLayer(const slughorn::Layer& layer) {
		clear();

		addLayer(layer);
	}

	void compile() override;

private:
	slug_t _size = 1_cv;
};

// SubdividedDrawable - generalized mesh drawable for the slug pipeline.
//
// Subclasses (or direct users) supply a position function:
//
// (float u, float v) -> Vec3
//
// The subdivider handles em-coord mapping, index stitching, and vertex attribute binding. The slug
// pipeline sees exactly the same data as ShapeDrawable, just with more triangles and non-flat
// positions.
//
// Single-layer: uses _layers[0] for shape/color/effectId. The position function owns all geometric
// decisions; the base class owns all slug plumbing.
//
// TODO: multi-layer support (different shapes per region).
// TODO: proper expand strategy for 3D meshes (SLUG_EXPAND=0.01 is a
//
// 2D quad concept; for curved surfaces "expand" in em-space may need to account for surface
// curvature / texel density).
class SubdividedDrawable: public ShapeDrawable {
public:
	using PositionCallback = std::function<Vec3(slug_t u, slug_t v)>;

	SubdividedDrawable() = default;

	void setStepsU(index_element_type s) { _stepsU = s; }
	void setStepsV(index_element_type s) { _stepsV = s; }

	// Set the position callback; called once per vertex with u,v in [0, 1].
	void setPositionCallback(PositionCallback cb) { _positionCallback = std::move(cb); }

protected:
	index_element_type _stepsU = 64;
	index_element_type _stepsV = 64;

	PositionCallback _positionCallback;
};

// GL3SubdividedDrawable: 8-attribute vertex path for subdivided meshes, GL 3.x compatible.
class GL3SubdividedDrawable: public SubdividedDrawable {
public:
	GL3SubdividedDrawable() = default;

	void compile() override;
};

// ------------------------------------------------------------------------------------------------
// SSBOShapeDrawable / SSBOSubdividedDrawable
//
// SSBO-backed variants of ShapeDrawable and SubdividedDrawable. compile() emits only two
// vertex attribute arrays (a_position at loc 0, a_emCoord at loc 1) and packs all per-shape
// data into a GL_SHADER_STORAGE_BUFFER indexed by a_position.w.
//
// Requires GL 4.3+ and the osgSlug-ssbo-{vert,frag}.glsl shaders.
// The drawable sets its own program via getOrCreateStateSet(); textures and uniforms are
// still inherited from the geode's state set (createDefaultStateSet()) as normal.
// ------------------------------------------------------------------------------------------------
class SSBOShapeDrawable : public ShapeDrawable {
public:
	SSBOShapeDrawable() = default;

	void compile() override;

	// Fine-grained mutation; write the relevant SSBO slot(s) and keep _layers in sync.
	// Call dirtyLayers() once after all mutations in a frame.
	void setLayerColor(size_t index, const slughorn::Color& color);
	void setLayerEffectId(size_t index, uint32_t effectId);

	// Full re-pack from a Layer struct. Re-runs gradient packing internally.
	// Preserves worldWidth in effectData.w (geometry, not layer state).
	void updateLayer(size_t index, const slughorn::Layer& layer);

	// Flush all accumulated writes to the GPU.
	void dirtyLayers();

	// Layer SSBO (binding 1). Valid after compile(); null before first compile().
	osgx::Vec4Array* getLayerBuffer() const { return _layerBuffer.get(); }

private:
	osg::ref_ptr<osgx::Vec4Array> _layerBuffer;
};

class SSBOSubdividedDrawable : public SubdividedDrawable {
public:
	SSBOSubdividedDrawable() = default;

	void compile() override;

	void setLayerColor(size_t index, const slughorn::Color& color);
	void setLayerEffectId(size_t index, uint32_t effectId);
	void updateLayer(size_t index, const slughorn::Layer& layer);
	void dirtyLayers();

	osgx::Vec4Array* getLayerBuffer() const { return _layerBuffer.get(); }

private:
	osg::ref_ptr<osgx::Vec4Array> _layerBuffer;
};

// ------------------------------------------------------------------------------------------------
// HalfCylinderDrawable
//
// Inside surface of a partial cylinder; a "curved monitor". The arc sweeps arcAngle radians
// centred on the Z axis (viewer looks in -Z). Y is linear. UV maps naturally: u along arc, v along
// height.
// ------------------------------------------------------------------------------------------------
class HalfCylinderDrawable: public SSBOSubdividedDrawable {
public:
	HalfCylinderDrawable(
		slug_t radius=2.0_cv,
		slug_t height=1.0_cv,
		// 120 degrees default
		slug_t arcAngle=osg::PIf * (2.0_cv / 3.0_cv),
		index_element_type stepsU=64,
		// height needs far fewer steps
		index_element_type stepsV=8
	) {
		_stepsU = stepsU;
		_stepsV = stepsV;

		setPositionCallback([radius, height, arcAngle](slug_t u, slug_t v) -> Vec3 {
			const slug_t angle = (u - 0.5_cv) * arcAngle;
			return {
				radius * std::sin(angle),
				v * height - height * 0.5_cv,
				radius * std::cos(angle)
			};
		});
	}
};

// ------------------------------------------------------------------------------------------------
// SphereDrawable (reimplemented as SubdividedDrawable)
// ------------------------------------------------------------------------------------------------
class SphereDrawable : public SSBOSubdividedDrawable {
public:
	SphereDrawable(
		slug_t radius=1.0_cv,
		index_element_type stacks=64,
		index_element_type slices=128
	) {
		_stepsU = slices;
		_stepsV = stacks;

		setPositionCallback([radius](slug_t u, slug_t v) -> Vec3 {
			const slug_t PI = M_PIf;
			const slug_t TAU = 2.0_cv * PI;
			const slug_t lat = PI * v - PI * 0.5_cv;
			const slug_t lon = TAU * u;

			return Vec3(
				std::cos(lat) * std::cos(lon),
				std::sin(lat),
				std::cos(lat) * std::sin(lon)
			) * radius;
		});
	}
};

}
