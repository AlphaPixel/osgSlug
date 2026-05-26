#include "osgSlug/Atlas.hpp"

// #include "slughorn/serial.hpp"

#include <osg/Image>
#include <osg/BlendFunc>
#include <stdexcept>

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

namespace osgSlug {

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

osg::StateSet* Atlas::createDefaultStateSet(bool useGL3) const {
	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	program->addShader(osg::Shader::readShaderFile(
		osg::Shader::VERTEX,
		useGL3 ? "../src/osgSlug-gl3-vert.glsl" : "../src/osgSlug-vert.glsl"
	));

	program->addShader(osg::Shader::readShaderFile(
		osg::Shader::FRAGMENT,
		"../src/osgSlug-frag.glsl"
	));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("osgSlug_curveTexture", 0));
	ss->addUniform(new osg::Uniform("osgSlug_bandTexture", 1));
	ss->addUniform(new osg::Uniform("osgSlug_effectTexture", 2));
	ss->addUniform(new osg::Uniform("osgSlug_gradientTexture", 3));
	ss->addUniform(new osg::Uniform("osgSlug_gradientCount", static_cast<int>(getGradients().size())));
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
