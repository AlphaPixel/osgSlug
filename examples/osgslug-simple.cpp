//vimrun! ./osgslug-simple

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/ComputeBoundsVisitor>

OSGSLUG_ENABLE_WARNINGS

struct SpinCallback: public osg::NodeCallback {
	osgSlug::Vec3 _center;

	bool _initialized = false;

	double _startTime = 0.0;

	void computeCenter(osg::Node* node) {
		osg::ComputeBoundsVisitor cbv;

		node->accept(cbv);

		osg::BoundingBox bb = cbv.getBoundingBox();

		OSG_NOTICE
			<< bb.xMin() << ", " << bb.yMin() << " x "
			<< bb.xMax() << ", " << bb.yMax()
			<< std::endl
		;

		_center = -bb.center();
		_initialized = true;
	}

	virtual void operator()(osg::Node* node, osg::NodeVisitor* nv) {
		auto* mt = dynamic_cast<osg::MatrixTransform*>(node);

		if(!mt) return;

		if(!_initialized) {
			computeCenter(node);

			if(nv && nv->getFrameStamp()) _startTime = nv->getFrameStamp()->getSimulationTime();
		}

		double t = 0.0;

		if(nv && nv->getFrameStamp()) t = nv->getFrameStamp()->getSimulationTime() - _startTime;

		float angle = static_cast<float>(t); // radians/sec (adjust speed as needed)

		osgSlug::Matrix R =
			osgSlug::Matrix::translate(_center) *
			osgSlug::Matrix::rotate(angle, osgSlug::Vec3(0.0_cv, 0.0_cv, 1.0_cv)) *
			osgSlug::Matrix::translate(-_center)
		;

		R *= osgSlug::Matrix::translate(osgSlug::Vec3(726.0_cv, 0.0_cv, 1.0_cv));

		mt->setMatrix(R);

		traverse(node, nv);
	}
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates text rendering with a manually injected glyph")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	osgSlug::Atlas::ShapeInfo tri;

	// Rather than using a slughorn "backend" (like Cairo/Skia/FreeType/NanoSVG) or the native
	// slughorn Canvas, we'll demonstrate how to MANUALLY inject raw quadratic curves into the
	// pipeline. The points below--in "em space"--create a triangle with a single curved side,
	// dividing it into a 2x5 (TOTALLY ARBITRARY) band arrangement.
	tri.numBandsX = 2;
	tri.numBandsY = 5;
	tri.curves = {
		{0.0_cv, 0.0_cv, 0.5_cv, 0.35_cv, 1.0_cv, 0.0_cv}, // bottom
		{1.0_cv, 0.0_cv, 0.75_cv, 0.35_cv, 0.5_cv, 0.7_cv}, // right
		{0.5_cv, 0.7_cv, 0.25_cv, 0.35_cv, 0.0_cv, 0.0_cv}, // le_cvt
	};

	/* slughorn::Atlas::ShapeInfo dumbbell;

	dumbbell.curves = {
		// Bottom-left blob: rough circle around (0.25, 0.2)
		// 4 quadratic arcs, each covering one quadrant of the circle
		{0.10_cv, 0.20_cv, 0.10_cv, 0.05_cv, 0.25_cv, 0.05_cv},
		{0.25_cv, 0.05_cv, 0.40_cv, 0.05_cv, 0.40_cv, 0.20_cv},
		{0.40_cv, 0.20_cv, 0.40_cv, 0.35_cv, 0.25_cv, 0.35_cv},
		{0.25_cv, 0.35_cv, 0.10_cv, 0.35_cv, 0.10_cv, 0.20_cv},

		// Top-right blob: rough circle around (0.75, 0.8)
		{0.60_cv, 0.80_cv, 0.60_cv, 0.65_cv, 0.75_cv, 0.65_cv},
		{0.75_cv, 0.65_cv, 0.90_cv, 0.65_cv, 0.90_cv, 0.80_cv},
		{0.90_cv, 0.80_cv, 0.90_cv, 0.95_cv, 0.75_cv, 0.95_cv},
		{0.75_cv, 0.95_cv, 0.60_cv, 0.95_cv, 0.60_cv, 0.80_cv},
	}; */

	// Here's where things get even crazier; we add the triangle above to the `osgSlug::Atlas`
	// instance... AS THE CODEPOINT FOR THE CHARCTER `F`! :) This means any time the scene THINKS it
	// should render an `F` (capitalized), our shape will appear instead.
	atlas->addShape('F', tri);

	// Feed font into atlas
	// Now we'll load a font file and populate the atlas with the shapes for the ASCII glyphs; since
	// `F` is already set, the atlas will ignore it from the font data!
	auto font = osgx::make_ref<osgSlug::Font>("UbuntuMono-R.ttf", atlas);

	// TODO: This part isn't ... ideal (the whole load/loaded dance). But until the API settles,
	// there's no point changing it.
	font->load();

	if(!font->loaded()) {
		OSG_WARN << "Couldn't load font: " << std::endl;

		return 1;
	}

	atlas->build();
	atlas->packTextures();

	// osgSlug::Text takes the atlas directly; no font path, no cache
	auto text = new osgSlug::Text(atlas, osgSlug::Text::fromPixels(100.0f));

	text->setFontMetrics(font->metrics());

	text->addText("Line 0: ABCDEFGabcdefg\n", {1_cv, 0.5_cv, 0_cv, 1_cv});
	text->addText("Line 1: 1234568790\n");
	text->addText("You can also");
	text->addText(" mix", {0.5_cv, 0.7_cv, 0.9_cv, 1_cv});
	text->addText(" colors ", {0.7_cv, 0.9_cv, 0.5_cv, 1_cv});
	text->addText("in the same line!");
	text->compile();

	// OSG_WARN << "\n\n\n\n\n\n\n\n\n\n\n\n\nABCDEFGabcdefg" << std::endl;

	/* auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->setAtlas(atlas);
	// atlas->build();

	// sd->addShape({ 'F', {0,0}, 200.0f });
	sd->addShape({'F', {250,0}, osg::Vec4(1.0f, 1.0f, 0.0f, 1.0f), 200.0f});
	sd->addShape({'F', {500,0}, osg::Vec4(1.0f, 0.0f, 1.0f, 1.0f), 200.0f});
	sd->addShape({'F', {-250,0}, osg::Vec4(1.0f, 1.0f, 1.0f, 0.5f), 200.0f});
	sd->addShape({'F', {-350,0}, osg::Vec4(1.0f, 1.0f, 1.0f, 0.5f), 200.0f});
	sd->compile(); */

	auto mat = osgx::make_ref<osg::MatrixTransform>();
	auto matsd = example::makeShapeDrawable();
	auto matsdg = osgx::make_ref<osg::Geode>();

	matsd->addLayer({'F', {1_cv, 1_cv, 0_cv, 0.5_cv}, slughorn::Matrix::identity(), 100_cv});
	matsd->setAtlas(atlas);
	matsd->compile();
	matsdg->addDrawable(matsd);
	matsdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));
	mat->addChild(matsdg);
	mat->setUpdateCallback(new SpinCallback());

	// auto sdg = osgx::make_ref<osg::Geode>();

	// sdg->addDrawable(sd);
	// sdg->setStateSet(createStateSetForAtlas(atlas));

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osgSlug::Matrix::rotate(osg::DegreesToRadians(90.0f), osgSlug::Vec3(1.0_cv, 0.0_cv, 0.0_cv)));

	// root->addChild(sdg);
	root->addChild(mat);
	root->addChild(text);

	viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));

	return example::run(viewer, args, root, false);
}
