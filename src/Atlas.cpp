#include "osgSlug/Atlas.hpp"

#include "slughorn-serial.hpp"

#include <osg/Image>
#include <osg/BlendFunc>

#include <cstring>

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

osg::ref_ptr<Atlas> Atlas::read(std::filesystem::path path) {
	// slughorn::serial will throw an exception for us; so, using the following line is optional.
	// if(!std::filesystem::exists(path)) return nullptr;

	osg::ref_ptr<Atlas> atlas = new osgSlug::Atlas();

	static_cast<slughorn::Atlas&>(*atlas) = slughorn::serial::read(path.string());

	atlas->packTextures();

	return atlas;
}

osg::ref_ptr<Atlas> Atlas::read(std::ifstream& ifs) {
	osg::ref_ptr<Atlas> atlas = new osgSlug::Atlas();

	static_cast<slughorn::Atlas&>(*atlas) = slughorn::serial::read(ifs);

	atlas->packTextures();

	return atlas;
}

void Atlas::packTextures() {
	if(_curveTexture.valid()) return;

	_curveTexture = _makeTexture(getCurveTextureData());
	_bandTexture = _makeTexture(getBandTextureData());
}

osg::ref_ptr<osg::Texture2D> Atlas::_makeTexture(const slughorn::Atlas::TextureData& data) {
	osg::ref_ptr<osg::Image> img = new osg::Image();

	const auto width = static_cast<int>(data.width);
	const auto height = static_cast<int>(data.height);

	if(data.format == slughorn::Atlas::TextureData::Format::RGBA32F) {
		img->allocateImage(width, height, 1, GL_RGBA, GL_FLOAT);
		img->setInternalTextureFormat(GL_RGBA32F_ARB);
	}

	else {
		img->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_SHORT);
		img->setInternalTextureFormat(GL_RGBA16UI);
		img->setPixelFormat(GL_RGBA_INTEGER);
	}

	memcpy(img->data(), data.bytes.data(), data.bytes.size());

	osg::ref_ptr<osg::Texture2D> tex = new osg::Texture2D(img);

	tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);
	tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
	tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
	tex->setResizeNonPowerOfTwoHint(false);

	return tex;
}

osg::StateSet* Atlas::createDefaultStateSet() const {
	auto* ss = new osg::StateSet();
	auto* program = new osg::Program();

	// TODO: IMPROVE THIS SUBSTANTIALLY!
	program->addShader(osg::Shader::readShaderFile(osg::Shader::VERTEX, "../src/osgSlug-vert.glsl"));
	program->addShader(osg::Shader::readShaderFile(osg::Shader::FRAGMENT, "../src/osgSlug-frag.glsl"));

	ss->setAttributeAndModes(program, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("osgSlug_curveTexture", 0));
	ss->addUniform(new osg::Uniform("osgSlug_bandTexture", 1));
	ss->addUniform(new osg::Uniform("osgSlug_effectTexture", 2));
	ss->setTextureAttributeAndModes(0, _curveTexture, osg::StateAttribute::ON);
	ss->setTextureAttributeAndModes(1, _bandTexture, osg::StateAttribute::ON);
	ss->setMode(GL_BLEND, osg::StateAttribute::ON);
	ss->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
	ss->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);

	if(const char* debug = getenv("OSGSLUG_DEBUG"); debug) {
		int dm = 0;

		try {
			dm = static_cast<int>(std::stoul(debug, nullptr));
		}

		catch(const std::exception& e) {
			OSG_WARN << "Invalid OSGSLUG_DEBUG value; ignoring..." << std::endl;
		}

		OSG_NOTICE << "OSGSLUG_DEBUG mode=" << dm << " detected..." << std::endl;

		ss->addUniform(new osg::Uniform("osgSlug_debugMode", dm));
	}

	return ss;
}

}
