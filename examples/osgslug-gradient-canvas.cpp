//vimrun! ./osgslug-gradient-canvas --clear-color 0.2,0.2,0.2,1.0

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

using slughorn::PI_CV;
using slughorn::PI_2_CV;

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Canvas + gradients", {
		{
			"--gradient <string>",
			"Example to run; one of:\n"
			"\tstroke (DEFAULT)\n"
			"\tlinear-0\n"
			"\tlinear-1\n"
			"\tlinear-2\n"
			"\tradial-0\n"
			"\tradial-1\n"
			"\tsweep-0\n"
			"\tsweep-1\n"
		}
	})) return 0;

	std::string gradient = "stroke";

	while(args.read("--gradient", gradient)) {
		if(!example::validateArgument(args, "--gradient", gradient, {
			"stroke",
			"linear-0",
			"linear-1",
			"linear-2",
			"radial-0",
			"radial-1",
			"sweep-0",
			"sweep-1"
		})) return example::fail(args, 1);
	}

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

	auto compositeShape = slughorn::CompositeShape();

	if(gradient == "linear-0") {
		// Left-to-right red -> blue gradient over a square
		auto grad = canvas.createLinearGradient(
			0_cv, 0_cv, 0.5_cv, 0.5_cv,
			{
				{0_cv, {1_cv, 0.5_cv, 0_cv, 1_cv}},
				{0.5_cv, {1_cv, 1_cv, 0_cv, 1_cv}},
				{1_cv, {1_cv, 1_cv, 1_cv, 1_cv}}
			}
		);

		canvas.beginPath();
		canvas.rect(0.05_cv, 0.05_cv, 0.9_cv, 0.9_cv);
		canvas.fillGradient(grad);

		compositeShape = canvas.finalize();
	}

	else if(gradient == "linear-1") {
		auto grad = canvas.createLinearGradient(
			0.5_cv, 0.1_cv, // bottom tip
			0.5_cv, 0.95_cv, // top
			{
				{0_cv, {0.2_cv, 0.5_cv, 0.1_cv, 1_cv}},
				{0.2_cv, {0.4_cv, 0.9_cv, 0.0_cv, 1_cv}},
				{0.4_cv, {0.6_cv, 0.1_cv, 0.2_cv, 1_cv}},
				{0.9_cv, {0.8_cv, 0.7_cv, 0.3_cv, 1_cv}},
				{1_cv, {1_cv, 0.2_cv, 0.7_cv, 1_cv}}
			}
		);

		canvas.beginPath();
		canvas.moveTo(0.5_cv, 0.08_cv); // sharp bottom tip

		// right side up to rightmost point
		canvas.bezierTo(
				0.5_cv, 0.25_cv, // c1: directly above tip - makes it sharp
				0.92_cv, 0.35_cv,
				0.92_cv, 0.62_cv
		);
		// right lobe over the top
		canvas.bezierTo(
				0.92_cv, 0.88_cv,
				0.65_cv, 0.96_cv,
				0.5_cv, 0.78_cv // center dip
		);
		// left lobe
		canvas.bezierTo(
				0.35_cv, 0.96_cv,
				0.08_cv, 0.88_cv,
				0.08_cv, 0.62_cv
		);
		// left side back down to tip
		canvas.bezierTo(
				0.08_cv, 0.35_cv,
				0.5_cv, 0.25_cv, // c2: directly above tip - mirrors the sharpness
				0.5_cv, 0.08_cv
		);
		canvas.closePath();
		canvas.fillGradient(grad);

		compositeShape = canvas.finalize();
	}

	else if(gradient == "linear-2") {
		// Three disconnected rects sharing one left-to-right gradient — proves that a single
		// shape can have multiple disconnected sub-paths and the gradient clips correctly to each.
		auto grad = canvas.createLinearGradient(
			0.1_cv, 0.5_cv, // left edge
			0.9_cv, 0.5_cv, // right edge
			{
				{0.0_cv, {0_cv, 0.8_cv, 1_cv, 1_cv}}, // cyan
				{0.5_cv, {0.6_cv, 0_cv, 1_cv, 1_cv}}, // violet
				{1.0_cv, {1_cv, 0_cv, 0.8_cv, 1_cv}} // magenta
			}
		);

		auto addRect = [&](slug_t x, slug_t y, slug_t w, slug_t h) {
			canvas.moveTo(x, y);
			canvas.lineTo(x + w, y);
			canvas.lineTo(x + w, y + h);
			canvas.lineTo(x, y + h);
			canvas.closePath();
		};

		canvas.beginPath();
		addRect(0.1_cv, 0.25_cv, 0.2_cv, 0.5_cv); // left
		addRect(0.4_cv, 0.25_cv, 0.2_cv, 0.5_cv); // center
		addRect(0.7_cv, 0.25_cv, 0.2_cv, 0.5_cv); // right
		canvas.fillGradient(grad);

		compositeShape = canvas.finalize();
	}

	else if(gradient == "radial-0") {
		// Point-centre radial: red at centre, blue at the rim, over a circle.
		auto grad = canvas.createRadialGradient(
			0.5_cv, 0.5_cv, // center
			0_cv, // inner radius (point centre)
			0.5_cv, // outer radius (reaches the circle edge)
			{
				{0_cv, {0.2_cv, 0.4_cv, 0.6_cv, 1_cv}}, // red at t=0 (centre)
				{0.75_cv, {1_cv, 1_cv, 1_cv, 1_cv}}, // blue at t=1 (rim)
				{1_cv, {0.2_cv, 0.4_cv, 0.6_cv, 1_cv}}, // red at t=0 (centre)
			}
		);

		canvas.circle(0.5_cv, 0.5_cv, 0.45_cv);
		canvas.fillGradient(grad, 1_cv, "grad_radial_circle_shape");

		compositeShape = canvas.finalize();
	}

	else if(gradient == "radial-1") {
		// Annular radial: green ring (inner radius 0.2, outer 0.45) over a circle.
		auto grad = canvas.createRadialGradient(
			0.3_cv, 0.3_cv,
			0.2_cv, // inner radius
			0.65_cv, // outer radius
			{
				{0_cv, {0_cv, 1.0_cv, 0_cv, 1_cv}}, // bright green at inner edge
				{1_cv, {0.1_cv, 0.25_cv, 0.5_cv, 1_cv}} // dark green at outer edge
			}
		);

		canvas.circle(0.5_cv, 0.5_cv, 0.45_cv);
		canvas.fillGradient(grad, 1_cv, "grad_radial_ring_shape");

		compositeShape = canvas.finalize();
	}

	else if(gradient == "sweep-0") {
		// Full-circle colour wheel: red -> yellow -> green -> cyan -> blue -> magenta -> red.
		auto grad = canvas.createSweepGradient(
			0.5_cv, 0.5_cv, // center
			-PI_CV, PI_CV, // full circle, seam at -a/+a (left edge)
			{
				{0.000_cv, {1_cv, 0_cv, 0_cv, 1_cv}}, // red
				{0.167_cv, {1_cv, 1_cv, 0_cv, 1_cv}}, // yellow
				{0.333_cv, {0_cv, 1_cv, 0_cv, 1_cv}}, // green
				{0.500_cv, {0_cv, 1_cv, 1_cv, 1_cv}}, // cyan
				{0.667_cv, {0_cv, 0_cv, 1_cv, 1_cv}}, // blue
				{0.833_cv, {1_cv, 0_cv, 1_cv, 1_cv}}, // magenta
				{1.000_cv, {1_cv, 0_cv, 0_cv, 1_cv}} // red (closes seam-free)
			}
		);

		canvas.circle(0.5_cv, 0.5_cv, 0.45_cv);
		canvas.fillGradient(grad, 1_cv, "grad_sweep_wheel_shape");

		compositeShape = canvas.finalize();
	}

	else if(gradient == "sweep-1") {
		auto grad = canvas.createSweepGradient(
			0.5_cv, 0.5_cv,
			-PI_CV * 0.75_cv, PI_CV * 0.75_cv,
			{
				{0.0_cv, {1_cv, 1_cv, 1_cv, 1_cv}}, // green
				{1.0_cv, {1_cv, 1_cv, 1_cv, 0_cv}} // red
			}
		);

		canvas.circle(0.5_cv, 0.5_cv, 0.45_cv);
		canvas.fillGradient(grad, 1_cv, "grad_sweep_gauge_shape");

		compositeShape = canvas.finalize();
	}

	else if(gradient == "stroke") {
		canvas.beginPath();
		canvas.roundedRect(0.1_cv, 0.1_cv, 0.9_cv, 0.9_cv, 0.1_cv);

		/* canvas.beginPath();
		canvas.roundedRect(0.1_cv, 0.1_cv, 0.9_cv, 0.9_cv, 0.1_cv);
		canvas.stroke(0.018_cv, slughorn::Color{0.0_cv, 0.8_cv, 1.0_cv, 0.035_cv});

		canvas.beginPath();
		canvas.roundedRect(0.1_cv, 0.1_cv, 0.9_cv, 0.9_cv, 0.1_cv);
		canvas.stroke(0.009_cv, slughorn::Color{0.0_cv, 0.9_cv, 1.0_cv, 0.06_cv});

		canvas.beginPath();
		canvas.roundedRect(0.1_cv, 0.1_cv, 0.9_cv, 0.9_cv, 0.1_cv);
		canvas.stroke(0.003_cv, slughorn::Color{0.85_cv, 0.98_cv, 1.0_cv, 0.9_cv}); */

		auto grad = canvas.createLinearGradient(
			0.1_cv, 0.1_cv, // bottom-left of the shape
			1.0_cv, 1.0_cv, // top-right of the shape
			{
				{0.0_cv, slughorn::Color{0_cv, 0.8_cv, 1_cv, 1_cv}},
				{1.0_cv, slughorn::Color{1_cv, 0_cv, 0.6_cv, 0.01_cv}}
			}
		);

		canvas.strokeGradient(0.02_cv, grad);

		compositeShape = canvas.finalize();
	}

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	auto sd = example::makeShapeDrawable();

	// sd->addLayer(slughorn::Layer{key, slughorn::Color{1_cv, 1_cv, 1_cv, 1_cv}});
	sd->addCompositeShape(compositeShape);
	atlas->addChild(sd);

	auto mat = osgx::make_ref<osg::MatrixTransform>();

	mat->addChild(atlas);

	return example::run(viewer, args, mat);
}
