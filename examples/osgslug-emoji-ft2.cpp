//vimrun! ./osgslug-emoji-ft2

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

#define SLUGHORN_EMOJI_IMPLEMENTATION
#include "slughorn-emoji.hpp"
#include "slughorn-serial.hpp"

#include "osgDebug.hpp"

#include "CLI/CLI.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/io_utils>
#include <osg/MatrixTransform>

#include <osgGA/TrackballManipulator>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

int main(int argc, char** argv) {
	CLI::App app{"osgslug-emoji-ft2"};

	std::string fontFile;
	std::string emoji;

	app.add_option("font", fontFile, "Input font file (TTT/OTF)")->required();
	app.add_option("emoji", emoji, "Emoji codepoint (or name from slughorn-emoji.hpp)");

	CLI11_PARSE(app, argc, argv);

	uint32_t emojiCodepoint = slughorn::emoji::randomCodepoint();

	if(emoji.size()) {
		if(auto cp = slughorn::emoji::nameToCodepoint(emoji); cp) emojiCodepoint = *cp;

		else {
			try {
				emojiCodepoint = static_cast<uint32_t>(std::stoul(emoji, nullptr, 16));
			}

			catch(const std::exception& e) {
				OSG_FATAL << "Invalid hex value '" << emoji << "'; exiting..." << std::endl;

				return 2;
			}

			if(!slughorn::emoji::codepointToName(emojiCodepoint)) OSG_WARN
				<< "Couldn't resolve codepoint; we might crash!" << std::endl
			;
		}
	}

	osgViewer::Viewer viewer;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>(atlas);

	// TODO: This isn't reliable yet...
	if(!font->loadEmoji(fontFile, {emojiCodepoint})) {
		OSG_WARN << "Couldn't find " << emojiCodepoint << " in font: " << fontFile << std::endl;

		return 3;
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

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
	root->addChild(sdg);
	root->setName("root");

	auto dv = osgDebug::DrawVisitor();

	root->accept(dv);

	auto debugSupported = osgx::make_ref<osgDebug::GraphicsOperation>();

	viewer.setRealizeOperation(debugSupported);
	viewer.setCameraManipulator(new osgGA::TrackballManipulator());
	viewer.realize();
	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
