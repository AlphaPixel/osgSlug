// vimrun! ./osgslug-shape-canvas

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

#include "slughorn-serial.hpp"

#include "CLI/CLI.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <algorithm>
#include <cmath>
#include <string>
#include <charconv>

// osgslug-serial.cpp
// Usage: osgslug-serial <file.slug|file.slugb> <ShapeName|codepoint>
//
// Loads a serialized Atlas and renders either a named CompositeShape or a named/codepoint Shape in
// a basic OSG viewer.

// Try to parse argv[2] as either a uint32_t codepoint or a named string Key.
slughorn::Key parseKey(const std::string& s) {
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

void showKeys(slughorn::Atlas& atlas, std::ostream& out=osg::notify(osg::NOTICE)) {
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
}

int main(int argc, char** argv) {
	CLI::App app{"osgslug-serial"};

	std::string slugFile;

	app.add_option("slugfile", slugFile, "Input slughorn Atlas file")->required();

	auto* group = app.add_option_group("Shape selection");
	auto* shape = group->add_option("-s,--shape", "A simple shape");
	auto* composite_shape = group->add_option("-c,--composite-shape", "A composite shape");
	auto* showkeys = group->add_flag("-k,--keys", "Dump all slughorn Atlas keys");

	group->require_option(1);

	CLI11_PARSE(app, argc, argv);

	osg::ref_ptr<osgSlug::Atlas> atlas;

	try {
		atlas = osgSlug::Atlas::read(slugFile);
	}

	catch(const std::exception& e) {
		OSG_WARN << e.what() << std::endl;

		return 2;
	}

	OSG_NOTICE << "Atlas loaded: "
		<< atlas->getShapes().size() << " shapes, "
		<< atlas->getCompositeShapes().size() << " composites" << std::endl
	;

	if(*showkeys) {
		showKeys(*atlas);

		return 0;
	}

	const auto keyStr = *shape ? shape->as<std::string>() : composite_shape->as<std::string>();

	OSG_NOTICE << "Looking for key '" << keyStr << "'" << std::endl;

	const auto key = parseKey(keyStr);
	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	if(*composite_shape) {
		const slughorn::CompositeShape* s = atlas->getCompositeShape(key);

		if(s) {
			OSG_NOTICE << "Found CompositeShape with "
				<< s->layers.size() << " layers" << std::endl
			;

			sd->addCompositeShape(*s);
		}
	}

	else {
		const slughorn::Atlas::Shape* s = atlas->getShape(key);

		if(s) {
			OSG_NOTICE << "Found Shape: w=" << s->width
				<< " h=" << s->height << std::endl
			;

			sd->addLayer({key, {1_cv, 1_cv, 1_cv, 1_cv}});
		}
	}

	if(!sd->getLayers().size()) {
		OSG_WARN << "Couldn't find any Layers; exiting..." << std::endl;

		return 3;
	}

	sd->setAtlas(atlas);
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet());

	osgViewer::Viewer viewer;

	// TODO: Move this to some kind of HELPER for the backend (if the backend doesn't use Y-up)!
	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
	root->addChild(sdg);

	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
