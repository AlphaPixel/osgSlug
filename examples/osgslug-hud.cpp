//vimrun! ./osgslug-hud --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"
#include "osgSlug/Font.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

const static std::string VERT_EFFECTS = R"(
#version 430 core

#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex(
	vec3 pos,
	vec2 emCoord,
	vec2 uv,
	int effectId,
	vec2 origin,
	float effectParam,
	float time
) {
	if(effectId == 1) return osgSlug_Vertex_Rotate(pos, emCoord, origin, effectParam * time);
	if(effectId == 2) return osgSlug_Vertex_Scale(pos, emCoord, origin, 1.0 + 0.18 * sin(effectParam * time));

	return pos;
}
)";

struct Ring {
	slug_t radius;
	slug_t width;
	slug_t speed; // radians/second; sign = CCW (+) or CW (-)
};

static constexpr Ring RINGS[] = {
	{0.42_cv, 0.010_cv,  0.48_cv},
	{0.26_cv, 0.014_cv, -1.20_cv},
	{0.10_cv, 0.020_cv,  2.00_cv},
};

static constexpr size_t NUM_RINGS = sizeof(RINGS) / sizeof(RINGS[0]);
static constexpr slug_t DOT_PULSE_SPEED = 1.8_cv;
static constexpr slug_t SWEEP_SPEED    = -0.65_cv; // CW, ~10 seconds/revolution

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "HUD Demo")) return 0;

	const slug_t CX = 0.5_cv, CY = 0.5_cv;
	const slug_t GAP = slughorn::PI_CV / 6_cv;

	using Origin = slughorn::Atlas::ShapeInfo::Origin;

	const slughorn::Color C_RING    = {0.60_cv, 0.85_cv, 1.00_cv, 1.00_cv};
	const slughorn::Color C_STRUCT  = {0.60_cv, 0.85_cv, 1.00_cv, 0.30_cv};
	const slughorn::Color C_GHOST   = {0.60_cv, 0.85_cv, 1.00_cv, 0.12_cv};
	const slughorn::Color C_TICK_MJ = {0.60_cv, 0.85_cv, 1.00_cv, 0.55_cv};
	const slughorn::Color C_TICK_MN = {0.60_cv, 0.85_cv, 1.00_cv, 0.25_cv};
	const slughorn::Color C_DOT     = {0.85_cv, 0.95_cv, 1.00_cv, 0.95_cv};
	const slughorn::Color C_LABEL   = {0.60_cv, 0.85_cv, 1.00_cv, 0.50_cv};

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas, slughorn::KeyIterator());
	canvas.setTolerance(slughorn::TOLERANCE_BALANCED);

	// Ghost reference circle — sweep gradient gives the radar-trail appearance.
	// atan2 returns [-π, +π], so startAngle=-π / endAngle=+π maps t cleanly to [0,1].
	// t=0.0/1.0 = left (9 o'clock), t=0.25 = bottom, t=0.50 = right, t=0.75 = top.
	// Bright at t=0 (left), fading through bottom→right, dim toward top and back to seam.
	auto radarTrail = canvas.createSweepGradient(
		CX, CY, -slughorn::PI_CV, slughorn::PI_CV,
		{
			{0.00_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.75_cv}},
			{0.25_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.35_cv}},
			{0.55_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.05_cv}},
			{0.85_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.03_cv}},
			{1.00_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.75_cv}},
		}
	);

	canvas.beginPath();
	canvas.arc(CX, CY, 0.48_cv, 0_cv, 2_cv * slughorn::PI_CV);
	canvas.strokeGradient(0.010_cv, radarTrail, 1_cv, slughorn::Key("ghost_circle"));

	// Crosshair
	canvas.beginPath()
		.moveTo(0.06_cv, CY).lineTo(0.94_cv, CY)
		.moveTo(CX, 0.06_cv).lineTo(CX, 0.94_cv);
	canvas.stroke(0.0020_cv, C_GHOST, 1_cv, slughorn::Key("crosshair"));

	// Minor tick marks (every 15°, between the major marks)
	canvas.beginPath();

	for(int i = 0; i < 24; i++) {
		if(i % 2 == 0) continue;

		const slug_t a  = slughorn::cv(i) * slughorn::PI_CV / 12_cv;
		const slug_t ca = std::cos(a), sa = std::sin(a);

		canvas.moveTo(CX + 0.446_cv * ca, CY + 0.446_cv * sa)
		      .lineTo(CX + 0.456_cv * ca, CY + 0.456_cv * sa);
	}

	canvas.stroke(0.0025_cv, C_TICK_MN, 1_cv, slughorn::Key("ticks_minor"));

	// Major tick marks (every 30°, 12 total)
	canvas.beginPath();

	for(int i = 0; i < 12; i++) {
		const slug_t a  = slughorn::cv(i) * slughorn::PI_CV / 6_cv;
		const slug_t ca = std::cos(a), sa = std::sin(a);

		canvas.moveTo(CX + 0.443_cv * ca, CY + 0.443_cv * sa)
		      .lineTo(CX + 0.463_cv * ca, CY + 0.463_cv * sa);
	}

	canvas.stroke(0.003_cv, C_TICK_MJ, 1_cv, slughorn::Key("ticks_major"));

	// Corner brackets — 4 L-shapes as one multi-subpath stroke
	const slug_t BM = 0.05_cv, BL = 0.07_cv;

	canvas.beginPath()
		.moveTo(BM + BL, BM)               .lineTo(BM, BM)               .lineTo(BM, BM + BL)
		.moveTo(1_cv - BM - BL, BM)        .lineTo(1_cv - BM, BM)        .lineTo(1_cv - BM, BM + BL)
		.moveTo(1_cv - BM, 1_cv - BM - BL) .lineTo(1_cv - BM, 1_cv - BM) .lineTo(1_cv - BM - BL, 1_cv - BM)
		.moveTo(BM, 1_cv - BM - BL)        .lineTo(BM, 1_cv - BM)        .lineTo(BM + BL, 1_cv - BM);

	canvas.stroke(0.004_cv, C_STRUCT, 1_cv, slughorn::Key("brackets"));

	// Center accent ring — frames the dot
	canvas.beginPath();
	canvas.arc(CX, CY, 0.055_cv, 0_cv, 2_cv * slughorn::PI_CV);
	canvas.stroke(0.003_cv, C_STRUCT, 1_cv, slughorn::Key("center_ring"));

	// Rotating radar sweep needle — thin line from just outside the center ring to the ghost circle
	const size_t SWEEP_IDX = canvas.layerCount();

	canvas.beginPath()
		.moveTo(CX + 0.07_cv, CY)
		.lineTo(CX + 0.475_cv, CY);
	canvas.stroke(0.0018_cv, {0.60_cv, 0.85_cv, 1.00_cv, 0.60_cv}, 1_cv,
		slughorn::Key("sweep_needle"), Origin(CX, CY));

	// Degree labels at the 12 major tick positions.
	// Clockwise from north: i=3 (top in Y-up) → 0°, then decreasing i wraps CW.
	// Formula: ((3 - i + 12) % 12) * 30
	auto font = osgx::make_ref<osgSlug::Font>("font/Silkscreen-Regular.ttf", atlas);

	if(!font->load()) return 1;

	for(int i = 0; i < 12; i++) {
		const slug_t a   = slughorn::cv(i) * slughorn::PI_CV / 6_cv;
		const slug_t lx  = CX + 0.505_cv * std::cos(a);
		const slug_t ly  = CY + 0.505_cv * std::sin(a);
		const int    deg = ((3 - i + 12) % 12) * 30;

		canvas.text(
			std::to_string(deg),
			0.032_cv, lx, ly,
			C_LABEL,
			font->metrics(),
			slughorn::canvas::TextAnchorY::CapCenter,
			slughorn::canvas::TextAlignX::Center
		);
	}

	// Spinning rings — Pivot(CX, CY) keeps rotation centered on the geometric circle center
	// regardless of bbox asymmetry caused by the gap.
	const size_t RING_START = canvas.layerCount();

	for(size_t i = 0; i < NUM_RINGS; i++) {
		canvas.beginPath();
		canvas.arc(CX, CY, RINGS[i].radius, GAP / 2_cv, 2_cv * slughorn::PI_CV - GAP / 2_cv);
		canvas.stroke(RINGS[i].width, C_RING, 1_cv, slughorn::Key("ring_" + std::to_string(i)), Origin(CX, CY));
	}

	// Pulsing center dot
	const size_t DOT_IDX = canvas.layerCount();

	canvas.beginPath();
	canvas.circle(CX, CY, 0.022_cv);
	canvas.fill(C_DOT, 1_cv, slughorn::Key("center_dot"), Origin(CX, CY));

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	auto hud = canvas.finalize();

	// Patch animated effectIds by name
	hud.layer(slughorn::Key("sweep_needle")).effectId = 1;

	for(size_t i = 0; i < NUM_RINGS; i++) hud.layer(slughorn::Key("ring_" + std::to_string(i))).effectId = 1;

	hud.layer(slughorn::Key("center_dot")).effectId = 2;

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	sd->addCompositeShape(hud);
	sd->compile();

	sd->setLayerEffectParam(SWEEP_IDX, SWEEP_SPEED);

	for(size_t i = 0; i < NUM_RINGS; i++) sd->setLayerEffectParam(RING_START + i, RINGS[i].speed);

	sd->setLayerEffectParam(DOT_IDX, DOT_PULSE_SPEED);

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3, {{osgSlug::Atlas::VertexHook, VERT_EFFECTS}}));

	return example::run(viewer, args, sdg);
}
