//vimrun! ./osgslug-simple-animation

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	osgSlug::Atlas::ShapeInfo tri;

	tri.numBandsX = 2;
	tri.numBandsY = 5;
	tri.curves = {
		{0.0_cv, 0.0_cv, 0.5_cv, 0.35_cv, 1.0_cv, 0.0_cv}, // bottom
		{1.0_cv, 0.0_cv, 0.75_cv, 0.35_cv, 0.5_cv, 0.7_cv}, // right
		{0.5_cv, 0.7_cv, 0.25_cv, 0.35_cv, 0.0_cv, 0.0_cv}, // le_cvt
	};

	auto key = slughorn::Key::fromString("tri");

	atlas->addShape(key, tri);
	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();
	auto sdg = osgx::make_ref<osg::Geode>();

	sd->addLayer({
		.key = key,
		.color = {1_cv, 0.5_cv, 0_cv, 1_cv},
		.effectId = 6
	});
	// TODO: Use `expandBy`; also, why does `effectId` 6 "offset" the shape from its center?
	sd->setInitialBound(osg::BoundingBox(
		-1.25f, -1.25f, -1.25f,
		 1.25f, 1.25f, 1.25f
	));

	/* osgSlug::Atlas::ShapeInfo quad;

	quad.numBandsX = 10;
	quad.numBandsY = 10;
	quad.curves = {
		// bottom
		{0,0, 0.5,0, 1,0},
		// right
		{1,0, 1,0.5, 1,1},
		// top
		{1,1, 0.5,1, 0,1},
		// left
		{0,1, 0,0.5, 0,0}
	};

	auto key = slughorn::Key::fromString("quad");

	atlas->addShape(key, quad);
	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();
	auto sdg = osgx::make_ref<osg::Geode>();

	sd->addLayer({
		.key = key,
		.color = {1_cv, 0.5_cv, 0_cv, 1_cv},
		.effectId = 7
	}); */

	sd->setAtlas(atlas);
	sd->compile();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet());

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
	root->addChild(sdg);

	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
