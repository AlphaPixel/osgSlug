// vimrun! ./osgslug-compare font/UbuntuMono-R.ttf
//
// Side-by-side comparison of glyph rendering techniques on ONE glyph (default `R`) from ONE font,
// each rendered by its own osgx::RTT into one cell of the window:
//
//   slughorn (osgSlug) - analytic coverage computed from the curves
//   osgx::SDF (MSDF) - multi-channel distance field, baked by slughorn from the curves
//   osgx::SDF (SDF) - single-channel distance field, baked by slughorn from the curves
//   osgText (GREYSCALE) - FreeType-rasterized glyph bitmap
//
// Built with osgRive (CMake OSGRIVE_DIR), the grid grows from 2x2 to 3x2 and adds:
//
//   Rive - HarfBuzz glyph outline filled by Rive's renderer (osgRive::TextDraw). DEFAULT is
//     screen-space: every frame the camera's em -> panel-pixel mapping is handed to Rive as a 2D
//     affine, so Rive re-tessellates at the true on-screen size (exact face-on at any zoom; under
//     perspective tilt the affine can only approximate the projection). --rive-texture instead
//     renders Rive once into a glyph-sized texture on a 3D quad (--rive-resolution px/em).
//   osgText (SIGNED_DISTANCE_FIELD) - distance field osgText derives from the rasterized bitmap
//
// Every RTT is a RELATIVE_RF child of the main camera, so all inherit its view and projection and
// one TrackballManipulator drives them identically (see makePanel() for the aspect correction).
//
// --resolution sets the texel budget per em shared by every texture-based technique: osgText's
// font resolution, the SDF/MSDF bake density (texelsPerEm), and (--rive-texture only) Rive's texture
// density. --rive-resolution overrides Rive's alone: Rive has no distance field to reconstruct
// edges from, so at equal density it is judged purely on bilinear magnification of its texture.
//
// Pixel-peep: '[' / ']' step a nearest-neighbor zoom through 1, 2, 4, 8, 16, 32x, anchored on the
// mouse position (the same panel-local spot in every panel); 'g' toggles a pixel grid
// (zoom >= 4), 'f' freezes the anchor. See PixelPeep.
//
// The window is a fixed size: the RTT textures are sized to its cells at startup.

#include "osgslug-example.hpp"

#include "osgSlug/Font.hpp"

#include "osgx/RTT.hpp"
#include "osgx/SDF.hpp"
#include "osgx/Shader.hpp"

#include "slughorn/freetype.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Image>
#include <osg/Texture2D>

#include <osgText/Font>
#include <osgText/Text>

#ifdef OSGSLUG_WITH_OSGRIVE
#include <osgRive/Scene>
#include <osgRive/Text>
#endif

OSGSLUG_ENABLE_WARNINGS

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>

// With osgRive: 3x2 (adds Rive, plus osgText's SIGNED_DISTANCE_FIELD technique in the sixth cell).
#ifdef OSGSLUG_WITH_OSGRIVE
static constexpr int GRID_COLS = 3;
#else
static constexpr int GRID_COLS = 2;
#endif

static constexpr int GRID_ROWS = 2;
static constexpr int PANEL_W = 520;
static constexpr int PANEL_H = 390;
static constexpr int WINDOW_W = PANEL_W * GRID_COLS;
static constexpr int WINDOW_H = PANEL_H * GRID_ROWS;

// World units per em, shared by every technique.
static constexpr float EM = 100.0f;

static const osg::Vec4 BACKGROUND(0.1f, 0.1f, 0.1f, 1.0f);
static const osg::Vec4 FOREGROUND(0.95f, 0.95f, 0.95f, 1.0f);

// Generic vertex attributes at explicit locations, same as osgx-sdf.cpp.
static const char* FIELD_VERT = R"(
#version 430 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 vUV;

void main() {
	vUV = uv;
	gl_Position = osg_ModelViewProjectionMatrix * vec4(position, 1.0);
})";

// SAMPLING must precede TEXTURE - see registerSDFShaderLibs() in osgx/SDF.hpp.
static const char* FIELD_FRAG = R"(
#version 430 core

#pragma osgx::sdf SAMPLING,TEXTURE

in vec2 vUV;

uniform vec4 fillColor;

out vec4 fragColor;

void main() {
	fragColor = vec4(fillColor.rgb, fillColor.a * osgx_SDF_Coverage(vUV));
})";

static const char* COMPOSITE_VERT = R"(
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 uv;

void main() {
	uv = osg_MultiTexCoord0;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
})";

// Panels are row-major from the top-left cell of a (cols, rows) grid.
static const char* COMPOSITE_FRAG = R"(
#version 330 core

uniform sampler2D panel0;
uniform sampler2D panel1;
uniform sampler2D panel2;
uniform sampler2D panel3;
uniform sampler2D panel4;
uniform sampler2D panel5;

// (cols, rows)
uniform vec2 grid;

// Magnification factor; 1 is an exact 1:1 copy of each panel.
uniform float peepZoom;

// Panel-local [0, 1] point the magnification is anchored on (stays fixed on screen).
uniform vec2 peepCenter;

// Size, in texels, of each panel texture.
uniform vec2 peepTexels;

uniform bool peepGrid;

in vec2 uv;

out vec4 color;

vec4 samplePanel(int i, vec2 st) {
	if(i == 0) return texture(panel0, st);
	if(i == 1) return texture(panel1, st);
	if(i == 2) return texture(panel2, st);
	if(i == 3) return texture(panel3, st);
	if(i == 4) return texture(panel4, st);

	return texture(panel5, st);
}

void main() {
	vec2 q = uv * grid;
	vec2 cell = min(floor(q), grid - 1.0);
	vec2 local = q - cell;

	vec2 st = peepCenter + (local - peepCenter) / peepZoom;

	color = samplePanel(int(cell.x) + (int(grid.y) - 1 - int(cell.y)) * int(grid.x), st);

	if(peepGrid) {
		vec2 p = st * peepTexels;
		vec2 f = fract(p);
		vec2 w = fwidth(p);

		color.rgb = mix(color.rgb, vec3(0.5), 0.5 * max(step(f.x, w.x), step(f.y, w.y)));
	}

	// One-pixel dividers on the interior cell boundaries.
	vec2 n = round(q);
	vec2 d = abs(q - n) / fwidth(q);
	bvec2 interior = bvec2(n.x > 0.0 && n.x < grid.x, n.y > 0.0 && n.y < grid.y);

	if((interior.x && d.x < 1.0) || (interior.y && d.y < 1.0)) color = vec4(0.6, 0.6, 0.6, 1.0);
})";

// Nearest-neighbor magnifier over the composite: the panels always render at full resolution, and
// the composite shader samples a zoomed sub-rect of each panel texture (GL_NEAREST), so every
// rendered pixel - including exactly how its antialiasing resolved - shows as a flat ZxZ block.
// The zoom is anchored on the mouse's panel-local position (the same spot in every panel, whichever
// panel the mouse is over); 'f' freezes that anchor. Plain MOVE events only, so trackball drags
// never retarget it. Only ever touches composite uniforms; knows nothing about what the panels
// contain or how they are rendered.
struct PixelPeep: public osgGA::GUIEventHandler {
	static constexpr int ZOOMS[] = {1, 2, 4, 8, 16, 32};

	// Below this zoom the texel grid would draw a line every one or two screen pixels.
	static constexpr int GRID_MIN_ZOOM = 4;

	osg::ref_ptr<osg::Uniform> _zoom;
	osg::ref_ptr<osg::Uniform> _center;
	osg::ref_ptr<osg::Uniform> _grid;

	osg::ref_ptr<osgText::Text> _status;

	osg::Vec2 _anchor{0.5f, 0.5f};

	int _cols;
	int _rows;

	std::size_t _level = 0;
	bool _showGrid = true;
	bool _frozen = false;

	PixelPeep(
		int cols,
		int rows,
		int panelW,
		int panelH,
		osg::StateSet* compositeStateSet,
		osgText::Text* status
	):
	_status(status),
	_cols(cols),
	_rows(rows) {
		_zoom = new osg::Uniform("peepZoom", 1.0f);
		_center = new osg::Uniform("peepCenter", _anchor);
		_grid = new osg::Uniform("peepGrid", false);

		compositeStateSet->addUniform(_zoom);
		compositeStateSet->addUniform(_center);
		compositeStateSet->addUniform(_grid);
		compositeStateSet->addUniform(new osg::Uniform("peepTexels", osg::Vec2(
			static_cast<float>(panelW),
			static_cast<float>(panelH)
		)));

		apply();
	}

	int zoom() const { return ZOOMS[_level]; }

	void apply() {
		const int z = zoom();

		_zoom->set(static_cast<float>(z));
		_center->set(_anchor);
		_grid->set(_showGrid && z >= GRID_MIN_ZOOM);

		if(_status) _status->setText(std::format(
			"pixel-peep {}x{}  [ / ] zoom, g grid, f freeze",
			z,
			_frozen ? " (frozen)" : ""
		));
	}

	bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override {
		if(ea.getEventType() == osgGA::GUIEventAdapter::MOVE) {
			if(_frozen) return false;

			// Window-normalized [-1, 1] -> window [0, 1] -> panel-local [0, 1].
			const float u = (ea.getXnormalized() + 1.0f) * 0.5f * static_cast<float>(_cols);
			const float v = (ea.getYnormalized() + 1.0f) * 0.5f * static_cast<float>(_rows);

			_anchor.set(
				std::clamp(u - std::floor(u), 0.0f, 1.0f),
				std::clamp(v - std::floor(v), 0.0f, 1.0f)
			);

			_center->set(_anchor);

			return false;
		}

		if(ea.getEventType() != osgGA::GUIEventAdapter::KEYDOWN) return false;

		switch(ea.getKey()) {
			case '[':
				if(_level > 0) _level--;

				break;

			case ']':
				if(_level + 1 < std::size(ZOOMS)) _level++;

				break;

			case 'g':
				_showGrid = !_showGrid;

				break;

			case 'f':
				_frozen = !_frozen;

				break;

			default: return false;
		}

		apply();

		return true;
	}
};

// One baked distance field for one glyph, ready to attach.
struct Field {
	osg::ref_ptr<osgx::SDF> sdf;

	slughorn::Atlas::SDF::Tile tile;
};

static std::optional<Field> bakeField(
	const std::string& fontPath,
	uint32_t codepoint,
	slughorn::Atlas::SDF::Type type,
	uint32_t tileSize
) {
	slughorn::Atlas atlas;

	if(!slughorn::freetype::loadFontGlyphs(fontPath, {codepoint}, atlas)) return {};

	slughorn::Atlas::SDF::Config config;

	config.type = type;
	config.tileSize = tileSize;

	atlas.setSDF(config);
	atlas.requestSDF(codepoint);
	atlas.build();

	const auto shape = atlas.getShape(codepoint);

	if(!shape || !shape->sdf) return {};

	const auto& texture = atlas.getSDF().texture;
	const bool msdf = type == slughorn::Atlas::SDF::Type::MSDF;

	auto* data = new unsigned char[texture.bytes.size()];

	std::memcpy(data, texture.bytes.data(), texture.bytes.size());

	// Row 0 is the bottom row in slughorn's tile contract, same as osg::Image; no flip.
	auto image = osgx::make_ref<osg::Image>();

	image->setImage(
		static_cast<int>(texture.width),
		static_cast<int>(texture.height),
		1,
		msdf ? GL_RGB32F_ARB : GL_R32F,
		msdf ? GL_RGB : GL_RED,
		GL_FLOAT,
		data,
		osg::Image::USE_NEW_DELETE
	);

	const auto& tile = *shape->sdf;
	const float tw = static_cast<float>(texture.width);
	const float th = static_cast<float>(texture.height);

	auto sdf = osgx::make_ref<osgx::SDF>();

	sdf->setTexture(osgx::SDF::makeTexture(image));
	sdf->setSDFType(msdf ? osgx::SDF::SDFType::MSDF : osgx::SDF::SDFType::SDF);
	sdf->setPixelRange(static_cast<float>(tile.pixelRange()));
	sdf->setUVRect(osg::Vec4(
		static_cast<float>(tile.x) / tw,
		static_cast<float>(tile.y) / th,
		static_cast<float>(tile.x + tile.w) / tw,
		static_cast<float>(tile.y + tile.h) / th
	));

	return Field{sdf, tile};
}

// SDF::Config exposes tileSize (longest tile axis, range padding included), not texelsPerEm; the
// two are proportional for one shape, so a probe bake measures the ratio and a second bake hits
// the requested density.
static std::optional<Field> bakeFieldAt(
	const std::string& fontPath,
	uint32_t codepoint,
	slughorn::Atlas::SDF::Type type,
	float texelsPerEm
) {
	static constexpr uint32_t PROBE_TILE_SIZE = 128;

	const auto probe = bakeField(fontPath, codepoint, type, PROBE_TILE_SIZE);

	if(!probe) return {};

	const auto tileSize = static_cast<uint32_t>(std::lround(
		PROBE_TILE_SIZE * texelsPerEm / static_cast<float>(probe->tile.texelsPerEm)
	));

	return bakeField(fontPath, codepoint, type, std::max(tileSize, 8u));
}

// World-space quad (XY plane) with UV (0, 0) at (x0, y0), for the FIELD_VERT attribute layout.
static osg::ref_ptr<osg::Geometry> makeQuad(float x0, float y0, float x1, float y1) {
	auto positions = new osg::Vec3Array();
	auto uvs = new osg::Vec2Array();

	positions->push_back(osg::Vec3(x0, y0, 0.0f));
	positions->push_back(osg::Vec3(x1, y0, 0.0f));
	positions->push_back(osg::Vec3(x1, y1, 0.0f));
	positions->push_back(osg::Vec3(x0, y1, 0.0f));

	uvs->push_back(osg::Vec2(0.0f, 0.0f));
	uvs->push_back(osg::Vec2(1.0f, 0.0f));
	uvs->push_back(osg::Vec2(1.0f, 1.0f));
	uvs->push_back(osg::Vec2(0.0f, 1.0f));

	positions->setBinding(osg::Array::BIND_PER_VERTEX);
	uvs->setBinding(osg::Array::BIND_PER_VERTEX);

	auto geometry = osgx::make_ref<osg::Geometry>();

	geometry->setUseDisplayList(false);
	geometry->setUseVertexBufferObjects(true);
	geometry->setVertexArray(positions);
	geometry->setVertexAttribArray(0, positions);
	geometry->setVertexAttribArray(1, uvs);
	geometry->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLE_FAN, 0, 4));

	return geometry;
}

// The tile's quad in world space: em-space tile corners scaled by EM, so it lands exactly where
// the other techniques place the same glyph.
static osg::ref_ptr<osg::Geometry> makeFieldQuad(const slughorn::Atlas::SDF::Tile& tile) {
	const float x0 = static_cast<float>(tile.emOriginX) * EM;
	const float y0 = static_cast<float>(tile.emOriginY) * EM;

	return makeQuad(
		x0,
		y0,
		x0 + static_cast<float>(tile.w) / static_cast<float>(tile.texelsPerEm) * EM,
		y0 + static_cast<float>(tile.h) / static_cast<float>(tile.texelsPerEm) * EM
	);
}

static osg::ref_ptr<osg::Geode> makeTextNode(
	osgText::Font* font,
	const std::string& glyph,
	int resolution,
	osgText::ShaderTechnique technique
) {
	auto text = osgx::make_ref<osgText::Text>();

	text->setFont(font);
	text->setFontResolution(
		static_cast<unsigned int>(resolution),
		static_cast<unsigned int>(resolution)
	);
	text->setShaderTechnique(technique);
	text->setCharacterSize(EM);
	text->setAlignment(osgText::Text::LEFT_BASE_LINE);
	text->setPosition(osg::Vec3(0.0f, 0.0f, 0.0f));
	text->setColor(FOREGROUND);
	text->setText(glyph);

	auto geode = osgx::make_ref<osg::Geode>();

	geode->addDrawable(text);

	return geode;
}

#ifdef OSGSLUG_WITH_OSGRIVE
// Screen-space mode: the quad already covers the whole panel in NDC, no transform.
static const char* RIVE_SCREEN_VERT = R"(
#version 430 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 uv;

out vec2 vUV;

void main() {
	vUV = uv;
	gl_Position = vec4(position.xy, 0.0, 1.0);
})";

static const char* RIVE_FRAG = R"(
#version 430 core

uniform sampler2D riveTexture;

in vec2 vUV;

out vec4 fragColor;

void main() {
	fragColor = texture(riveTexture, vUV);
})";

// Texture mode: transparent texels kept around the glyph's ink box in the Rive texture, so the AA
// fringe is never clipped by the texture edge.
static constexpr int RIVE_MARGIN = 4;

struct RivePanel {
	// The producer stage: renders the Rive Scene into its own texture. A PRE_RENDER osgx::RTT
	// ordered BEFORE the panels (order -1) with a 1x1 dummy attachment, holding nothing but the
	// Scene: Rive binds its own FBO while drawing, and OSG rebinds after every FBO stage, so nothing
	// else ever draws into Rive's target - and the panels sample THIS frame's texture.
	osg::ref_ptr<osg::Node> producer;

	// Panel content sampling the Rive texture.
	osg::ref_ptr<osg::Geode> node;

	std::string label;
};

static uint32_t toARGB(const osg::Vec4& c) {
	const auto byte = [](float v) {
		return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
	};

	return byte(c.a()) << 24 | byte(c.r()) << 16 | byte(c.g()) << 8 | byte(c.b());
}

static osg::ref_ptr<osgx::RTT> makeRiveProducerStage(osgRive::Scene* scene) {
	// Scene's own unit display quad is never used - every mode samples getTexture() itself. That
	// leaves the Scene with no valid bound (its producer drawable reports none by design), so
	// nothing on this path may be culled.
	scene->getDisplayGeode()->setNodeMask(0);
	scene->setCullingActive(false);

	auto dummy = osgx::make_ref<osg::Texture2D>();

	dummy->setTextureSize(1, 1);
	dummy->setInternalFormat(GL_RGBA8);

	auto stage = osgx::make_ref<osgx::RTT>(1, 1);

	stage->setRenderOrder(osg::Camera::PRE_RENDER, -1);
	stage->setClearMask(0);
	stage->setCullingActive(false);
	stage->attach(osg::Camera::COLOR_BUFFER, dummy);
	stage->addChild(scene);

	return stage;
}

static void setRiveQuadState(osg::StateSet* ss, osg::Program* program, osg::Texture2D* texture) {
	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(0, texture, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("riveTexture", 0));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	// Rive writes premultiplied color.
	ss->setAttributeAndModes(
		new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA),
		osg::StateAttribute::ON
	);
}

// Texture mode (--rive-texture): Rive renders once-sized into a glyph-sized texture at `resolution`
// px/em, placed on a world-space quad like the SDF tiles. Shows Rive as a texture in a 3D scene:
// zooming only magnifies that fixed texture.
static RivePanel makeRiveTexturePanel(
	const std::string& fontPath,
	const std::string& glyph,
	const slughorn::Atlas::Shape& shape,
	int resolution
) {
	const float res = static_cast<float>(resolution);
	const float margin = static_cast<float>(RIVE_MARGIN);
	const int w = static_cast<int>(std::ceil(static_cast<float>(shape.width) * res)) + RIVE_MARGIN * 2;
	const int h = static_cast<int>(std::ceil(static_cast<float>(shape.height) * res)) + RIVE_MARGIN * 2;

	// Rive target pixels are y-down: put the ink box's top-left corner at (margin, margin), which
	// puts the baseline origin at (margin - bearingX, margin + bearingY) in pixels.
	auto scene = osgx::make_ref<osgRive::Scene>(
		osgRive::makeTextDraw(
			fontPath,
			glyph,
			res,
			margin - static_cast<float>(shape.bearingX) * res,
			margin + static_cast<float>(shape.bearingY) * res,
			toARGB(FOREGROUND)
		),
		static_cast<unsigned int>(w),
		static_cast<unsigned int>(h)
	);

	auto program = osgx::make_ref<osg::Program>();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, FIELD_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, RIVE_FRAG));

	// The texture's em-space rect, scaled by EM.
	const float x0 = (static_cast<float>(shape.bearingX) - margin / res) * EM;
	const float y1 = (static_cast<float>(shape.bearingY) + margin / res) * EM;

	auto node = osgx::make_ref<osg::Geode>();

	node->addDrawable(makeQuad(
		x0,
		y1 - static_cast<float>(h) / res * EM,
		x0 + static_cast<float>(w) / res * EM,
		y1
	));

	setRiveQuadState(node->getOrCreateStateSet(), program, scene->getTexture());

	return {
		makeRiveProducerStage(scene),
		node,
		std::format(
			"Rive - texture in 3D (HarfBuzz outline)\n{} px/em texture, {}x{}",
			resolution,
			w,
			h
		)
	};
}

// Screen-space mode's per-frame transform. Lives on a Group directly under the main camera (the
// parent of the producer stage), so at cull time the CullVisitor's matrices are exactly the main
// camera's view and projection for THIS frame - the same ones every RELATIVE_RF panel inherits.
//
// Rive only takes a 2D affine transform, so the projective em -> panel-pixel mapping is linearized
// at the glyph's ink-box center (central differences). Exact whenever the glyph plane is parallel
// to the screen (any zoom, pan, in-plane rotation); under perspective tilt it drifts away from the
// true mapping with distance from the center - that drift is Rive's real limitation, not a bug.
struct RiveAffineCallback: public osg::NodeCallback {
	osgRive::TextDraw textDraw;

	// Linearization point, Rive em coordinates (y down).
	osg::Vec2d center;

	RiveAffineCallback(const osgRive::TextDraw& td, const osg::Vec2d& c):
	textDraw(td),
	center(c) {
	}

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		if(auto* cv = nv->asCullVisitor()) update(
			*cv->getModelViewMatrix() *
			*cv->getProjectionMatrix() *
			// makePanel()'s aspect correction, so this matches what the panels render.
			osg::Matrixd::scale(
				static_cast<double>(GRID_COLS) / static_cast<double>(GRID_ROWS),
				1.0,
				1.0
			)
		);

		traverse(node, nv);
	}

	// Rive em point (x, y down) -> panel pixel (origin top-left, y down); false behind the camera.
	static bool toPixel(const osg::Matrixd& mvp, const osg::Vec2d& em, osg::Vec2d& pixel) {
		const osg::Vec4d clip = osg::Vec4d(em.x() * EM, -em.y() * EM, 0.0, 1.0) * mvp;

		if(clip.w() <= 1e-6) return false;

		pixel.set(
			(clip.x() / clip.w() * 0.5 + 0.5) * PANEL_W,
			(0.5 - clip.y() / clip.w() * 0.5) * PANEL_H
		);

		return true;
	}

	void update(const osg::Matrixd& mvp) {
		static constexpr double STEP = 1e-3;

		const osg::Vec2d dx(STEP, 0.0);
		const osg::Vec2d dy(0.0, STEP);

		osg::Vec2d p0, px0, px1, py0, py1;

		const bool visible =
			toPixel(mvp, center, p0) &&
			toPixel(mvp, center - dx, px0) &&
			toPixel(mvp, center + dx, px1) &&
			toPixel(mvp, center - dy, py0) &&
			toPixel(mvp, center + dy, py1)
		;

		textDraw.setVisible(visible);

		if(!visible) return;

		const osg::Vec2d xAxis = (px1 - px0) / (2.0 * STEP);
		const osg::Vec2d yAxis = (py1 - py0) / (2.0 * STEP);
		const osg::Vec2d t = p0 - xAxis * center.x() - yAxis * center.y();

		textDraw.setTransform({
			static_cast<float>(xAxis.x()),
			static_cast<float>(xAxis.y()),
			static_cast<float>(yAxis.x()),
			static_cast<float>(yAxis.y()),
			static_cast<float>(t.x()),
			static_cast<float>(t.y())
		});
	}
};

// Screen-space mode (default): Rive renders straight into a panel-sized texture through the camera's
// em -> panel-pixel transform every frame, so it re-tessellates for the true on-screen size at any
// zoom - Rive the way a Rive app draws. Shown as a panel-filling quad, not a quad in 3D.
static RivePanel makeRiveScreenPanel(
	const std::string& fontPath,
	const std::string& glyph,
	const slughorn::Atlas::Shape& shape
) {
	osgRive::TextDraw textDraw(fontPath, glyph, toARGB(FOREGROUND));

	auto scene = osgx::make_ref<osgRive::Scene>(
		textDraw.drawFunction(),
		static_cast<unsigned int>(PANEL_W),
		static_cast<unsigned int>(PANEL_H)
	);

	auto program = osgx::make_ref<osg::Program>();

	program->addShader(new osg::Shader(osg::Shader::VERTEX, RIVE_SCREEN_VERT));
	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, RIVE_FRAG));

	auto node = osgx::make_ref<osg::Geode>();
	auto quad = makeQuad(-1.0f, -1.0f, 1.0f, 1.0f);

	// NDC geometry: its bound reads as a 2x2 world-unit square at the world origin, which leaves
	// the panel's frustum as soon as the camera zooms away from the origin. The CullVisitor tests the
	// Geode AND each Drawable's own bound, so both must opt out.
	quad->setCullingActive(false);
	node->addDrawable(quad);
	node->setCullingActive(false);

	auto* ss = node->getOrCreateStateSet();

	setRiveQuadState(ss, program, scene->getTexture());
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

	// Ink-box center in Rive em coordinates (y down).
	const osg::Vec2d center(
		static_cast<double>(shape.bearingX + shape.width * 0.5_cv),
		-static_cast<double>(shape.bearingY - shape.height * 0.5_cv)
	);

	auto producer = osgx::make_ref<osg::Group>();

	producer->setCullingActive(false);
	producer->setCullCallback(new RiveAffineCallback(textDraw, center));
	producer->addChild(makeRiveProducerStage(scene));

	return {
		producer,
		node,
		std::format(
			"Rive - screen-space affine (HarfBuzz outline)\nre-tessellated per frame, {}x{} target",
			PANEL_W,
			PANEL_H
		)
	};
}
#endif

static osg::ref_ptr<osg::Geode> makeFieldNode(osg::Program* program, const Field& field) {
	auto geode = osgx::make_ref<osg::Geode>();
	auto* ss = geode->getOrCreateStateSet();

	geode->addDrawable(makeFieldQuad(field.tile));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	ss->setAttributeAndModes(field.sdf, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("fillColor", FOREGROUND));
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setAttributeAndModes(
		new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
		osg::StateAttribute::ON
	);

	return geode;
}

static osg::ref_ptr<osg::Texture2D> makePanelTexture() {
	auto tex = osgx::make_ref<osg::Texture2D>();

	tex->setTextureSize(PANEL_W, PANEL_H);
	tex->setInternalFormat(GL_RGBA8);
	tex->setSourceFormat(GL_RGBA);
	tex->setSourceType(GL_UNSIGNED_BYTE);
	tex->setResizeNonPowerOfTwoHint(false);
	// NEAREST at 1x is an exact 1:1 copy; at Nx it is the whole point of pixel-peep.
	tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	return tex;
}

// RELATIVE_RF with no view matrix of its own: renders what the main camera sees. The panel's own
// projection is post-multiplied onto the main camera's (Camera::POST_MULTIPLY, the default), so an
// NDC x-scale of (window aspect / panel aspect) == cols / rows corrects the inherited projection
// for the panel's aspect ratio; identity for 2x2.
static osg::ref_ptr<osgx::RTT> makePanel(osg::Texture2D* tex, osg::Node* content) {
	auto rtt = osgx::make_ref<osgx::RTT>(PANEL_W, PANEL_H, osg::Transform::RELATIVE_RF);

	rtt->setProjectionMatrix(osg::Matrix::scale(
		static_cast<double>(GRID_COLS) / static_cast<double>(GRID_ROWS),
		1.0,
		1.0
	));

	rtt->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	rtt->setClearColor(BACKGROUND);
	rtt->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);
	rtt->attach(osg::Camera::COLOR_BUFFER, tex);
	rtt->addChild(content);

	return rtt;
}

static osg::ref_ptr<osgText::Text> makeLabel(
	osgText::Font* font,
	const osg::Vec3& position,
	osgText::Text::AlignmentType alignment,
	const std::string& str
) {
	auto text = osgx::make_ref<osgText::Text>();

	text->setFont(font);
	text->setCharacterSize(18.0f);
	text->setPosition(position);
	text->setAlignment(alignment);
	text->setColor(osg::Vec4(1.0f, 0.8f, 0.3f, 1.0f));
	text->setText(str);

	return text;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	auto lib = osgSlug::initialize(args);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, " - compares slughorn, osgx::SDF (MSDF/SDF), and osgText", {
		{"--glyph <char>", "ASCII glyph to compare (DEFAULT R)"},
		{
			"--resolution <texels>",
			"Texels per em for the SDF/MSDF bakes and osgText's font resolution (DEFAULT 64)"
		},
		{
			"--rive-texture",
			"Rive as a fixed texture on a quad in 3D, instead of the DEFAULT screen-space mode "
			"(re-tessellated through the camera every frame); only with osgRive"
		},
		{
			"--rive-resolution <texels>",
			"Texels per em for --rive-texture (DEFAULT --resolution); only with osgRive"
		}
	}, 1, "<font>")) return 0;

	std::string glyph = "R";
	int resolution = 64;

	while(args.read("--glyph", glyph)) {}
	while(args.read("--resolution", resolution)) {}

	// 0 = same as --resolution.
	int riveResolution = 0;

	while(args.read("--rive-resolution", riveResolution)) {}

	if(!riveResolution) riveResolution = resolution;

	bool riveTexture = false;

	while(args.read("--rive-texture")) riveTexture = true;

	// Every option this example (and osgViewer::Viewer's constructor) understands has been read by
	// now; anything left over is a typo or another example's flag, not something to ignore.
	args.reportRemainingOptionsAsUnrecognized();

	if(args.errors()) {
		args.writeErrorMessages(std::cerr);

		return 1;
	}

	if(glyph.size() != 1) return example::fail(args, 1, "--glyph must be one ASCII character");
	if(resolution < 1) return example::fail(args, 1, "--resolution must be positive");
	if(riveResolution < 1) return example::fail(args, 1, "--rive-resolution must be positive");

	const std::string fontPath = args[1];
	const auto codepoint = static_cast<uint32_t>(static_cast<unsigned char>(glyph[0]));

	// slughorn (osgSlug) --------------------------------------------------------------------------
	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	auto font = osgx::make_ref<osgSlug::Font>(fontPath, atlas);

	if(!font->load()) return example::fail(args, 1, "Couldn't load font: " + fontPath);

	atlas->build();
	atlas->packTextures();

	const auto shape = atlas->getShape(codepoint);

	if(!shape) return example::fail(args, 1, "Glyph not in font: " + glyph);

	auto sd = example::makeShapeDrawable();

	sd->addLayer({
		.key = slughorn::Key(codepoint),
		.color = {
			cv(FOREGROUND.r()),
			cv(FOREGROUND.g()),
			cv(FOREGROUND.b()),
			cv(FOREGROUND.a())
		},
		.scale = cv(EM)
	});

	atlas->addChild(sd);

	// osgx::SDF (MSDF + SDF) ---------------------------------------------------------------------
	std::optional<Field> msdf;
	std::optional<Field> sdf;

	try {
		msdf = bakeFieldAt(
			fontPath,
			codepoint,
			slughorn::Atlas::SDF::Type::MSDF,
			static_cast<float>(resolution)
		);

		sdf = bakeFieldAt(
			fontPath,
			codepoint,
			slughorn::Atlas::SDF::Type::SDF,
			static_cast<float>(resolution)
		);
	}

	catch(const std::exception& e) {
		return example::fail(args, 1, std::string("SDF bake failed: ") + e.what());
	}

	if(!msdf || !sdf) return example::fail(args, 1, "SDF bake produced no tile for: " + glyph);

	// Built after at least one osgx::SDF exists: its constructor registers the `osgx::sdf` library.
	auto fieldProgram = osgx::make_ref<osg::Program>();

	fieldProgram->addShader(new osg::Shader(osg::Shader::VERTEX, FIELD_VERT));
	fieldProgram->addShader(new osg::Shader(
		osg::Shader::FRAGMENT,
		osgx::resolveShaderLibs(FIELD_FRAG)
	));

	// osgText -----------------------------------------------------------------------------------
	auto osgFont = osgText::readRefFontFile(fontPath);

	if(!osgFont) return example::fail(args, 1, "osgText couldn't load font: " + fontPath);

	// Panels ------------------------------------------------------------------------------------
	struct PanelSource {
		osg::ref_ptr<osg::Node> content;

		std::string label;
	};

	const PanelSource slughornPanel = {atlas, "slughorn (osgSlug)\nanalytic coverage from curves"};

	const PanelSource msdfPanel = {
		makeFieldNode(fieldProgram, *msdf),
		std::format(
			"osgx::SDF - MSDF\n{:.1f} texels/em, pixelRange {:.1f}, tile {}x{}",
			static_cast<float>(msdf->tile.texelsPerEm),
			static_cast<float>(msdf->tile.pixelRange()),
			msdf->tile.w,
			msdf->tile.h
		)
	};

	const PanelSource sdfPanel = {
		makeFieldNode(fieldProgram, *sdf),
		std::format(
			"osgx::SDF - SDF\n{:.1f} texels/em, pixelRange {:.1f}, tile {}x{}",
			static_cast<float>(sdf->tile.texelsPerEm),
			static_cast<float>(sdf->tile.pixelRange()),
			sdf->tile.w,
			sdf->tile.h
		)
	};

	// GREYSCALE is osgText's default; set explicitly since SIGNED_DISTANCE_FIELD is a different
	// technique (its own panel in the 3x2 layout).
	const PanelSource bitmapPanel = {
		makeTextNode(osgFont, glyph, resolution, osgText::GREYSCALE),
		std::format("osgText - bitmap (GREYSCALE)\n{} px/em font resolution", resolution)
	};

#ifdef OSGSLUG_WITH_OSGRIVE
	RivePanel riveGlyph;

	try {
		riveGlyph = riveTexture
			? makeRiveTexturePanel(fontPath, glyph, *shape, riveResolution)
			: makeRiveScreenPanel(fontPath, glyph, *shape)
		;
	}

	catch(const std::exception& e) {
		return example::fail(args, 1, std::string("Rive panel failed: ") + e.what());
	}

	const PanelSource rivePanel = {riveGlyph.node, riveGlyph.label};

	// Distance field built by osgText from the FreeType-rasterized bitmap, not from the curves.
	const PanelSource textSDFPanel = {
		makeTextNode(osgFont, glyph, resolution, osgText::SIGNED_DISTANCE_FIELD),
		std::format("osgText - SDF (from bitmap)\n{} px/em font resolution", resolution)
	};

	const std::vector<PanelSource> sources = {
		slughornPanel,
		rivePanel,
		msdfPanel,
		sdfPanel,
		textSDFPanel,
		bitmapPanel
	};
#else
	const std::vector<PanelSource> sources = {slughornPanel, msdfPanel, sdfPanel, bitmapPanel};
#endif

	std::vector<osg::ref_ptr<osg::Texture2D>> textures;
	std::vector<osg::ref_ptr<osgx::RTT>> panels;

	for(const auto& source : sources) {
		textures.push_back(makePanelTexture());
		panels.push_back(makePanel(textures.back(), source.content));

		OSG_NOTICE << source.label << std::endl;
	}

	// Composite ---------------------------------------------------------------------------------
	auto composite = osgx::make_ref<osg::Camera>();

	composite->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	composite->setRenderOrder(osg::Camera::POST_RENDER);
	composite->setClearMask(GL_DEPTH_BUFFER_BIT);
	composite->setAllowEventFocus(false);
	composite->setProjectionMatrixAsOrtho2D(0.0, WINDOW_W, 0.0, WINDOW_H);
	composite->setViewMatrix(osg::Matrix::identity());
	composite->getOrCreateStateSet()->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

	auto quad = osg::ref_ptr<osg::Geometry>(osg::createTexturedQuadGeometry(
		osg::Vec3(0.0f, 0.0f, 0.0f),
		osg::Vec3(WINDOW_W, 0.0f, 0.0f),
		osg::Vec3(0.0f, WINDOW_H, 0.0f)
	));

	auto* quadState = quad->getOrCreateStateSet();

	for(std::size_t i = 0; i < textures.size(); i++) {
		const auto unit = static_cast<unsigned int>(i);

		quadState->setTextureAttributeAndModes(unit, textures[i], osg::StateAttribute::ON);
		quadState->addUniform(new osg::Uniform(
			("panel" + std::to_string(i)).c_str(),
			static_cast<int>(unit)
		));
	}

	quadState->addUniform(new osg::Uniform(
		"grid",
		osg::Vec2(static_cast<float>(GRID_COLS), static_cast<float>(GRID_ROWS))
	));

	auto compositeProgram = osgx::make_ref<osg::Program>();

	compositeProgram->addShader(new osg::Shader(osg::Shader::VERTEX, COMPOSITE_VERT));
	compositeProgram->addShader(new osg::Shader(osg::Shader::FRAGMENT, COMPOSITE_FRAG));
	quadState->setAttributeAndModes(compositeProgram);

	auto hud = osgx::make_ref<osg::Geode>();

	hud->addDrawable(quad);

	for(std::size_t i = 0; i < sources.size(); i++) {
		const auto col = static_cast<int>(i) % GRID_COLS;
		const auto row = static_cast<int>(i) / GRID_COLS;
		const float x = static_cast<float>(col * PANEL_W) + 12.0f;
		const float y = static_cast<float>(WINDOW_H - row * PANEL_H) - 12.0f;

		hud->addDrawable(makeLabel(
			osgFont,
			osg::Vec3(x, y, 0.0f),
			osgText::Text::LEFT_TOP,
			sources[i].label
		));
	}

	auto status = makeLabel(
		osgFont,
		osg::Vec3(WINDOW_W - 12.0f, 12.0f, 0.0f),
		osgText::Text::RIGHT_BOTTOM,
		""
	);

	status->setDataVariance(osg::Object::DYNAMIC);
	hud->addDrawable(status);
	composite->addChild(hud);

	auto peep = osgx::make_ref<PixelPeep>(
		GRID_COLS,
		GRID_ROWS,
		PANEL_W,
		PANEL_H,
		quadState,
		status.get()
	);

	// Scene -------------------------------------------------------------------------------------
	auto root = osgx::make_ref<osg::Group>();

	for(const auto& panel : panels) root->addChild(panel);

	root->addChild(composite);

#ifdef OSGSLUG_WITH_OSGRIVE
	// Its own PRE_RENDER stage, ordered before the panels - see RivePanel::producer.
	root->addChild(riveGlyph.producer);

	// osgRive: multi-threaded viewers crash the NVIDIA driver (VAO state invalidated across threads).
	viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);
#endif

	// Home view from the glyph's own quad, not root's bound: the composite camera's pixel-space
	// content would otherwise skew it.
	const auto glyphQuad = shape->computeQuad({}, cv(EM));
	const osg::Vec3 center(
		static_cast<float>(glyphQuad.x0 + glyphQuad.x1) * 0.5f,
		static_cast<float>(glyphQuad.y0 + glyphQuad.y1) * 0.5f,
		0.0f
	);
	const float radius = 0.5f * std::hypot(
		static_cast<float>(glyphQuad.x1 - glyphQuad.x0),
		static_cast<float>(glyphQuad.y1 - glyphQuad.y0)
	);

	auto manipulator = osgx::make_ref<osgGA::TrackballManipulator>();

	manipulator->setHomePosition(
		center + osg::Vec3(0.0f, 0.0f, radius * 3.5f),
		center,
		osg::Vec3(0.0f, 1.0f, 0.0f)
	);

	viewer.setSceneData(root);
	viewer.setCameraManipulator(manipulator);
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(peep);
	viewer.addEventHandler(new example::DebugModeHandler(panels[0]->getOrCreateStateSet()));
	viewer.setUpViewInWindow(50, 50, WINDOW_W, WINDOW_H);
	// Nothing draws directly under the main camera; the RTTs inherit its projection as-is.
	viewer.getCamera()->setComputeNearFarMode(osg::CullSettings::DO_NOT_COMPUTE_NEAR_FAR);

	return viewer.run();
}
