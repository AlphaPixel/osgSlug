// vimrun! ./osgslug-simple-path

// A companion to osgslug-simple: GPU-stroked paths with osgSlug::PathDrawable (Sluggit mode).
//
// A PathDrawable strokes polylines entirely on the GPU: every segment is one instanced quad, and
// the Slug coverage evaluated across it gives real joins, caps and analytic antialiasing on every
// edge, at any zoom. The CPU never builds a stroke outline. All a PathDrawable needs is points.
//
// Part 1 hands it points directly (setPaths() with artificial data).
// Part 2 stores paths in the Atlas as DrawMode::Geometry layers (Canvas::defineGeometry()), next
// to an ordinary filled shape, then strokes them where that composite draws (setPaths() with an
// Atlas and a Layer).
//
// Everything is in world units, Y up: Part 1 on the left, Part 2 in the middle, and Part 2's
// composite again at half size on the right.

#include "osgslug-example.hpp"

#include "osgSlug/Drawable/PathDrawable.hpp"
#include "osgSlug/Drawable/ShapeDrawable.hpp"

#include "slughorn/canvas.hpp"

#include <cmath>

using slughorn::canvas::LineCap;
using slughorn::canvas::LineJoin;

// PathDrawable reads each point's z as its arc length along the path (world units), which stroke
// patterns and shader hooks use to know how far along the stroke a pixel is. This fills it in for
// hand-made points; w is PathDrawable's own (setPaths() overwrites it).
std::vector<osgSlug::Vec4> withArcLength(std::vector<osgSlug::Vec4> points) {
	slug_t length = 0_cv;

	for(size_t i = 0; i < points.size(); i++) {
		if(i > 0) {
			const slug_t dx = points[i].x() - points[i - 1].x();
			const slug_t dy = points[i].y() - points[i - 1].y();

			length += std::sqrt(dx * dx + dy * dy);
		}

		points[i].z() = length;
	}

	return points;
}

osgSlug::PathDrawable* createPath(
	slug_t halfWidth,
	const osgSlug::Vec4& color,
	LineJoin join,
	LineCap cap,
	const std::string& name
) {
	// Sluggit mode with no setShapeKey() renders PathDrawable's built-in unit square stretched
	// along each segment; nothing has to be added to the Atlas for it.
	auto* path = new osgSlug::PathDrawable(osgSlug::PathMode::Sluggit);

	path->setHalfWidth(halfWidth);
	path->setColor(color);
	path->setJoin(join);
	path->setCap(cap);
	path->setName(name);

	// Drawn after the fills (render bin 1, below).
	path->getOrCreateStateSet()->setRenderBinDetails(2, "RenderBin");

	return path;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	auto lib = osgSlug::initialize(args);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates GPU-stroked paths (PathDrawable)")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	// --------------------------------------------------------------------------------------------
	// Part 1: points by hand
	// --------------------------------------------------------------------------------------------

	// One PathDrawable can hold many disconnected subpaths and still draw them all in ONE instanced
	// draw call; they share its color, width, join and cap. Here: a sine wave and a zig-zag.
	std::vector<osgSlug::Vec4> wave;

	for(size_t i = 0; i <= 48; i++) {
		const slug_t t = static_cast<slug_t>(i) / 48_cv;

		wave.emplace_back(t * 400_cv, 420_cv + 40_cv * std::sin(t * 4_cv * osg::PIf), 0_cv, 0_cv);
	}

	std::vector<osgSlug::Vec4> zigzag;

	for(size_t i = 0; i <= 8; i++) zigzag.emplace_back(
		static_cast<slug_t>(i) * 50_cv,
		(i % 2) ? 320_cv : 260_cv,
		0_cv,
		0_cv
	);

	auto* open = createPath(8_cv, {1_cv, 0.55_cv, 0.2_cv, 1_cv}, LineJoin::Round, LineCap::Round, "open paths");

	open->setPaths({withArcLength(wave), withArcLength(zigzag)});

	// Closed subpaths: their last point joins their first with the join style, and they get no
	// caps. Each subpath's flag goes in the second argument; the last point should repeat the first
	// (one is appended if it doesn't).
	std::vector<osgSlug::Vec4> hexagon;

	for(size_t i = 0; i <= 6; i++) {
		const slug_t a = static_cast<slug_t>(i) * osg::PIf / 3_cv;

		hexagon.emplace_back(100_cv + 70_cv * std::cos(a), 120_cv + 70_cv * std::sin(a), 0_cv, 0_cv);
	}

	std::vector<osgSlug::Vec4> square = {
		{230_cv, 60_cv, 0_cv, 0_cv},
		{370_cv, 60_cv, 0_cv, 0_cv},
		{370_cv, 180_cv, 0_cv, 0_cv},
		{230_cv, 180_cv, 0_cv, 0_cv},
		{230_cv, 60_cv, 0_cv, 0_cv}
	};

	auto* closed = createPath(10_cv, {0.3_cv, 0.65_cv, 1_cv, 1_cv}, LineJoin::Miter, LineCap::Butt, "closed paths");

	closed->setPaths({withArcLength(hexagon), withArcLength(square)}, {true, true});

	// --------------------------------------------------------------------------------------------
	// Part 2: paths stored in the Atlas
	// --------------------------------------------------------------------------------------------

	// A Canvas builds one composite: a filled "park" (an ordinary visible layer) and two paths
	// committed with defineGeometry(). A Geometry layer's curves live in the Atlas like any shape,
	// and the Layer remembers where they go, but ShapeDrawable never draws it. Something else
	// decides what to do with it; here, stroke it.
	slughorn::canvas::Canvas canvas(*atlas, slughorn::KeyIterator("simple-path"));

	canvas.beginPath().ellipse(760_cv, 240_cv, 170_cv, 120_cv);
	canvas.fill({0.2_cv, 0.45_cv, 0.25_cv, 1_cv});

	// An open path: a winding route across the park.
	canvas
		.beginPath()
		.moveTo(570_cv, 120_cv)
		.quadTo(680_cv, 420_cv, 760_cv, 240_cv)
		.quadTo(840_cv, 60_cv, 950_cv, 360_cv)
	;

	canvas.defineGeometry(slughorn::Key("route"));

	// A closed path: a ring road around the park. circle() closes itself.
	canvas.beginPath().circle(760_cv, 240_cv, 195_cv);
	canvas.defineGeometry(slughorn::Key("ring road"));

	auto composite = canvas.finalize();

	// The same composite at half size, to the right. Shape::computeQuad() places a layer's em point
	// e at (transform - origin + e) * scale, so moving a scaled copy by `offset` in world units
	// means adding offset / scale to its transform.
	const slug_t copyScale = 0.5_cv;
	const slug_t copyOffsetX = 780_cv;
	const slug_t copyOffsetY = 120_cv;

	auto copy = composite;

	for(auto& layer : copy.layers) {
		layer.transform.x += copyOffsetX / copyScale;
		layer.transform.y += copyOffsetY / copyScale;
		layer.scale = copyScale;
	}

	atlas->build();
	atlas->packTextures();

	// Both composites draw through ShapeDrawable as usual; only the park shows.
	auto shapes = example::makeShapeDrawable();

	shapes->addCompositeShape(composite);
	shapes->addCompositeShape(copy);
	shapes->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

	atlas->addChild(shapes);

	// Each Geometry layer becomes a PathDrawable. setPaths(atlas, layer, step) reads the layer's
	// curves back from the Atlas, flattens them to points at most `step` world units apart, and
	// places them exactly where ShapeDrawable would have drawn that layer. Closed subpaths stay
	// closed. The stroke's look (color, width, join, cap) belongs to the PathDrawable, not the Layer.
	for(const auto* c : {&composite, &copy}) {
		for(const auto& layer : c->layers) {
			if(layer.drawMode != slughorn::DrawMode::Geometry) continue;

			const bool route = layer.key == slughorn::Key("route");

			auto* path = route
				? createPath(9_cv * layer.scale, {0.95_cv, 0.85_cv, 0.3_cv, 1_cv}, LineJoin::Round, LineCap::Round, "route")
				: createPath(5_cv * layer.scale, {0.9_cv, 0.9_cv, 0.9_cv, 1_cv}, LineJoin::Round, LineCap::Butt, "ring road")
			;

			path->setPaths(*atlas, layer, 2_cv);

			atlas->addChild(path);
		}
	}

	atlas->addChild(open);
	atlas->addChild(closed);

	viewer.getCamera()->setClearColor(osg::Vec4(0.08f, 0.08f, 0.1f, 1.0f));

	return example::run(viewer, args, atlas);
}
