//vimrun! ./osgslug-pattern

// Fill patterns as map symbology: four land-use areas, each a plain square filled with a repeating
// tile - water (waves), forest (pines), desert (dots) and residential (houses).
//
// Each tile is drawn ONCE (definePattern()) and the GPU repeats it across its square
// (fillPattern()), so the Atlas holds four small tiles plus four squares, however many times each
// tile repeats on screen. The line-art tiles are open paths turned into fillable outlines with
// strokePath() first, since a tile is always filled.
//
// A tile's ink must stay inside its cell: anything past the cell edge is clipped, not wrapped.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

#include <functional>

namespace {

using slughorn::Color;
using slughorn::canvas::LineCap;
using slughorn::canvas::LineJoin;
using slughorn::canvas::Canvas;

// Each area is SIZE x SIZE canvas units, GAP apart.
constexpr slug_t SIZE = 6_cv;
constexpr slug_t GAP = 1_cv;

// Maps a normalized [0, 1] cell coordinate into the tile's cell, in canvas units.
using Point = std::function<std::pair<slug_t, slug_t>(slug_t, slug_t)>;

struct Area {
	const char* name;

	// Lower-left corner of the area's square.
	slug_t x;
	slug_t y;

	// The tile cell: one repeat, in canvas units.
	slug_t cellWidth;
	slug_t cellHeight;

	Color background;
	Color ink;

	// Adds the tile to the canvas's current path, in normalized [0, 1] cell coordinates.
	std::function<void(Canvas&, const Point&)> tile;

	// Stroke width (canvas units) for line-art tiles; 0 fills the tile path as-is.
	slug_t strokeWidth;
};

void moveTo(Canvas& canvas, const Point& p, slug_t x, slug_t y) {
	auto [px, py] = p(x, y);

	canvas.moveTo(px, py);
}

void lineTo(Canvas& canvas, const Point& p, slug_t x, slug_t y) {
	auto [px, py] = p(x, y);

	canvas.lineTo(px, py);
}

void quadTo(Canvas& canvas, const Point& p, slug_t cx, slug_t cy, slug_t x, slug_t y) {
	auto [pcx, pcy] = p(cx, cy);
	auto [px, py] = p(x, y);

	canvas.quadTo(pcx, pcy, px, py);
}

// Two breaking crests: each rises slowly into a sharp peak and falls away steeply.
void waves(Canvas& canvas, const Point& p) {
	moveTo(canvas, p, 0.08_cv, 0.4_cv);
	quadTo(canvas, p, 0.3_cv, 0.4_cv, 0.33_cv, 0.62_cv);
	quadTo(canvas, p, 0.36_cv, 0.4_cv, 0.52_cv, 0.4_cv);
	quadTo(canvas, p, 0.6_cv, 0.4_cv, 0.63_cv, 0.62_cv);
	quadTo(canvas, p, 0.66_cv, 0.4_cv, 0.82_cv, 0.4_cv);
	quadTo(canvas, p, 0.9_cv, 0.4_cv, 0.92_cv, 0.52_cv);
}

// A three-tier pine, open at the trunk.
void pine(Canvas& canvas, const Point& p) {
	moveTo(canvas, p, 0.44_cv, 0.1_cv);
	lineTo(canvas, p, 0.44_cv, 0.24_cv);
	lineTo(canvas, p, 0.14_cv, 0.2_cv);
	lineTo(canvas, p, 0.34_cv, 0.44_cv);
	lineTo(canvas, p, 0.24_cv, 0.44_cv);
	lineTo(canvas, p, 0.4_cv, 0.66_cv);
	lineTo(canvas, p, 0.33_cv, 0.66_cv);
	lineTo(canvas, p, 0.5_cv, 0.9_cv);
	lineTo(canvas, p, 0.67_cv, 0.66_cv);
	lineTo(canvas, p, 0.6_cv, 0.66_cv);
	lineTo(canvas, p, 0.76_cv, 0.44_cv);
	lineTo(canvas, p, 0.66_cv, 0.44_cv);
	lineTo(canvas, p, 0.86_cv, 0.2_cv);
	lineTo(canvas, p, 0.56_cv, 0.24_cv);
	lineTo(canvas, p, 0.56_cv, 0.1_cv);
}

// One filled dot per cell.
void dot(Canvas& canvas, const Point& p) {
	auto [cx, cy] = p(0.5_cv, 0.5_cv);
	auto [rx, ry] = p(0.62_cv, 0.5_cv);

	canvas.circle(cx, cy, rx - cx);
}

// A roof and two walls: three open subpaths.
void house(Canvas& canvas, const Point& p) {
	moveTo(canvas, p, 0.12_cv, 0.5_cv);
	lineTo(canvas, p, 0.5_cv, 0.8_cv);
	lineTo(canvas, p, 0.88_cv, 0.5_cv);

	moveTo(canvas, p, 0.26_cv, 0.56_cv);
	lineTo(canvas, p, 0.26_cv, 0.16_cv);

	moveTo(canvas, p, 0.74_cv, 0.56_cv);
	lineTo(canvas, p, 0.74_cv, 0.16_cv);
}

}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	auto lib = osgSlug::initialize(args);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates Canvas fill patterns as map symbology", {})) return 0;

	const slug_t top = SIZE + GAP;

	const Area areas[] = {
		{
			"water", 0_cv, top, 3_cv, 2_cv,
			{0.04_cv, 0.1_cv, 0.18_cv, 1_cv}, {0.2_cv, 0.8_cv, 0.9_cv, 1_cv},
			waves, 0.14_cv
		},
		{
			"forest", top, top, 3_cv, 3_cv,
			{0.04_cv, 0.14_cv, 0.06_cv, 1_cv}, {0.2_cv, 0.9_cv, 0.35_cv, 1_cv},
			pine, 0.14_cv
		},
		{
			"desert", 0_cv, 0_cv, 2_cv, 2_cv,
			{0.22_cv, 0.18_cv, 0.08_cv, 1_cv}, {0.6_cv, 0.6_cv, 0.25_cv, 1_cv},
			dot, 0_cv
		},
		{
			"residential", top, 0_cv, 3_cv, 3_cv,
			{0.14_cv, 0.14_cv, 0.16_cv, 1_cv}, {0.95_cv, 0.95_cv, 0.95_cv, 1_cv},
			house, 0.14_cv
		}
	};

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	Canvas canvas(*atlas);

	for(const auto& area : areas) {
		// The tile, drawn in the cell (0, 0) - (cellWidth, cellHeight).
		const Point p = [&](slug_t x, slug_t y) {
			return std::pair{x * area.cellWidth, y * area.cellHeight};
		};

		canvas.beginPath();

		area.tile(canvas, p);

		if(area.strokeWidth > 0_cv) canvas.strokePath(area.strokeWidth, false, LineJoin::Round, LineCap::Round);

		auto handle = canvas.definePattern(0_cv, 0_cv, area.cellWidth, area.cellHeight);

		if(handle.info.tile == slughorn::Key{}) {
			OSG_WARN << "osgslug-pattern: " << area.name << " tile is empty" << std::endl;

			continue;
		}

		// The tile was drawn at the canvas origin; move the pattern so a whole cell starts at the
		// area's corner.
		handle.transform = slughorn::Matrix::translate(area.x, area.y);

		// The area's background, then the same square filled with its pattern on top.
		canvas.beginPath();
		canvas.rect(area.x, area.y, SIZE, SIZE);
		canvas.fill(area.background);

		canvas.beginPath();
		canvas.rect(area.x, area.y, SIZE, SIZE);
		canvas.fillPattern(handle, area.ink);
	}

	auto compositeShape = canvas.finalize();

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addCompositeShape(compositeShape);
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
