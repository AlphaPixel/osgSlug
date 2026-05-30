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
		1,
		"FONT_FILE EMOJI"
	)) return 1;

	// if(!example::validatePositional(args, 2, "FONT_FILE EMOJI")) return example::fail(args, 1);

	std::string fontFile = args[1];
	std::string emoji = args.argc() >= 3 ? args[2] : "";

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
	if(!font->loadEmoji(fontFile, {emojiCodepoint})) {
		OSG_WARN << "Couldn't find " << emojiCodepoint << " in font: " << fontFile << std::endl;

		return example::fail(args, 3);
	}

	OSG_NOTICE << "Found emoji '" << emojiCodepoint << "'; continuing..." << std::endl;

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

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

		atlas->addCompositeShape(slughorn::Key("emoji"), cs);
	}

	// slughorn::serial::writeJSON(*atlas, std::cerr);

	sd->setAtlas(atlas);
	sd->addCompositeShape(cs);
	sd->compile();
	sd->setName("sd");
	// sd->setUserValue("path", std::string("emoji"));

	auto sdg = osgx::make_ref<osg::Geode>();

	sdg->addDrawable(sd);
	sdg->setStateSet(atlas->createDefaultStateSet(example::USE_GL3));
	sdg->setName("sdg");
	// sdg->getOrCreateStateSet()->addUniform(new osg::Uniform("osgSlug_layerMask", 2));

	auto* lu = new osg::Uniform("osgSlug_layerMask", 0);

	sdg->getOrCreateStateSet()->addUniform(lu);

	viewer.addEventHandler(new osgx::LambdaKeyHandler('n', [&lu](auto& ea, auto& aa) {
		int luv = 0;

		lu->get(luv);

		constexpr int maxBits = 30; // avoid signed int sign bit

		if(luv != ((1 << maxBits) - 1)) luv = (luv << 1) | 1;

		lu->set(luv);

		return true;
	}));

	return example::run(viewer, args, sdg);
}
