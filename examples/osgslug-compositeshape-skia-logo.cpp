// vimrun! ./osgslug-compositeshape-skia-logo

// =============================================================================
// Demonstrates per-layer shader effects (effectId) using the AlphaPixel logo
// as a test subject. The five tile layers (1-5) are assigned effectId=1, which
// triggers the texture-fill branch in osgSlug-frag.glsl.
//
// For now the "texture" is a procedural checkerboard generated entirely in
// GLSL from v_emCoord - no actual osg::Texture2D required. This lets us
// validate the full pipeline (Layer::effectId -> osgSlug::Shape::effectId ->
// vertex attrib 5 -> v_effectId -> shader branch -> v_emCoord used as UV)
// before adding texture upload plumbing.
//
// Once the checkerboard confirms the UVs look right, swapping in a real
// texture is a one-liner in the shader.
//
// Layer stack (back to front):
//   Layer  0     black union outline   (effectId = 0, standard fill)
//   Layers 1-5   five colored tiles    (effectId = 1, procedural fill)
//   Layers 6-10  "ALPHA" glyphs        (effectId = 0, standard fill)
//   Layers 11-15 "PIXEL" glyphs        (effectId = 0, standard fill)
// =============================================================================

#include "osgslug-example.hpp"

#include "slughorn/serial.hpp"

#define SLUGHORN_SKIA_IMPLEMENTATION
#include "slughorn/skia.hpp"

#include <osgDB/ReadFile>

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPath.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"

#include <iostream>

// =============================================================================
// Canvas constants
// =============================================================================

static constexpr float CANVAS_W = 800.0f;
// static constexpr float CANVAS_H = 300.0f;
static constexpr slug_t SCALE = 1.0_cv / static_cast<slug_t>(CANVAS_W);

static constexpr float TILE_W = 100.0f;
static constexpr float TILE_H = 100.0f;
static constexpr float START_X = 50.0f;
static constexpr float START_Y = 50.0f;
static constexpr float SPACING = 120.0f;

// static constexpr float OUTLINE_STROKE = 30.0f;
static constexpr float OUTLINE_STROKE = 20.0f;
// static constexpr float FONT_SIZE = 48.0f;
static constexpr float FONT_SIZE = 60.0f;

// =============================================================================
// Font setup
// =============================================================================

static SkFont makeFont() {
	sk_sp<SkFontMgr> fm = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
	sk_sp<SkTypeface> tf = fm->matchFamilyStyle("sans-serif", SkFontStyle());

	return SkFont(tf, FONT_SIZE);
}

// =============================================================================
// Path builders
// =============================================================================

static SkPath makeTilePath(uint32_t i) {
	float x = START_X + float(i) * SPACING;

	SkPathBuilder b;

	b.addRect(SkRect::MakeXYWH(x, START_Y, TILE_W, TILE_H));

	return b.detach();
}

static SkPath makeGlyphPath(const SkFont& font, char c, float x, float y) {
	SkGlyphID gid = font.unicharToGlyph(c);

	auto opt = font.getPath(gid);

	if(!opt) return SkPath();

	return opt->makeTransform(SkMatrix::Translate(x, y));
}

// =============================================================================
// buildPixelLogo
// =============================================================================

slughorn::CompositeShape buildPixelLogo(osgSlug::Atlas* atlas) {
	slughorn::CompositeShape shape;
	shape.advance = 1.0_cv;

	uint32_t key = 0xC0000;

	const char* textAlpha = "ALPHA";
	const char* textPixel = "PIXEL";

	const slughorn::Color tileColors[5] = {
		{ 140.0_cv/255.0_cv, 170.0_cv/255.0_cv, 200.0_cv/255.0_cv, 1.0_cv },
		{ 130.0_cv/255.0_cv, 165.0_cv/255.0_cv, 185.0_cv/255.0_cv, 1.0_cv },
		{ 120.0_cv/255.0_cv, 160.0_cv/255.0_cv, 170.0_cv/255.0_cv, 1.0_cv },
		{ 110.0_cv/255.0_cv, 155.0_cv/255.0_cv, 155.0_cv/255.0_cv, 1.0_cv },
		{ 100.0_cv/255.0_cv, 150.0_cv/255.0_cv, 140.0_cv/255.0_cv, 1.0_cv },
	};

	const slughorn::Color glyphColor = { 0.47_cv, 0.47_cv, 0.47_cv, 1.0_cv };
	const slughorn::Color outlineColor = { 0.07_cv, 0.07_cv, 0.07_cv, 1.0_cv };

	auto font = makeFont();

	// -------------------------------------------------------------------------
	// Layer 0: union outline (canvas coords, effectId = 0)
	// -------------------------------------------------------------------------
	{
		SkPathBuilder combined;

		for(uint32_t i = 0; i < 5; i++) combined.addPath(makeTilePath(i));

		/* for(int i = 0; i < 5; i++) {
			float cx = START_X + float(i) * SPACING + TILE_W * 0.5f;
			float x = cx - 15.0f;
			float y = START_Y + TILE_H * 0.5f + FONT_SIZE * 0.35f;

			combined.addPath(makeGlyphPath(font, textAlpha[i], x, y));
		} */

		for(int i = 0; i < 5; i++) {
			float cx = START_X + float(i) * SPACING + TILE_W * 0.5f;
			float x = cx - 15.0f;
			float y = 200.0f;

			combined.addPath(makeGlyphPath(font, textPixel[i], x, y));
		}

		auto xform = slughorn::skia::loadStrokedShape(
			combined.detach(), *atlas, key, OUTLINE_STROKE, SCALE,
			// CANVAS_H,
			// 0_cv,
			// SkPaint::kMiter_Join,
			SkPaint::kRound_Join,
			// SkPaint::kSquare_Cap
			SkPaint::kRound_Cap
		);

		/* slughorn::skia::loadShape(
			combined.detach(), *atlas, key, SCALE
		); */

		shape.layers.push_back({ key++, outlineColor, xform });
	}

#if 1
	// -------------------------------------------------------------------------
	// Layers 1-5: tiles (local coords, effectId = 1 -> procedural fill)
	// -------------------------------------------------------------------------
	for(uint32_t i = 0; i < 5; i++) {
		// slughorn::Matrix xform;
		auto xform = slughorn::skia::loadShape(makeTilePath(i), *atlas, key, SCALE);

		// if(slughorn::skia::loadShapeLocal(makeTilePath(i), *atlas, key, xform, SCALE)) {
			slughorn::Layer layer;

			layer.key = key++;
			layer.color = tileColors[i];
			layer.transform = xform;
			layer.effectId = i % 5; // procedural texture fill

			shape.layers.push_back(layer);
		// }
	}

	// -------------------------------------------------------------------------
	// Layers 6-10: "ALPHA" glyphs (local coords, effectId = 0)
	// -------------------------------------------------------------------------
	for(int i = 0; i < 5; i++) {
		float cx = START_X + float(i) * SPACING + TILE_W * 0.5f;
		float x = cx - 15.0f;
		float y = START_Y + TILE_H * 0.5f + FONT_SIZE * 0.35f;

		SkPath g = makeGlyphPath(font, textAlpha[i], x, y);

		if(g.isEmpty()) continue;

		// slughorn::Matrix xform;
		auto xform = slughorn::skia::loadShape(g, *atlas, key, SCALE);

		// if(slughorn::skia::loadShapeLocal(g, *atlas, key, xform, SCALE)) {
			slughorn::Layer layer;

			layer.key = key++;
			layer.color = glyphColor;

			if(i == 3) layer.color = {1.0_cv, 0.0_cv, 0.0_cv, 0.5_cv};

			else if(i == 4) layer.color = {0.0_cv, 0.0_cv, 0.0_cv, 0.75_cv};

			layer.transform = xform;

			shape.layers.push_back(layer);
		// }
	}

	// -------------------------------------------------------------------------
	// Layers 11-15: "PIXEL" glyphs (local coords, effectId = 0)
	// -------------------------------------------------------------------------
	for(int i = 0; i < 5; i++) {
		float cx = START_X + float(i) * SPACING + TILE_W * 0.5f;
		float x = cx - 15.0f;
		float y = 200.0f;

		SkPath g = makeGlyphPath(font, textPixel[i], x, y);

		if(g.isEmpty()) continue;

		// slughorn::Matrix xform;
		auto xform = slughorn::skia::loadShape(g, *atlas, key, SCALE);

		// if(slughorn::skia::loadShapeLocal(g, *atlas, key, xform, SCALE)) {
			slughorn::Layer layer;

			layer.key = key++;
			layer.color = glyphColor;
			layer.transform = xform;

			shape.layers.push_back(layer);
		// }
	}
#endif

	std::cout
		<< "PIXEL logo: "
		<< shape.layers.size()
		<< " layers, keys 0xC0000-0x"
		<< std::hex << (key - 1)
		<< std::dec
		<< std::endl;

	return shape;
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Skia-authored CompositeShapes with textured effects")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::CompositeShape logo = buildPixelLogo(atlas);

	atlas->build();
	atlas->packTextures();

	// slughorn::serial::write(*atlas, "offending_shape.slugb");

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	// sd->addCompositeShape(logo, { 0.0f, 0.0f }, 300.0f);
	sd->addCompositeShape(logo);
	sd->compile();

	// Load an image and bind it to unit 2
	osg::ref_ptr<osg::Image> img = osgDB::readImageFile("steel_128.png");
	auto tex = osgx::make_ref<osg::Texture2D>(img);
	tex->setWrap(osg::Texture::WRAP_S, osg::Texture::REPEAT);
	tex->setWrap(osg::Texture::WRAP_T, osg::Texture::REPEAT);
	tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

	auto* ss = atlas->createDefaultStateSet(example::USE_GL3);

	ss->setTextureAttributeAndModes(2, tex, osg::StateAttribute::ON);

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(sd);
	geode->setStateSet(ss);

	return example::run(viewer, args, geode);
}
