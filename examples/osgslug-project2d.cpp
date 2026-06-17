// vimrun! ./osgslug-project2d

#include "osgslug-example.hpp"

#define SLUGHORN_CAIRO_IMPLEMENTATION
#include "slughorn/cairo.hpp"
#include "slughorn/canvas.hpp"

// ================================================================================================
// osgSlug-grid.hpp - GPU-rendered 2D grid using stretched Slug unit-square quads
//
// Creates an orthographic grid with two levels of line thickness:
//   - Major lines at a configurable interval (e.g. every 100 units)
//   - Minor lines at a finer interval (e.g. every 10 units)
//
// All grid lines share a single unit-square shape in the Atlas, stretched via
// non-uniform quad placement. The Slug fragment shader provides resolution-
// independent antialiasing on all edges automatically.
//
// USAGE
// -----
//   auto grid = osgx::make_ref<osgSlug::GridDrawable>();
//
//   grid->setAtlas(atlas);           // must contain the unit square shape
//   grid->setGridExtent(1000, 1000); // world-space width x height
//   grid->setMajorInterval(100);     // thick line every 100 units
//   grid->setMinorInterval(10);      // thin line every 10 units
//   grid->setMajorThickness(0.5f);   // world-space half-width of major lines
//   grid->setMinorThickness(0.15f);  // world-space half-width of minor lines
//   grid->setMajorColor({0.4f, 0.4f, 0.4f, 1.0f});
//   grid->setMinorColor({0.25f, 0.25f, 0.25f, 0.8f});
//   grid->compile();
//
// UNIT SQUARE REGISTRATION
// ------------------------
// Call GridDrawable::registerUnitSquare(atlas) once before build() to add the
// shared unit-square shape. This uses a well-known string key ("_grid_unit_sq")
// so multiple GridDrawables can share the same Atlas without conflict.
//
// The unit square is a 1x1 filled rect at the origin, producing:
//   bearingX=0, bearingY=1, width=1, height=1 (via autoMetrics)
// ================================================================================================

namespace osgSlug {

class GridDrawable: public ShapeDrawable {
public:
	// The well-known key used for the shared unit square shape.
	static inline const slughorn::Key UNIT_SQUARE_KEY = slughorn::Key("_grid_unit_sq");

	// Register the unit-square shape in the Atlas. Call once before atlas.build().
	// Safe to call multiple times (Canvas::defineShape -> addShape silently replaces).
	//
	// TODO: Change this to `slugit`, because it's awesome...
	static void registerUnitSquare(slughorn::Atlas& atlas) {
		slughorn::canvas::Canvas canvas(atlas);

		canvas.rect(0.0_cv, 0.0_cv, 1.0_cv, 1.0_cv);
		canvas.defineShape(UNIT_SQUARE_KEY);
	}

	// --------------------------------------------------------------------------------------------
	// Configuration
	// --------------------------------------------------------------------------------------------

	// World-space extent of the grid, centered at the origin.
	// The grid spans [-width/2, +width/2] x [-height/2, +height/2].
	void setGridExtent(slug_t width, slug_t height) {
		_gridWidth = width;
		_gridHeight = height;
	}

	// Interval (in world units) between major and minor grid lines.
	// Major interval should be a multiple of minor interval for visual clarity,
	// but this is not enforced.
	void setMajorInterval(slug_t interval) { _majorInterval = interval; }
	void setMinorInterval(slug_t interval) { _minorInterval = interval; }

	// Half-thickness of grid lines in world units.
	// A major line at position X spans [X - thickness, X + thickness].
	void setMajorThickness(slug_t halfWidth) { _majorThickness = halfWidth; }
	void setMinorThickness(slug_t halfWidth) { _minorThickness = halfWidth; }

	// Colors for major and minor lines.
	void setMajorColor(slughorn::Color c) { _majorColor = c; }
	void setMinorColor(slughorn::Color c) { _minorColor = c; }

	// --------------------------------------------------------------------------------------------
	// Compile - builds vertex arrays from current configuration
	// --------------------------------------------------------------------------------------------

	void compile() override {
		if(!_atlas || !_atlas->isBuilt()) return;

		const auto shape = _atlas->getShape(UNIT_SQUARE_KEY);

		if(!shape) return;

		// Pre-compute the em-space corners (same for every quad).
		// Unit square with autoMetrics: bearingX=0, bearingY=1, width=1, height=1.
		static constexpr slug_t EXPAND = 0.01_cv;

		const slug_t emX0 = shape->bearingX - EXPAND;
		const slug_t emY0 = (shape->bearingY - shape->height) - EXPAND;
		const slug_t emX1 = (shape->bearingX + shape->width) + EXPAND;
		const slug_t emY1 = shape->bearingY + EXPAND;

		// Band transform (same for every quad - all reference the same shape).
		const slug_t bsX = shape->bandScaleX;
		const slug_t bsY = shape->bandScaleY;
		const slug_t boX = shape->bandOffsetX;
		const slug_t boY = shape->bandOffsetY;

		const slug_t sdX = cv(shape->bandTexX);
		const slug_t sdY = cv(shape->bandTexY);
		const slug_t sdMX = cv(shape->bandMaxX);
		const slug_t sdMY = cv(shape->bandMaxY);

		// Accumulators - we'll size these after counting lines.
		// In a real osg::Geometry subclass these would be the typed OSG arrays;
		// shown here as std::vector for clarity.
		struct QuadVert {
			slug_t px, py, pz; // position
			slug_t cr, cg, cb, ca; // color
			slug_t emU, emV; // em-coord
			slug_t bsX, bsY, boX, boY; // band xform
			slug_t sdX, sdY, sdMX, sdMY; // shape data
			slug_t effectId;
		};

		std::vector<QuadVert> verts;
		std::vector<uint16_t> indices;

		const slug_t halfW = _gridWidth * 0.5_cv;
		const slug_t halfH = _gridHeight * 0.5_cv;

		// Lambda: emit one stretched quad (a single grid line).
		//
		// (x0,y0)-(x1,y1) is the world-space rectangle for the line.
		// The em-coords and band data are identical for every quad.
		auto emitLine = [&](
			slug_t x0, slug_t y0,
			slug_t x1, slug_t y1,
			const slughorn::Color& color
		) {
			const auto base = static_cast<uint16_t>(verts.size());

			// Four corners: BL, BR, TR, TL
			auto pushVert = [&](slug_t px, slug_t py, slug_t eu, slug_t ev) {
				verts.push_back({
					px, py, 0.0_cv,
					color.r, color.g, color.b, color.a,
					eu, ev,
					bsX, bsY, boX, boY,
					sdX, sdY, sdMX, sdMY,
					0.0_cv // effectId
				});
			};

			pushVert(x0, y0, emX0, emY0); // BL
			pushVert(x1, y0, emX1, emY0); // BR
			pushVert(x1, y1, emX1, emY1); // TR
			pushVert(x0, y1, emX0, emY1); // TL

			indices.insert(indices.end(), {
				base, uint16_t(base + 1), uint16_t(base + 2),
				base, uint16_t(base + 2), uint16_t(base + 3)
			});
		};

		// Helper: emit all lines along one axis.
		//
		// For vertical lines: axisPos sweeps X, line runs full Y extent.
		// For horizontal lines: axisPos sweeps Y, line runs full X extent.
		auto emitGridLines = [&](
			slug_t minPos, slug_t maxPos, // axis range to fill
			slug_t interval, // spacing between lines
			slug_t thickness, // half-width of each line
			slug_t crossMin, slug_t crossMax, // extent on the perpendicular axis
			bool vertical, // true = vertical lines (sweep X)
			const slughorn::Color& color,
			slug_t skipInterval // skip lines that fall on this interval (0 = skip none)
		) {
			if(interval <= 0.0_cv) return;

			// Start from the first multiple of interval >= minPos.
			const int first = static_cast<int>(std::ceil(minPos / interval));
			const int last = static_cast<int>(std::floor(maxPos / interval));

			for(int i = first; i <= last; ++i) {
				const slug_t pos = cv(i) * interval;

				// Skip lines that coincide with a coarser grid level
				// (major lines are drawn separately so we don't double-draw).
				if(skipInterval > 0.0_cv) {
					const slug_t ratio = pos / skipInterval;
					const slug_t rounded = std::round(ratio);

					if(std::abs(ratio - rounded) < 1e-6_cv) continue;
				}

				if(vertical) {
					emitLine(
						pos - thickness, crossMin,
						pos + thickness, crossMax,
						color
					);
				}

				else {
					emitLine(
						crossMin, pos - thickness,
						crossMax, pos + thickness,
						color
					);
				}
			}
		};

		// --------------------------------------------------------------------
		// Emit minor lines first (drawn underneath), then major lines on top.
		// Minor lines skip positions that coincide with major lines.
		// --------------------------------------------------------------------

		// Minor vertical lines (sweep X, full Y extent)
		emitGridLines(
			-halfW, halfW, _minorInterval, _minorThickness,
			-halfH, halfH, true, _minorColor, _majorInterval
		);

		// Minor horizontal lines (sweep Y, full X extent)
		emitGridLines(
			-halfH, halfH, _minorInterval, _minorThickness,
			-halfW, halfW, false, _minorColor, _majorInterval
		);

		// Major vertical lines
		emitGridLines(
			-halfW, halfW, _majorInterval, _majorThickness,
			-halfH, halfH, true, _majorColor, 0.0_cv
		);

		// Major horizontal lines
		emitGridLines(
			-halfH, halfH, _majorInterval, _majorThickness,
			-halfW, halfW, false, _majorColor, 0.0_cv
		);

		if(verts.empty()) return;

		// --------------------------------------------------------------------
		// Upload to OSG vertex attribute arrays.
		//
		// In the real implementation, this would populate osg::Vec3Array, etc.
		// and call setVertexAttribArray() exactly like ShapeDrawable::compile().
		// Pseudocode shown to match the existing pattern:
		// --------------------------------------------------------------------

		auto vertices = osgx::make_ref<osgx::Vec3Array>();
		auto colors = osgx::make_ref<osgx::Vec4Array>();
		auto emCoords = osgx::make_ref<osgx::Vec2Array>();
		auto bandXform = osgx::make_ref<osgx::Vec4Array>();
		auto shapeDataArr = osgx::make_ref<osgx::Vec4Array>();
		auto effectIdsArr = osgx::make_ref<osgx::FloatArray>();
		auto idxArr = osgx::make_ref<osgx::DrawElementsUShort>();

		for(const auto& v : verts) {
			vertices->push_back({v.px, v.py, v.pz});
			colors->push_back({v.cr, v.cg, v.cb, v.ca});
			emCoords->push_back({v.emU, v.emV});
			bandXform->push_back({v.bsX, v.bsY, v.boX, v.boY});
			shapeDataArr->push_back({v.sdX, v.sdY, v.sdMX, v.sdMY});
			effectIdsArr->push_back(v.effectId);
		}

		for(auto idx : indices) idxArr->push_back(idx);

		setVertexAttribArray(0, vertices);
		setVertexAttribBinding(0, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(1, colors);
		setVertexAttribBinding(1, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(2, emCoords);
		setVertexAttribBinding(2, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(3, bandXform);
		setVertexAttribBinding(3, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(4, shapeDataArr);
		setVertexAttribBinding(4, osg::Geometry::BIND_PER_VERTEX);
		setVertexAttribArray(5, effectIdsArr);
		setVertexAttribBinding(5, osg::Geometry::BIND_PER_VERTEX);
		addPrimitiveSet(idxArr);
	}

private:
	// slughorn::Atlas* _atlas = nullptr;

	slug_t _gridWidth = 1000.0_cv;
	slug_t _gridHeight = 1000.0_cv;

	slug_t _majorInterval = 100.0_cv;
	slug_t _minorInterval = 10.0_cv;

	slug_t _majorThickness = 0.5_cv; // half-width in world units
	slug_t _minorThickness = 0.15_cv;

	slughorn::Color _majorColor = {0.4_cv, 0.4_cv, 0.4_cv, 1.0_cv};
	slughorn::Color _minorColor = {0.25_cv, 0.25_cv, 0.25_cv, 0.8_cv};
};

}

// TODO: Convert to osgx::make_ref!
osg::Camera* createOrthoCamera(slug_t width, slug_t height) {
	osg::Camera* camera = new osg::Camera();

	camera->getOrCreateStateSet()->setMode(
		GL_LIGHTING,
		osg::StateAttribute::PROTECTED | osg::StateAttribute::OFF
	);

	camera->setProjectionMatrix(osgSlug::Matrix::ortho2D(0_cv, width, 0_cv, height));
	camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	camera->setViewMatrix(osgSlug::Matrix::identity());
	camera->setViewport(new osg::Viewport(0, 0, static_cast<int>(width), static_cast<int>(height)));
	camera->setClearMask(GL_DEPTH_BUFFER_BIT);
	camera->setRenderOrder(osg::Camera::POST_RENDER);

	return camera;
}

void buildTrianglePath(cairo_t* cr) {
	cairo_new_path(cr);
	cairo_move_to(cr, 0, 0); // bottom-left (right angle)
	cairo_line_to(cr, 100, 0); // bottom-right
	cairo_line_to(cr, 0, 100); // top-left
	cairo_close_path(cr);
}

void buildCirclePath(cairo_t* cr) {
	cairo_new_path(cr);
	cairo_arc(cr, 50, 50, 40, 0, 2.0 * 3.14159);
	cairo_close_path(cr);
}

void buildPseudostrokeCirclePath(cairo_t* cr) {
	constexpr double cx = 50.0;
	constexpr double cy = 50.0;
	constexpr double r = 40.0;
	constexpr double w = 3.0;
	const double ro = r + w * 0.5; // outer radius
	const double ri = r - w * 0.5; // inner radius

	cairo_new_path(cr);

	// Start on outer circle at angle 0
	cairo_move_to(cr, cx + ro, cy);

	// Outer circle, full CCW
	cairo_arc(cr, cx, cy, ro, 0.0, 2.0 * 3.14159);

	// Connect directly inward at the seam point
	cairo_line_to(cr, cx + ri, cy);

	// Inner circle, full CW back to same seam
	cairo_arc_negative(cr, cx, cy, ri, 0.0, -2.0 * 3.14159);

	cairo_close_path(cr);
}

static constexpr double KAPPA90 = 0.5522847498307936;

void buildRoundedRectPath(cairo_t* cr) {
	constexpr double x = 10.0;
	constexpr double y = 10.0;
	constexpr double w = 80.0;
	constexpr double h = 80.0;
	constexpr double r0 = 15.0;

	const double x0 = x;
	const double y0 = y;
	const double x1 = x + w;
	const double y1 = y + h;
	const double r = std::max(0.0, std::min(r0, std::min(w, h) * 0.5));
	const double c = KAPPA90 * r;

	cairo_new_path(cr);

	// Start on bottom edge.
	cairo_move_to(cr, x0 + r, y0);

	// Bottom edge -> bottom-right corner
	cairo_line_to(cr, x1 - r, y0);
	cairo_curve_to(cr,
		x1 - r + c, y0,
		x1, y0 + r - c,
		x1, y0 + r
	);

	// Right edge -> top-right corner
	cairo_line_to(cr, x1, y1 - r);
	cairo_curve_to(cr,
		x1, y1 - r + c,
		x1 - r + c, y1,
		x1 - r, y1
	);

	// Top edge -> top-left corner
	cairo_line_to(cr, x0 + r, y1);
	cairo_curve_to(cr,
		x0 + r - c, y1,
		x0, y1 - r + c,
		x0, y1 - r
	);

	// Left edge -> bottom-left corner
	cairo_line_to(cr, x0, y0 + r);
	cairo_curve_to(cr,
		x0, y0 + r - c,
		x0 + r - c, y0,
		x0 + r, y0
	);

	cairo_close_path(cr);
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Displays a Shape on a 3D mesh", {
		{
			"--perspective",
			"Use a traditional 3D perspective view (instead of ortho2D)"
		}
	})) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	osgSlug::GridDrawable::registerUnitSquare(*atlas);

	// +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	constexpr uint32_t KEY = 1;
	// constexpr slug_t SCALE = 1.0_cv / 100.0_cv;
	constexpr slug_t SCALE = 2_cv;

	// Cairo requires a surface even if we only want path data; an image
	// surface at 1x1 is the lightest possible option for this purpose.
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t* cr = cairo_create(surface);

	// buildTrianglePath(cr);
	// buildCirclePath(cr);
	buildPseudostrokeCirclePath(cr);
	// buildRoundedRectPath(cr);

	slughorn::Atlas::ShapeInfo info;

	std::tie(info, std::ignore) = slughorn::cairo::decomposePath(cr, SCALE);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	std::cout
		<< "Decomposed shape into "
		<< info.curves.size()
		<< " quadratic segments." << std::endl
	;

	// auto atlas = osgx::make_ref<osgSlug::Atlas>();

	atlas->addShape(KEY, info);
	atlas->build();
	atlas->packTextures();

	const auto shape = atlas->getShape(KEY);

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

	auto grid = osgx::make_ref<osgSlug::GridDrawable>();

	// atlas->build();
	// atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->setAtlas(atlas);
	// sd->addLayer({KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}, slughorn::Matrix::identity(), 300_cv});
	// sd->addLayer({KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}, slughorn::Matrix{.dx=0.1_cv, .dy=0.1_cv}, 300_cv});
	sd->addLayer({KEY, {0.2_cv, 0.8_cv, 0.4_cv, 0.8_cv}});
	// sd->addLayer({KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}});
	sd->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));
	sd->compile();
	sd->setName("sd");
	// sd->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	grid->setAtlas(atlas); // must contain the unit square shape
	grid->setGridExtent(800, 600); // world-space width x height
	grid->setMajorInterval(100); // thick line every 100 units
	grid->setMinorInterval(10); // thin line every 10 units
	grid->setMajorThickness(2.5_cv); // world-space half-width of major lines
	grid->setMinorThickness(0.5_cv); // world-space half-width of minor lines
	grid->setMajorColor({0.8_cv, 0.8_cv, 0.8_cv, 1.0_cv});
	grid->setMinorColor({0.65_cv, 0.65_cv, 0.65_cv, 0.8_cv});
	grid->compile();
	grid->setName("grid");
	grid->setStateSet(atlas->createDefaultStateSet(true));

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(grid);
	sdg->addDrawable(sd);
	// sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));

	auto mat = osgx::make_ref<osg::MatrixTransform>();

	mat->addChild(sdg);
	mat->setMatrix(osgSlug::Matrix::translate(osgSlug::Vec3(400.0_cv, 300.0_cv, 0.0_cv)));

	// If the user wants to view the scene in typical 3D...
	if(args.read("--perspective")) return example::run(viewer, args, mat);

	// Otherwise, stick our scene into a traditional ortho2D setup.
	else {
		auto* project2d = createOrthoCamera(800_cv, 600_cv);

		project2d->addChild(mat);

		return example::run(viewer, args, project2d);
	}
}
