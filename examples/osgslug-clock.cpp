//vimrun! ./osgslug-clock --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"
#include "osgSlug/Font.hpp"

#include <chrono>
#include <ctime>

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

uniform float u_hourAngle;
uniform float u_minuteAngle;
uniform float u_secondAngle;

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	// Negate angles: osgSlug_Vertex_Rotate is CCW, clocks run CW.
	if(data.effectId == 1) return osgSlug_Vertex_Rotate(data, -u_secondAngle);
	if(data.effectId == 2) return osgSlug_Vertex_Rotate(data, -u_minuteAngle);
	if(data.effectId == 3) return osgSlug_Vertex_Rotate(data, -u_hourAngle);

	return osgSlug_VertexDefault(data);
}
)";

struct ClockCallback: public osg::NodeCallback {
	osg::ref_ptr<osg::Uniform> _hourAngle;
	osg::ref_ptr<osg::Uniform> _minuteAngle;
	osg::ref_ptr<osg::Uniform> _secondAngle;

	ClockCallback(osg::StateSet* ss):
	_hourAngle(new osg::Uniform("u_hourAngle", 0.0f)),
	_minuteAngle(new osg::Uniform("u_minuteAngle", 0.0f)),
	_secondAngle(new osg::Uniform("u_secondAngle", 0.0f)) {
		ss->addUniform(_hourAngle);
		ss->addUniform(_minuteAngle);
		ss->addUniform(_secondAngle);
	}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		const auto now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		const std::tm* tm = std::localtime(&t);

		const float sec = static_cast<float>(tm->tm_sec);
		const float min = static_cast<float>(tm->tm_min) + sec / 60.0f;
		const float hour = static_cast<float>(tm->tm_hour % 12) + min / 60.0f;

		static constexpr float TWO_PI = 2.0f * static_cast<float>(M_PI);

		_secondAngle->set((sec / 60.0f) * TWO_PI);
		_minuteAngle->set((min / 60.0f) * TWO_PI);
		_hourAngle->set( (hour / 12.0f) * TWO_PI);

		traverse(node, nv);
	}
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Clock HUD demo - single CompositeShape, real time")) return 0;

	// ============================================================================================
	// Geometry constants. Canvas is Y-up: larger Y = higher on screen.
	// Clock positions: x = CX + r*sin(a), y = CY + r*cos(a) (a=0 at 12 o'clock, CW).
	// ============================================================================================

	const slug_t CX = 0.5_cv, CY = 0.5_cv;
	const slug_t FACE_R = 0.45_cv;
	const slug_t TICK_OUTER = 0.43_cv;
	const slug_t TICK_INNER = 0.36_cv;
	const slug_t TICK_WIDTH = 0.025_cv;
	const slug_t NUM_R = 0.29_cv;
	const slug_t FONT_SIZE = 0.07_cv;

	const slug_t HOUR_LENGTH = 0.24_cv, HOUR_WIDTH = 0.035_cv;
	const slug_t MIN_LENGTH = 0.33_cv, MIN_WIDTH = 0.025_cv;
	const slug_t SEC_LENGTH = 0.38_cv, SEC_WIDTH = 0.010_cv;

	// ============================================================================================
	// Atlas + Canvas
	// ============================================================================================

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas, slughorn::KeyIterator());
	canvas.setTolerance(slughorn::TOLERANCE_BALANCED);

	// ============================================================================================
	// Clock face
	// ============================================================================================

	canvas.circle(CX, CY, FACE_R);
	canvas.fill(
		{0.95_cv, 0.92_cv, 0.82_cv, 1_cv}, 1_cv,
		slughorn::Atlas::ShapeInfo::Origin::Type::Centered
	);

	// ============================================================================================
	// Tick marks - 12 strokes baked into one Shape
	// ============================================================================================

	canvas.beginPath();

	for(size_t i = 0; i < 12; i++) {
		canvas.save();
		canvas.translate(CX, CY);
		canvas.rotate(cv(i) * 2_cv * slughorn::PI_CV / 12_cv);
		canvas.moveTo(0_cv, TICK_INNER);
		canvas.lineTo(0_cv, TICK_OUTER);
		canvas.strokePath(
			TICK_WIDTH, false,
			slughorn::canvas::LineJoin::Miter, slughorn::canvas::LineCap::Square
		);
		canvas.restore();
	}

	canvas.fill(
		{0.15_cv, 0.15_cv, 0.15_cv, 1_cv}, 1_cv,
		slughorn::Atlas::ShapeInfo::Origin::Type::Centered
	);

	// ============================================================================================
	// Hour numbers 1-12. canvas.text() is pre-build safe: reads getShape() for advance only.
	// Y-up canvas: y = CY + r*cos(angle) places 12 at the top.
	// ============================================================================================

	auto font = osgx::make_ref<osgSlug::Font>("font/Orbitron-VariableFont_wght.ttf", atlas);

	if(!font->load()) return 1;

	for(int i = 1; i <= 12; i++) {
		const double angle = (i % 12) / 12.0 * 2.0 * M_PI;
		const slug_t nx = CX + cv(std::sin(angle)) * NUM_R;
		const slug_t ny = CY + cv(std::cos(angle)) * NUM_R;

		canvas.text(
			std::to_string(i), FONT_SIZE, nx, ny,
			{0.12_cv, 0.12_cv, 0.18_cv, 1_cv},
			font->metrics(),
			slughorn::canvas::TextAnchorY::CapCenter,
			slughorn::canvas::TextAlignX::Center
		);
	}

	// ============================================================================================
	// Hands - capture layer index before each stroke so we can patch effectId after finalize.
	// Numbers are already accumulated above, so hands naturally render on top.
	// ============================================================================================

	canvas.beginPath();
	canvas.moveTo(CX, CY);
	canvas.lineTo(CX, CY + HOUR_LENGTH);
	canvas.stroke(HOUR_WIDTH, {0.12_cv, 0.12_cv, 0.18_cv, 1_cv}, 1_cv,
		"clock_hour_hand", slughorn::Atlas::ShapeInfo::Origin(CX, CY),
		slughorn::canvas::LineJoin::Miter, slughorn::canvas::LineCap::Round);

	canvas.beginPath();
	canvas.moveTo(CX, CY);
	canvas.lineTo(CX, CY + MIN_LENGTH);
	canvas.stroke(MIN_WIDTH, {0.12_cv, 0.12_cv, 0.18_cv, 1_cv}, 1_cv,
		"clock_minute_hand", slughorn::Atlas::ShapeInfo::Origin(CX, CY),
		slughorn::canvas::LineJoin::Miter, slughorn::canvas::LineCap::Round);

	canvas.beginPath();
	canvas.moveTo(CX, CY);
	canvas.lineTo(CX, CY + SEC_LENGTH);
	canvas.stroke(SEC_WIDTH, {0.85_cv, 0.15_cv, 0.10_cv, 1_cv}, 1_cv,
		"clock_second_hand", slughorn::Atlas::ShapeInfo::Origin(CX, CY));

	// ============================================================================================
	// Build, finalize the single CompositeShape, then patch effectIds by name.
	// ============================================================================================

	atlas->build();
	atlas->packTextures();

	auto clock = canvas.finalize();

	clock.layer("clock_hour_hand").effectId = 3;
	clock.layer("clock_minute_hand").effectId = 2;
	clock.layer("clock_second_hand").effectId = 1;

	// ============================================================================================
	// Render
	// ============================================================================================

	auto sd = example::makeShapeDrawable();
	auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::VertexHook, VERT_SHADER}});

	sd->setStateSet(ss);
	sd->addCompositeShape(clock);

	atlas->setUpdateCallback(new ClockCallback(ss));

	// compile() fires automatically inside addChild() because the atlas is already Packed.
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
