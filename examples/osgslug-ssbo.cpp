//vimrun! ./osgslug-ssbo --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

#include <cmath>

struct ColorCallback: public osg::NodeCallback {
	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		auto* sd = dynamic_cast<osgSlug::SSBOShapeDrawable*>(node);
		const osg::FrameStamp* fs = nv->getFrameStamp();

		if(!sd || !fs) { traverse(node, nv); return; }

		const double t = fs->getSimulationTime();

		sd->setLayerColor(1, {
			cv(0.5f + 0.5f * std::sin(t)),
			cv(0.5f + 0.5f * std::sin(t + 2.0944)),
			cv(0.5f + 0.5f * std::sin(t + 4.1888)),
			0.75_cv
		});

		sd->dirtyLayers(1);

		traverse(node, nv);
	}
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Compute shader SSBO color animation")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);
	slughorn::canvas::Path house;

	// Build a reusable "house" shape in full "normalized" space.
	house.moveTo(0_cv, 0_cv);
	house.lineTo(0_cv, 0.5_cv);
	house.lineTo(0.5_cv, 1_cv);
	house.lineTo(1_cv, 0.5_cv);
	house.lineTo(1_cv, 0_cv);

	canvas.fill(house, {1_cv, 1_cv, 1_cv, 1_cv});
	canvas.translate(0.33_cv, 0_cv);
	canvas.fill(house, {1_cv, 1_cv, 1_cv, 1_cv});
	canvas.translate(0.33_cv, 0_cv);
	canvas.fill(house, {1_cv, 1_cv, 1_cv, 0.5_cv});

	auto compositeShape = canvas.finalize();

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	auto sd = osgx::make_ref<osgSlug::SSBOShapeDrawable>();

	// sd->addLayer({shape, {1_cv, 0.5_cv, 0_cv, 0.5_cv}});
	sd->addCompositeShape(compositeShape);
	sd->setUpdateCallback(new ColorCallback());
	sd->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
