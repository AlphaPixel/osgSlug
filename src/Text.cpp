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

void Text::setAutoScaleToScreen(bool value) {
	_autoScaleToScreen = value;

	osg::AutoTransform::setAutoScaleToScreen(value);
	setAutoRotateMode(osg::AutoTransform::ROTATE_TO_SCREEN);
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

	for(const auto& run : _runs) {
		for(char ch : run.text) {
			if(ch == '\n') {
				cursorX = 0_cv;
				cursorY -= _fontSize; // / 1.5_cv;

				continue;
			}

			const uint32_t key = static_cast<uint32_t>(static_cast<unsigned char>(ch));
			const slughorn::Atlas::Shape* shape = _atlas->getShape(key);

			if(!shape) {
				cursorX += 0.5_cv * _fontSize;

				continue;
			}

			// Metric-only shapes (whitespace etc.); advance cursor but emit no geometry.
			if(shape->width < 1e-6_cv || shape->height < 1e-6_cv) {
				cursorX += shape->advance * _fontSize;

				continue;
			}

			_drawable->addLayer(slughorn::Layer{
				key,
				run.color,
				slughorn::Matrix{.dx = cursorX / _fontSize, .dy = cursorY / _fontSize},
				cv(_fontSize)
			});

			cursorX += shape->advance * _fontSize;
		}
	}

	_drawable->compile();
}

}
