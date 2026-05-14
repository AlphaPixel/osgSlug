#pragma once

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"
#include "osgDebug.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <charconv>

namespace example {

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

	for(const auto& a : extraArgs) args.getApplicationUsage()->addCommandLineOption(
		a.first,
		a.second
	);

	if(args.read("--help")) {
		fail(args);

		return false;
	}

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

		root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
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

		root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
		root->addChild(sceneData);

		viewer.setSceneData(root);
	}

	else viewer.setSceneData(sceneData);

	if(args.read("--profile")) {
		auto dv = osgDebug::DrawVisitor<120, 60>();

		// Adds the osgDebug::DrawCallback to every detected `Drawable` in the subgraph.
		sceneData->accept(dv);

		// auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();
		// viewer.setRealizeOperation(debugSupported);
		// viewer.realize();
	}

	viewer.addEventHandler(new osgViewer::StatsHandler());

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

		if(ec == std::errc{}) return slughorn::Key::fromCodepoint(cp);
	}

	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), cp);

	if(ec == std::errc{}) return slughorn::Key::fromCodepoint(cp);

	// Not a number; treat as a named key
	return slughorn::Key::fromString(s);
}

}
