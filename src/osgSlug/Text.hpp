#pragma once

#include "Drawable.hpp"
#include "Drawable/ShapeDrawable.hpp"
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
// auto* text = new osgSlug::Text(atlas, Font::fromPoints(12_cv, 96_cv));
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
	bool _autoScaleToScreen = false;

	slughorn::FontMetrics _metrics;

	osg::ref_ptr<Atlas> _atlas;
	osg::ref_ptr<osg::Geode> _geode;
	osg::ref_ptr<ShapeDrawable> _drawable;
};

}
