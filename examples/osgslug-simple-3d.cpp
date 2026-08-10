//vimrun! ./osgslug-simple-3d

#include "osgslug-example.hpp"

#include "osgSlug/Drawable/SphereDrawable.hpp"
#include "osgSlug/Drawable/BoxDrawable.hpp"
#include "osgSlug/Drawable/HalfCylinderDrawable.hpp"

#include "slughorn/canvas.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	example::setupArguments(args, "Displays a Shape on a 3D mesh", {
		{
			"--shape <string>",
			"3D shape; one of: sphere (DEFAULT), box, half-cylinder, subdivide"
		}
	});

	std::string shape = "sphere";

	while(args.read("--shape", shape)) {
		if(!example::validateArgument(args, "--shape", shape, {
			"sphere",
			"box",
			"half-cylinder",
			"subdivide"
		})) return example::fail(args, 1);
	}

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	osgSlug::Atlas::ShapeInfo tri;

	tri.numBandsX = 3;
	tri.numBandsY = 3;
	tri.curves = {
		{0.0_cv, 0.0_cv, 0.5_cv, 0.35_cv, 1.0_cv, 0.0_cv}, // bottom
		{1.0_cv, 0.0_cv, 0.75_cv, 0.35_cv, 0.5_cv, 0.7_cv}, // right
		{0.5_cv, 0.7_cv, 0.25_cv, 0.35_cv, 0.0_cv, 0.0_cv}, // le_cvt
	};

	auto key = slughorn::Key("tri");

#if 1
		slughorn::canvas::Canvas canvas(*atlas);

		// Three disconnected rects sharing one left-to-right gradient -- proves that a single
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
		auto layer = canvas.fillGradient(grad);
#endif

	atlas->addShape(key, tri);
	atlas->build();
	atlas->packTextures();

	osg::ref_ptr<osgSlug::ShapeDrawable> sd;

	// This block draws a SPHERE, using the same kind of projection you'd use to "map" a 2D image
	// of the Earth to a 3D sphere...
	if(shape == "sphere") {
		auto sds = osgx::make_ref<osgSlug::SphereDrawable>();

		// sds->setRadius(1.0f);
		// sds->setStacks(64);
		// sds->setSlices(128);
		sds->addLayer({.key = key, .color = {1_cv, 0.5_cv, 0_cv, 1_cv}});

		sd = sds;
	}

	// This block dras a CUBE, using a different `effectId` per face.
	else if(shape == "box") {
		auto sdb = osgx::make_ref<osgSlug::BoxDrawable>();

		for(uint32_t i = 0; i < 6; i++) sdb->setFace(i, slughorn::CompositeShape{
			.layers = {{
				.key = key,
				.color = {1_cv, 0.5_cv, 0_cv, 1_cv},
				.effectId = i % 5
			}}
		});

		sd = sdb;
	}

	else if(shape == "half-cylinder") {
		auto sds = osgx::make_ref<osgSlug::HalfCylinderDrawable>(2.0f, 1.0f, osg::PIf * 0.75f);

		sds->addLayer({.key = key, .color = {1_cv, 0.5_cv, 0_cv, 1_cv}});

		sd = sds;
	}

	// TODO: Make parameters arguments accept to `example::setupArguments` above!
	else if(shape == "subdivide") {
		auto sdsd = osgx::make_ref<osgSlug::SubdividedDrawable>();

		sdsd->setStepsU(128);
		sdsd->setStepsV(1);
		sdsd->setPositionCallback([](float u, float v) -> osg::Vec3 {
			// torus, parametric surface, heightmap sample, anything...
			return { u, v, std::sin(u * 5.0f) * 0.5f };
		});
		// sdsd->addLayer({.key=key, .color={1_cv, 0.5_cv, 0_cv, 1_cv}});
		sdsd->addLayer(layer);

		sd = sdsd;
	}

	atlas->addChild(sd);

	// To visualize the entire 3D shape in debug mode (optional):
	// atlas->getOrCreateStateSet()->addUniform(new osg::Uniform("osgSlug_debugMode", 6));

	return example::run(viewer, args, atlas);
}
