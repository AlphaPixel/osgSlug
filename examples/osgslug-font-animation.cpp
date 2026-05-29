//vimrun! ./osgslug-font-animation

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

static const std::string VERT_SHADER = R"(
#version 430 core

vec3 osgSlug_Vertex(
	vec3 pos,
	vec2 emCoord,
	vec2 uv,
	int effectId,
	vec2 origin,
	float effectParam,
	float time
) {
	if(effectId == 1) {
		// Wave: pos.x varies smoothly across the string, giving a continuous ripple.
		pos.y += sin(pos.x * 2.0 + time * 4.0) * 0.1;

		// Scale: origin.x is identical for all 4 vertices of a glyph, so each
		// letter pulses uniformly (no shear). Different time rate = compound motion.
		float s = 1.0 + 0.4 * sin(origin.x * 2.0 + time * 3.0);
		pos.xy = origin + (pos.xy - origin) * s;
	}

	return pos;
}
)";

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates animated glyph effect layers")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>("UbuntuMono-R.ttf", atlas);

	font->load();

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);

	const char str[] = "osgSlug";

	for(size_t i = 0; i < 7; i++) {
		sd->addLayer({
			static_cast<uint32_t>(str[i]),
			{1_cv, 1_cv, 1_cv, 1_cv},
			slughorn::Matrix{.dx=cv(i), .dy=0_cv},
			1_cv,
			1u
		});
	}

	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3, VERT_SHADER));

	return example::run(viewer, args, sdg);
}
