#pragma once

#include "Atlas.hpp"
#include "slughorn/freetype.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Referenced>

OSGSLUG_ENABLE_WARNINGS

#include <string>
#include <cstdint>
#include <vector>
#include <map>

namespace osgSlug {

// Font
//
// A thin FreeType frontend that decomposes a TrueType/OpenType font into
// quadratic Bezier curves and feeds them into an Atlas.
//
// Usage:
//
// osg::ref_ptr<Atlas> atlas = new Atlas();
// osg::ref_ptr<Font> font = new Font("Roboto-Regular.ttf", atlas);
// font->load(); // FreeType runs; skips any key already in the atlas
// atlas->build(); // pack textures; atlas is now frozen
//
// TODO: Investigate how necessary this comment it; should we change this contract?
// The atlas is owned externally; Font holds a non-owning raw pointer.
//
// Key encoding:
//
//   Regular glyphs: Unicode codepoint cast to uint32_t
//   COLR layers: (codepoint << 8) | layerIndex (max 256 layers per glyph)
//
// COLR support:
//
// v0 - flat layer list via FT_Get_Color_Glyph_Layer (Twemoji etc.)
// v1 - paint graph via FT_Get_Color_Glyph_Paint, supporting:
//      PaintColrLayers - layer container
//      PaintGlyph - outline + child paint
//      PaintSolid - flat color fill
//      PaintTransform - affine transform baked into curve coords
//      PaintTranslate - translation baked into curve coords
//      PaintComposite - both paints rendered, blend mode ignored
//      PaintLinearGradient / PaintRadialGradient / PaintSweepGradient
//      first color stop used as flat approximation
//
// Everything else is skipped (for now).
class Font: public osg::Referenced {
public:
	Font(Atlas* atlas);
	Font(const std::string& fontPath, Atlas* atlas);

	// Decompose all printable ASCII glyphs (codepoints 32-126) via FreeType
	// and register them in the atlas. Codepoints already present in the
	// atlas (e.g. custom shapes injected before load()) are skipped.
	//
	// Does nothing if called more than once.
	bool load(slughorn::freetype::LoadConfig* config=nullptr);

	// Load COLR emoji from a font file (may be the same or different from
	// the text font; e.g. NotoColorEmoji.ttf or Twemoji.Mozilla.ttf).
	//
	// Automatically detects COLRv0 vs COLRv1 and uses the appropriate path.
	// Each codepoint is decomposed into N layers, each registered in the
	// atlas under key (codepoint << 8) | layerIndex. Layer colors are
	// read from the font's first palette (index 0).
	//
	// Must be called before atlas->build().
	bool loadEmoji(const std::string& fontPath, const std::vector<uint32_t>& codepoints);

	bool loaded() const { return _loaded; }

	const slughorn::FontMetrics& metrics() const { return _metrics; }

	// Convenience: map a Unicode codepoint to its atlas key.
	static uint32_t keyFor(uint32_t codepoint) { return codepoint; }

	// -------------------------------------------------------------------------
	// COLR emoji types
	//
	// Aliased from slughorn so that callers using osgSlug::Font::ColorLayer /
	// osgSlug::Font::ColorGlyph continue to compile without changes.
	// The underlying types are slughorn::ColorLayer / slughorn::ColorGlyph;
	// colors are slughorn::Color (four slug_t fields r/g/b/a) rather than
	// osg::Vec4. Convert at the call site with:
	// osg::Vec4(layer.color.r, layer.color.g, layer.color.b, layer.color.a)
	// -------------------------------------------------------------------------
	using ColorLayer = slughorn::Layer;
	using ColorGlyph = slughorn::CompositeShape;

	// Returns nullptr if the codepoint has no COLR data (i.e. was not loaded
	// via loadEmoji(), or the font has no COLR table).
	const ColorGlyph* getColorGlyph(uint32_t codepoint) const {
		auto it = _colorGlyphs.find(codepoint);

		return it != _colorGlyphs.end() ? &it->second : nullptr;
	}

protected:
	virtual ~Font();

private:
	std::string _fontPath;

	// TODO: This should probably be an OWNING instance (ref_ptr)!
	Atlas* _atlas = nullptr;

	bool _loaded = false;

	slughorn::FontMetrics _metrics;
	std::map<uint32_t, ColorGlyph> _colorGlyphs;
};

}
