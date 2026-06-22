//vimrun! ./osgslug-simple-animation

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex(osgSlug_VertexData data) {
	if(data.effectId == 1) {
		data.pos.x += sin(data.emCoord.y * 6.0 + data.time * 2.0) * 0.2;
		data.pos.y += sin(data.emCoord.x * 4.0 + data.time * 1.5) * 0.1;
	}

	return data.pos;
}
)";

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates animated ShapeDrawable effects")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	osgSlug::Atlas::ShapeInfo tri;

	// tri.origin = slughorn::Atlas::ShapeInfo::Origin(slughorn::Atlas::ShapeInfo::Origin::Type::Centered);
	tri.origin = slughorn::Atlas::ShapeInfo::Origin::Type::Centered;
	tri.numBandsX = 2;
	tri.numBandsY = 5;
	tri.curves = {
		{0.0_cv, 0.0_cv, 0.5_cv, 0.35_cv, 1.0_cv, 0.0_cv}, // bottom
		{1.0_cv, 0.0_cv, 0.75_cv, 0.35_cv, 0.5_cv, 0.7_cv}, // right
		{0.5_cv, 0.7_cv, 0.25_cv, 0.35_cv, 0.0_cv, 0.0_cv}, // le_cvt
	};

	auto key = slughorn::Key("tri");

	atlas->addShape(key, tri);
	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addLayer({
		.key = key,
		.color = {1_cv, 0.5_cv, 0_cv, 1_cv},
		.effectId = 1
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

	auto key = slughorn::Key("quad");

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

	sd->setStateSet(atlas->createHookStateSet({{osgSlug::Atlas::VertexHook, VERT_SHADER}}));
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
