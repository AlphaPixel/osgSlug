//vimrun! ./osgslug-compositeshape-mixed --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"

#include "slughorn/canvas.hpp"
#include "slughorn/serial.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Claude Axolotl Canvas Demo")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

	// Flip Y to match OSG (OpenGL) convention vs. SVG top-down coordinates
	canvas.translate(0_cv, 520_cv);
	canvas.scale(1_cv, -1_cv);
	// canvas.scale(1_cv / 516_cv, 1_cv / 516_cv);

	// ── CARD BACKGROUND (own CompositeShape) ──────────────────────────────────

	// Base fill — dark navy
	canvas.beginPath();
	canvas.roundedRect(2_cv, 2_cv, 356_cv, 516_cv, 18_cv);
	canvas.fill({0.11_cv, 0.10_cv, 0.18_cv, 1.0_cv});

	// Top region gradient overlay — rect is SVG y=2..250 (top half).
	// Gradient coords must be in POST-CTM (atlas/Y-up) space: SVG y → atlas y = 520-y.
	// SVG top (y=2) → atlas y=518. SVG dividing line (y=250) → atlas y=270.
	auto topGrad = canvas.createLinearGradient(
		180_cv, 518_cv,   // atlas: top of card
		180_cv, 270_cv,   // atlas: dividing line
		{
			slughorn::GradientStop{0.0_cv, {0.08_cv, 0.22_cv, 0.58_cv, 0.7_cv}},
			slughorn::GradientStop{1.0_cv, {0.08_cv, 0.22_cv, 0.58_cv, 0.0_cv}}
		}
	);

	canvas.beginPath();
	canvas.roundedRect(2_cv, 2_cv, 356_cv, 248_cv, 18_cv);
	canvas.fillGradient(topGrad);

	// Gold border — single stroke around full card
	canvas.beginPath();
	canvas.roundedRect(2_cv, 2_cv, 356_cv, 516_cv, 18_cv);
	canvas.stroke(8_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.7_cv});

	// Upper/lower dividing line
	canvas.beginPath();
	canvas.moveTo(20_cv,  250_cv);
	canvas.lineTo(340_cv, 250_cv);
	canvas.stroke(1.5_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.5_cv});

	// auto cardShape = canvas.finalize();
	canvas.finalize("cardShape");

	// ── AXOLOTL (own CompositeShape) ──────────────────────────────────────────

	// Axolotl body — cream/white bezier
	canvas.beginPath();
	canvas.moveTo(180_cv, 128_cv);
	canvas.bezierTo(216_cv, 122_cv, 250_cv, 134_cv, 252_cv, 154_cv);
	canvas.bezierTo(254_cv, 172_cv, 239_cv, 192_cv, 226_cv, 200_cv);
	canvas.bezierTo(211_cv, 209_cv, 196_cv, 212_cv, 180_cv, 212_cv);
	canvas.bezierTo(164_cv, 212_cv, 149_cv, 209_cv, 134_cv, 200_cv);
	canvas.bezierTo(121_cv, 192_cv, 106_cv, 172_cv, 108_cv, 154_cv);
	canvas.bezierTo(110_cv, 134_cv, 144_cv, 122_cv, 180_cv, 128_cv);
	canvas.closePath();
	canvas.fill({0.97_cv, 0.95_cv, 0.91_cv, 1.0_cv});

	// Left eye — outer ring, iris, pupil
	canvas.beginPath();
	canvas.ellipse(156_cv, 150_cv, 10_cv, 10_cv);
	canvas.fill({0.08_cv, 0.16_cv, 0.37_cv, 1.0_cv});

	canvas.beginPath();
	canvas.ellipse(156_cv, 150_cv, 7.5_cv, 7.5_cv);
	canvas.fill({0.18_cv, 0.35_cv, 0.66_cv, 1.0_cv});

	canvas.beginPath();
	canvas.ellipse(156_cv, 150_cv, 4_cv, 4_cv);
	canvas.fill({0.05_cv, 0.12_cv, 0.28_cv, 1.0_cv});

	// Right eye — outer ring, iris, pupil
	canvas.beginPath();
	canvas.ellipse(204_cv, 150_cv, 10_cv, 10_cv);
	canvas.fill({0.08_cv, 0.16_cv, 0.37_cv, 1.0_cv});

	canvas.beginPath();
	canvas.ellipse(204_cv, 150_cv, 7.5_cv, 7.5_cv);
	canvas.fill({0.18_cv, 0.35_cv, 0.66_cv, 1.0_cv});

	canvas.beginPath();
	canvas.ellipse(204_cv, 150_cv, 4_cv, 4_cv);
	canvas.fill({0.05_cv, 0.12_cv, 0.28_cv, 1.0_cv});

	// Eye highlights
	canvas.beginPath(); canvas.ellipse(153_cv, 147_cv, 2.2_cv, 2.2_cv); canvas.fill({1.0_cv, 1.0_cv, 1.0_cv, 0.9_cv});
	canvas.beginPath(); canvas.ellipse(158_cv, 153_cv, 1.0_cv, 1.0_cv); canvas.fill({1.0_cv, 1.0_cv, 1.0_cv, 0.4_cv});
	canvas.beginPath(); canvas.ellipse(201_cv, 147_cv, 2.2_cv, 2.2_cv); canvas.fill({1.0_cv, 1.0_cv, 1.0_cv, 0.9_cv});
	canvas.beginPath(); canvas.ellipse(206_cv, 153_cv, 1.0_cv, 1.0_cv); canvas.fill({1.0_cv, 1.0_cv, 1.0_cv, 0.4_cv});

	// Nose + smile
	const slughorn::Color face = {0.72_cv, 0.60_cv, 0.47_cv, 1.0_cv};

	canvas.beginPath(); canvas.ellipse(175_cv, 160_cv, 1.6_cv, 1.6_cv); canvas.fill(face);
	canvas.beginPath(); canvas.ellipse(185_cv, 160_cv, 1.6_cv, 1.6_cv); canvas.fill(face);

	canvas.beginPath();
	canvas.moveTo(168_cv, 168_cv);
	canvas.bezierTo(174_cv, 174_cv, 186_cv, 174_cv, 192_cv, 168_cv);
	canvas.stroke(1.8_cv, face);

	// ── GILL PLUMES ────────────────────────────────────────────────────────────
	// Colors: main stalk (dark pink), branches (light pink)
	const slughorn::Color gillMain   = {0.85_cv, 0.41_cv, 0.53_cv, 1.0_cv};
	const slughorn::Color gillBranch = {0.95_cv, 0.63_cv, 0.74_cv, 1.0_cv};

	// Left outer stalk + branches
	canvas.beginPath();
	canvas.moveTo(130_cv, 152_cv);
	canvas.bezierTo(116_cv, 142_cv, 101_cv, 122_cv, 90_cv, 96_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(115_cv, 133_cv); canvas.bezierTo(109_cv, 125_cv, 104_cv, 118_cv, 101_cv, 110_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(107_cv, 120_cv); canvas.bezierTo(101_cv, 113_cv,  97_cv, 107_cv,  96_cv, 100_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo( 99_cv, 106_cv); canvas.bezierTo( 94_cv, 100_cv,  91_cv,  95_cv,  90_cv,  90_cv); canvas.stroke(1.4_cv, gillBranch);

	// Left middle stalk + branches
	canvas.beginPath();
	canvas.moveTo(140_cv, 141_cv);
	canvas.bezierTo(132_cv, 127_cv, 127_cv, 106_cv, 124_cv, 78_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(135_cv, 124_cv); canvas.bezierTo(128_cv, 117_cv, 124_cv, 111_cv, 122_cv, 105_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(130_cv, 109_cv); canvas.bezierTo(124_cv, 103_cv, 121_cv,  97_cv, 120_cv,  91_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(125_cv,  93_cv); canvas.bezierTo(120_cv,  87_cv, 118_cv,  82_cv, 117_cv,  77_cv); canvas.stroke(1.4_cv, gillBranch);

	// Left inner stalk + branches
	canvas.beginPath();
	canvas.moveTo(152_cv, 134_cv);
	canvas.bezierTo(149_cv, 119_cv, 148_cv, 101_cv, 151_cv, 74_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(151_cv, 117_cv); canvas.bezierTo(146_cv, 111_cv, 143_cv, 105_cv, 142_cv,  99_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(150_cv, 101_cv); canvas.bezierTo(146_cv,  95_cv, 144_cv,  89_cv, 143_cv,  83_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(151_cv,  85_cv); canvas.bezierTo(148_cv,  79_cv, 147_cv,  73_cv, 147_cv,  67_cv); canvas.stroke(1.4_cv, gillBranch);

	// Right outer stalk + branches
	canvas.beginPath();
	canvas.moveTo(230_cv, 152_cv);
	canvas.bezierTo(244_cv, 142_cv, 259_cv, 122_cv, 270_cv, 96_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(245_cv, 133_cv); canvas.bezierTo(251_cv, 125_cv, 256_cv, 118_cv, 259_cv, 110_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(253_cv, 120_cv); canvas.bezierTo(259_cv, 113_cv, 263_cv, 107_cv, 264_cv, 100_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(261_cv, 106_cv); canvas.bezierTo(266_cv, 100_cv, 269_cv,  95_cv, 270_cv,  90_cv); canvas.stroke(1.4_cv, gillBranch);

	// Right middle stalk + branches
	canvas.beginPath();
	canvas.moveTo(220_cv, 141_cv);
	canvas.bezierTo(228_cv, 127_cv, 233_cv, 106_cv, 236_cv, 78_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(225_cv, 124_cv); canvas.bezierTo(232_cv, 117_cv, 236_cv, 111_cv, 238_cv, 105_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(230_cv, 109_cv); canvas.bezierTo(236_cv, 103_cv, 239_cv,  97_cv, 240_cv,  91_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(235_cv,  93_cv); canvas.bezierTo(240_cv,  87_cv, 242_cv,  82_cv, 243_cv,  77_cv); canvas.stroke(1.4_cv, gillBranch);

	// Right inner stalk + branches
	canvas.beginPath();
	canvas.moveTo(208_cv, 134_cv);
	canvas.bezierTo(211_cv, 119_cv, 212_cv, 101_cv, 209_cv, 74_cv);
	canvas.stroke(2.8_cv, gillMain);

	canvas.beginPath(); canvas.moveTo(209_cv, 117_cv); canvas.bezierTo(214_cv, 111_cv, 217_cv, 105_cv, 218_cv,  99_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(210_cv, 101_cv); canvas.bezierTo(214_cv,  95_cv, 216_cv,  89_cv, 217_cv,  83_cv); canvas.stroke(1.4_cv, gillBranch);
	canvas.beginPath(); canvas.moveTo(209_cv,  85_cv); canvas.bezierTo(212_cv,  79_cv, 213_cv,  73_cv, 213_cv,  67_cv); canvas.stroke(1.4_cv, gillBranch);

	// ── ARMS & LEGS ────────────────────────────────────────────────────────────
	const slughorn::Color limb = {0.93_cv, 0.88_cv, 0.81_cv, 1.0_cv};

	// Front left arm
	canvas.beginPath();
	canvas.moveTo(122_cv, 174_cv);
	canvas.bezierTo(106_cv, 178_cv, 90_cv, 182_cv, 76_cv, 192_cv);
	canvas.bezierTo( 68_cv, 198_cv, 64_cv, 206_cv, 66_cv, 212_cv);
	canvas.stroke(9_cv, limb);

	// Left toes
	canvas.beginPath(); canvas.moveTo(66_cv, 212_cv); canvas.bezierTo(60_cv, 219_cv, 55_cv, 224_cv, 51_cv, 227_cv); canvas.stroke(4_cv, limb);
	canvas.beginPath(); canvas.moveTo(66_cv, 212_cv); canvas.bezierTo(64_cv, 220_cv, 63_cv, 226_cv, 62_cv, 231_cv); canvas.stroke(4_cv, limb);
	canvas.beginPath(); canvas.moveTo(66_cv, 212_cv); canvas.bezierTo(70_cv, 219_cv, 72_cv, 225_cv, 73_cv, 230_cv); canvas.stroke(4_cv, limb);

	// Front right arm
	canvas.beginPath();
	canvas.moveTo(238_cv, 174_cv);
	canvas.bezierTo(254_cv, 178_cv, 270_cv, 182_cv, 284_cv, 192_cv);
	canvas.bezierTo(292_cv, 198_cv, 296_cv, 206_cv, 294_cv, 212_cv);
	canvas.stroke(9_cv, limb);

	// Right toes
	canvas.beginPath(); canvas.moveTo(294_cv, 212_cv); canvas.bezierTo(300_cv, 219_cv, 305_cv, 224_cv, 309_cv, 227_cv); canvas.stroke(4_cv, limb);
	canvas.beginPath(); canvas.moveTo(294_cv, 212_cv); canvas.bezierTo(296_cv, 220_cv, 297_cv, 226_cv, 298_cv, 231_cv); canvas.stroke(4_cv, limb);
	canvas.beginPath(); canvas.moveTo(294_cv, 212_cv); canvas.bezierTo(290_cv, 219_cv, 288_cv, 225_cv, 287_cv, 230_cv); canvas.stroke(4_cv, limb);

	// Back left leg
	canvas.beginPath();
	canvas.moveTo(138_cv, 203_cv);
	canvas.bezierTo(125_cv, 213_cv, 114_cv, 222_cv, 108_cv, 233_cv);
	canvas.stroke(7_cv, limb);

	// Back right leg
	canvas.beginPath();
	canvas.moveTo(222_cv, 203_cv);
	canvas.bezierTo(235_cv, 213_cv, 246_cv, 222_cv, 252_cv, 233_cv);
	canvas.stroke(7_cv, limb);

	// auto compositeShape = canvas.finalize();
	canvas.finalize("axolotl");

	// Load just the three glyphs we need into the same atlas
	// auto font = osgx::make_ref<osgSlug::Font>("UbuntuMono-R.ttf", atlas);
	auto font = osgx::make_ref<osgSlug::Font>(
		"font/EB_Garamond/EBGaramond-VariableFont_wght.ttf",
		atlas
	);

	font->load();

	atlas->build();
	atlas->packTextures();

	slughorn::serial::writeJSON(*atlas, std::cerr);

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	// sd->addCompositeShape(cardShape);
	// sd->addCompositeShape(compositeShape);
	// Place "AXO" just below the dividing line, in SVG canvas coordinates (y-down).
	// SVG y=305 → world y=215 (dividing line is at world y=270).
	const slug_t fontSize = 70_cv;
	const slug_t baseline = 305_cv;

	canvas.text(
		"AXO",
		fontSize,
		180_cv, baseline,
		{1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv},
		font->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas.finalize("text");

	sd->addCompositeShape(*atlas->getCompositeShape("cardShape"));
	sd->addCompositeShape(*atlas->getCompositeShape("axolotl"));
	sd->addCompositeShape(*atlas->getCompositeShape("text"));

	sd->compile();

	// ── SECOND ATLAS: italic flavor text ─────────────────────────────────────
	// Separate Atlas so the italic glyphs don't collide with the regular ones
	// already loaded above. Atlas state is merged into each drawable's StateSet
	// (which compile() already populated) rather than replacing it.

	auto atlas2 = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas2(*atlas2);

	canvas2.translate(0_cv, 520_cv);
	canvas2.scale(1_cv, -1_cv);

	// Flavor text frame — SVG space (y-down); box spans SVG y=330..475.
	canvas2.beginPath();
	canvas2.roundedRect(22_cv, 330_cv, 316_cv, 145_cv, 8_cv);
	canvas2.fill({0.06_cv, 0.06_cv, 0.12_cv, 0.75_cv});

	canvas2.beginPath();
	canvas2.roundedRect(22_cv, 330_cv, 316_cv, 145_cv, 8_cv);
	canvas2.stroke(1.5_cv, {0.85_cv, 0.66_cv, 0.26_cv, 0.5_cv});

	canvas2.finalize("flavorBox");

	auto fontItalic = osgx::make_ref<osgSlug::Font>(
		"font/EB_Garamond/EBGaramond-Italic-VariableFont_wght.ttf",
		atlas2
	);

	fontItalic->load();
	atlas2->build();
	atlas2->packTextures();

	const slug_t flavorSize = 22_cv;
	const slughorn::Color flavorColor = {0.92_cv, 0.88_cv, 0.78_cv, 1.0_cv};

	canvas2.text(
		"We hacked off every limb.",
		flavorSize, 180_cv, 375_cv, flavorColor,
		fontItalic->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas2.text(
		"We ran out of swords first.",
		flavorSize, 180_cv, 410_cv, flavorColor,
		fontItalic->metrics(),
		slughorn::canvas::TextAnchorY::Baseline,
		slughorn::canvas::TextAlignX::Center
	);

	canvas2.finalize("flavor");

	auto sdtext = example::makeShapeDrawable();

	sdtext->setAtlas(atlas2);
	sdtext->addCompositeShape(*atlas2->getCompositeShape("flavorBox"));
	sdtext->addCompositeShape(*atlas2->getCompositeShape("flavor"));
	sdtext->compile();

	// Merge atlas state into each drawable's existing StateSet (set by compile()).
	// compile() writes program + layer SSBO (binding 1); merge() adds the atlas
	// shape SSBO (binding 0) + textures + uniforms without clobbering binding 1.
	sd->getOrCreateStateSet()->merge(*atlas->createDefaultStateSet(example::USE_GL3));
	sdtext->getOrCreateStateSet()->merge(*atlas2->createDefaultStateSet(example::USE_GL3));

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->addDrawable(sdtext);

	return example::run(viewer, args, sdg);
}
