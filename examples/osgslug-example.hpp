#pragma once

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable/GL3ShapeDrawable.hpp"
#include "osgSlug/Drawable/SSBOShapeDrawable.hpp"
#include "osgSlug/Drawable/GL3SubdividedDrawable.hpp"
#include "osgSlug/Drawable/SSBOSubdividedDrawable.hpp"

#include "slughorn/serial.hpp"

#include "osgDebug.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>
#include <osg/Uniform>
#include <osg/io_utils>

#include <osgGA/GUIEventHandler>
#include <osgGA/OrbitManipulator>
#include <osgGA/StateSetManipulator>
#include <osgGA/TrackballManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <charconv>

namespace example {

inline bool USE_GL3 = false;

#ifdef OSGDEBUG_IMGUI
// The OSG camera manipulator is dispatched in a separate loop AFTER all event
// handlers, so Widget::handle() returning true cannot block it. This wrapper
// deflects mouse events when ImGui has an active context and wants capture.
struct ImGuiAwareManipulator: public osgx::Ortho2DManipulator {
	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
			const auto t = ea.getEventType();

			if(
				t == osgGA::GUIEventAdapter::PUSH ||
				t == osgGA::GUIEventAdapter::DRAG ||
				t == osgGA::GUIEventAdapter::RELEASE||
				t == osgGA::GUIEventAdapter::SCROLL
			) return false;
		}

		return osgx::Ortho2DManipulator::handle(ea, aa);
	}
};
#endif

struct DebugModeHandler: public osgGA::GUIEventHandler {
	static constexpr int MAX_MODE = 6;

	static constexpr const char* MODE_NAMES[] = {
		"normal",
		"checkerboard",
		"band edges",
		"quad border",
		"heatmap",
		"heatmap + grid",
		"half-white"
	};

	osg::ref_ptr<osg::StateSet> _ss;
	osg::ref_ptr<osg::Uniform> _uniform;
	int _mode = 0;
	bool _msaa = true;

	DebugModeHandler(osg::StateSet* ss) : _ss(ss) {
		_ss->setMode(GL_MULTISAMPLE, osg::StateAttribute::ON);

		_uniform = ss->getUniform("osgSlug_debugMode");

		if(!_uniform) {
			_uniform = new osg::Uniform("osgSlug_debugMode", 0);
			ss->addUniform(_uniform);
		}

		int dm = 0;

		_uniform->get(dm);

		_mode = dm;
	}

	void setMode(int mode) {
		_mode = mode;

		_uniform->set(_mode);

		OSG_NOTICE << "osgSlug_debugMode = " << _mode << " (" << MODE_NAMES[_mode] << ")" << std::endl;
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
		if(ea.getEventType() != osgGA::GUIEventAdapter::KEYDOWN) return false;

		if(ea.getKey() == 'm') {
			_msaa = !_msaa;

			_ss->setMode(GL_MULTISAMPLE, _msaa
				? osg::StateAttribute::ON
				: osg::StateAttribute::OFF
			);

			OSG_NOTICE << "MSAA: " << (_msaa ? "ON" : "OFF") << std::endl;

			return true;
		}

		int target = -1;

		switch(ea.getKey()) {
			case osgGA::GUIEventAdapter::KEY_F1: target = 1; break;
			case osgGA::GUIEventAdapter::KEY_F2: target = 2; break;
			case osgGA::GUIEventAdapter::KEY_F3: target = 3; break;
			case osgGA::GUIEventAdapter::KEY_F4: target = 4; break;
			case osgGA::GUIEventAdapter::KEY_F5: target = 5; break;
			case osgGA::GUIEventAdapter::KEY_F6: target = 6; break;
			default: return false;
		}

		setMode(_mode == target ? 0 : target);

		return true;
	}
};

// Positions a TrackballManipulator to look at the XY plane from +Z (Y-up), so
// content built in XY space is visible face-on without a scene-graph rotation.
inline osgGA::TrackballManipulator* makeTrackball(osg::Node* scene) {
	auto* m = new osgGA::TrackballManipulator();
	auto bs = scene->getBound();

	m->setHomePosition(
		bs.center() + osg::Vec3d(0.0, 0.0, bs.radius() * 3.5),
		bs.center(),
		osg::Vec3d(0.0, 1.0, 0.0)
	);

	return m;
}

struct ManipulatorToggleHandler: public osgGA::GUIEventHandler {
	osg::Matrixd _savedProjection;
	bool _hasSavedProjection = false;

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa) override {
		if(ea.getEventType() != osgGA::GUIEventAdapter::KEYDOWN) return false;
		if(ea.getKey() != osgGA::GUIEventAdapter::KEY_F12) return false;

		auto* view = dynamic_cast<osgViewer::View*>(&aa);

		if(!view) return false;

		auto* cam = view->getCamera();

		if(dynamic_cast<osgx::Ortho2DManipulator*>(view->getCameraManipulator())) {
			// Ortho2DManipulator sets DO_NOT_COMPUTE_NEAR_FAR; restore OSG's default
			// before handing off to TrackballManipulator or it will clip the scene.
			cam->setComputeNearFarMode(
				osg::CullSettings::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES
			);

			// Restore the perspective projection Ortho2DManipulator replaced.
			// If we never saved one (started with ortho), fall back to a sensible default.
			if(_hasSavedProjection) cam->setProjectionMatrix(_savedProjection);

			else {
				const auto* vp = cam->getViewport();
				// double aspect = (vp && vp->height() > 0.0) ? vp->width() / vp->height() : 1.0;

				cam->setProjectionMatrixAsPerspective(
					30.0,
					(vp && vp->height() > 0.0) ? vp->width() / vp->height() : 1.0,
					1.0,
					10000.0
				);
			}

			view->setCameraManipulator(makeTrackball(view->getSceneData()));

			OSG_NOTICE << "Manipulator: TrackballManipulator" << std::endl;
		}

		else {
			// Save the perspective projection before Ortho2DManipulator overwrites it.
			_savedProjection = cam->getProjectionMatrix();
			_hasSavedProjection = true;

			view->setCameraManipulator(new osgx::Ortho2DManipulator());

			OSG_NOTICE << "Manipulator: Ortho2DManipulator" << std::endl;
		}

		return true;
	}
};

struct AtlasVisitor: public osg::NodeVisitor {
	std::vector<osgSlug::Atlas*> atlases;

	AtlasVisitor(): osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

	void apply(osg::Geometry& geom) override {
		if(auto* sd = dynamic_cast<osgSlug::ShapeDrawable*>(&geom)) {
			if(auto* atlas = sd->getAtlas()) {
				if(std::ranges::find(atlases, atlas) == atlases.end()) atlases.push_back(atlas);
			}
		}

		traverse(geom);
	}
};

inline osg::ref_ptr<osgSlug::ShapeDrawable> makeShapeDrawable() {
	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>(nullptr);

	if(USE_GL3) sd = new osgSlug::GL3ShapeDrawable();

	else sd = new osgSlug::SSBOShapeDrawable();

	// TODO: Add some common stuff to help with the `--profile` option!
	// sd->setName("osgSlug::ShapeDrawable");

	return sd;
}

inline osg::ref_ptr<osgSlug::SubdividedDrawable> makeSubdividedDrawable() {
	if(USE_GL3) return osgx::make_ref<osgSlug::GL3SubdividedDrawable>();

	return osgx::make_ref<osgSlug::SSBOSubdividedDrawable>();
}

inline int fail(osg::ArgumentParser& args, int r=0, const std::string& err="") {
	if(err.size()) {
		args.reportError(err);
		args.writeErrorMessages(std::cerr);
	}

	args.getApplicationUsage()->write(
		std::cout,
		osg::ApplicationUsage::COMMAND_LINE_OPTION
	);

	return r;
}

template<typename T>
bool validateArgument(
	osg::ArgumentParser& args,
	const std::string& arg,
	const T& value,
	const std::vector<T>& valid
) {
	if(std::ranges::find(valid, value) == valid.end()) {
		args.reportError(std::string("Invalid value for ") + arg + ": " + value);
		args.writeErrorMessages(std::cerr);

		return false;
	}

	return true;
}

// We always add 1 to `num` to account for the implicit argv[0] itself.
inline bool validatePositional(osg::ArgumentParser& args, int num, const std::string& pos) {
	if(args.argc() < num + 1) {
		args.reportError(std::string("Invalid number of required POSITIONAL; expected: " + pos));
		args.writeErrorMessages(std::cerr);

		return false;
	}

	return true;
}

inline bool setupArguments(
	osg::ArgumentParser& args,
	const std::string& description,
	std::initializer_list<std::pair<std::string, std::string>> extraArgs={},
	int num=0,
	const std::string& positional=""
) {
	args.getApplicationUsage()->setDescription(
		args.getApplicationName() + description
	);

	args.getApplicationUsage()->setCommandLineUsage(
		args.getApplicationName() + " [options] " + positional
	);

	if(num && !validatePositional(args, num, positional)) return false;

	args.getApplicationUsage()->addCommandLineOption(
		"--profile",
		"Applies an osgDebug::DrawVisitor for profiling"
	);

	args.getApplicationUsage()->addCommandLineOption(
		"--gl3",
		"Use GL3ShapeDrawable instead of the default SSBOShapeDrawable"
	);

	args.getApplicationUsage()->addCommandLineOption(
		"--dump-atlas <file>",
		"Traverse the scene, find all ShapeDrawables, and write the first atlas to <file> (.slug or .slugb)"
	);

	for(const auto& a : extraArgs) args.getApplicationUsage()->addCommandLineOption(
		a.first,
		a.second
	);

	if(args.read("--help")) {
		fail(args);

		return false;
	}

	USE_GL3 = args.read("--gl3");

	if(USE_GL3) OSG_NOTICE << "GL3 mode requested..." << std::endl;

	return true;
}

inline auto run(
	osgViewer::Viewer& viewer,
	osg::ArgumentParser& args,
	osg::ref_ptr<osg::Node> sceneData
) {
	auto b = sceneData->getBound();

	OSG_NOTICE << "Bounds: center=" << b.center() << " radius=" << b.radius() << std::endl;

	viewer.setSceneData(sceneData);

	if(args.read("--profile")) {
		setenv("__GL_SYNC_TO_VBLANK", "0", 1);

		auto dsv = osgx::DescribeSceneVisitor();
		sceneData->accept(dsv);

#ifdef OSGDEBUG_IMGUI
		auto* gui = new osgDebug::imgui::Widget(viewer);

		gui->addProfilerSection(sceneData.get());
		gui->addTextureSection();
#else

		auto dv = osgDebug::ProfilerVisitor<>();

		sceneData->accept(dv);

		osgDebug::appendCameraDrawCallback(
			viewer.getCamera(),
			osgDebug::CameraDrawCallbackSlot::FINAL_DRAW,
			new osgDebug::ProfilerFinalCallback<>()
		);
#endif
	}

	if(args.read("--trackball")) {
		viewer.setCameraManipulator(makeTrackball(sceneData.get()));

		OSG_NOTICE << "Manipulator: TrackballManipulator" << std::endl;
	}

	else {

#ifdef OSGDEBUG_IMGUI
		auto* m = new ImGuiAwareManipulator();
#else
		auto* m = new osgx::Ortho2DManipulator();
#endif

		viewer.setCameraManipulator(m);

		OSG_NOTICE
			<< "Manipulator: Ortho2DManipulator"
			<< "\n  center=" << m->getCenter()
			<< "\n  halfExtentY=" << m->getHalfExtentY()
			<< "\n  matrix=\n" << m->getMatrix()
			<< std::endl
		;
	}

	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
	viewer.addEventHandler(new DebugModeHandler(viewer.getCamera()->getOrCreateStateSet()));
	viewer.addEventHandler(new ManipulatorToggleHandler());
	viewer.setUpViewInWindow(50, 50, 800, 600);

	// Grab all the atlases in the scene.
	AtlasVisitor visitor;

	sceneData->accept(visitor);

	for(const auto& a : visitor.atlases) {
		OSG_NOTICE << "PackingStats: " << a->getPackingStats() << std::endl;
	}

	std::string dumpAtlasPath;

	if(args.read("--dump-atlas", dumpAtlasPath)) {
		if(visitor.atlases.empty()) {
			OSG_WARN << "dump-atlas: no ShapeDrawable found" << std::endl;
		}

		else {
			if(visitor.atlases.size() > 1) OSG_WARN
				<< "dump-atlas: " << visitor.atlases.size()
				<< " atlases found; writing first only" << std::endl
			;

			slughorn::serial::write(*visitor.atlases[0], dumpAtlasPath);

			OSG_NOTICE << "dump-atlas: wrote " << dumpAtlasPath << std::endl;
		}
	}

	return viewer.run();
}

/* void getKeys(slughorn::Atlas& atlas, std::ostream& out=osg::notify(osg::NOTICE)) {
	out << "Shapes:\n";

	for(const auto& [key, shape] : atlas.getShapes()) {
		if(key.type() == slughorn::Key::Type::Codepoint)
			out << "  codepoint " << key.codepoint()
					  << " ('" << static_cast<char>(key.codepoint()) << "')"
					  << "  advance=" << shape.advance << "\n";
		else
			out << "  name \"" << key.name() << "\""
					  << "  advance=" << shape.advance << "\n";
	}

	out << "\nComposites:\n";

	for(const auto& [key, composite] : atlas.getCompositeShapes()) {
		if(key.type() == slughorn::Key::Type::Codepoint) out
			<< "  codepoint " << key.codepoint()
			<< ":" << std::endl
		;

		else out << "  name \"" << key.name() << "\":" << std::endl;

		out << "    advance=" << composite.advance << std::endl;

		for(const auto& layer : composite.layers) {
			out << "    layer -> ";

			if(layer.key.type() == slughorn::Key::Type::Codepoint)
				out << "codepoint " << layer.key.codepoint();
			else
				out << "name \"" << layer.key.name() << "\"";

			out << "  effect=" << layer.effectId << std::endl;
		}
	}
} */

// Try to convert `s` to a uint32_t codepoint; otherwise, returns whatever was passed in as if it
// were a string key.
inline slughorn::Key parseKey(const std::string& s) {
	uint32_t cp = 0;

	// Accept "0x1F600" hex or plain decimal
	if(s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		auto [ptr, ec] = std::from_chars(s.data() + 2, s.data() + s.size(), cp, 16);

		if(ec == std::errc{}) return slughorn::Key(cp);
	}

	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), cp);

	if(ec == std::errc{}) return slughorn::Key(cp);

	// Not a number; treat as a named key
	return slughorn::Key(s);
}

}
