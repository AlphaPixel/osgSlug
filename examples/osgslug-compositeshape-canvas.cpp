// vimrun! ./osgslug-compositeshape-canvas

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	if(data.effectId == 1) {
		// effectParam (effectData.w) carries the per-bar index set via setLayerEffectParam().
		// The actual bar width is constant: roundedRect(0.1, 0.1, 0.8, ...) * scale=1. (Quads
		// are the TRUE authored bounds now - no baked expand fudge in this number anymore.)
		float i = data.effectParam;
		const float barWidth = 0.80;

		float amp = 0.5 + 0.5 * sin(data.time * 4.0 + i * 0.7);
		float sx = 0.15 + amp * 0.85;

		float uv_x = data.uv.x;
		float leftX = data.pos.x - uv_x * barWidth;

		const float capFrac = 0.122;
		float capW = capFrac * barWidth;
		float bodyW = (1.0 - 2.0 * capFrac) * barWidth;

		if(uv_x > (1.0 - capFrac)) {
			float localT = (uv_x - (1.0 - capFrac)) / capFrac;
			data.pos.x = leftX + capW + sx * bodyW + localT * capW;
		}

		else if(uv_x > capFrac) {
			float bodyT = (uv_x - capFrac) / (1.0 - 2.0 * capFrac);
			data.pos.x = leftX + capW + bodyT * sx * bodyW;
		}
	}

	// Passthrough emCoord is CORRECT here: squashing the artwork along with the quad is the
	// point. (The default axes leave the AA margin's rate slightly off while squashed -
	// subpixel, harmless.)
	return osgSlug_VertexDefault(data);
}
)";

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Canvas-authored CompositeShapes")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

#if 1
	// Draw the "pill" shape, but cheat a little bit...
	canvas.beginPath();
	canvas.roundedRect(0.1_cv, 0.1_cv, 0.8_cv, 0.1_cv, 0.1_cv);
	canvas.defineShape("bar");

	auto compositeShape = slughorn::CompositeShape();

	for(size_t y = 0; y < 12; y++) {
		compositeShape.layers.push_back({
			.key = "bar",
			.color = {1_cv, 1_cv, 1_cv, 1_cv},
			.transform = slughorn::Transform{.x=0, .y=(cv(y) * 0.15_cv)},
			.scale = 1_cv,
			.effectId = 1u
		});
	}

	// atlas->addCompositeShape(slughorn::Key("audio_bars"), compositeShape);
	atlas->addCompositeShape("audio_bars", compositeShape);
#endif

#if 0
	canvas.rect(0.05_cv, 0.05_cv, 0.9_cv, 0.9_cv);
	canvas.fill({0.5_cv, 0_cv, 0_cv, 1_cv});

	canvas.circle(0.5_cv, 0.5_cv, 0.35_cv);
	canvas.fill({0_cv, 0.5_cv, 0_cv, 1_cv});

	canvas.roundedRect(0.25_cv, 0.25_cv, 0.5_cv, 0.5_cv, 0.08_cv);
	canvas.fill({0_cv, 0_cv, 0.5_cv, 1_cv});

	auto compositeShape = canvas.finalize();
#endif

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cout);

	// SubdividedDrawable with stepsU=8 gives a 9-column vertex grid per bar.
	// No position callback needed — the default flat-quad path uses computeQuad() bounds
	// and bakes the world width into a_effectData.w for the vertex shader's 9-slice math.
	auto sd = example::makeSubdividedDrawable();

	sd->setStepsU(8);
	sd->setStepsV(1);
	sd->addCompositeShape(compositeShape);
	// Expand the initials bound so rotation doesn't "clip" our scene.
	// TODO: This is a total HACK! We need some ... "official" way ... of telling OSG the shape
	// needs more room; it currently uses the literal vertex values, but if we're CHANGING them on
	// the GPU, OSG has no easy of knowing (and happily clips and/or sets the near/far clip)!
	sd->setInitialBound(osg::BoundingBox(
		-1.25f, -1.25f, -1.25f,
		 1.25f, 1.25f, 1.25f
	));
	sd->setStateSet(atlas->createHookStateSet({{osgSlug::Atlas::VertexHook, VERT_SHADER}}));

	// atlas->addChild triggers compile immediately; setLayerEffectParam calls are safe after.
	atlas->addChild(sd);

	// TODO: This needs to be moved into a HELPER of some kind!
	for(size_t i = 0; i < compositeShape.layers.size(); i++) sd->setLayerEffectParam(i, cv(i));

	return example::run(viewer, args, atlas);
}
