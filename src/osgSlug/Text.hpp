#pragma once

#include "Drawable.hpp"
#include "slughorn/freetype.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/AutoTransform>
#include <osg/Geode>

OSGSLUG_ENABLE_WARNINGS

#include <string>
#include <vector>

namespace osgSlug {

// Inherits from osg::AutoTransform so that setAutoScaleToScreen(true) keeps the text at a constant
// pixel size regardless of camera distance. When auto-scale is off (default), the transform is
// identity and behaviour is identical to using a plain Geode.
//
// Text is atlas-agnostic: it renders whatever keys the atlas contains, whether those came from Font
// (FreeType), hand-crafted curves, Skia paths, or any other source.
//
// Internally, Text is a thin cursor/advance layer on top of ShapeDrawable. All geometry building
// is delegated there; Text only knows about strings, glyph metrics, and baseline advancement.
//
// Usage:
//
// auto* text = new osgSlug::Text(atlas, Text::fromPoints(12_cv, 96_cv));
// text->addText("Hello ", {1_cv, 0.5_cv, 0_cv, 1_cv});
// text->addText("World", {0.4_cv, 0.6_cv, 1_cv, 1_cv});
// text->addText("\nLine 2");
// text->compile();
class Text: public osg::AutoTransform {
public:
	Text();
	Text(Atlas* atlas, slug_t fontSize=32_cv);

	// Append a styled text run. Newlines within the string start new lines.
	// Runs accumulate until compile() is called.
	void addText(
		const std::string& text,
		const slughorn::Color& color = {1_cv, 1_cv, 1_cv, 1_cv}
	);

	// Clear all accumulated runs (does not recompile).
	void clear();

	void setAtlas(Atlas* atlas);
	void setFontSize(slug_t pixelsPerEm);

	slug_t getFontSize() const { return _fontSize; }

	void setFontMetrics(const slughorn::FontMetrics& m) { _metrics = m; }
	const slughorn::FontMetrics& getFontMetrics() const { return _metrics; }

	void setDpi(slug_t dpi) { _dpi = dpi; }
	slug_t getDpi() const { return _dpi; }

	// --- Static conversion helpers (usable before a Text object exists) ------

	// em-size in world units for a given point size at dpi.
	static slug_t fromPoints(slug_t pointSize, slug_t dpi=96_cv) {
		return cv(pointSize * (dpi / 72_cv));
	}

	// em-size so that cap-height letters are exactly capPixels world units tall.
	static slug_t fromCapHeight(slug_t capPixels, slug_t capHeightRatio) {
		return cv(capPixels / capHeightRatio);
	}

	// em-size = pixels directly (explicit / power-user path).
	static slug_t fromPixels(slug_t pixels) {
		return cv(pixels);
	}

	// Reverse: point size that corresponds to emSize at dpi.
	static slug_t toPoints(slug_t emSize, slug_t dpi=96_cv) {
		return static_cast<slug_t>(emSize) * (72_cv / dpi);
	}

	// Reverse: cap-height in world units at emSize.
	static slug_t toCapHeight(slug_t emSize, slug_t capHeightRatio) {
		return static_cast<slug_t>(emSize) * static_cast<slug_t>(capHeightRatio);
	}

	// --- Instance helpers (use stored metrics / dpi) --------------------------

	slug_t fromCapHeight(slug_t capPixels) const {
		return fromCapHeight(capPixels, static_cast<slug_t>(_metrics.capHeightRatio));
	}

	slug_t toCapHeight(slug_t emSize) const {
		return toCapHeight(emSize, static_cast<slug_t>(_metrics.capHeightRatio));
	}

	void setAutoScaleToScreen(bool value);
	bool getAutoScaleToScreen() const { return _autoScaleToScreen; }

	// Build geometry from accumulated runs. Call after addText().
	void compile();

	const osg::BoundingBox& getBoundingBox() const;

protected:
	virtual ~Text();

private:
	struct Run {
		std::string text;
		slughorn::Color color;
	};

	std::vector<Run> _runs;

	slug_t _fontSize = 32_cv;
	slug_t _dpi = 96_cv;
	bool _autoScaleToScreen = false;

	slughorn::FontMetrics _metrics;

	osg::ref_ptr<Atlas> _atlas;
	osg::ref_ptr<osg::Geode> _geode;
	osg::ref_ptr<ShapeDrawable> _drawable;
};

}
