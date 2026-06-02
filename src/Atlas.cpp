#include "osgSlug/Atlas.hpp"

// #include "slughorn/serial.hpp"

#include <osg/Image>
#include <osg/BlendFunc>
#include <stdexcept>
#include <fstream>
#include <sstream>

// TODO: Remove these!
#ifndef GL_RGBA_INTEGER
#define GL_RGBA_INTEGER 0x8D99
#endif
#ifndef GL_RGBA16UI
#define GL_RGBA16UI 0x8D76
#endif
#ifndef GL_RGBA32F_ARB
#define GL_RGBA32F_ARB 0x8814
#endif

namespace {

// TODO: There may already be a OSG util file for this! Investigate!
std::string readFile(const std::string& path) {
	std::ifstream f(path);

	if(!f) throw std::runtime_error("osgSlug: cannot read shader file: " + path);

	std::ostringstream ss;

	ss << f.rdbuf();

	return ss.str();
}

}

namespace osgSlug {

// This is the default osgSlug SSBO shader "header"; make sure any GLSL source that needs access to
// the structs osgSlug uses includes this!
const std::string Atlas::SHADER_TYPES = R"(
// Atlas-level shape data: static after packTextures(), never mutated at runtime.
// One entry per unique shape in the atlas, indexed via LayerData.effectData.y.
struct AtlasShapeData {
	vec4 bandXform; // xy = bandScaleX/Y, zw = bandOffsetX/Y
	vec4 shapeData; // xy = glyphLoc (ivec2), zw = bandMax (ivec2)
	vec4 originData; // xy = originX/Y, zw = unused
};

// Per-layer data: one entry per layer in each drawable, indexed by (a_position.w - 1).
// Slot order: slughorn contract first (color, gradient), osgSlug machinery last (effectData).
struct LayerData {
	vec4 color; // RGBA flat color
	vec4 gradientMeta; // x = gradientId (1-based), yz = gradient center, w = r0_norm
	vec4 gradientXform; // gradient transform (B matrix / direction / sweep)
	vec4 effectData; // x = effectId, y = shapeIndex (into AtlasShapeBuffer), z = 0 (reserved), w = effectParam (user-settable)
};

layout(std430, binding = 0) readonly buffer AtlasShapeBuffer {
	AtlasShapeData atlasShapes[];
};

layout(std430, binding = 1) buffer LayerBuffer {
	LayerData layers[];
};
)";

// Optional vertex helper library; include via: #pragma osgSlug lib_vertex
const std::string Atlas::SHADER_LIB_VERTEX = R"(
// Rotate pos.xy around the Pivot origin.
// Expects Origin::Pivot; origin is the pivot point in local em-space.
// Pass time, effectParam, or any angle expression as `angle`.
vec3 osgSlug_Vertex_Rotate(vec3 pos, vec2 emCoord, vec2 origin, float angle) {
	float c = cos(angle), s = sin(angle);
	mat2 R = mat2(c, s, -s, c);
	vec2 pivot = pos.xy - emCoord.xy + origin;
	pos.xy = R * (pos.xy - pivot) + pivot;
	return pos;
}
)";

// Optional fragment helper library; include via: #pragma osgSlug lib_fragment
const std::string Atlas::SHADER_LIB_FRAGMENT = R"(
)";

// The VERTEX shader injection point; perform any position animation here.
const std::string Atlas::SHADER_NOOP_VERTEX = R"(
#version 430 core

vec3 osgSlug_Vertex(
	vec3 pos,
	vec2 emCoord,
	vec2 uv,
	int effectId,
	vec2 origin,
	float effectParam,
	float time
) {
	return pos;
}
)";

// The FRAGMENT shader injection point; controls w
const std::string Atlas::SHADER_NOOP_FRAGMENT = R"(
#version 330 core

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(
	float fill,
	vec2 emCoord,
	vec2 uv,
	vec4 layerColor,
	int effectId,
	float time
) {
	return vec4(layerColor.rgb, fill * layerColor.a);
}

)";

Atlas::Atlas(uint32_t texWidth):
slughorn::Atlas(texWidth) {
}

Atlas::Atlas(const slughorn::Atlas& src) {
	static_cast<slughorn::Atlas&>(*this) = src;

	packTextures();
}

osg::ref_ptr<Atlas> Atlas::read(std::filesystem::path path) {
	// slughorn::serial will throw an exception for us; so, using the following line is optional.
	// if(!std::filesystem::exists(path)) return nullptr;

	return fromAtlas(path.string());
}

osg::ref_ptr<Atlas> Atlas::read(std::ifstream& ifs) {
	return fromAtlas(ifs);
}

void Atlas::packTextures() {
	if(_curveTexture.valid()) return;

	if(
		!getCurveTextureData().bytes.size() ||
		!getBandTextureData().bytes.size()
	) throw std::runtime_error("Atlas::build() must be called before Atlas::packTextures()");

	_curveTexture = _makeTexture(getCurveTextureData());
	_bandTexture = _makeTexture(getBandTextureData());

	if(!getGradientTextureData().bytes.empty()) {
		_gradientTexture = _makeTexture(getGradientTextureData());
	}

	// Build the atlas-level shape SSBO (binding 0). One entry per unique shape;
	// 3 vec4s = 48 bytes per entry: bandXform, shapeData, originData.
	_shapeBuffer = osgx::make_ref<osgx::Vec4Array>();

	uint32_t idx = 0;

	for(const auto& [key, shape] : getShapes()) {
		_shapeIndex[key] = idx++;

		_shapeBuffer->push_back({shape.bandScaleX, shape.bandScaleY, shape.bandOffsetX, shape.bandOffsetY});
		_shapeBuffer->push_back({cv(shape.bandTexX), cv(shape.bandTexY), cv(shape.bandMaxX), cv(shape.bandMaxY)});
		_shapeBuffer->push_back({cv(shape.originX), cv(shape.originY), 0.0f, 0.0f});
	}

	_shapeBuffer->setBufferObject(new osg::ShaderStorageBufferObject());
}

uint32_t Atlas::getShapeIndex(const slughorn::Key& key) const {
	auto it = _shapeIndex.find(key);

	if(it == _shapeIndex.end()) throw std::runtime_error("Atlas::getShapeIndex: key not found");

	return it->second;
}

osg::ref_ptr<osg::Texture2D> Atlas::_makeTexture(const slughorn::Atlas::TextureData& data) {
	osg::ref_ptr<osg::Image> img = new osg::Image();

	const auto width = static_cast<int>(data.width);
	const auto height = static_cast<int>(data.height);

	if(data.format == slughorn::Atlas::TextureData::Format::RGBA32F) {
		img->allocateImage(width, height, 1, GL_RGBA, GL_FLOAT);
		img->setInternalTextureFormat(GL_RGBA32F_ARB);
	}

	else if(data.format == slughorn::Atlas::TextureData::Format::RGBA16UI) {
		img->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_SHORT);
		img->setInternalTextureFormat(GL_RGBA16UI);
		img->setPixelFormat(GL_RGBA_INTEGER);
	}

	else {
		// RGBA8 (gradient data)
		img->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
		img->setInternalTextureFormat(GL_RGBA8);
	}

	memcpy(img->data(), data.bytes.data(), data.bytes.size());

	osg::ref_ptr<osg::Texture2D> tex = new osg::Texture2D(img);

	// Gradient atlas uses bilinear filtering to smooth the color ramp between texels.
	// Curve and band textures require NEAREST (exact texel fetch; bilinear would corrupt data).
	const bool isGradient = (data.format == slughorn::Atlas::TextureData::Format::RGBA8);
	const auto filter = isGradient ? osg::Texture::LINEAR : osg::Texture::NEAREST;

	tex->setFilter(osg::Texture::MIN_FILTER, filter);
	tex->setFilter(osg::Texture::MAG_FILTER, filter);
	tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	tex->setResizeNonPowerOfTwoHint(false);

	return tex;
}

osg::StateSet* Atlas::createDefaultStateSet(
	bool useGL3,
	const std::string& vertEffects,
	const std::string& fragEffects
) const {
	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	// const std::string types = readFile("../src/osgSlug-types.glsl");
	const std::string types = SHADER_TYPES;

	// #version must be the first line in GLSL source. Insert the types block
	// immediately after it, before the rest of the shader body.
	auto resolveLib = [](std::string src, const std::string& pragma, const std::string& lib) {
		size_t pos = 0;
		while((pos = src.find(pragma, pos)) != std::string::npos) {
			src.replace(pos, pragma.size(), lib);
			pos += lib.size();
		}
		return src;
	};

	auto resolveLibs = [&resolveLib](std::string src) {
		src = resolveLib(std::move(src), "#pragma osgSlug lib_vertex",   SHADER_LIB_VERTEX);
		src = resolveLib(std::move(src), "#pragma osgSlug lib_fragment",  SHADER_LIB_FRAGMENT);
		return src;
	};

	auto makeVertShader = [&](const std::string& src) {
		const std::string resolved = resolveLibs(src);
		const auto vp = resolved.find("#version");
		const auto nl = (vp != std::string::npos) ? resolved.find('\n', vp) : std::string::npos;
		const std::string full = (nl != std::string::npos)
			? resolved.substr(0, nl + 1) + types + resolved.substr(nl + 1)
			: types + resolved
		;

		return new osg::Shader(osg::Shader::VERTEX, full);
	};

	// GL3 effect units must be #version 330 core; swap any 430 declaration.
	auto makeGL3EffectShader = [&](std::string src) {
		src = resolveLibs(src);
		const auto vp = src.find("#version");

		if(vp != std::string::npos) {
			const auto nl = src.find('\n', vp);

			if(nl != std::string::npos) src.replace(vp, nl - vp, "#version 330 core");
		}

		return new osg::Shader(osg::Shader::VERTEX, src);
	};

	if(useGL3) {
		program->addShader(osg::Shader::readShaderFile(
			osg::Shader::VERTEX, "../src/osgSlug-gl3-vert.glsl"
		));

		program->addShader(makeGL3EffectShader(vertEffects));
	}

	else {
		program->addShader(makeVertShader(readFile("../src/osgSlug-vert.glsl")));
		program->addShader(makeVertShader(vertEffects));
	}

	program->addShader(osg::Shader::readShaderFile(
		osg::Shader::FRAGMENT, "../src/osgSlug-frag.glsl"
	));

	program->addShader(new osg::Shader(osg::Shader::FRAGMENT, resolveLibs(fragEffects)));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("osgSlug_curveTexture", 0));
	ss->addUniform(new osg::Uniform("osgSlug_bandTexture", 1));
	ss->addUniform(new osg::Uniform("osgSlug_effectTexture", 2));
	ss->addUniform(new osg::Uniform("osgSlug_gradientTexture", 3));
	ss->addUniform(new osg::Uniform("osgSlug_gradientCount", static_cast<int>(getGradients().size())));
	ss->addUniform(new osg::Uniform("osgSlug_emTile", osg::Vec2(1.0f, 1.0f)));
	ss->setTextureAttributeAndModes(0, _curveTexture, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(1, _bandTexture, osg::StateAttribute::ON);

	if(_gradientTexture.valid()) {
		ss->setTextureAttributeAndModes(3, _gradientTexture, osg::StateAttribute::ON);
	}
	if(_shapeBuffer.valid()) {
		ss->setAttributeAndModes(
			new osg::ShaderStorageBufferBinding(0, _shapeBuffer, 0, _shapeBuffer->getTotalDataSize()),
			osg::StateAttribute::ON
		);
	}

	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	// TODO: This is premultiplied alpha, and needs to be synchronized with the shader!
	// ss->setAttributeAndModes(new osg::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	if(auto dm = getEnv<int>("DEBUG", 0); dm) ss->addUniform(new osg::Uniform(
		"osgSlug_debugMode",
		dm
	));

	return ss;
}

}
