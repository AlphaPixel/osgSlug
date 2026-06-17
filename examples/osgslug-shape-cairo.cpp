// vimrun! ./osgslug-shape-cairo

#include "osgslug-example.hpp"
#include "osgSlug/Util.hpp"

// slughorn doesn't (currently) provide an option for "compiling in" the Cairo bits it needs, so the
// implemenation line here is necessary.
#define SLUGHORN_CAIRO_IMPLEMENTATION
#include "slughorn/cairo.hpp"

#include <cairo/cairo.h>

#include <iostream>
#include <algorithm>
#include <cmath>

using slughorn::PI_CV;
using slughorn::PI_2_CV;

// =============================================================================
// buildJigsawPiecePath (Cairo version)
//
// Identical geometry to the Skia version -- four edges, each with an inward
// notch. Cairo uses cairo_curve_to for cubics (control points are absolute,
// same convention as Skia's cubicTo).
//
// Note: Cairo is Y-down by default. We apply a flip transform on the cairo_t
// before drawing so the path is in Y-up coordinates, matching slughorn's
// convention. This is equivalent to the canvas.scale(1, -1) in the Skia
// PNG export helper.
// =============================================================================
void buildJigsawPiecePath(cairo_t* cr) {
	constexpr double L = 0.0;
	constexpr double R = 100.0;
	constexpr double T = 100.0;
	constexpr double B = 0.0;

	constexpr double MX = (L + R) * 0.5;
	constexpr double MY = (B + T) * 0.5;
	constexpr double TAB = 18.0;
	constexpr double NECK = 12.0;
	constexpr double PULL = 10.0;

	cairo_new_path(cr);
	cairo_move_to(cr, L, B);

	// Bottom edge -- inward notch
	cairo_line_to(cr, MX - NECK, B);
	cairo_curve_to(cr,
		MX - NECK, B - PULL,
		MX - NECK, B - TAB,
		MX, B - TAB
	);
	cairo_curve_to(cr,
		MX + NECK, B - TAB,
		MX + NECK, B - PULL,
		MX + NECK, B
	);
	cairo_line_to(cr, R, B);

	// Right edge -- inward notch
	cairo_line_to(cr, R, MY - NECK);
	cairo_curve_to(cr,
		R - PULL, MY - NECK,
		R - TAB, MY - NECK,
		R - TAB, MY
	);
	cairo_curve_to(cr,
		R - TAB, MY + NECK,
		R - PULL, MY + NECK,
		R, MY + NECK
	);
	cairo_line_to(cr, R, T);

	// Top edge -- inward notch
	cairo_line_to(cr, MX + NECK, T);
	cairo_curve_to(cr,
		MX + NECK, T - PULL,
		MX + NECK, T - TAB,
		MX, T - TAB
	);
	cairo_curve_to(cr,
		MX - NECK, T - TAB,
		MX - NECK, T - PULL,
		MX - NECK, T
	);
	cairo_line_to(cr, L, T);

	// Left edge -- inward notch
	cairo_line_to(cr, L, MY + NECK);
	cairo_curve_to(cr,
		L - PULL, MY + NECK,
		L - TAB, MY + NECK,
		L - TAB, MY
	);
	cairo_curve_to(cr,
		L - TAB, MY - NECK,
		L - PULL, MY - NECK,
		L, MY - NECK
	);
	cairo_line_to(cr, L, B);

	cairo_close_path(cr);
}

// =============================================================================
// Halftone fragment hook: staggered dot grid with diagonal dot-size gradient.
// The solid fill area lets the full offset-printing pattern read clearly.
// =============================================================================

static const std::string FRAG_SHADER = R"(
#version 330 core

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(
	float fill,
	vec2 emCoord,
	vec2 uv,
	vec4 layerColor,
	int effectId,
	float time
) {
	if(effectId == 1) {
		const float kGrid = 22.0;

		// Stagger every other row by half a cell (classic halftone offset).
		vec2 cell = uv * kGrid;
		float row = floor(cell.y);
		cell.x += mod(row, 2.0) * 0.5;

		vec2 local = fract(cell) - 0.5;
		float dist = length(local);

		// Diagonal brightness ramp: small dots at top-left, large at bottom-right.
		float brightness = clamp(dot(uv, vec2(0.6, 0.4)), 0.0, 1.0);
		float radius = 0.04 + brightness * 0.42;

		// Smoothstep over one screen-pixel for anti-aliased sphere edges.
		float edge = fwidth(dist);
		float dotMask = 1.0 - smoothstep(radius - edge, radius + edge, dist);

		vec3 paper = vec3(0.97, 0.94, 0.87);
		vec3 ink = layerColor.rgb * 0.15;

		return vec4(mix(paper, ink, dotMask), fill * layerColor.a);
	}

	return vec4(layerColor.rgb, fill * layerColor.a);
}
)";

// =============================================================================
// main
// =============================================================================
int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Cairo-authored Shapes")) return 0;

	constexpr uint32_t PIECE_KEY = 1;
	constexpr slug_t SCALE = 1.0_cv / 100.0_cv;

	// Cairo requires a surface even if we only want path data -- an image
	// surface at 1x1 is the lightest possible option for this purpose.
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* cr = cairo_create(surface);

	buildJigsawPiecePath(cr);

	auto [info, transform ] = slughorn::cairo::decomposePath(cr, SCALE);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	std::cout
		<< "Decomposed jigsaw piece into "
		<< info.curves.size()
		<< " quadratic segments." << std::endl
	;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	atlas->addShape(PIECE_KEY, info);
	atlas->build();
	atlas->packTextures();

	const auto shape = atlas->getShape(PIECE_KEY);

	if(shape) {
		std::cout
			<< "Shape metrics:" << std::endl
			<< " bearing : (" << shape->bearingX << ", " << shape->bearingY << ")" << std::endl
			<< " size : " << shape->width << " x " << shape->height << std::endl
			<< " advance : " << shape->advance << std::endl
			<< " bandTex : (" << shape->bandTexX << ", " << shape->bandTexY << ")" << std::endl
			<< " bandMax : (" << shape->bandMaxX << ", " << shape->bandMaxY << ")" << std::endl
		;
	}

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	sd->addLayer({PIECE_KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}, {}, 1_cv, 1});
	sd->compile();

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(
		example::USE_GL3,
		{{osgSlug::Atlas::FragmentHook, FRAG_SHADER}}
	));

	auto mat = osgx::make_ref<osg::MatrixTransform>();

	mat->setMatrix(osgSlug::util::yDownToOSG());
	mat->addChild(sdg);

	return example::run(viewer, args, mat);
}
