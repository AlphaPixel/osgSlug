// vimrun! ./osgslug-shape-canvas

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Canvas-authored Shapes")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	uint32_t keyBase = 0xE0000;

	slughorn::canvas::Canvas canvas(*atlas, keyBase);

	// Example 1: Simple triangle...
	/* canvas.beginPath();
	canvas.moveTo(0.0_cv, 0.0_cv);
	canvas.lineTo(1.0_cv, 0.0_cv);
	canvas.lineTo(0.5_cv, 1.0_cv);
	canvas.closePath(); */

	/* // Example 2: A "punched-out" rectangle...
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

	auto key = canvas.fill({}); */

	/* // Example 3: A stroked chevron!
	canvas.beginPath();
	canvas.moveTo(0.1_cv, 0.1_cv);
	canvas.lineTo(0.5_cv, 0.5_cv);
	canvas.lineTo(0.9_cv, 0.1_cv);
	// canvas.strokePath(0.3_cv);

	auto key = canvas.stroke(0.35_cv, {}); */

	// First, we stroke a single, fat chevron using the default CCW winding.
	canvas
		.beginPath()
		.moveTo(0.1_cv, 0.1_cv)
		.lineTo(0.5_cv, 0.5_cv)
		.lineTo(0.9_cv, 0.1_cv)
		.strokePath(0.35_cv)
	;

	// Second, we stroke (essentially) the exact same chevron shape, only slightly offset; we use a
	// smaller "stroke size", and CW winding. When osgSlug evaluates the coverag using the
	// "non-zero" rule, it determines the OVERLAPPING AREA should be SKIPPED (achieving a kind of
	// "punch out" effect).
	canvas
		.moveTo(0.135_cv, 0.135_cv)
		.lineTo(0.5_cv, 0.5_cv)
		.lineTo(0.865_cv, 0.135_cv)
		.strokePath(0.25_cv, true)
	;

	/* // Arm direction from left tip toward apex: (0.707, 0.707)
	// Inset by roughly innerHalfWidth (0.125) so the inner cap sits inside the outer body
	canvas.moveTo(0.188_cv, 0.188_cv);  // was (0.1, 0.1), inset ~0.088 along NE arm
	canvas.lineTo(0.5_cv, 0.5_cv);      // apex unchanged
	canvas.lineTo(0.812_cv, 0.188_cv);  // was (0.9, 0.1), inset ~0.088 along NW arm
	canvas.strokePath(0.25_cv, true); */

	// auto layer = canvas.fill({});
	auto layer = canvas.fill({0.6_cv, 0.7_cv, 0.8_cv, 1.0_cv});

	atlas->build();
	atlas->packTextures();

	// Saves your Atlas to a glTF-compatible SLUG file.
	slughorn::serial::writeJSON(*atlas, std::cerr);

	// auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();
	auto sd = example::makeShapeDrawable();

	// sd->addLayer({layer.key, {0.6_cv, 0.7_cv, 0.8_cv, 1.0_cv}});
	sd->addLayer(layer);

	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
