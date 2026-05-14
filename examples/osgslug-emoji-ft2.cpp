//vimrun! ./osgslug-emoji-ft2

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

#define SLUGHORN_EMOJI_IMPLEMENTATION
#include "slughorn/emoji.hpp"
#include "slughorn/serial.hpp"

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(
		args,
		"Load a Shape/CompositeShape from a .slug/.slugb file",
		{},
		2,
		"FONT_FILE EMOJI"
	)) return 1;

	// if(!example::validatePositional(args, 2, "FONT_FILE EMOJI")) return example::fail(args, 1);

	std::string fontFile = args[1];
	std::string emoji = args[2];

	uint32_t emojiCodepoint = slughorn::emoji::randomCodepoint();

	if(emoji.size()) {
		if(auto cp = slughorn::emoji::nameToCodepoint(emoji); cp) emojiCodepoint = *cp;

		else {
			try {
				emojiCodepoint = static_cast<uint32_t>(std::stoul(emoji, nullptr, 16));
			}

			catch(const std::exception& e) {
				OSG_FATAL << "Invalid hex value '" << emoji << "'; exiting..." << std::endl;

				return example::fail(args, 2);
			}

			if(!slughorn::emoji::codepointToName(emojiCodepoint)) OSG_WARN
				<< "Couldn't resolve codepoint; we might crash!" << std::endl
			;
		}
	}

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>(atlas);

	// TODO: This isn't reliable yet...
	if(!font->loadEmoji(fontFile, {emojiCodepoint}, true)) {
		OSG_WARN << "Couldn't find " << emojiCodepoint << " in font: " << fontFile << std::endl;

		return example::fail(args, 3);
	}

	OSG_NOTICE << "Found emoji '" << emojiCodepoint << "'; continuing..." << std::endl;

	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	const osgSlug::Font::ColorGlyph* dragon = font->getColorGlyph(emojiCodepoint);

	slughorn::CompositeShape cs;

	if(dragon) {
		for(const auto& layer : dragon->layers) {
			auto color = osg::Vec4(
				layer.color.r,
				layer.color.g,
				layer.color.b,
				layer.color.a
			);

			OSG_NOTICE
				<< "Adding layer: " << std::hex << layer.key
				<< " color=" << color
				<< std::endl
			;

			cs.layers.push_back(layer);
		}

		atlas->addCompositeShape(slughorn::Key::fromString("emoji"), cs);
	}

	// slughorn::serial::writeJSON(*atlas, std::cerr);

	sd->setAtlas(atlas);
	sd->addCompositeShape(cs);
	sd->compile();
	sd->setName("sd");
	// sd->setUserValue("path", std::string("emoji"));

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet());
	sdg->setName("sdg");

	return example::run(viewer, args, sdg);
}
