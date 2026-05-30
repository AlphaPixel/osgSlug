#include "osgSlug/Font.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Notify>

OSGSLUG_ENABLE_WARNINGS

namespace osgSlug {

Font::Font(Atlas* atlas):
_atlas(atlas) {
}

Font::Font(const std::string& fontPath, Atlas* atlas):
_fontPath(fontPath),
_atlas(atlas) {
#if 0
	// Forward slughorn::freetype log messages through OSG's notification system so
	// that they respect OSG_NOTIFY_LEVEL and appear alongside other OSG output.
	//
	// NOTE: setLogCallback is process-global state in slughorn::freetype. If your
	// application creates multiple Font instances (e.g. one per thread) you
	// should set the callback once at startup rather than in each constructor.
	slughorn::freetype::setLogCallback([](int level [[maybe_unused]], const std::string& msg) {
		OSG_NOTIFY(
			level >= slughorn::freetype::LOG_WARN ? osg::WARN :
			level >= slughorn::freetype::LOG_NOTICE ? osg::NOTICE :
			osg::INFO
		) << msg << std::endl;
	});
#endif
}

Font::~Font() = default;

void Font::load(const slughorn::freetype::LoadConfig& config) {
	if(_loaded) return;

	if(!_atlas) {
		OSG_WARN << "osgSlug::Font::load: no atlas set" << std::endl;

		return;
	}

	if(!slughorn::freetype::loadAsciiFont(_fontPath, *_atlas, config)) return;

	if(auto m = slughorn::freetype::loadFontMetrics(_fontPath)) _metrics = *m;

	_loaded = true;
}

// TODO: This is a quick, hacky way... we need to be able VERIFY that the requested codepoints were
// actually FOUND and loaded.
bool Font::loadEmoji(const std::string& fontPath, const std::vector<uint32_t>& codepoints) {
	if(!_atlas) {
		OSG_WARN << "osgSlug::Font::loadEmoji: no atlas set" << std::endl;

		return false;
	}

	return slughorn::freetype::loadEmojiFont(fontPath, codepoints, *_atlas, _colorGlyphs);

#if 0
	// return false;
	else return slughorn::freetype::loadEmojiFont(
		fontPath,
		codepoints,
		*_atlas,
		_colorGlyphs,
		[](const slughorn::Atlas::Curves& curves) {
			int n = static_cast<int>(std::min(size_t(16), std::max(size_t(1), curves.size() / 2)));

			return slughorn::Atlas::computeAdaptiveSplits(curves, n, n);
		}
	);
#endif
}

}
