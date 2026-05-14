// vimrun! ./osgslug-compositeshape-canvas

#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

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

	slughorn::canvas::Canvas canvas(*atlas);

#if 0
	// Draw the "pill" shape, but cheat a little bit...
	canvas.beginPath();
	canvas.roundedRect(0.1_cv, 0.1_cv, 0.8_cv, 0.1_cv, 0.1_cv);
	canvas.defineShape("bar");

	auto compositeShape = slughorn::CompositeShape();

	for(size_t y = 0; y < 12; y++) {
		compositeShape.layers.push_back({
			"bar",
			{1_cv, 1_cv, 1_cv, 1_cv},
			slughorn::Matrix{.dx=0, .dy=(cv(y) * 0.15_cv)},
			1_cv,
			700 + static_cast<uint32_t>(y)
		});
	}

	// atlas->addCompositeShape(slughorn::Key::fromString("audio_bars"), compositeShape);
	atlas->addCompositeShape("audio_bars", compositeShape);
#endif

	canvas.rect(0.05_cv, 0.05_cv, 0.9_cv, 0.9_cv);
	canvas.fill({0.5_cv, 0_cv, 0_cv, 1_cv});

	canvas.circle(0.5_cv, 0.5_cv, 0.35_cv);
	canvas.fill({0_cv, 0.5_cv, 0_cv, 1_cv});

	canvas.roundedRect(0.25_cv, 0.25_cv, 0.5_cv, 0.5_cv, 0.08_cv);
	canvas.fill({0_cv, 0_cv, 0.5_cv, 1_cv});

	// canvas.finalize(Key::fromString("three_layer"));
	auto compositeShape = canvas.finalize();

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cout);

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->setAtlas(atlas);
	sd->addCompositeShape(compositeShape);
	/* sd->setInitialBound(osg::BoundingBox(
		-1.25f, -1.25f, -1.25f,
		 1.25f, 1.25f, 1.25f
	)); */
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
