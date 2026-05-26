//vimrun! ./osgslug-clock --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"
#include "slughorn/canvas.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Clock HUD demo - face, ticks, rotating hand")) return 0;

	// ============================================================================================
	// Shared geometry constants. All authoring is in [0, 1] em-space; scale = 1.0 throughout.
	// CX/CY is both the clock centre AND the hand's rotation pivot.
	// ============================================================================================

	const slug_t CX = 0.5_cv, CY = 0.5_cv;
	const slug_t FACE_R = 0.45_cv;
	const slug_t TICK_OUTER = 0.43_cv;
	const slug_t TICK_INNER = 0.36_cv;
	const slug_t TICK_WIDTH = 0.025_cv;
	const slug_t HAND_LENGTH = 0.38_cv; // tip lands just inside the tick ring
	const slug_t HAND_WIDTH = 0.03_cv;

	// ============================================================================================
	// Atlas + Canvas
	// ============================================================================================

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas, slughorn::KeyIterator());
	canvas.decomposer().tolerance = slughorn::TOLERANCE_BALANCED;

	// ============================================================================================
	// Clock face - Centered origin so layer.transform.dx/dy = (0.5, 0.5)
	// ============================================================================================

	canvas.circle(CX, CY, FACE_R);
	canvas.fill(
		{0.95_cv, 0.92_cv, 0.82_cv, 1_cv}, 1_cv,
		"clock_face_shape",
		slughorn::Atlas::ShapeInfo::Origin::Type::Centered
	);

	// ============================================================================================
	// Tick marks - 12 strokes baked into one Shape. Same Centered origin as the face, so both
	// quads are anchored at (0.5, 0.5) regardless of their slightly different bbox sizes.
	// ============================================================================================

	canvas.beginPath();

	for(int i = 0; i < 12; i++) {
		canvas.save();
		canvas.translate(CX, CY);
		canvas.rotate(cv(i * 2.0 * M_PI / 12.0));
		canvas.moveTo(0_cv, TICK_INNER);
		canvas.lineTo(0_cv, TICK_OUTER);
		canvas.strokePath(TICK_WIDTH);
		canvas.restore();
	}

	canvas.fill(
		{0.15_cv, 0.15_cv, 0.15_cv, 1_cv}, 1_cv,
		"clock_ticks_shape",
		slughorn::Atlas::ShapeInfo::Origin::Type::Centered
	);

	// ============================================================================================
	// Clock hand - base at clock centre, tip toward 12 o'clock (Y-down: smaller Y = up).
	//
	// Origin(CX, CY) makes layer.transform.dx/dy = (0.5, 0.5); matching the face and ticks.
	// effectId=1 will drive time-based rotation in the vertex shader.
	// ============================================================================================

	canvas.beginPath();
	canvas.moveTo(CX, CY);
	canvas.lineTo(CX, CY - HAND_LENGTH);
	canvas.stroke(
		HAND_WIDTH,
		{0.12_cv, 0.12_cv, 0.18_cv, 1_cv},
		1_cv,
		"clock_hand_shape",
		slughorn::Atlas::ShapeInfo::Origin(CX, CY)
	);

	atlas->build();
	atlas->packTextures();

	// ============================================================================================
	// CompositeShape - all three layers share the clock centre as their world-space anchor.
	//
	// Centered origin (face, ticks) and Custom origin at (0.5, 0.5) (hand) both produce
	// layer.transform.dx/dy = (0.5, 0.5), so computeQuad places every quad around the same
	// point. The GPU rotation pivot for the hand is therefore automatically correct.
	// ============================================================================================

	const slughorn::Matrix clockCenter{.dx=CX, .dy=CY};

	slughorn::CompositeShape clock;

	clock.layers.push_back(slughorn::Layer(
		"clock_face_shape",
		{0.95_cv, 0.92_cv, 0.82_cv, 1_cv},
		clockCenter
	));

	clock.layers.push_back(slughorn::Layer(
		"clock_ticks_shape",
		{0.15_cv, 0.15_cv, 0.15_cv, 1_cv},
		clockCenter
	));

	// effectId=8: vertex shader will rotate this layer's em-coords by osg_SimulationTime.
	// (Effects 1-7 are reserved by the existing osgSlug shader stack.)
	clock.layers.push_back(slughorn::Layer(
		"clock_hand_shape",
		{0.12_cv, 0.12_cv, 0.18_cv, 1_cv},
		clockCenter,
		1_cv, // scale
		8u // effectId ("rotating hand")
	));

	// ============================================================================================
	// Render
	// ============================================================================================

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	sd->addCompositeShape(clock);
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	auto mat = osgx::make_ref<osg::MatrixTransform>();

	mat->addChild(sdg);

	return example::run(viewer, args, mat);
}
