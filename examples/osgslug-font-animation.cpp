//vimrun! ./osgslug-font-animation

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates animated glyph effect layers")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>("UbuntuMono-R.ttf", atlas);

	font->load();

	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::SSBOShapeDrawable>();

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

	return example::run(viewer, args, sdg);
}
