//vimrun! ./test.py

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

#include "pyosg/pyosg.hpp"

#include "pybind11x.hpp"

#include <pybind11/stl/filesystem.h>
#include <pybind11/functional.h>

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
		.def(py::init<bool, uint32_t>(),
			"useGL3"_a=false,
			"texWidth"_a=slughorn::Atlas::DEFAULT_TEXTURE_WIDTH
		)
		.def(py::init([](const slughorn::Atlas& src) {
			return osg::ref_ptr<osgSlug::Atlas>(new osgSlug::Atlas(src));
		}), "atlas"_a, "Construct from a slughorn.Atlas.")
		.def_static("read", py::overload_cast<std::filesystem::path>(&osgSlug::Atlas::read))
		.def_static("fromAtlas",
			[](const slughorn::Atlas& src, bool useGL3) {
				return osgSlug::Atlas::fromAtlas(src, useGL3);
			},
			"atlas"_a,
			"useGL3"_a=false,
			"Build and pack a slughorn.Atlas in one step."
		)
		.def("packTextures", &osgSlug::Atlas::packTextures)
		.def(
			"createDefaultStateSet",
			[](const osgSlug::Atlas& self, bool useGL3, py::dict hooks) {
				return self.createDefaultStateSet(useGL3, parseHookList(hooks));
			},
			"useGL3"_a=false,
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
		.def_property_readonly("useGL3", &osgSlug::Atlas::getUseGL3)
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

	py::class_<
		osgSlug::ShapeDrawable,
		detail::ShapeDrawable,
		osgSlug::Drawable,
		osg::ref_ptr<osgSlug::ShapeDrawable>
	>(m, "ShapeDrawable")
		.def("addLayer", &osgSlug::ShapeDrawable::addLayer)
		.def("addCompositeShape", &osgSlug::ShapeDrawable::addCompositeShape)
		.def("clear", &osgSlug::ShapeDrawable::clear)

		.def_property_readonly(
			"layers",
			[](const osgSlug::ShapeDrawable& self) { return self.getLayers(); }
		)

		.def("setLayerColor", &osgSlug::ShapeDrawable::setLayerColor)
		.def("setLayerEffectId", &osgSlug::ShapeDrawable::setLayerEffectId)
		.def("setLayerEffectParam", &osgSlug::ShapeDrawable::setLayerEffectParam)
		.def("setLayerShapeIndex", &osgSlug::ShapeDrawable::setLayerShapeIndex)
		.def("setLayerGradientTransform", &osgSlug::ShapeDrawable::setLayerGradientTransform)
		.def("updateLayer", &osgSlug::ShapeDrawable::updateLayer)
		.def("dirtyLayers", py::overload_cast<>(&osgSlug::ShapeDrawable::dirtyLayers))
		.def("dirtyLayers", py::overload_cast<size_t>(&osgSlug::ShapeDrawable::dirtyLayers))
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
		.def("setStepsU", &osgSlug::SubdividedDrawable::setStepsU, "steps"_a)
		.def("setStepsV", &osgSlug::SubdividedDrawable::setStepsV, "steps"_a)
		.def("setIsolatedVertices", &osgSlug::SubdividedDrawable::setIsolatedVertices, "isolated"_a)
		.def_property_readonly(
			"isolatedVertices",
			&osgSlug::SubdividedDrawable::getIsolatedVertices
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
		osgSlug::GL3ShapeDrawable,
		osgSlug::ShapeDrawable,
		osg::ref_ptr<osgSlug::GL3ShapeDrawable>
	>(m, "GL3ShapeDrawable")
		.def(py::init<>())
	;

	py::class_<
		osgSlug::GL3SubdividedDrawable,
		osgSlug::SubdividedDrawable,
		osg::ref_ptr<osgSlug::GL3SubdividedDrawable>
	>(m, "GL3SubdividedDrawable")
		.def(py::init<>())
	;

	py::class_<
		osgSlug::SSBOShapeDrawable,
		osgSlug::ShapeDrawable,
		osg::ref_ptr<osgSlug::SSBOShapeDrawable>
	>(m, "SSBOShapeDrawable")
		.def(py::init<>())
	;

	py::class_<
		osgSlug::SSBOSubdividedDrawable,
		osgSlug::SubdividedDrawable,
		osg::ref_ptr<osgSlug::SSBOSubdividedDrawable>
	>(m, "SSBOSubdividedDrawable")
		.def(py::init<>())
	;

	py::class_<
		osgSlug::SSBODecalDrawable,
		osgSlug::SSBOSubdividedDrawable,
		osg::ref_ptr<osgSlug::SSBODecalDrawable>
	>(m, "SSBODecalDrawable")
		.def(py::init<slug_t>(), "radius"_a=1_cv)
		.def("setRadius", &osgSlug::SSBODecalDrawable::setRadius, "radius"_a)
		.def("addDecal",
			&osgSlug::SSBODecalDrawable::addDecal,
			"layer"_a,
			"latDeg"_a,
			"lonDeg"_a,
			"halfWidthDeg"_a,
			"halfHeightDeg"_a=-1_cv
		)
		.def("updateDecalPosition",
			&osgSlug::SSBODecalDrawable::updateDecalPosition,
			"index"_a,
			"latDeg"_a,
			"lonDeg"_a,
			"halfWidthDeg"_a,
			"halfHeightDeg"_a=-1_cv
		)
		.def("setDecalTransform",
			&osgSlug::SSBODecalDrawable::setDecalTransform,
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
		osgSlug::SSBOSubdividedDrawable,
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
		osgSlug::SSBOSubdividedDrawable,
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
