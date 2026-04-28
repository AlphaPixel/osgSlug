// vimrun! ./osgslug-nanosvg

// =============================================================================
// Demonstrates loading a real-world SVG file directly into a slughorn
// CompositeShape using slughorn-nanosvg.hpp.
// =============================================================================

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

#include "slughorn-nanosvg.hpp"
#include "slughorn-serial.hpp"

#include "osgDebug.hpp"

#include "CLI/CLI.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <iostream>

// =============================================================================
// main
// =============================================================================

int main(int argc, char** argv) {
	CLI::App app{"osgslug-nanosvg"};

	std::string svgFile;

	app.add_option("svgfile", svgFile, "Input SVG file")->required();

	CLI11_PARSE(app, argc, argv);

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	// TODO: Change this to `KeyIterator` instead (in slughorn-nanosvg.hpp).
	uint32_t baseKey = 0xD0000;
	auto logo = slughorn::nanosvg::loadFile(svgFile, *atlas, baseKey);

	if(logo.layers.empty()) {
		OSG_WARN << "No layers loaded - check SVG path: " << svgFile << std::endl;

		return 1;
	}

	OSG_NOTICE
		<< "Loaded '" << svgFile << "': "
		<< logo.layers.size() << " layers, "
		<< "keys 0xD0000-0x" << std::hex << (baseKey - 1) << std::dec
		<< std::endl
	;

	atlas->addCompositeShape(slughorn::Key::fromString("logo"), logo);
	atlas->build();
	atlas->packTextures();

	// slughorn::serial::write(*atlas, "osgslug-nanosvg.slugb");

	// Debug: dump each layer and its resolved atlas shape.
	for(const auto& layer : logo.layers) {
		const auto* s = atlas->getShape(layer.key);

		if(s) OSG_NOTICE << layer << std::endl << " " << *s << std::endl;
	}

	auto sd = osgx::make_nref<osgSlug::ShapeDrawable>(svgFile);

	sd->setAtlas(atlas);
	sd->addCompositeShape(logo);
	sd->compile();

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(sd);
	geode->setStateSet(atlas->createDefaultStateSet());

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(
		osg::Matrix::scale(1.0, -1.0, 1.0) *
		osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0))
	);
	root->addChild(geode);
	root->setName("root");

	auto dv = osgDebug::DrawVisitor<120, 60>();

	// Adds the osgDebug::DrawCallback to every detected `Drawable` in the subgraph.
	root->accept(dv);

	auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();

	osgViewer::Viewer viewer;

	viewer.getCamera()->setClearColor(osg::Vec4(0.85_cv, 0.85_cv, 0.85_cv, 1_cv));
	viewer.setRealizeOperation(debugSupported);
	viewer.realize();
	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
