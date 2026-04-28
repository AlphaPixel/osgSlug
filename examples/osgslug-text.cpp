//vimrun! ./osgslug-simple

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/MatrixTransform>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

OSGSLUG_ENABLE_WARNINGS

#include <CLI/CLI.hpp>

#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace rv = std::ranges::views;

static std::string read_all(std::istream& in) {
	return {
		std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>()
	};
}

static std::string read_file(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	if(!in) throw std::runtime_error("Failed to open input file: " + path);

	return read_all(in);
}

static std::vector<std::string> splitLines(std::string_view text) {
	auto to_string = [](auto&& part) {
		return std::string(std::ranges::begin(part), std::ranges::end(part));
	};

	std::vector<std::string> lines;
	for(auto&& line : text | rv::split('\n') | rv::transform(to_string)) {
		// Optional CR stripping for Windows-style line endings.
		// if(!line.empty() && line.back() == '\r') line.pop_back();

		lines.push_back(std::move(line));
	}

	return lines;
}

int main(int argc, char** argv) {
	CLI::App app{"osgSlug text demo"};

	std::string inputFile;
	std::string fontFile;
	bool randomColors = false;

	app.add_option("font", fontFile, "Specify the TTF/OTF to use")->required();
	app.add_option("-i,--input", inputFile, "Read text from file instead of stdin");
	app.add_flag("-r,--random", randomColors, "Use random colors for each line of text");

	CLI11_PARSE(app, argc, argv);

	std::string textStr = inputFile.empty() ? read_all(std::cin) : read_file(inputFile);

	auto lines = splitLines(textStr);

	if(!lines.empty() && lines.back().empty()) lines.pop_back();

	std::random_device rd;
	std::mt19937 rng(rd());
	std::uniform_real_distribution<slug_t> dist(0_cv, 1_cv);

	std::vector<slughorn::Color> colors;
	colors.reserve(lines.size());

	for(std::size_t i = 0; i < lines.size(); ++i) {
		if(randomColors) colors.push_back(slughorn::Color{
			dist(rng),
			dist(rng),
			dist(rng),
			1_cv
		});

		else colors.push_back(slughorn::Color{1_cv, 1_cv, 1_cv, 1_cv});
	}

	osgViewer::Viewer viewer;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>(fontFile, atlas);

	font->load();

	if(!font->loaded()) {
		OSG_WARN << "Couldn't load font: " << std::endl;

		return 1;
	}

	atlas->build();
	atlas->packTextures();

	auto text = osgx::make_ref<osgSlug::Text>(atlas, 1_cv);

	for(std::size_t i = 0; i < lines.size(); ++i) {
		const auto& c = colors[i];

		text->addText(lines[i] + '\n', c);

		std::cout
			<< "[" << i << "] "
			<< "\"" << lines[i] << "\"  "
			<< "color=("
			<< c.r << ", " << c.g << ", " << c.b << ", " << c.a << ")"
			<< std::endl
		;
	}

	text->compile();

	auto root = osgx::make_ref<osg::MatrixTransform>();

	root->setMatrix(osg::Matrix::rotate(osg::DegreesToRadians(90.0), osg::Vec3(1, 0, 0)));
	root->addChild(text);

	viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));
	viewer.setSceneData(root);
	viewer.addEventHandler(new osgViewer::StatsHandler());

	return viewer.run();
}
