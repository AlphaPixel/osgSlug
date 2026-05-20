// vimrun! ./osgslug-compositeshape-canvas

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Canvas-authored CompositeShapes")) return 0;

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

	// atlas->addCompositeShape(slughorn::Key("audio_bars"), compositeShape);
	atlas->addCompositeShape("audio_bars", compositeShape);
#endif

	canvas.rect(0.05_cv, 0.05_cv, 0.9_cv, 0.9_cv);
	canvas.fill({0.5_cv, 0_cv, 0_cv, 1_cv});

	canvas.circle(0.5_cv, 0.5_cv, 0.35_cv);
	canvas.fill({0_cv, 0.5_cv, 0_cv, 1_cv});

	canvas.roundedRect(0.25_cv, 0.25_cv, 0.5_cv, 0.5_cv, 0.08_cv);
	canvas.fill({0_cv, 0_cv, 0.5_cv, 1_cv});

	// canvas.finalize(Key("three_layer"));
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

	return example::run(viewer, args, sdg);
}
