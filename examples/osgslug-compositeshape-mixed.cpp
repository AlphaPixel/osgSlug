//vimrun! ./osgslug-compositeshape-mixed --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

static const std::string VERT_SHADER = R"(
#version 430 core

#pragma osgSlug lib_vertex

vec3 osgSlug_Vertex(osgSlug_VertexData data) {
	if(data.effectId >= 1 && data.effectId <= 6) {
		// effectId encodes the per-stalk phase via a lookup; effectParam carries the
		// stalk's world-space root Y so branches anchor to the same cantilever origin.
		const float phases[6] = float[](0.0, 0.7, 1.4, 2.1, 2.8, 3.5);
		data.pos.x += sin(data.time * 1.2 + phases[data.effectId - 1]) * 0.08 * (data.pos.y - data.effectParam);
	}

	return data.pos;
}
)";

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Claude Axolotl Canvas Demo")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

	// Flip Y to match OSG (OpenGL) convention vs. SVG top-down coordinates
	canvas.translate(0_cv, 520_cv);
	canvas.scale(1_cv, -1_cv);

	// ── CARD BACKGROUND (own CompositeShape) ──────────────────────────────────

	canvas
		.beginPath()
		.roundedRect(2_cv, 2_cv, 356_cv, 516_cv, 18_cv)
		.fill({0.11_cv, 0.10_cv, 0.18_cv, 1.0_cv})
	;

	// Top region gradient overlay — rect is SVG y=2..250 (top half).
	// Gradient coords must be in POST-CTM (atlas/Y-up) space: SVG y → atlas y = 520-y.
	// SVG top (y=2) → atlas y=518. SVG dividing line (y=250) → atlas y=270.
	auto topGrad = canvas.createLinearGradient(
		180_cv, 518_cv,
		180_cv, 270_cv,
		{
			slughorn::GradientStop{0.0_cv, {0.08_cv, 0.22_cv, 0.58_cv, 0.7_cv}},
			slughorn::GradientStop{1.0_cv, {0.08_cv, 0.22_cv, 0.58_cv, 0.0_cv}}
		}
	);

	canvas
		.beginPath()
		.roundedRect(2_cv, 2_cv, 356_cv, 248_cv, 18_cv)
		.fillGradient(topGrad)
	;

	canvas
		.beginPath()
		.roundedRect(2_cv, 2_cv, 356_cv, 516_cv, 18_cv)
		.stroke(8_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.7_cv})
	;

	canvas
		.beginPath()
		.moveTo(20_cv,  250_cv)
		.lineTo(340_cv, 250_cv)
		.stroke(1.5_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.5_cv})
	;

	canvas.finalize("cardShape");

	// ── AXOLOTL (own CompositeShape) ──────────────────────────────────────────

	canvas
		.beginPath()
		.moveTo(180_cv, 128_cv)
		.bezierTo(216_cv, 122_cv, 250_cv, 134_cv, 252_cv, 154_cv)
		.bezierTo(254_cv, 172_cv, 239_cv, 192_cv, 226_cv, 200_cv)
		.bezierTo(211_cv, 209_cv, 196_cv, 212_cv, 180_cv, 212_cv)
		.bezierTo(164_cv, 212_cv, 149_cv, 209_cv, 134_cv, 200_cv)
		.bezierTo(121_cv, 192_cv, 106_cv, 172_cv, 108_cv, 154_cv)
		.bezierTo(110_cv, 134_cv, 144_cv, 122_cv, 180_cv, 128_cv)
		.closePath()
		.fill({0.97_cv, 0.95_cv, 0.91_cv, 1.0_cv})
	;

	// Left eye — outer ring, iris, pupil
	canvas.beginPath().ellipse(156_cv, 150_cv, 10_cv,  10_cv ).fill({0.08_cv, 0.16_cv, 0.37_cv, 1.0_cv});
	canvas.beginPath().ellipse(156_cv, 150_cv, 7.5_cv, 7.5_cv).fill({0.18_cv, 0.35_cv, 0.66_cv, 1.0_cv});
	canvas.beginPath().ellipse(156_cv, 150_cv, 4_cv,   4_cv  ).fill({0.05_cv, 0.12_cv, 0.28_cv, 1.0_cv});

	// Right eye — outer ring, iris, pupil
	canvas.beginPath().ellipse(204_cv, 150_cv, 10_cv,  10_cv ).fill({0.08_cv, 0.16_cv, 0.37_cv, 1.0_cv});
	canvas.beginPath().ellipse(204_cv, 150_cv, 7.5_cv, 7.5_cv).fill({0.18_cv, 0.35_cv, 0.66_cv, 1.0_cv});
	canvas.beginPath().ellipse(204_cv, 150_cv, 4_cv,   4_cv  ).fill({0.05_cv, 0.12_cv, 0.28_cv, 1.0_cv});

	// Eye highlights
	canvas.beginPath().ellipse(153_cv, 147_cv, 2.2_cv, 2.2_cv).fill({1.0_cv, 1.0_cv, 1.0_cv, 0.9_cv});
	canvas.beginPath().ellipse(158_cv, 153_cv, 1.0_cv, 1.0_cv).fill({1.0_cv, 1.0_cv, 1.0_cv, 0.4_cv});
	canvas.beginPath().ellipse(201_cv, 147_cv, 2.2_cv, 2.2_cv).fill({1.0_cv, 1.0_cv, 1.0_cv, 0.9_cv});
	canvas.beginPath().ellipse(206_cv, 153_cv, 1.0_cv, 1.0_cv).fill({1.0_cv, 1.0_cv, 1.0_cv, 0.4_cv});

	// Nose + smile
	const slughorn::Color face = {0.72_cv, 0.60_cv, 0.47_cv, 1.0_cv};

	canvas.beginPath().ellipse(175_cv, 160_cv, 1.6_cv, 1.6_cv).fill(face);
	canvas.beginPath().ellipse(185_cv, 160_cv, 1.6_cv, 1.6_cv).fill(face);

	canvas
		.beginPath()
		.moveTo(168_cv, 168_cv)
		.bezierTo(174_cv, 174_cv, 186_cv, 174_cv, 192_cv, 168_cv)
		.stroke(1.8_cv, face)
	;

	// ── GILL PLUMES ────────────────────────────────────────────────────────────
	const slughorn::Color gillMain   = {0.85_cv, 0.41_cv, 0.53_cv, 1.0_cv};
	const slughorn::Color gillBranch = {0.95_cv, 0.63_cv, 0.74_cv, 1.0_cv};

	// Left outer stalk + branches
	canvas
		.beginPath()
		.moveTo(130_cv, 152_cv)
		.bezierTo(116_cv, 142_cv, 101_cv, 122_cv, 90_cv, 96_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_l0")
	;

	canvas.beginPath().moveTo(115_cv, 133_cv).bezierTo(109_cv, 125_cv, 104_cv, 118_cv, 101_cv, 110_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l0_b0");
	canvas.beginPath().moveTo(107_cv, 120_cv).bezierTo(101_cv, 113_cv,  97_cv, 107_cv,  96_cv, 100_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l0_b1");
	canvas.beginPath().moveTo( 99_cv, 106_cv).bezierTo( 94_cv, 100_cv,  91_cv,  95_cv,  90_cv,  90_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l0_b2");

	// Left middle stalk + branches
	canvas
		.beginPath()
		.moveTo(140_cv, 141_cv)
		.bezierTo(132_cv, 127_cv, 127_cv, 106_cv, 124_cv, 78_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_l1")
	;

	canvas.beginPath().moveTo(135_cv, 124_cv).bezierTo(128_cv, 117_cv, 124_cv, 111_cv, 122_cv, 105_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l1_b0");
	canvas.beginPath().moveTo(130_cv, 109_cv).bezierTo(124_cv, 103_cv, 121_cv,  97_cv, 120_cv,  91_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l1_b1");
	canvas.beginPath().moveTo(125_cv,  93_cv).bezierTo(120_cv,  87_cv, 118_cv,  82_cv, 117_cv,  77_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l1_b2");

	// Left inner stalk + branches
	canvas
		.beginPath()
		.moveTo(152_cv, 134_cv)
		.bezierTo(149_cv, 119_cv, 148_cv, 101_cv, 151_cv, 74_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_l2")
	;

	canvas.beginPath().moveTo(151_cv, 117_cv).bezierTo(146_cv, 111_cv, 143_cv, 105_cv, 142_cv,  99_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l2_b0");
	canvas.beginPath().moveTo(150_cv, 101_cv).bezierTo(146_cv,  95_cv, 144_cv,  89_cv, 143_cv,  83_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l2_b1");
	canvas.beginPath().moveTo(151_cv,  85_cv).bezierTo(148_cv,  79_cv, 147_cv,  73_cv, 147_cv,  67_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_l2_b2");

	// Right outer stalk + branches
	canvas
		.beginPath()
		.moveTo(230_cv, 152_cv)
		.bezierTo(244_cv, 142_cv, 259_cv, 122_cv, 270_cv, 96_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_r0")
	;

	canvas.beginPath().moveTo(245_cv, 133_cv).bezierTo(251_cv, 125_cv, 256_cv, 118_cv, 259_cv, 110_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r0_b0");
	canvas.beginPath().moveTo(253_cv, 120_cv).bezierTo(259_cv, 113_cv, 263_cv, 107_cv, 264_cv, 100_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r0_b1");
	canvas.beginPath().moveTo(261_cv, 106_cv).bezierTo(266_cv, 100_cv, 269_cv,  95_cv, 270_cv,  90_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r0_b2");

	// Right middle stalk + branches
	canvas
		.beginPath()
		.moveTo(220_cv, 141_cv)
		.bezierTo(228_cv, 127_cv, 233_cv, 106_cv, 236_cv, 78_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_r1")
	;

	canvas.beginPath().moveTo(225_cv, 124_cv).bezierTo(232_cv, 117_cv, 236_cv, 111_cv, 238_cv, 105_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r1_b0");
	canvas.beginPath().moveTo(230_cv, 109_cv).bezierTo(236_cv, 103_cv, 239_cv,  97_cv, 240_cv,  91_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r1_b1");
	canvas.beginPath().moveTo(235_cv,  93_cv).bezierTo(240_cv,  87_cv, 242_cv,  82_cv, 243_cv,  77_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r1_b2");

	// Right inner stalk + branches
	canvas
		.beginPath()
		.moveTo(208_cv, 134_cv)
		.bezierTo(211_cv, 119_cv, 212_cv, 101_cv, 209_cv, 74_cv)
		.stroke(2.8_cv, gillMain, 1_cv, "gill_r2")
	;

	canvas.beginPath().moveTo(209_cv, 117_cv).bezierTo(214_cv, 111_cv, 217_cv, 105_cv, 218_cv,  99_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r2_b0");
	canvas.beginPath().moveTo(210_cv, 101_cv).bezierTo(214_cv,  95_cv, 216_cv,  89_cv, 217_cv,  83_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r2_b1");
	canvas.beginPath().moveTo(209_cv,  85_cv).bezierTo(212_cv,  79_cv, 213_cv,  73_cv, 213_cv,  67_cv).stroke(1.4_cv, gillBranch, 1_cv, "gill_r2_b2");

	// ── ARMS & LEGS ────────────────────────────────────────────────────────────
	const slughorn::Color limb = {0.93_cv, 0.88_cv, 0.81_cv, 1.0_cv};

	canvas
		.beginPath()
		.moveTo(122_cv, 174_cv)
		.bezierTo(106_cv, 178_cv,  90_cv, 182_cv,  76_cv, 192_cv)
		.bezierTo( 68_cv, 198_cv,  64_cv, 206_cv,  66_cv, 212_cv)
		.stroke(9_cv, limb)
	;

	canvas.beginPath().moveTo(66_cv, 212_cv).bezierTo(60_cv, 219_cv, 55_cv, 224_cv, 51_cv, 227_cv).stroke(4_cv, limb);
	canvas.beginPath().moveTo(66_cv, 212_cv).bezierTo(64_cv, 220_cv, 63_cv, 226_cv, 62_cv, 231_cv).stroke(4_cv, limb);
	canvas.beginPath().moveTo(66_cv, 212_cv).bezierTo(70_cv, 219_cv, 72_cv, 225_cv, 73_cv, 230_cv).stroke(4_cv, limb);

	canvas
		.beginPath()
		.moveTo(238_cv, 174_cv)
		.bezierTo(254_cv, 178_cv, 270_cv, 182_cv, 284_cv, 192_cv)
		.bezierTo(292_cv, 198_cv, 296_cv, 206_cv, 294_cv, 212_cv)
		.stroke(9_cv, limb)
	;

	canvas.beginPath().moveTo(294_cv, 212_cv).bezierTo(300_cv, 219_cv, 305_cv, 224_cv, 309_cv, 227_cv).stroke(4_cv, limb);
	canvas.beginPath().moveTo(294_cv, 212_cv).bezierTo(296_cv, 220_cv, 297_cv, 226_cv, 298_cv, 231_cv).stroke(4_cv, limb);
	canvas.beginPath().moveTo(294_cv, 212_cv).bezierTo(290_cv, 219_cv, 288_cv, 225_cv, 287_cv, 230_cv).stroke(4_cv, limb);

	canvas
		.beginPath()
		.moveTo(138_cv, 203_cv)
		.bezierTo(125_cv, 213_cv, 114_cv, 222_cv, 108_cv, 233_cv)
		.stroke(7_cv, limb)
	;

	canvas
		.beginPath()
		.moveTo(222_cv, 203_cv)
		.bezierTo(235_cv, 213_cv, 246_cv, 222_cv, 252_cv, 233_cv)
		.stroke(7_cv, limb)
	;

	auto axo = canvas.finalize();

	// effectId 1–6 encodes per-stalk phase (shader array); effectParam will carry
	// the stalk's world-space root Y so all 4 layers in a group share one anchor.
	const struct { uint32_t id; const char* keys[4]; } groups[] = {
		{1, {"gill_l0", "gill_l0_b0", "gill_l0_b1", "gill_l0_b2"}},
		{2, {"gill_l1", "gill_l1_b0", "gill_l1_b1", "gill_l1_b2"}},
		{3, {"gill_l2", "gill_l2_b0", "gill_l2_b1", "gill_l2_b2"}},
		{4, {"gill_r0", "gill_r0_b0", "gill_r0_b1", "gill_r0_b2"}},
		{5, {"gill_r1", "gill_r1_b0", "gill_r1_b1", "gill_r1_b2"}},
		{6, {"gill_r2", "gill_r2_b0", "gill_r2_b1", "gill_r2_b2"}},
	};

	for(const auto& g : groups) {
		for(const char* k : g.keys) axo.layer(k).effectId = g.id;
	}

	auto font = osgx::make_ref<osgSlug::Font>(
		"font/EB_Garamond/EBGaramond-VariableFont_wght.ttf",
		atlas
	);

	if(!font->load()) return 1;

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);

	// Place "AXO" just below the dividing line; SVG y=305 → atlas y=215.
	canvas.text(
		"AXO", 70_cv, 180_cv, 305_cv,
		{1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv},
		font->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas.finalize("text");

	sd->addCompositeShape(*atlas->getCompositeShape("cardShape"));
	sd->addCompositeShape(axo);
	sd->addCompositeShape(*atlas->getCompositeShape("text"));

	sd->compile();

	// Root Y in atlas space (520 - SVG_base_y) anchors branch displacement to the
	// same world-space origin as the parent stalk: l0/r0→368, l1/r1→379, l2/r2→386.
	{
		const float rootYs[] = {368.0f, 379.0f, 386.0f, 368.0f, 379.0f, 386.0f};
		constexpr size_t GILL_START = 4 + 14;

		for(size_t grp = 0; grp < 6; grp++)
			for(size_t b = 0; b < 4; b++) sd->setLayerEffectParam(GILL_START + grp * 4 + b, rootYs[grp]);
	}

	// ── SECOND ATLAS: italic flavor text ─────────────────────────────────────
	// Separate Atlas so the italic glyphs don't collide with the regular ones
	// already loaded above. Atlas state is merged into each drawable's StateSet
	// (which compile() already populated) rather than replacing it.

	auto atlas2 = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas2(*atlas2);

	canvas2.translate(0_cv, 520_cv);
	canvas2.scale(1_cv, -1_cv);

	// Flavor text frame — SVG y=330..475.
	canvas2
		.beginPath()
		.roundedRect(22_cv, 330_cv, 316_cv, 145_cv, 8_cv)
		.fill({0.06_cv, 0.06_cv, 0.12_cv, 0.75_cv})
	;

	canvas2
		.beginPath()
		.roundedRect(22_cv, 330_cv, 316_cv, 145_cv, 8_cv)
		.stroke(1.5_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.5_cv})
	;

	// canvas2.finalize("flavorBox");
	auto fb = canvas2.finalize();

	auto fontItalic = osgx::make_ref<osgSlug::Font>(
		"font/EB_Garamond/EBGaramond-Italic-VariableFont_wght.ttf",
		atlas2
	);

	fontItalic->load();
	atlas2->build();
	atlas2->packTextures();

	const slughorn::Color flavorColor = {0.92_cv, 0.88_cv, 0.78_cv, 1.0_cv};

	canvas2.text(
		"We hacked off every limb.",
		22_cv, 180_cv, 375_cv, flavorColor,
		fontItalic->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas2.text(
		"We ran out of swords first.",
		22_cv, 180_cv, 410_cv, flavorColor,
		fontItalic->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	// canvas2.finalize("flavor");
	auto f = canvas2.finalize();

	auto sdtext = example::makeShapeDrawable();

	sdtext->setAtlas(atlas2);
	// sdtext->addCompositeShape(*atlas2->getCompositeShape("flavorBox"));
	//sdtext->addCompositeShape(*atlas2->getCompositeShape("flavor"));
	sdtext->addCompositeShape(fb);
	sdtext->addCompositeShape(f);
	sdtext->compile();

	// compile() writes program + layer SSBO (binding 1); merge() adds the atlas
	// shape SSBO (binding 0) + textures + uniforms without clobbering binding 1.
	sd->getOrCreateStateSet()->merge(*atlas->createDefaultStateSet(example::USE_GL3, {{osgSlug::Atlas::VertexHook, VERT_SHADER}}));
	sd->getOrCreateStateSet()->setRenderBinDetails(0, "RenderBin");
	sdtext->getOrCreateStateSet()->merge(*atlas2->createDefaultStateSet(example::USE_GL3));
	sdtext->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->addDrawable(sdtext);

	return example::run(viewer, args, sdg);
}
