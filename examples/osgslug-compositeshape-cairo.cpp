// vimrun! ./osgslug-compositeshape-cairo

// ================================================================================================
// Demonstrates building a slughorn::CompositeShape manually using Cairo paths, mirroring exactly
// what the FreeType COLRv1 path produces automatically.
//
// The subject is an axolotl emoji, 8 layers, all solid color, drawn back to front exactly as a COLR
// font would emit them:
//
//   Layer 0  black outline     (largest, drawn first / behind everything)
//   Layer 1  body base         salmon pink
//   Layer 2  belly highlight   lighter pink
//   Layer 3  legs              darker pink
//   Layer 4  gill stalks       deep pink strokes (stroke-expanded to fill)
//   Layer 5  gill plumes       hot pink ellipses
//   Layer 6  eyes              black + white specular
//   Layer 7  smile             stroke arc
//
// All layers are decomposed in local coordinate space via decomposePathLocal(); tight atlas bands,
// zero offset waste.
// ================================================================================================

#include "osgslug-example.hpp"

#include "slughorn/serial.hpp"

#define SLUGHORN_CAIRO_IMPLEMENTATION
#include "slughorn/cairo.hpp"

#include <cairo/cairo.h>
#include <cmath>
#include <vector>
#include <iostream>

using slughorn::PI_CV;

static constexpr double W = 600.0;
// static constexpr double H = 480.0;
static constexpr slug_t SCALE = 1.0_cv / static_cast<slug_t>(W);

// =============================================================================
// Path builders
// =============================================================================

static void pathBodyOutline(cairo_t* cr) {
	cairo_save(cr);
	cairo_translate(cr, 310, 195);
	cairo_scale(cr, 155, 95);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
	cairo_new_sub_path(cr);
	cairo_save(cr);
	cairo_translate(cr, 195, 235);
	cairo_scale(cr, 80, 68);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
	cairo_new_sub_path(cr);
	cairo_move_to(cr, 440, 205);
	cairo_curve_to(cr, 510, 250, 530, 290, 520, 320);
	cairo_curve_to(cr, 505, 340, 480, 335, 465, 312);
	cairo_curve_to(cr, 450, 290, 448, 255, 440, 205);
	cairo_close_path(cr);
	static const double legs[4][2] = {{185,110},{255,98},{355,98},{425,110}};
	for(auto& l : legs) {
		cairo_new_sub_path(cr);
		cairo_save(cr);
		cairo_translate(cr, l[0], l[1]);
		cairo_scale(cr, 28, 44);
		cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
		cairo_restore(cr);
	}
}

static void pathBody(cairo_t* cr) {
	cairo_save(cr);
	cairo_translate(cr, 310, 195);
	cairo_scale(cr, 148, 88);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
	cairo_new_sub_path(cr);
	cairo_save(cr);
	cairo_translate(cr, 196, 236);
	cairo_scale(cr, 73, 62);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
	cairo_new_sub_path(cr);
	cairo_move_to(cr, 438, 207);
	cairo_curve_to(cr, 500, 248, 522, 282, 512, 308);
	cairo_curve_to(cr, 498, 328, 475, 323, 461, 302);
	cairo_curve_to(cr, 448, 283, 446, 258, 438, 207);
	cairo_close_path(cr);
}

static void pathBelly(cairo_t* cr) {
	cairo_save(cr);
	cairo_translate(cr, 310, 188);
	cairo_scale(cr, 110, 62);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
	cairo_new_sub_path(cr);
	cairo_save(cr);
	cairo_translate(cr, 205, 226);
	cairo_scale(cr, 50, 42);
	cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
	cairo_restore(cr);
}

static void pathLegs(cairo_t* cr) {
	static const double legs[4][2] = {{185,110},{255,98},{355,98},{425,110}};
	for(auto& l : legs) {
		cairo_new_sub_path(cr);
		cairo_save(cr);
		cairo_translate(cr, l[0], l[1]);
		cairo_scale(cr, 22, 38);
		cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
		cairo_restore(cr);
	}
}

static void pathGillStalks(cairo_t* cr) {
	static const struct { double x1,y1,x2,y2,w; } stalks[] = {
		{155, 280, 120, 350, 9},
		{168, 290, 150, 360, 9},
		{182, 296, 180, 368, 9},
	};
	for(auto& s : stalks) {
		const double dx = s.x2 - s.x1;
		const double dy = s.y2 - s.y1;
		const double len = std::sqrt(dx*dx + dy*dy);
		const double nx = -dy / len * s.w;
		const double ny = dx / len * s.w;
		cairo_new_sub_path(cr);
		cairo_move_to(cr, s.x1 + nx, s.y1 + ny);
		cairo_line_to(cr, s.x2 + nx, s.y2 + ny);
		cairo_arc(cr,
			s.x2, s.y2,
			s.w,
			std::atan2(ny, nx),
			std::atan2(-ny, -nx)
		);
		cairo_line_to(cr, s.x1 - nx, s.y1 - ny);
		cairo_arc(cr,
			s.x1, s.y1,
			s.w,
			std::atan2(-ny, -nx),
			std::atan2(ny, nx)
		);
		cairo_close_path(cr);
	}
}

static void pathGillPlumes(cairo_t* cr) {
	static const struct { double cx,cy,rx,ry,angle; } plumes[] = {
		{120, 355, 12, 20, -20}, {108, 365, 9, 17, -40}, {133, 367, 9, 17, 5},
		{150, 365, 12, 20, -10}, {138, 375, 9, 17, -30}, {163, 377, 9, 17, 10},
		{180, 373, 12, 20, 5}, {168, 381, 9, 17, -15}, {192, 383, 9, 17, 20},
	};

	for(auto& p : plumes) {
		cairo_new_sub_path(cr);
		cairo_save(cr);
		cairo_translate(cr, p.cx, p.cy);
		cairo_rotate(cr, p.angle * PI_CV / 180.0_cv);
		cairo_scale(cr, p.rx, p.ry);
		cairo_arc(cr, 0, 0, 1, 0, 2_cv * PI_CV);
		cairo_restore(cr);
	}
}

static void pathEyeBlack(cairo_t* cr) {
	cairo_arc(cr, 172, 258, 12, 0, 2_cv * PI_CV);
}

static void pathEyeWhite(cairo_t* cr) {
	cairo_arc(cr, 175, 261, 4, 0, 2_cv * PI_CV);
}

static void pathSmile(cairo_t* cr) {
	cairo_move_to(cr, 181, 204);
	cairo_curve_to(cr, 190, 196, 208, 196, 224, 202);
	cairo_line_to(cr, 222, 208);
	cairo_curve_to(cr, 207, 203, 191, 203, 183, 210);
	cairo_close_path(cr);
}

// =============================================================================
// buildAxolotl
// =============================================================================

slughorn::CompositeShape buildAxolotl(osgSlug::Atlas* atlas) {
	slughorn::CompositeShape shape;

	shape.advance = 1.0_cv;

	cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* cr = cairo_create(surf);

	// TODO: This seems to make NO DIFFERENCE...
	// cairo_translate(cr, 0.0, H);
	// cairo_scale(cr, 1.0, -1.0);

	uint32_t key = 0xA0000;

	auto addLayer = [&](void (*buildPath)(cairo_t*), slughorn::Color color) {
		cairo_new_path(cr);
		buildPath(cr);

		// slughorn::Atlas::ShapeInfo info;

		auto [info, transform] = slughorn::cairo::decomposePath(cr, SCALE);

		// info.curves = curves;
		// info.origin = slughorn::Atlas::ShapeInfo::Origin::Centered;

		if(!info.curves.empty()) {
			atlas->addShape(key, info);

			shape.layers.push_back({key, color, slughorn::Transform{transform.dx, transform.dy}});
		}

		key++;
	};

	addLayer(pathBodyOutline, {0.07_cv, 0.07_cv, 0.07_cv, 1.0_cv});
	addLayer(pathBody, {0.976_cv, 0.627_cv, 0.706_cv, 1.0_cv});
	addLayer(pathBelly, {0.988_cv, 0.800_cv, 0.847_cv, 1.0_cv});
	addLayer(pathLegs, {0.941_cv, 0.439_cv, 0.565_cv, 1.0_cv});
	addLayer(pathGillStalks, {0.878_cv, 0.345_cv, 0.471_cv, 1.0_cv});
	addLayer(pathGillPlumes, {1.0_cv, 0.376_cv, 0.565_cv, 1.0_cv});
	addLayer(pathEyeBlack, {0.07_cv, 0.07_cv, 0.07_cv, 1.0_cv});
	addLayer(pathEyeWhite, {1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv});
	addLayer(pathSmile, {0.753_cv, 0.251_cv, 0.376_cv, 1.0_cv});

	cairo_destroy(cr);
	cairo_surface_destroy(surf);

	std::cout << "Axolotl: " << shape.layers.size() << " layers" << std::endl;

	return shape;
}

slughorn::CompositeShape buildTriangles(osgSlug::Atlas* atlas) {
	slughorn::CompositeShape shape;

	shape.advance = 1.0_cv;

	cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* cr = cairo_create(surf);

	// TODO: This seems to make NO DIFFERENCE...
	// cairo_translate(cr, 0.0, H);
	// cairo_scale(cr, 1.0, -1.0);

	uint32_t key = 0xA0000;

	for(double i = 0.0; i < 3.0; i += 1.0) {
		double x = i * 100.0;
		double y = i * 100.0;

		cairo_save(cr);
		cairo_new_path(cr);
		cairo_move_to(cr, x, y);
		cairo_line_to(cr, x + 100.0, y);
		cairo_line_to(cr, x, y + 100.0);
		cairo_close_path(cr);
		cairo_restore(cr);

		// slughorn::Atlas::ShapeInfo info;

		auto [info, transform] = slughorn::cairo::decomposePath(cr, SCALE);

		// info.curves = std::move(curves);

		if(!info.curves.empty()) {
			atlas->addShape(key, info);

			shape.layers.push_back({key, {1_cv, 0_cv, 0_cv, 1_cv}, slughorn::Transform{transform.dx, transform.dy}});
		}

		key++;
	};

	cairo_destroy(cr);
	cairo_surface_destroy(surf);

	return shape;
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Cairo-authored CompositeShapes")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::CompositeShape axolotl = buildAxolotl(atlas);
	// CompositeShape axolotl = buildTriangles(atlas);

	atlas->addCompositeShape(slughorn::Key("axolotl"), axolotl);

	atlas->build();
	atlas->packTextures();

	// slughorn::serial::write(*atlas, "osgslug-compositeshape-cairo.slugb");

	// Debug: dump each layer and its resolved atlas shape.
	for(const auto& layer : axolotl.layers) {
		const auto s = atlas->getShape(layer.key);

		if(s) std::cout << layer << std::endl << " " << *s << std::endl;
	}

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	sd->addCompositeShape(
		*atlas->getCompositeShape(slughorn::Key("axolotl"))
		// {0_cv, 0_cv},
		// 300_cv
	);
	sd->compile();

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(sd);
	geode->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	auto root = osgx::make_ref<osg::MatrixTransform>();

	// NOTE: The shape was AUTHORED "upside down", so for this example only there's no need to
	// invert/flip the Y axis.
	root->setMatrix(
		// osg::Matrix::scale(1.0, -1.0, 1.0) *
		osgSlug::Matrix::rotate(osg::DegreesToRadians(90.0f), osgSlug::Vec3(1.0_cv, 0.0_cv, 0.0_cv))
	);
	root->addChild(geode);

	return example::run(viewer, args, root, false);
}
