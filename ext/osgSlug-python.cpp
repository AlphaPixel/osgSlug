//vimrun! ./test.py

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"
#include "osgSlug/Drawable/ShapeDrawable.hpp"
#include "osgSlug/Drawable/BoxDrawable.hpp"
#include "osgSlug/Drawable/DecalDrawable.hpp"
#include "osgSlug/Drawable/HalfCylinderDrawable.hpp"
#include "osgSlug/Drawable/SphereDrawable.hpp"

#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/stl/filesystem.h>
#include <pybind11/functional.h>

#include <memory>

using namespace slughorn::literals;
using slughorn::slug_t;

namespace py = pybind11;
namespace pyx = pybind11x;

// ---------------------------------------------------------------------------
// Helper: convert a Python dict {Hook: str} into a HookList.
// ---------------------------------------------------------------------------
static osgSlug::Atlas::HookList parseHookList(py::dict hooks) {
	osgSlug::Atlas::HookList list;

	for(auto item : hooks) {
		list.emplace_back(
			item.first.cast<osgSlug::Atlas::Hook>(),
			item.second.cast<std::string>()
		);
	}

	return list;
}

namespace detail {
	class Drawable: public osgSlug::Drawable {
	public:
		void compile() override {
			PYBIND11_OVERRIDE_PURE(void, osgSlug::Drawable, compile);
		}

		osg::BoundingBox computeBoundingBox() const override {
			PYBIND11_OVERRIDE_PURE(osg::BoundingBox, osgSlug::Drawable, computeBoundingBox);
		}

		void compileGLObjects(osg::RenderInfo& ri) const override {
			PYBIND11_OVERRIDE(void, osgSlug::Drawable, compileGLObjects, ri);
		}
	};

	class ShapeDrawable: public osgSlug::ShapeDrawable {
	public:
		void setLayerColor(size_t index, const slughorn::Color& color) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, setLayerColor, index, color);
		}

		void setLayerEffectId(size_t index, uint32_t effectId) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, setLayerEffectId, index, effectId);
		}

		void setLayerEffectParam(size_t index, slug_t param) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, setLayerEffectParam, index, param);
		}

		void setLayerShapeIndex(size_t index, size_t shapeIndex) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, setLayerShapeIndex, index, shapeIndex);
		}

		void setLayerGradientTransform(size_t index, const slughorn::Matrix& m) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, setLayerGradientTransform, index, m);
		}

		void updateLayer(size_t index, const slughorn::Layer& layer) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, updateLayer, index, layer);
		}

		void dirtyLayers() override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, dirtyLayers);
		}

		void dirtyLayers(size_t index) override {
			PYBIND11_OVERRIDE(void, osgSlug::ShapeDrawable, dirtyLayers, index);
		}
	};

	// Layer handle for ShapeDrawable's `layers` sequence proxy (see the pyx::SequenceTraits
	// specialization below). slughorn::Layer isn't independently addressable -- it lives inside
	// the private RenderShape entries of a std::vector, with no stable identity across mutation --
	// so instead of wrapping a pointer to existing data, this wraps (owner, index) and forwards
	// each property through ShapeDrawable's existing per-index accessors. Unlike those raw C++
	// setters (which deliberately leave dirtyLayers() to the caller, for batched mutation),
	// writing through this handle auto-dirties: it's the Python-only convenience layer, not the
	// hot path.
	class Layer {
	public:
		Layer(osgSlug::ShapeDrawable* owner, size_t index): _owner(owner), _index(index) {}

		size_t index() const { return _index; }
		slughorn::Layer layer() const { return _owner->getLayer(_index); }

		slughorn::Color getColor() const { return _owner->getLayer(_index).color; }

		void setColor(const slughorn::Color& color) {
			_owner->setLayerColor(_index, color);
			_owner->dirtyLayers(_index);
		}

		uint32_t getEffectId() const { return _owner->getLayer(_index).effectId; }

		void setEffectId(uint32_t effectId) {
			_owner->setLayerEffectId(_index, effectId);
			_owner->dirtyLayers(_index);
		}

		slug_t getEffectParam() const { return _owner->getLayer(_index).effectParam; }

		void setEffectParam(slug_t param) {
			_owner->setLayerEffectParam(_index, param);
			_owner->dirtyLayers(_index);
		}

		// Raw GPU-only overrides: written straight into the packed SSBO slot, never mirrored back
		// into the Layer struct, so there's no accurate getter to pair with these. Read `.layer`
		// for the pre-override values.
		void setShapeIndex(size_t shapeIndex) {
			_owner->setLayerShapeIndex(_index, shapeIndex);
			_owner->dirtyLayers(_index);
		}

		void setGradientTransform(const slughorn::Matrix& m) {
			_owner->setLayerGradientTransform(_index, m);
			_owner->dirtyLayers(_index);
		}

		// Declared as the osg::Vec4Array base (not osgx::Vec4Array) on purpose: osgx::Vec4Array
		// has never been registered with pybind11 anywhere in this stack, but it's a safe public
		// upcast, and pybind11 falls back to a pointer's statically declared type when the
		// dynamic RTTI type isn't registered -- so this resolves to the already-registered
		// OpenSceneGraph.osg.Vec4Array instead of throwing.
		osg::Vec4Array* buffer() const { return _owner->getLayerBuffer(_index); }
		osgSlug::RenderMask* mask() const { return _owner->getLayerMask(_index); }

	private:
		osg::ref_ptr<osgSlug::ShapeDrawable> _owner;
		size_t _index;
	};

	// Gives pyx::SequenceTraits<ShapeDrawable>::get() a stable Layer* per index -- SlotCache
	// (inside SequenceProxy) compares pointer identity to decide whether to rebuild its cached
	// py::object, so the same index must always resolve to the same Layer instance.
	class LayerCache {
	public:
		explicit LayerCache() = default;
		explicit LayerCache(osgSlug::ShapeDrawable*) {}

		Layer* get(osgSlug::ShapeDrawable* owner, size_t index) {
			if(index >= _handles.size()) _handles.resize(index + 1);
			if(!_handles[index]) _handles[index] = std::make_unique<Layer>(owner, index);

			return _handles[index].get();
		}

	private:
		std::vector<std::unique_ptr<Layer>> _handles;
	};

	// Declared ahead of the pyx::SequenceTraits specialization below purely to break an ordering
	// cycle: SequenceTraits::get() needs somewhere to keep persistent Layer handles, but the
	// *sequence* proxy itself (LayersProxy, below) can't be named until SequenceTraits<ShapeDrawable>
	// is a complete specialization. Two sidecars on the same owner deviates from pybind11x's usual
	// "one canonical storage per owner" rule, but there's no way around it here.
	using LayerHandleStorage = pyx::ProxyStorageOSG<osgSlug::ShapeDrawable, LayerCache>;
}

template<>
struct pyx::SequenceTraits<osgSlug::ShapeDrawable> {
	// Fully qualified (::detail::...) because pybind11x.hpp itself declares a nested
	// pybind11x::detail namespace, which would otherwise shadow our global ::detail here.
	using element_type = ::detail::Layer;
	using value_type = slughorn::Layer;

	static value_type from_python(py::handle h) {
		return h.cast<value_type>();
	}

	static size_t size(const osgSlug::ShapeDrawable* d) {
		return d->getNumLayers();
	}

	static element_type* get(osgSlug::ShapeDrawable* d, size_t i) {
		return ::detail::LayerHandleStorage::get(*d)->template proxy<::detail::LayerCache>().get(d, i);
	}

	static void set(osgSlug::ShapeDrawable* d, size_t i, value_type layer) {
		d->updateLayer(i, layer);
		d->dirtyLayers(i);
	}
};

namespace detail {
	using LayersProxy = pyx::SequenceProxy<osgSlug::ShapeDrawable>;
	using ShapeDrawableStorage = pyx::ProxyStorageOSG<osgSlug::ShapeDrawable, LayersProxy>;
}

PYBIND11_MODULE(osgSlug, m) {
	m.doc() = "osgSlug - OpenSceneGraph + slughorn (https://github.com/AlphaPixel/osgSlug)";

	// The `maybe_unused` thing is WEIRD, I know...
	[[maybe_unused]] auto py_slughorn = py::module_::import("slughorn");
	auto py_osg = py::module_::import("OpenSceneGraph");

	py::enum_<osgSlug::Atlas::State>(m, "AtlasState")
		.value("Empty", osgSlug::Atlas::State::Empty)
		.value("Built", osgSlug::Atlas::State::Built)
		.value("Packed", osgSlug::Atlas::State::Packed)
		.export_values()
	;

	py::enum_<osgSlug::Atlas::Hook>(m, "Hook")
		.value("VertexHook", osgSlug::Atlas::VertexHook)
		.value("FragmentHook", osgSlug::Atlas::FragmentHook)
		.value("FragmentExtHook", osgSlug::Atlas::FragmentExtHook)
		.export_values()
	;

	py::class_<osgSlug::Atlas, osg::Group, osg::ref_ptr<osgSlug::Atlas>>(m, "Atlas")
		.def(py::init<uint32_t>(),
			"texWidth"_a=slughorn::Atlas::DEFAULT_TEXTURE_WIDTH
		)
		.def(py::init([](const slughorn::Atlas& src) {
			return osg::ref_ptr<osgSlug::Atlas>(new osgSlug::Atlas(src));
		}), "atlas"_a, "Construct from a slughorn.Atlas.")
		.def_static("read", py::overload_cast<std::filesystem::path>(&osgSlug::Atlas::read))
		.def_static("fromAtlas",
			[](const slughorn::Atlas& src) {
				return osgSlug::Atlas::fromAtlas(src);
			},
			"atlas"_a,
			"Build and pack a slughorn.Atlas in one step."
		)
		.def("packTextures", &osgSlug::Atlas::packTextures)
		.def(
			"createDefaultStateSet",
			[](const osgSlug::Atlas& self, py::dict hooks) {
				return self.createDefaultStateSet(parseHookList(hooks));
			},
			"hooks"_a=py::dict()
		)
		.def(
			"createHookStateSet",
			[](const osgSlug::Atlas& self, py::dict hooks) {
				return self.createHookStateSet(parseHookList(hooks));
			},
			"hooks"_a=py::dict()
		)
		.def(
			"createDecalProgram",
			[](const osgSlug::Atlas& self, py::dict hooks) {
				return self.createDecalProgram(parseHookList(hooks));
			},
			"hooks"_a=py::dict()
		)
		.def_property_readonly("state", &osgSlug::Atlas::getState)
		.def_property_readonly(
			"curveTexture",
			&osgSlug::Atlas::getCurveTexture,
			py::return_value_policy::reference_internal
		)
		.def_property_readonly(
			"bandTexture",
			&osgSlug::Atlas::getBandTexture,
			py::return_value_policy::reference_internal
		)
		.def_property_readonly(
			"gradientTexture",
			&osgSlug::Atlas::getGradientTexture,
			py::return_value_policy::reference_internal
		)
		.def_property_readonly(
			"msdfTexture",
			&osgSlug::Atlas::getMSDFTexture,
			py::return_value_policy::reference_internal
		)
		.def("getShapeIndex", &osgSlug::Atlas::getShapeIndex, "key"_a)
	;

	py::class_<
		osgSlug::Drawable,
		detail::Drawable,
		osg::Geometry,
		osg::ref_ptr<osgSlug::Drawable>
	>(m, "Drawable")
		.def("compile", &osgSlug::Drawable::compile)
		.def_property(
			"onAtlasAttached",
			[](const osgSlug::Drawable&) -> py::object { return py::none(); },
			[](osgSlug::Drawable& self, py::object cb) {
				if(cb.is_none()) {
					self.onAtlasAttached = nullptr;
					return;
				}

				self.onAtlasAttached = [cb](osgSlug::Atlas& atlas) {
					py::gil_scoped_acquire gil;
					cb(osg::ref_ptr<osgSlug::Atlas>(&atlas));
				};
			},
			"Callback fired immediately after compile(). "
			"Set before adding to an Atlas. Receives the Atlas as its only argument."
		)
	;

	// Opaque identity handle -- never bound before Layer.mask needed to return one. RenderGroup
	// compares these by pointer, not value, so no methods are exposed yet; add them if a real
	// use case shows up.
	py::class_<osgSlug::RenderMask, osg::ref_ptr<osgSlug::RenderMask>>(m, "RenderMask");

	auto shapeDrawable = py::class_<
		osgSlug::ShapeDrawable,
		detail::ShapeDrawable,
		osgSlug::Drawable,
		osg::ref_ptr<osgSlug::ShapeDrawable>
	>(m, "ShapeDrawable");

	// Per-layer handle: shape.layers[i].color = ..., etc. See detail::Layer's comment for why
	// this can't just be a pointer into existing data.
	py::class_<detail::Layer>(shapeDrawable, "Layer")
		.def_property_readonly("index", &detail::Layer::index)
		// Snapshot of the full slughorn.Layer at this index (by value).
		.def_property_readonly("layer", &detail::Layer::layer)
		.def_property("color", &detail::Layer::getColor, &detail::Layer::setColor)
		.def_property("effectId", &detail::Layer::getEffectId, &detail::Layer::setEffectId)
		.def_property("effectParam", &detail::Layer::getEffectParam, &detail::Layer::setEffectParam)
		.def("setShapeIndex", &detail::Layer::setShapeIndex, "shapeIndex"_a)
		.def("setGradientTransform", &detail::Layer::setGradientTransform, "matrix"_a)
		// Raw per-layer SSBO slice (osg.Vec4Array, backed by an osg.ShaderStorageBufferObject).
		// Valid after compile(); None if the drawable hasn't been compiled yet. Combined with
		// Array.bufferObject and BufferObject.glBufferObject(contextID).glObjectID (both in
		// OpenSceneGraph.py core), this is the path to the raw GL buffer id something outside
		// OSG entirely -- e.g. a CUDA kernel via cudaGraphicsGLRegisterBuffer() -- needs to write
		// into this same layer data directly.
		.def_property_readonly("buffer", &detail::Layer::buffer, py::return_value_policy::reference)
		// Shared across every layer from the same addCompositeShape() call; None if that layer's
		// composite had no mask. Valid immediately, no compile() required.
		.def_property_readonly("mask", &detail::Layer::mask, py::return_value_policy::reference)
	;

	pyx::bind_proxy_property<detail::LayersProxy, osgSlug::ShapeDrawable, detail::ShapeDrawableStorage>(
		shapeDrawable, "_Layers", "layers"
	);

	shapeDrawable
		.def(py::init<>())
		.def("addLayer", &osgSlug::ShapeDrawable::addLayer)
		.def("addCompositeShape", &osgSlug::ShapeDrawable::addCompositeShape)
		.def("clear", &osgSlug::ShapeDrawable::clear)
		.def("dirtyLayers", py::overload_cast<>(&osgSlug::ShapeDrawable::dirtyLayers))
	;

	py::class_<
		osgSlug::BoxDrawable,
		osgSlug::ShapeDrawable,
		osg::ref_ptr<osgSlug::BoxDrawable>
	>(m, "BoxDrawable")
		.def(py::init<>())
		.def("setLayer", &osgSlug::BoxDrawable::setLayer)
	;

	py::class_<
		osgSlug::SubdividedDrawable,
		osgSlug::ShapeDrawable,
		osg::ref_ptr<osgSlug::SubdividedDrawable>
	>(m, "SubdividedDrawable")
		.def(py::init<>())
		.def("setStepsU", &osgSlug::SubdividedDrawable::setStepsU, "steps"_a)
		.def("setStepsV", &osgSlug::SubdividedDrawable::setStepsV, "steps"_a)
		.def_property(
			"isolatedVertices",
			&osgSlug::SubdividedDrawable::getIsolatedVertices,
			&osgSlug::SubdividedDrawable::setIsolatedVertices
		)
		.def("setPositionCallback",
			[](osgSlug::SubdividedDrawable& self, py::function cb) {
				self.setPositionCallback([cb](slug_t u, slug_t v) -> osgSlug::Vec3 {
					py::gil_scoped_acquire gil;

					return cb(u, v).cast<osgSlug::Vec3>();
				});
			},
			"callback"_a,
			"Set the (u, v) -> Vec3 position callback. Called once per vertex during compile()."
		)
	;

	py::class_<
		osgSlug::DecalDrawable,
		osgSlug::SubdividedDrawable,
		osg::ref_ptr<osgSlug::DecalDrawable>
	>(m, "DecalDrawable")
		.def(py::init<slug_t>(), "radius"_a=1_cv)
		.def("setRadius", &osgSlug::DecalDrawable::setRadius, "radius"_a)
		.def("addDecal",
			&osgSlug::DecalDrawable::addDecal,
			"layer"_a,
			"latDeg"_a,
			"lonDeg"_a,
			"halfWidthDeg"_a,
			"halfHeightDeg"_a=-1_cv
		)
		.def("updateDecalPosition",
			&osgSlug::DecalDrawable::updateDecalPosition,
			"index"_a,
			"latDeg"_a,
			"lonDeg"_a,
			"halfWidthDeg"_a,
			"halfHeightDeg"_a=-1_cv
		)
		.def("setDecalTransform",
			&osgSlug::DecalDrawable::setDecalTransform,
			"index"_a,
			"latDeg"_a,
			"lonDeg"_a,
			"halfWidthDeg"_a,
			"halfHeightDeg"_a=-1_cv,
			"rotationAngle"_a=0_cv
		)
	;

	py::class_<
		osgSlug::HalfCylinderDrawable,
		osgSlug::SubdividedDrawable,
		osg::ref_ptr<osgSlug::HalfCylinderDrawable>
	>(m, "HalfCylinderDrawable")
		.def(py::init<slug_t, slug_t, slug_t, uint16_t, uint16_t>(),
			"radius"_a=2_cv,
			"height"_a=1_cv,
			"arcAngle"_a=osg::PIf * (2_cv / 3_cv),
			"stepsU"_a=uint16_t(64),
			"stepsV"_a=uint16_t(8)
		)
	;

	py::class_<
		osgSlug::SphereDrawable,
		osgSlug::SubdividedDrawable,
		osg::ref_ptr<osgSlug::SphereDrawable>
	>(m, "SphereDrawable")
		.def(py::init<slug_t, uint16_t, uint16_t>(),
			"radius"_a=1_cv,
			"stacks"_a=uint16_t(64),
			"slices"_a=uint16_t(128)
		)
	;

	m.attr("Vec2") = py_osg.attr("osg").attr("Vec2f");
	m.attr("Vec3") = py_osg.attr("osg").attr("Vec3f");
	m.attr("Vec4") = py_osg.attr("osg").attr("Vec4f");
	m.attr("Matrix") = py_osg.attr("osg").attr("Matrixf");

	py::dict info;

	info["version"] = py::make_tuple(
		OSGSLUG_VERSION_MAJOR,
		OSGSLUG_VERSION_MINOR,
		OSGSLUG_VERSION_PATCH
	);

	pyx::build_info(m, info);
}
