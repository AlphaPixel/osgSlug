//vimrun! ./osgslug-origin --clear-color 0.2,0.2,0.3,1.0

// Demonstrates how Atlas::ShapeInfo::Origin affects the rotation pivot.
//
// Three identical L-shapes spin at the same speed around different pivots:
// Red - Default : pivot at bbox bottom-left (originData = 0, 0)
// Green - Centered : pivot at bbox center (originData = width/2, height/2)
// Blue - Pivot : pivot at a named point (originData = pt - bbox_min)
//
// All three L-shapes occupy the same visual size and position on screen.
// Origin changes nothing about where a shape sits; it only controls what
// point the shader rotates around. The shapes briefly "converge" each cycle,
// which makes the pivot differences easy to see.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

const static std::string VERT_EFFECTS = R"(
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	// See the comment below (setLayerEffectParam) for HOW `effectParam` could be used...
	// if(data.effectId == 1) return osgSlug_Vertex_Rotate(data, data.effectParam * data.time);
	if(data.effectId == 1) return osgSlug_Vertex_Rotate(data, data.time);

	return osgSlug_VertexDefault(data);
}
)";

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Origin Demo")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	using Origin = slughorn::Atlas::ShapeInfo::Origin;

	slughorn::canvas::Canvas canvas(*atlas, slughorn::KeyIterator());

	// L-shape: foot extends right, upright extends up.
	// Drawn at (bx, by); W=total width, H=total height, T=stroke thickness.
	auto drawL = [&](slug_t bx, slug_t by) {
		const slug_t W = 0.2_cv, H = 0.3_cv, T = 0.06_cv;

		canvas.beginPath();
		canvas.moveTo(bx, by);
		canvas.lineTo(bx + W, by);
		canvas.lineTo(bx + W, by + T);
		canvas.lineTo(bx + T, by + T);
		canvas.lineTo(bx + T, by + H);
		canvas.lineTo(bx, by + H);
		canvas.closePath();
	};

	// Red: Default origin - pivot at bbox bottom-left (bx, by)
	drawL(0.05_cv, 0.1_cv);

	auto lDefault = canvas.fill({1_cv, 0.4_cv, 0.4_cv, 1_cv});

	// Green: Centered origin - pivot at bbox center
	drawL(0.45_cv, 0.1_cv);

	auto lCentered = canvas.fill({0.4_cv, 1_cv, 0.4_cv, 1_cv}, 1_cv, Origin(Origin::Type::Centered));

	// Blue: Pivot at the top-left corner of the upright bar (an arbitrary named point)
	drawL(0.85_cv, 0.1_cv);

	auto lPivot = canvas.fill({0.4_cv, 0.6_cv, 1_cv, 1_cv}, 1_cv, Origin(0.85_cv, 0.4_cv));

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	lDefault.effectId = 1;
	lCentered.effectId = 1;
	lPivot.effectId = 1;

	sd->addLayer(lDefault);
	sd->addLayer(lCentered);
	sd->addLayer(lPivot);

	// The "userParam" value that gets piped through the shader could be used to do ANYTHING (that's
	// why it exists :)) For example, you could DOUBLE the rotation time by passing this and then
	// uncommenting the line in the vertex shader above!
	// for(size_t i = 0; i < 3; i++) sd->setLayerEffectParam(i, 2_cv);

	sd->setHooks({{osgSlug::Atlas::VertexHook, VERT_EFFECTS}});
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
