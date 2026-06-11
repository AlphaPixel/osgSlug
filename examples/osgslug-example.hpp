#pragma once

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"
#include "osgDebug.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>
#include <osg/Uniform>

#include <osgGA/GUIEventHandler>
#include <osgGA/StateSetManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <charconv>

namespace example {

inline bool USE_GL3 = false;

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

	osg::ref_ptr<osg::Uniform> _uniform;
	int _mode = 0;

	DebugModeHandler(osg::StateSet* ss) {
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

struct AtlasExportVisitor: public osg::NodeVisitor {
	std::vector<osgSlug::Atlas*> atlases;

	AtlasExportVisitor(): osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}

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

/* inline bool validatePositional(
	osg::ArgumentParser& args,
	int num,
	const std::string& name="POSITIONAL"
) {
	if(args.argc() != num + 1) {
		args.reportError(
			"Invalid number of positional args for " + name +
			": expected " + std::to_string(num) +
			", got " + std::to_string(args.argc() - 1)
		);
		args.writeErrorMessages(std::cerr);

		return false;
	}

	for(int i = 1; i <= num; ++i) {
		if(!args.isString(i)) {
			args.reportError(
				"Invalid positional arg for " + name +
				": " + std::string(args[i])
			);
			args.writeErrorMessages(std::cerr);

			return false;
		}
	}

	return true;
} */

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

	return true;
}

/* inline auto run(
	osg::ArgumentParser& args,
	osg::ref_ptr<osg::Node> sceneData,
	bool rot90x=true
) {
	osgViewer::Viewer viewer(args);

	// if(args.read("--help")) return fail(args, 0);

	// By default, MOST backends want the resultant "scene" rotated 90 degrees; however, there might
	// be some cases (ortho2D, 3D) where you DON'T want to override OSG's "z-up" convention...
	if(rot90x) {
		auto root = osgx::make_ref<osg::MatrixTransform>();

		root->setMatrix(osgSlug::Matrix::rotate(osg::DegreesToRadians(90.0f), osgSlug::Vec3(1.0_cv, 0.0_cv, 0.0_cv)));
		root->addChild(sceneData);

		viewer.setSceneData(root);
	}

	else viewer.setSceneData(sceneData);

	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
} */

inline auto run(
	osgViewer::Viewer& viewer,
	osg::ArgumentParser& args,
	osg::ref_ptr<osg::Node> sceneData,
	bool rot90x=true
) {
	// By default, MOST backends want the resultant "scene" rotated 90 degrees; however, there might
	// be some cases (ortho2D, 3D) where you DON'T want to override OSG's "z-up" convention...
	if(rot90x) {
		auto root = osgx::make_ref<osg::MatrixTransform>();

		root->setMatrix(osgSlug::Matrix::rotate(osg::DegreesToRadians(90.0f), osgSlug::Vec3(1.0_cv, 0.0_cv, 0.0_cv)));
		root->addChild(sceneData);

		viewer.setSceneData(root);
	}

	else viewer.setSceneData(sceneData);

	if(args.read("--profile")) {
		// TODO: Is there a better way to do this!?
		setenv("__GL_SYNC_TO_VBLANK", "0", 1);

		auto dsv = osgx::DescribeSceneVisitor();
		auto dv = osgDebug::DrawVisitor();

		// Adds the osgDebug::DrawCallback to every detected `Drawable` in the subgraph.
		sceneData->accept(dsv);
		sceneData->accept(dv);

		// auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();
		// viewer.setRealizeOperation(debugSupported);
		// viewer.realize();

		viewer.getCamera()->setFinalDrawCallback(new osgDebug::FinalDrawCallback());
	}

	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
	viewer.addEventHandler(new DebugModeHandler(viewer.getCamera()->getOrCreateStateSet()));

	std::string dumpAtlasPath;

	if(args.read("--dump-atlas", dumpAtlasPath)) {
		AtlasExportVisitor visitor;

		sceneData->accept(visitor);

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
