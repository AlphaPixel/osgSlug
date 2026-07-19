//vimrun! ./osgslug-font-animation

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);

	if(data.effectId == 1) {
		// Wave: data.pos.x varies smoothly across the string, giving a continuous ripple.
		data.pos.y += sin(data.pos.x * 2.0 + data.time * 4.0) * 0.1;

		// Scale: data.origin.x is identical for all 4 vertices of a glyph, so each
		// letter pulses uniformly (no shear). Different time rate = compound motion.
		float s = 1.0 + 0.4 * sin(data.origin.x * 2.0 + data.time * 3.0);
		data.pos.xy = data.origin + (data.pos.xy - data.origin) * s;

		r.pos = data.pos;
		r.axisX.w *= s; // uniform pulse changes the local em<->world rate
		r.axisY.w *= s;
	}

	return r;
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

	const char str[] = "osgSlug";

	for(size_t i = 0; i < 7; i++) {
		sd->addLayer({
			static_cast<uint32_t>(str[i]),
			{1_cv, 1_cv, 1_cv, 1_cv},
			slughorn::Transform{.x=cv(i), .y=0_cv},
			1_cv,
			1u
		});
	}

	sd->setStateSet(atlas->createHookStateSet({{osgSlug::Atlas::VertexHook, VERT_SHADER}}));
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
