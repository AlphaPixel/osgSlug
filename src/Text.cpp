#include "osgSlug/Text.hpp"

namespace osgSlug {

Text::Text() {
	_geode = new osg::Geode();
	_drawable = new ShapeDrawable();

	_geode->addDrawable(_drawable);

	addChild(_geode);
}

Text::Text(Atlas* atlas, slug_t fontSize):
_fontSize(fontSize),
_atlas(atlas) {
	_geode = new osg::Geode();
	_drawable = new ShapeDrawable();

	_drawable->setAtlas(_atlas);
	_drawable->setName(getName());

	_geode->addDrawable(_drawable);
	_geode->setStateSet(_atlas->createDefaultStateSet());

	addChild(_geode);
}

Text::~Text() = default;

void Text::addText(const std::string& text, const slughorn::Color& color) {
	_runs.push_back({text, color});
}

void Text::clear() {
	_runs.clear();
}

void Text::setAtlas(Atlas* atlas) {
	_atlas = atlas;

	_drawable->setAtlas(_atlas);
	_geode->setStateSet(_atlas->createDefaultStateSet());
}

void Text::setFontSize(slug_t pixelsPerEm) {
	_fontSize = pixelsPerEm;
}

void Text::setHooks(Atlas::HookList hooks) {
	_drawable->setHooks(std::move(hooks));
}

void Text::setAutoScaleToScreen(bool value) {
	osg::AutoTransform::setAutoScaleToScreen(value);
}

void Text::setEffectId(uint32_t effectId) {
	_effectId = effectId;

	if(!_drawable->getLayerBuffer(0)) return;

	for(size_t i = 0; i < _drawable->getNumLayers(); i++) {
		_drawable->setLayerEffectId(i, effectId);
	}

	_drawable->dirtyLayers();
}

void Text::setEffectParam(slug_t effectParam) {
	_effectParam = effectParam;

	if(!_drawable->getLayerBuffer(0)) return;

	for(size_t i = 0; i < _drawable->getNumLayers(); i++) {
		_drawable->setLayerEffectParam(i, effectParam);
	}

	_drawable->dirtyLayers();
}

const osg::BoundingBox& Text::getBoundingBox() const {
	return _drawable->getBoundingBox();
}

// ================================================================================================
// compile()
//
// Walks accumulated runs in order, resolving glyph keys, tracking cursor advancement, and skipping
// whitespace / missing glyphs. All geometry building is delegated to ShapeDrawable.
//
// dx/dy are stored in em-space on each Layer; computeQuad() multiplies by Layer::scale (fontSize)
// to recover world-space position. This is the standard FreeType convention; see slughorn's scale
// contract documentation.
// ================================================================================================

void Text::compile() {
	// _drawable->clearLayers();

	if(_runs.empty() || !_atlas || !_atlas->isBuilt()) return;

	slug_t cursorX = 0_cv;
	slug_t cursorY = 0_cv; // -cv(_fontSize); // 0_cv;

	const auto tpa = osgSlug::getEnv<int>("TEXT_PIXEL_ALIGN", 0);

	for(const auto& run : _runs) {
		for(char ch : run.text) {
			if(ch == '\n') {
				cursorX = 0_cv;

				// Use font's recommended line gap; fall back to CSS-standard 1.2x when absent.
				const slug_t leading = _metrics.lineGapRatio > 0_cv
					? _fontSize * (1_cv + _metrics.lineGapRatio)
					: _fontSize * 1.2_cv
				;

				cursorY -= leading;

				continue;
			}

			const uint32_t key = static_cast<uint32_t>(static_cast<unsigned char>(ch));
			const auto shape = _atlas->getShape(key);

			if(!shape) {
				cursorX += 0.5_cv * _fontSize;

				continue;
			}

			// Metric-only shapes (whitespace etc.); advance cursor but emit no geometry.
			if(shape->width < 1e-6_cv || shape->height < 1e-6_cv) {
				cursorX += shape->advance * _fontSize;

				continue;
			}

			slughorn::Layer layer{
				key,
				run.color,
				slughorn::Transform{.x = cursorX / _fontSize, .y = cursorY / _fontSize},
				cv(_fontSize)
			};

			layer.effectId = _effectId;
			layer.effectParam = _effectParam;

			_drawable->addLayer(layer);

			cursorX += shape->advance * _fontSize;

			if(tpa == 1) cursorX = std::round(cursorX);

			else if(tpa == 2) cursorX = std::round(cursorX) + 0.5_cv;
		}
	}

	_drawable->compile();
}

}
