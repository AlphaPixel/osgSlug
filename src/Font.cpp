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
}

Font::~Font() = default;

void Font::load(bool adaptive) {
	if(_loaded) return;

	if(!_atlas) {
		OSG_WARN << "osgSlug::Font::load: no atlas set" << std::endl;

		return;
	}

	if(!adaptive) {
		if(!slughorn::freetype::loadAsciiFont(_fontPath, *_atlas)) {
			// loadAsciiFont already logged the reason via the callback above.
			return;
		}
	}

	else {
		if(!slughorn::freetype::loadAsciiFont(
			_fontPath,
			*_atlas,
			[](const slughorn::Atlas::Curves& curves) {
				int n = static_cast<int>(std::min(size_t(16), std::max(size_t(1), curves.size() / 2)));

				return slughorn::Atlas::computeAdaptiveSplits(curves, n, n);
				// return slughorn::Atlas::computeUniformSplits(curves, n, n);
			}
		)) {
			return;
		}
	}

	_loaded = true;
}

// TODO: This is a quick, hacky way... we need to be able VERIFY that the requested codepoints were
// actually FOUND and loaded.
bool Font::loadEmoji(const std::string& fontPath, const std::vector<uint32_t>& codepoints, bool adaptive) {
	if(!_atlas) {
		OSG_WARN << "osgSlug::Font::loadEmoji: no atlas set" << std::endl;

		return false;
	}

	if(!adaptive) return slughorn::freetype::loadEmojiFont(fontPath, codepoints, *_atlas, _colorGlyphs);

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
}

}
