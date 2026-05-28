//vimrun! ./osgslug-emoji-grid

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

#define SLUGHORN_EMOJI_IMPLEMENTATION
#include "slughorn/emoji.hpp"
#include "slughorn/serial.hpp"

#include <cmath>

static const std::vector<std::string> DEFAULT_EMOJIS = {
	"fire",
	"rainbow",
	"crystal_ball",
	"gem_stone",
	"butterfly",
	"water_wave",
	"dragon",
	"fireworks",
	"sunrise"
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(
		args,
		"Render a grid of COLR emoji",
		{},
		1,
		"FONT_FILE [EMOJI ...]"
	)) return 1;

	std::string fontFile = args[1];

	// Collect emoji from remaining args; fall back to gradient-heavy defaults.
	std::vector<uint32_t> codepoints;

if(auto cp = slughorn::emoji::nameToCodepoint("fireworks"); cp) {
    OSG_NOTICE << "fireworks = U+" << std::hex << *cp << std::dec << std::endl;
}
else {
    OSG_WARN << "fireworks lookup failed" << std::endl;
}

	for(int i = 2; i < args.argc(); i++) {
		const std::string tok = args[i];

		if(auto cp = slughorn::emoji::nameToCodepoint(tok); cp) {
			codepoints.push_back(*cp);
		}

		else {
			try {
				codepoints.push_back(static_cast<uint32_t>(std::stoul(tok, nullptr, 16)));
			}

			catch(const std::exception&) {
				OSG_FATAL << "Invalid emoji '" << tok << "'; expected a name or hex codepoint" << std::endl;

				return example::fail(args, 2);
			}
		}
	}

	if(codepoints.empty()) {
		for(const auto& name : DEFAULT_EMOJIS) {
			if(auto cp = slughorn::emoji::nameToCodepoint(name); cp) codepoints.push_back(*cp);
		}
	}

	// Load all emoji into one shared atlas before build().
	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font  = osgx::make_ref<osgSlug::Font>(atlas);

	if(!font->loadEmoji(fontFile, codepoints, true)) {
		OSG_WARN << "Some emoji may be missing from the font" << std::endl;
	}

	atlas->build();
	atlas->packTextures();

	auto stateSet = atlas->createDefaultStateSet(example::USE_GL3);

	// Grid layout: square-ish, 1.2-unit spacing, centered at origin.
	const size_t cols = static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(codepoints.size()))));
	const size_t rows = static_cast<size_t>(std::ceil(static_cast<double>(codepoints.size()) / static_cast<double>(cols)));
	const float step  = 1.2f;
	const float ox    = -step * static_cast<float>(cols - 1) / 2.0f;
	const float oy    =  step * static_cast<float>(rows - 1) / 2.0f;

	auto root = osgx::make_ref<osg::Group>();

	for(size_t idx = 0; idx < codepoints.size(); idx++) {
		const uint32_t cp = codepoints[idx];
		const auto* glyph = font->getColorGlyph(cp);

		if(!glyph || glyph->layers.empty()) {
			OSG_WARN << "No COLR data for U+" << std::hex << cp << std::endl;

			continue;
		}

		auto sd = example::makeShapeDrawable();

		sd->setAtlas(atlas);
		sd->addCompositeShape(*glyph);
		sd->compile();

		auto geode = osgx::make_ref<osg::Geode>();

		geode->addDrawable(sd);
		geode->setStateSet(stateSet);

		const float col = static_cast<float>(idx % cols);
		const float row = static_cast<float>(idx / cols);

		auto xform = osgx::make_ref<osg::MatrixTransform>();

		xform->setMatrix(osgSlug::Matrix::translate(
			ox + col * step,
			oy - row * step,
			0.0_cv
		));

		xform->addChild(geode);
		root->addChild(xform);
	}

	return example::run(viewer, args, root);
}
