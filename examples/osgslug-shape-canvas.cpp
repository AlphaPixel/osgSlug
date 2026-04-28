// vimrun! ./osgslug-shape-canvas

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

#include "slughorn-canvas.hpp"
#include "slughorn-serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <iostream>
#include <algorithm>
#include <cmath>

int main(int argc, char** argv) {
	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	uint32_t keyBase = 0xE0000;

	slughorn::canvas::Canvas canvas(*atlas, keyBase);

	/* canvas.beginPath();
	canvas.moveTo(0.0_cv, 0.0_cv);
	canvas.lineTo(1.0_cv, 0.0_cv);
	canvas.lineTo(0.5_cv, 1.0_cv);
	canvas.closePath(); */

canvas.beginPath();

// Outer square; counter-clockwise (Y-up)
canvas.moveTo(0, 0);
canvas.lineTo(1, 0);
canvas.lineTo(1, 1);
canvas.lineTo(0, 1);
canvas.closePath();

// Inner square; clockwise (opposite winding), same path, no beginPath()!
canvas.moveTo(0.25, 0.25);
canvas.lineTo(0.25, 0.75);
canvas.lineTo(0.75, 0.75);
canvas.lineTo(0.75, 0.25);
canvas.closePath();

// Single fill; both contours go into one Shape
canvas.fill({1_cv, 1_cv, 1_cv, 1_cv});

	auto key = canvas.fill({1_cv, 0_cv, 0_cv, 1_cv});

	atlas->build();
	atlas->packTextures();

	// Saves your Atlas to a glTF-compatible SLUG file.
	// slughorn::serial::writeJSON(*atlas, std::cout);

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->setAtlas(atlas);
	sd->addLayer({key, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}});
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
