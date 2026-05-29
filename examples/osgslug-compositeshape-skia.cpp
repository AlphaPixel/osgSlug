//vimrun! ./osgslug-compositeshape-skia

#include "osgslug-example.hpp"

#define SLUGHORN_SKIA_IMPLEMENTATION
#include "slughorn/skia.hpp"

#include "include/core/SkPathBuilder.h"
#include "include/core/SkPath.h"
#include "include/core/SkCanvas.h"

slughorn::CompositeShape buildCompositeShape(osgSlug::Atlas* atlas) {
	slughorn::CompositeShape shape;

	const slug_t SCALE = 1.0_cv / 100.0_cv;
	uint32_t key = 0xF0000;

	auto addLayer = [&](SkPathBuilder& b, slughorn::Color color) {
		// slughorn::Atlas::ShapeInfo info;

		// slughorn::skia::decomposePath(b.detach(), info.curves, SCALE);
		// std::tie(info.curves, std::ignore) = slughorn::skia::decomposePath(b.detach(), SCALE);
		// auto [curves, transform] = slughorn::skia::decomposePath(b.detach(), SCALE);
		auto [info, transform] = slughorn::skia::decomposePath(
			b.detach(),
			SCALE,
			slughorn::Atlas::ShapeInfo::Origin::Type::Centered
		);

		// info.curves = curves;
		// info.origin = slughorn::Atlas::ShapeInfo::Origin::Centered;

		atlas->addShape(key, info);

		shape.layers.push_back({key++, color, slughorn::Transform{transform.dx, transform.dy}});
	};

	// ── Left gill (back, behind head) ──
	{
		SkPathBuilder b;
		// Three frond-like branches fanning out from the left side of the head
		// Branch 1 (upper-left)
		b.moveTo(45, 30);
		b.cubicTo(25, 10, 5, 5, 0, 15);
		b.cubicTo(5, 20, 20, 22, 45, 35);
		b.close();
		// Branch 2 (mid-left)
		b.moveTo(40, 42);
		b.cubicTo(15, 35, -5, 30, -8, 42);
		b.cubicTo(-5, 50, 15, 48, 40, 47);
		b.close();
		// Branch 3 (lower-left)
		b.moveTo(42, 55);
		b.cubicTo(20, 60, 5, 68, 5, 78);
		b.cubicTo(10, 75, 25, 68, 45, 58);
		b.close();

		addLayer(b, {0.85_cv, 0.25_cv, 0.35_cv, 1.0_cv}); // darker pink/red
	}

	// ── Right gill (back, behind head) ──
	{
		SkPathBuilder b;
		// Branch 1 (upper-right)
		b.moveTo(155, 30);
		b.cubicTo(175, 10, 195, 5, 200, 15);
		b.cubicTo(195, 20, 180, 22, 155, 35);
		b.close();
		// Branch 2 (mid-right)
		b.moveTo(160, 42);
		b.cubicTo(185, 35, 205, 30, 208, 42);
		b.cubicTo(205, 50, 185, 48, 160, 47);
		b.close();
		// Branch 3 (lower-right)
		b.moveTo(158, 55);
		b.cubicTo(180, 60, 195, 68, 195, 78);
		b.cubicTo(190, 75, 175, 68, 155, 58);
		b.close();

		addLayer(b, {0.85_cv, 0.25_cv, 0.35_cv, 1.0_cv}); // darker pink/red
	}

	// ── Head (main body shape) ──
	{
		SkPathBuilder b;
		// Wide, rounded axolotl head — slightly wider than tall
		b.moveTo(100, 15);
		b.cubicTo(55, 15, 25, 35, 30, 65);
		b.cubicTo(33, 85, 55, 100, 100, 100);
		b.cubicTo(145, 100, 167, 85, 170, 65);
		b.cubicTo(175, 35, 145, 15, 100, 15);
		b.close();

		addLayer(b, {1.0_cv, 0.6_cv, 0.7_cv, 1.0_cv}); // soft pink
	}

	// ── Left cheek blush ──
	{
		SkPathBuilder b;
		b.moveTo(45, 65);
		b.cubicTo(42, 58, 48, 52, 58, 55);
		b.cubicTo(65, 57, 68, 65, 63, 72);
		b.cubicTo(58, 78, 47, 73, 45, 65);
		b.close();

		addLayer(b, {1.0_cv, 0.45_cv, 0.55_cv, 1.0_cv}); // rosy blush
	}

	// ── Right cheek blush ──
	{
		SkPathBuilder b;
		b.moveTo(155, 65);
		b.cubicTo(158, 58, 152, 52, 142, 55);
		b.cubicTo(135, 57, 132, 65, 137, 72);
		b.cubicTo(142, 78, 153, 73, 155, 65);
		b.close();

		addLayer(b, {1.0_cv, 0.45_cv, 0.55_cv, 1.0_cv}); // rosy blush
	}

	// ── Left eye (white) ──
	{
		SkPathBuilder b;
		b.moveTo(65, 45);
		b.cubicTo(60, 35, 70, 28, 82, 32);
		b.cubicTo(90, 35, 92, 45, 87, 52);
		b.cubicTo(82, 58, 68, 55, 65, 45);
		b.close();

		addLayer(b, {0.05_cv, 0.05_cv, 0.08_cv, 1.0_cv}); // near-black
	}

	// ── Left pupil ──
	{
		SkPathBuilder b;
		b.moveTo(73, 42);
		b.cubicTo(71, 37, 76, 34, 80, 37);
		b.cubicTo(83, 39, 83, 44, 80, 47);
		b.cubicTo(77, 49, 74, 47, 73, 42);
		b.close();

		addLayer(b, {1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv}); // white glint
	}

	// ── Right eye (white) ──
	{
		SkPathBuilder b;
		b.moveTo(135, 45);
		b.cubicTo(140, 35, 130, 28, 118, 32);
		b.cubicTo(110, 35, 108, 45, 113, 52);
		b.cubicTo(118, 58, 132, 55, 135, 45);
		b.close();

		addLayer(b, {0.05_cv, 0.05_cv, 0.08_cv, 1.0_cv}); // near-black
	}

	// ── Right pupil ──
	{
		SkPathBuilder b;
		b.moveTo(127, 42);
		b.cubicTo(129, 37, 124, 34, 120, 37);
		b.cubicTo(117, 39, 117, 44, 120, 47);
		b.cubicTo(123, 49, 126, 47, 127, 42);
		b.close();

		addLayer(b, {1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv}); // white glint
	}

	// ── Smile ──
	{
		SkPathBuilder b;
		// A gentle upward arc — "stroked to path" as a thin filled crescent
		b.moveTo(80, 78);
		b.cubicTo(88, 88, 112, 88, 120, 78);
		b.lineTo(117, 75);
		b.cubicTo(110, 83, 90, 83, 83, 75);
		b.close();

		addLayer(b, {0.65_cv, 0.15_cv, 0.2_cv, 1.0_cv}); // dark rosy
	}

	// ── Nostrils (left) ──
	{
		SkPathBuilder b;
		b.moveTo(90, 68);
		b.cubicTo(89, 66, 91, 64, 93, 66);
		b.cubicTo(94, 68, 92, 70, 90, 68);
		b.close();

		addLayer(b, {0.7_cv, 0.3_cv, 0.4_cv, 1.0_cv});
	}

	// ── Nostrils (right) ──
	{
		SkPathBuilder b;
		b.moveTo(110, 68);
		b.cubicTo(111, 66, 109, 64, 107, 66);
		b.cubicTo(106, 68, 108, 70, 110, 68);
		b.close();

		addLayer(b, {0.7_cv, 0.3_cv, 0.4_cv, 1.0_cv});
	}

	shape.advance = 2.0_cv; // ~200 units normalised

	return shape;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Skia-authored CompositeShapes")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::CompositeShape shape = buildCompositeShape(atlas);

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	sd->addCompositeShape(shape);
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	return example::run(viewer, args, sdg);
}
