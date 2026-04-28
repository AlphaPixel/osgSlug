//vimrun! ./osgslug-font-animation

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>
#include <osg/ComputeBoundsVisitor>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

int main(int argc, char** argv) {
	osgViewer::Viewer viewer;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>("UbuntuMono-R.ttf", atlas);

	font->load();

	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->setAtlas(atlas);

	const char str[] = "osgSlug";

	// for(const auto& c : "HELLO") {
	for(size_t i = 0; i < 7; i++) {
		sd->addLayer({
			static_cast<uint32_t>(str[i]),
			{1_cv, 1_cv, 1_cv, 1_cv},
			slughorn::Matrix{.dx=cv(i), .dy=0_cv},
			1_cv,
			// static_cast<uint32_t>(5 + (i % 2))
			6
		});
	}

	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet());

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
	root->addChild(sdg);

	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
