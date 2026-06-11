//vimrun! ./osgslug-text font/UbuntuMono-R.ttf --input-file poem.txt

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"
#include "osgSlug/Text.hpp"

#include <random>

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

// TODO: Convert to osgx::make_ref!
osg::Camera* createOrthoCamera(slug_t width, slug_t height) {
	osg::Camera* camera = new osg::Camera();

	camera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	camera->setProjectionMatrix(osgSlug::Matrix::ortho2D(0_cv, width, 0_cv, height));
	camera->setViewMatrix(osgSlug::Matrix::identity());
	camera->setViewport(new osg::Viewport(0, 0, static_cast<int>(width), static_cast<int>(height)));
	camera->setClearMask(GL_DEPTH_BUFFER_BIT);
	camera->setRenderOrder(osg::Camera::POST_RENDER);

	return camera;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(
		args,
		"Reads text from a file or stdin",
		{
			{
				"--input-file <string>",
				"File to read text input from"
			},
			{
				"--font-size <int>",
				"Size in font pts (DEFAULT: 16)"
			},
			{
				"--font-color <color>",
				"Font color to use (RGBA 0.0-1.0)"
			},
			{
				"--perspective",
				"Use a traditional 3D perspective view (instead of ortho2D)"
			}
		},
		1,
		"FONT_FILE"
	)) return 1;

	std::string inputFile;
	std::string fontColor;

	bool randomColors = false;
	bool smallText = false;

	if(args.read("--input-file", inputFile)) {}
	if(args.read("--random-colors")) randomColors = true;
	if(args.read("--small-text")) smallText = true;

	int fontSize = 16;

	while(args.read("--font-size", fontSize)) {}

	slughorn::Color color{1_cv, 1_cv, 1_cv, 1_cv};

	while(args.read("--font-color", fontColor)) {
		int cnt = sscanf(fontColor.c_str(), "%f,%f,%f,%f", &color.r, &color.g, &color.b, &color.a);

		if(cnt <= 2) return example::fail(args, 1, "Invalid '--font-color' argument");

		OSG_WARN << "FONT_COLOR: " << color << std::endl;
	}

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

		else colors.push_back(color);
	}

	if(!args[1]) return example::fail(args, 1, "Missing required FONT_FILE argument");

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>(args[1], atlas);

	slughorn::freetype::LoadConfig config;

	config.strategy = [](const slughorn::Atlas::Curves& c) {
		return slughorn::Atlas::computeUniformSplits(c, 15, 15);
	};

	if(!font->load(&config)) return example::fail(args, 1, "Couldn't load font: " + std::string(args[1]));

	atlas->build();
	atlas->packTextures();

	auto text = osgx::make_ref<osgSlug::Text>(
		atlas,
		osgSlug::Text::fromPixels(cv(fontSize))
	);

	text->setFontMetrics(font->metrics());

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
	text->setName("text");

	if(smallText) {
		text->getOrCreateStateSet()->addUniform(new osg::Uniform("osgSlug_textMode", true));
		text->getOrCreateStateSet()->addUniform(new osg::Uniform("osgSlug_stemDarken", true));
		text->getOrCreateStateSet()->addUniform(new osg::Uniform("osgSlug_gamma", 0.454f));
	}

	if(args.read("--perspective")) return example::run(viewer, args, text);

	else {
		auto mat = osgx::make_ref<osg::MatrixTransform>();

		mat->addChild(text);
		// mat->setMatrix(osg::Matrix::translate(osgSlug::Vec3(3_cv, 440_cv, 0)));

		auto* project2d = createOrthoCamera(800_cv, 600_cv);

		project2d->addChild(mat);

		return example::run(viewer, args, project2d, false);
	}
}
