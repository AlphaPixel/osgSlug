//vimrun! ./osgslug-shape-skia

#include "osgslug-example.hpp"

#define SLUGHORN_SKIA_IMPLEMENTATION
#include "slughorn/skia.hpp"

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPath.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"

#include <iostream>

SkPath buildJigsawPiecePath() {
	SkPathBuilder b;

	constexpr float L = 0.0f;
	constexpr float R = 100.0f;
	constexpr float T = 100.0f;
	constexpr float B = 0.0f;

	constexpr float MX = (L + R) * 0.5f;
	constexpr float MY = (B + T) * 0.5f;

	constexpr float TAB  = 18.0f;
	constexpr float NECK = 12.0f;
	constexpr float PULL = 10.0f;

	b.moveTo(L, B);

	// --- Bottom edge -- concave notch (inward, toward T) ---
	b.lineTo(MX - NECK, B);
	b.cubicTo(
		MX - NECK, B - PULL, // shoulder in
		MX - NECK, B - TAB,  // notch bottom-left control
		MX,        B - TAB   // notch tip
	);
	b.cubicTo(
		MX + NECK, B - TAB,  // notch tip right
		MX + NECK, B - PULL, // shoulder in
		MX + NECK, B         // back to edge
	);
	b.lineTo(R, B);

	// Right edge -- notch (inward, so R - TAB instead of R + TAB)
	b.lineTo(R, MY - NECK);
	b.cubicTo(
		R - PULL, MY - NECK,
		R - TAB,  MY - NECK,
		R - TAB,  MY
	);
	b.cubicTo(
		R - TAB,  MY + NECK,
		R - PULL, MY + NECK,
		R,        MY + NECK
	);
	b.lineTo(R, T);

	// Top edge -- notch (inward, so T - TAB instead of T + TAB)
	b.lineTo(MX + NECK, T);
	b.cubicTo(
		MX + NECK, T - PULL,
		MX + NECK, T - TAB,
		MX,        T - TAB
	);
	b.cubicTo(
		MX - NECK, T - TAB,
		MX - NECK, T - PULL,
		MX - NECK, T
	);
	b.lineTo(L, T);

	// --- Left edge -- concave notch (inward, toward R) ---
	b.lineTo(L, MY + NECK);
	b.cubicTo(
		L - PULL, MY + NECK, // shoulder in (negative X)
		L - TAB,  MY + NECK, // notch left control
		L - TAB,  MY         // notch tip
	);
	b.cubicTo(
		L - TAB,  MY - NECK, // notch tip bottom
		L - PULL, MY - NECK, // shoulder in
		L,        MY - NECK  // back to edge
	);
	b.lineTo(L, B);

	b.close();

	return b.detach();
}

/* void savePathToPNG(const SkPath& path, const char* filename, int size = 256) {
	SkBitmap bmp;

	bmp.allocN32Pixels(size, size);
	bmp.eraseColor(SK_ColorWHITE);

	SkCanvas canvas(bmp);

	// Scale and center the path within the image.
	// The piece is 100x100 (plus tab overhang), so fit it with some padding.
	const float padding = cv(size) * 0.1f;
	const float available = cv(size) - padding * 2.0f;
	const float scale = available / 118.0f; // 118 = 100 + max tab protrusion

	canvas.translate(padding, padding);
	canvas.scale(scale, scale);

	// Skia is Y-down, so flip vertically around the shape's centre
	// to match the Y-up convention we're using in slughorn.
	canvas.translate(0.0f, 118.0f);
	canvas.scale(1.0f, -1.0f);

	SkPaint paint;
	paint.setAntiAlias(true);
	paint.setColor(SK_ColorBLUE);
	paint.setStyle(SkPaint::kFill_Style);
	canvas.drawPath(path, paint);

	// Outline so we can see the edges clearly
	paint.setStyle(SkPaint::kStroke_Style);
	paint.setColor(SK_ColorBLACK);
	paint.setStrokeWidth(1.0f);
	canvas.drawPath(path, paint);

	SkFILEWStream stream(filename);
	SkPngEncoder::Encode(&stream, bmp.pixmap(), {});
} */

// =============================================================================
// main
// =============================================================================
int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Skia-authored Shapes")) return 0;

	constexpr uint32_t PIECE_KEY = 1;

	// Normalise into a ~1.0 em square so it plays nicely with slughorn metrics
	constexpr slug_t SCALE = 1.0_cv / 100.0_cv;

	SkPath path = buildJigsawPiecePath();

	// savePathToPNG(path, "osgslugpath-skia.png");

	slughorn::Atlas::ShapeInfo info;

	// info.autoMetrics = true; // let slughorn derive bounds from the curves
	// info.numBands = 4;

	path = slughorn::skia::strokeToFill(path, 8.0);
	// slughorn::skia::decomposePath(path, info.curves, SCALE);
	// std::tie(info.curves, std::ignore) = slughorn::skia::decomposePath(path, SCALE);
	std::tie(info, std::ignore) = slughorn::skia::decomposePath(path, SCALE);

	std::cout
		<< "Decomposed jigsaw piece into "
		<< info.curves.size()
		<< " quadratic segments." << std::endl
	;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	atlas->addShape(PIECE_KEY, info);
	atlas->build();
	atlas->packTextures();

	const osgSlug::Atlas::Shape* shape = atlas->getShape(PIECE_KEY);

	if(shape) {
		std::cout
			<< "Shape metrics:" << std::endl
			<< "  bearing  : (" << shape->bearingX << ", " << shape->bearingY << ")" << std::endl
			<< "  size     : " << shape->width << " x " << shape->height << "" << std::endl
			<< "  advance  : " << shape->advance << "" << std::endl
			<< "  bandTex  : (" << shape->bandTexX << ", " << shape->bandTexY << ")" << std::endl
			<< "  bandMax  : (" << shape->bandMaxX << ", " << shape->bandMaxY << ")" << std::endl
		;
	}

	auto sd = osgx::make_ref<osgSlug::SSBOShapeDrawable>();

	sd->setAtlas(atlas);
	// sd->addShape({PIECE_KEY, {250,0}, osg::Vec4(1.0f, 0.5f, 0.0f, 1.0f), 200.0f});
	sd->addLayer({PIECE_KEY, {1.0_cv, 0.5_cv, 0.0_cv, 1.0_cv}, {}, 200.0_cv});
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet());
	// sdg->setStateSet(createStateSetForAtlas(atlas));
	// sdg->getOrCreateStateSet()->addUniform(new osg::Uniform("slug_debugMode", 4));

	return example::run(viewer, args, sdg);
}
