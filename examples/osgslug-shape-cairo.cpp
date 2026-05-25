// vimrun! ./osgslug-shape-cairo

#include "osgslug-example.hpp"

#define SLUGHORN_CAIRO_IMPLEMENTATION
#include "slughorn/cairo.hpp"

#include <cairo/cairo.h>

#include <iostream>
#include <algorithm>
#include <cmath>

// =============================================================================
// buildJigsawPiecePath (Cairo version)
//
// Identical geometry to the Skia version -- four edges, each with an inward
// notch.  Cairo uses cairo_curve_to for cubics (control points are absolute,
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

    constexpr double MX   = (L + R) * 0.5;
    constexpr double MY   = (B + T) * 0.5;
    constexpr double TAB  = 18.0;
    constexpr double NECK = 12.0;
    constexpr double PULL = 10.0;

    cairo_new_path(cr);
    cairo_move_to(cr, L, B);

    // Bottom edge -- inward notch
    cairo_line_to(cr, MX - NECK, B);
    cairo_curve_to(cr,
        MX - NECK, B - PULL,
        MX - NECK, B - TAB,
        MX,        B - TAB
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
        R - TAB,  MY - NECK,
        R - TAB,  MY
    );
    cairo_curve_to(cr,
        R - TAB,  MY + NECK,
        R - PULL, MY + NECK,
        R,        MY + NECK
    );
    cairo_line_to(cr, R, T);

    // Top edge -- inward notch
    cairo_line_to(cr, MX + NECK, T);
    cairo_curve_to(cr,
        MX + NECK, T - PULL,
        MX + NECK, T - TAB,
        MX,        T - TAB
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
        L - TAB,  MY + NECK,
        L - TAB,  MY
    );
    cairo_curve_to(cr,
        L - TAB,  MY - NECK,
        L - PULL, MY - NECK,
        L,        MY - NECK
    );
    cairo_line_to(cr, L, B);

    cairo_close_path(cr);
}

void buildTrianglePath(cairo_t* cr) {
    cairo_new_path(cr);
    cairo_move_to(cr,   0,   0);   // bottom-left  (right angle)
    cairo_line_to(cr, 100,   0);   // bottom-right
    cairo_line_to(cr,   0, 100);   // top-left
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

    constexpr double r  = 40.0;
    constexpr double w  = 1.0;

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

#if 0
void buildRoundedRectPath(
    cairo_t* cr,
    double x, double y,
    double w, double h,
    double r
) {
    // Clamp radius so it stays sane.
    r = std::max(0.0, std::min(r, std::min(w, h) * 0.5));

    const double x0 = x;
    const double y0 = y;
    const double x1 = x + w;
    const double y1 = y + h;

    cairo_new_path(cr);

    // Start on bottom edge, just right of bottom-left corner.
    cairo_move_to(cr, x0 + r, y0);

    // Bottom edge -> bottom-right corner
    cairo_line_to(cr, x1 - r, y0);
    cairo_arc(cr, x1 - r, y0 + r, r, -0.5 * M_PI, 0.0);

    // Right edge -> top-right corner
    cairo_line_to(cr, x1, y1 - r);
    cairo_arc(cr, x1 - r, y1 - r, r, 0.0, 0.5 * M_PI);

    // Top edge -> top-left corner
    cairo_line_to(cr, x0 + r, y1);
    cairo_arc(cr, x0 + r, y1 - r, r, 0.5 * M_PI, M_PI);

    // Left edge -> bottom-left corner
    cairo_line_to(cr, x0, y0 + r);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 1.5 * M_PI);

    cairo_close_path(cr);
}

void buildPseudostrokeRoundedRectPath(
    cairo_t* cr,
    double x, double y,
    double w, double h,
    double r,
    double strokeWidth
) {
    const double half = strokeWidth * 0.5;

    // Outer rect expands outward.
    const double ox = x - half;
    const double oy = y - half;
    const double ow = w + strokeWidth;
    const double oh = h + strokeWidth;
    const double orr = r + half;

    // Inner rect shrinks inward.
    const double ix = x + half;
    const double iy = y + half;
    const double iw = w - strokeWidth;
    const double ih = h - strokeWidth;
    const double irr = r - half;

    // If stroke is too large, inner shape collapses.
    if (iw <= 0.0 || ih <= 0.0 || irr < 0.0) {
        buildRoundedRectPath(cr, ox, oy, ow, oh, orr);
        return;
    }

    const double ox0 = ox;
    const double oy0 = oy;
    const double ox1 = ox + ow;
    const double oy1 = oy + oh;

    const double ix0 = ix;
    const double iy0 = iy;
    const double ix1 = ix + iw;
    const double iy1 = iy + ih;

    cairo_new_path(cr);

    // ---- Outer contour, CCW ----
    cairo_move_to(cr, ox0 + orr, oy0);

    cairo_line_to(cr, ox1 - orr, oy0);
    cairo_arc(cr, ox1 - orr, oy0 + orr, orr, -0.5 * M_PI, 0.0);

    cairo_line_to(cr, ox1, oy1 - orr);
    cairo_arc(cr, ox1 - orr, oy1 - orr, orr, 0.0, 0.5 * M_PI);

    cairo_line_to(cr, ox0 + orr, oy1);
    cairo_arc(cr, ox0 + orr, oy1 - orr, orr, 0.5 * M_PI, M_PI);

    cairo_line_to(cr, ox0, oy0 + orr);
    cairo_arc(cr, ox0 + orr, oy0 + orr, orr, M_PI, 1.5 * M_PI);

    // Bridge inward at the same seam region.
    cairo_line_to(cr, ix0 + irr, iy0);

    // ---- Inner contour, CW ----
    cairo_line_to(cr, ix0, iy0 + irr);
    cairo_arc_negative(cr, ix0 + irr, iy0 + irr, irr, M_PI, 0.5 * M_PI);

    cairo_line_to(cr, ix1 - irr, iy1);
    cairo_arc_negative(cr, ix1 - irr, iy1 - irr, irr, 0.5 * M_PI, 0.0);

    cairo_line_to(cr, ix1, iy0 + irr);
    cairo_arc_negative(cr, ix1 - irr, iy0 + irr, irr, 0.0, -0.5 * M_PI);

    cairo_line_to(cr, ix0 + irr, iy0);
    cairo_arc_negative(cr, ix0 + irr, iy0 + irr, irr, -0.5 * M_PI, -M_PI);

    cairo_close_path(cr);
}
#endif

#if 0
void buildRoundedRectPath(
    cairo_t* cr,
    double x, double y,
    double w, double h,
    double r
) {
    r = std::max(0.0, std::min(r, std::min(w, h) * 0.5));

    const double x0 = x;
    const double y0 = y;
    const double x1 = x + w;
    const double y1 = y + h;

    cairo_new_path(cr);

    // Start on bottom edge.
    cairo_move_to(cr, x0 + r, y0);

    cairo_line_to(cr, x1 - r, y0);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0.0);

    cairo_line_to(cr, x1, y1 - r);
    cairo_arc(cr, x1 - r, y1 - r, r, 0.0, M_PI_2);

    cairo_line_to(cr, x0 + r, y1);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);

    cairo_line_to(cr, x0, y0 + r);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3.0 * M_PI_2);

    cairo_close_path(cr);
}

void buildPseudostrokeRoundedRectPath(
    cairo_t* cr,
    double x, double y,
    double w, double h,
    double r,
    double strokeWidth
) {
    const double half = strokeWidth * 0.5;

    // Outer rounded rect.
    const double ox0 = x - half;
    const double oy0 = y - half;
    const double ox1 = x + w + half;
    const double oy1 = y + h + half;
    const double ro  = r + half;

    // Inner rounded rect.
    const double ix0 = x + half;
    const double iy0 = y + half;
    const double ix1 = x + w - half;
    const double iy1 = y + h - half;
    const double ri  = r - half;

    // If the inner rect collapses, just draw the outer one.
    if ((ix1 <= ix0) || (iy1 <= iy0) || (ri < 0.0)) {
        buildRoundedRectPath(cr, ox0, oy0, ox1 - ox0, oy1 - oy0, ro);
        return;
    }

    cairo_new_path(cr);

    // Use the bottom-left corner seam.
    // Outer seam point: start of bottom edge after BL corner.
    cairo_move_to(cr, ox0 + ro, oy0);

    // ---- Outer contour (CCW) ----
    cairo_line_to(cr, ox1 - ro, oy0);
    cairo_arc(cr, ox1 - ro, oy0 + ro, ro, -M_PI_2, 0.0);

    cairo_line_to(cr, ox1, oy1 - ro);
    cairo_arc(cr, ox1 - ro, oy1 - ro, ro, 0.0, M_PI_2);

    cairo_line_to(cr, ox0 + ro, oy1);
    cairo_arc(cr, ox0 + ro, oy1 - ro, ro, M_PI_2, M_PI);

    cairo_line_to(cr, ox0, oy0 + ro);
    cairo_arc(cr, ox0 + ro, oy0 + ro, ro, M_PI, 3.0 * M_PI_2);

    // ---- Radial bridge inward at same seam direction ----
    cairo_line_to(cr, ix0 + ri, iy0);

    // ---- Inner contour (CW) ----
    // Walk the inner contour in reverse.
    cairo_line_to(cr, ix0, iy0 + ri);
    cairo_arc_negative(cr, ix0 + ri, iy0 + ri, ri, M_PI, M_PI_2);

    cairo_line_to(cr, ix0 + ri, iy1);
    cairo_arc_negative(cr, ix0 + ri, iy1 - ri, ri, M_PI_2, 0.0);

    cairo_line_to(cr, ix1, iy1 - ri);
    cairo_arc_negative(cr, ix1 - ri, iy1 - ri, ri, 0.0, -M_PI_2);

    cairo_line_to(cr, ix1 - ri, iy0);
    cairo_arc_negative(cr, ix1 - ri, iy0 + ri, ri, -M_PI_2, -M_PI);

    cairo_close_path(cr);
}
#endif

#if 0
static void appendRoundedRectCCW(
    cairo_t* cr,
    double x0, double y0,
    double x1, double y1,
    double r
) {
    r = std::max(0.0, std::min(r, std::min(x1 - x0, y1 - y0) * 0.5));

    cairo_move_to(cr, x0 + r, y0);

    cairo_line_to(cr, x1 - r, y0);
    cairo_arc(cr, x1 - r, y0 + r, r, -M_PI_2, 0.0);

    cairo_line_to(cr, x1, y1 - r);
    cairo_arc(cr, x1 - r, y1 - r, r, 0.0, M_PI_2);

    cairo_line_to(cr, x0 + r, y1);
    cairo_arc(cr, x0 + r, y1 - r, r, M_PI_2, M_PI);

    cairo_line_to(cr, x0, y0 + r);
    cairo_arc(cr, x0 + r, y0 + r, r, M_PI, 3.0 * M_PI_2);
}

static void appendRoundedRectCW(
    cairo_t* cr,
    double x0, double y0,
    double x1, double y1,
    double r
) {
    r = std::max(0.0, std::min(r, std::min(x1 - x0, y1 - y0) * 0.5));

    cairo_move_to(cr, x0 + r, y0);

    cairo_line_to(cr, x0, y0 + r);
    cairo_arc_negative(cr, x0 + r, y0 + r, r, M_PI, M_PI_2);

    cairo_line_to(cr, x0 + r, y1);
    cairo_arc_negative(cr, x0 + r, y1 - r, r, M_PI_2, 0.0);

    cairo_line_to(cr, x1, y1 - r);
    cairo_arc_negative(cr, x1 - r, y1 - r, r, 0.0, -M_PI_2);

    cairo_line_to(cr, x1 - r, y0);
    cairo_arc_negative(cr, x1 - r, y0 + r, r, -M_PI_2, -M_PI);
}

void buildPseudostrokeRoundedRectPath(cairo_t* cr) {
    constexpr double x = 10.0;
    constexpr double y = 10.0;
    constexpr double w = 80.0;
    constexpr double h = 80.0;
    constexpr double r = 15.0;
    constexpr double s = 10.0;

    const double half = s * 0.5;

    const double ox0 = x - half;
    const double oy0 = y - half;
    const double ox1 = x + w + half;
    const double oy1 = y + h + half;
    const double ro  = r + half;

    const double ix0 = x + half;
    const double iy0 = y + half;
    const double ix1 = x + w - half;
    const double iy1 = y + h - half;
    const double ri  = r - half;

    cairo_new_path(cr);

    if ((ix1 <= ix0) || (iy1 <= iy0) || (ri < 0.0)) {
        appendRoundedRectCCW(cr, ox0, oy0, ox1, oy1, ro);
        cairo_close_path(cr);
        return;
    }

    // Outer contour
    appendRoundedRectCCW(cr, ox0, oy0, ox1, oy1, ro);

    // Explicit seam bridge from outer start to inner start.
    cairo_line_to(cr, ix0 + ri, iy0);

    // Inner contour, reversed
    appendRoundedRectCW(cr, ix0, iy0, ix1, iy1, ri);

    cairo_close_path(cr);
}

void buildRoundedRectPath(cairo_t* cr) {
    cairo_new_path(cr);
    appendRoundedRectCCW(cr, 10.0, 10.0, 90.0, 90.0, 15.0);
    cairo_close_path(cr);
}
#endif

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
    const double r  = std::max(0.0, std::min(r0, std::min(w, h) * 0.5));
    const double c  = KAPPA90 * r;

    cairo_new_path(cr);

    // Start on bottom edge.
    cairo_move_to(cr, x0 + r, y0);

    // Bottom edge -> bottom-right corner
    cairo_line_to(cr, x1 - r, y0);
    cairo_curve_to(cr,
        x1 - r + c, y0,
        x1,         y0 + r - c,
        x1,         y0 + r
    );

    // Right edge -> top-right corner
    cairo_line_to(cr, x1, y1 - r);
    cairo_curve_to(cr,
        x1,         y1 - r + c,
        x1 - r + c, y1,
        x1 - r,     y1
    );

    // Top edge -> top-left corner
    cairo_line_to(cr, x0 + r, y1);
    cairo_curve_to(cr,
        x0 + r - c, y1,
        x0,         y1 - r + c,
        x0,         y1 - r
    );

    // Left edge -> bottom-left corner
    cairo_line_to(cr, x0, y0 + r);
    cairo_curve_to(cr,
        x0,         y0 + r - c,
        x0 + r - c, y0,
        x0 + r,     y0
    );

    cairo_close_path(cr);
}

void buildPseudostrokeRoundedRectPath(cairo_t* cr) {
    constexpr double x = 10.0;
    constexpr double y = 10.0;
    constexpr double w = 80.0;
    constexpr double h = 80.0;
    constexpr double r0 = 15.0;
    constexpr double strokeWidth = 10.0;

    const double half = strokeWidth * 0.5;

    // Outer box
    const double ox0 = x - half;
    const double oy0 = y - half;
    const double ox1 = x + w + half;
    const double oy1 = y + h + half;
    const double ro0 = r0 + half;

    // Inner box
    const double ix0 = x + half;
    const double iy0 = y + half;
    const double ix1 = x + w - half;
    const double iy1 = y + h - half;
    const double ri0 = r0 - half;

    // Clamp radii sanely
    const double ro = std::max(0.0, std::min(ro0, std::min(ox1 - ox0, oy1 - oy0) * 0.5));
    const double ri = std::max(0.0, std::min(ri0, std::min(ix1 - ix0, iy1 - iy0) * 0.5));

    // If inner collapses, just draw the outer rounded rect.
    if ((ix1 <= ix0) || (iy1 <= iy0) || (ri0 < 0.0)) {
        const double c = KAPPA90 * ro;

        cairo_new_path(cr);
        cairo_move_to(cr, ox0 + ro, oy0);

        cairo_line_to(cr, ox1 - ro, oy0);
        cairo_curve_to(cr, ox1 - ro + c, oy0, ox1, oy0 + ro - c, ox1, oy0 + ro);

        cairo_line_to(cr, ox1, oy1 - ro);
        cairo_curve_to(cr, ox1, oy1 - ro + c, ox1 - ro + c, oy1, ox1 - ro, oy1);

        cairo_line_to(cr, ox0 + ro, oy1);
        cairo_curve_to(cr, ox0 + ro - c, oy1, ox0, oy1 - ro + c, ox0, oy1 - ro);

        cairo_line_to(cr, ox0, oy0 + ro);
        cairo_curve_to(cr, ox0, oy0 + ro - c, ox0 + ro - c, oy0, ox0 + ro, oy0);

        cairo_close_path(cr);
        return;
    }

    const double co = KAPPA90 * ro;
    const double ci = KAPPA90 * ri;

    cairo_new_path(cr);

    // ============================================================
    // OUTER contour (CCW)
    // ============================================================
    cairo_move_to(cr, ox0 + ro, oy0);

    // Bottom -> BR
    cairo_line_to(cr, ox1 - ro, oy0);
    cairo_curve_to(cr,
        ox1 - ro + co, oy0,
        ox1,           oy0 + ro - co,
        ox1,           oy0 + ro
    );

    // Right -> TR
    cairo_line_to(cr, ox1, oy1 - ro);
    cairo_curve_to(cr,
        ox1,           oy1 - ro + co,
        ox1 - ro + co, oy1,
        ox1 - ro,      oy1
    );

    // Top -> TL
    cairo_line_to(cr, ox0 + ro, oy1);
    cairo_curve_to(cr,
        ox0 + ro - co, oy1,
        ox0,           oy1 - ro + co,
        ox0,           oy1 - ro
    );

    // Left -> BL
    cairo_line_to(cr, ox0, oy0 + ro);
    cairo_curve_to(cr,
        ox0,           oy0 + ro - co,
        ox0 + ro - co, oy0,
        ox0 + ro,      oy0
    );

    // Bridge inward along the same bottom-edge seam
    cairo_line_to(cr, ix0 + ri, iy0);

    // ============================================================
    // INNER contour (CW, explicit reverse)
    // Start at bottom edge, go toward bottom-left first.
    // ============================================================

    // Bottom -> BL
    cairo_line_to(cr, ix0 + ri, iy0);
    cairo_curve_to(cr,
        ix0 + ri - ci, iy0,
        ix0,           iy0 + ri - ci,
        ix0,           iy0 + ri
    );

    // Left -> TL
    cairo_line_to(cr, ix0, iy1 - ri);
    cairo_curve_to(cr,
        ix0,           iy1 - ri + ci,
        ix0 + ri - ci, iy1,
        ix0 + ri,      iy1
    );

    // Top -> TR
    cairo_line_to(cr, ix1 - ri, iy1);
    cairo_curve_to(cr,
        ix1 - ri + ci, iy1,
        ix1,           iy1 - ri + ci,
        ix1,           iy1 - ri
    );

    // Right -> BR
    cairo_line_to(cr, ix1, iy0 + ri);
    cairo_curve_to(cr,
        ix1,           iy0 + ri - ci,
        ix1 - ri + ci, iy0,
        ix1 - ri,      iy0
    );

    // Bottom edge back to seam start
    cairo_line_to(cr, ix0 + ri, iy0);

    cairo_close_path(cr);
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char** argv) {
    osg::ArgumentParser args(&argc, argv);

    osgViewer::Viewer viewer(args);

    if(!example::setupArguments(args, "Demonstrates Cairo-authored Shapes")) return 0;

    constexpr uint32_t PIECE_KEY = 1;
    constexpr slug_t   SCALE     = 1.0_cv / 100.0_cv;

    // Cairo requires a surface even if we only want path data -- an image
    // surface at 1x1 is the lightest possible option for this purpose.
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t*         cr      = cairo_create(surface);

    // buildJigsawPiecePath(cr);
    buildTrianglePath(cr);
    // buildCirclePath(cr);
    // buildPseudostrokeCirclePath(cr);
    // buildRoundedRectPath(cr, 10, 10, 80, 80, 15);
    // buildPseudostrokeRoundedRectPath(cr, 10, 10, 80, 80, 15, 10);
    // buildPseudostrokeRoundedRectPath(cr);

    // slughorn::Atlas::ShapeInfo info;

    // info.autoMetrics = true;

    // slughorn::cairo::decomposePath(cr, info.curves, SCALE);
    auto [info, transform ] = slughorn::cairo::decomposePath(cr, SCALE);

    // info.curves = curves;

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

    const osgSlug::Atlas::Shape* shape = atlas->getShape(PIECE_KEY);

    if(shape) {
        std::cout
            << "Shape metrics:"        << std::endl
            << "  bearing  : ("        << shape->bearingX << ", " << shape->bearingY << ")" << std::endl
            << "  size     : "         << shape->width << " x " << shape->height << std::endl
            << "  advance  : "         << shape->advance  << std::endl
            << "  bandTex  : ("        << shape->bandTexX << ", " << shape->bandTexY << ")" << std::endl
            << "  bandMax  : ("        << shape->bandMaxX << ", " << shape->bandMaxY << ")" << std::endl
        ;
    }

    auto sd = osgx::make_ref<osgSlug::SSBOShapeDrawable>();

    sd->setAtlas(atlas);
    // sd->addLayer({PIECE_KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}, slughorn::Matrix::identity(), 200_cv});
    sd->addLayer({PIECE_KEY, {0.2_cv, 0.8_cv, 0.4_cv, 1.0_cv}});
    sd->compile();

    auto sdg = osgx::make_ref<osg::Geode>();

    sdg->addDrawable(sd);
    sdg->setStateSet(atlas->createDefaultStateSet());

    return example::run(viewer, args, sdg);
}
