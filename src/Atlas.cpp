#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Image>

OSGSLUG_ENABLE_WARNINGS

#include <bit>
#include <stdexcept>
#include <fstream>

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
#ifndef GL_RGB32F
#define GL_RGB32F 0x8815
#endif

namespace osgSlug {

Atlas::Atlas(bool useGL3, uint32_t texWidth):
slughorn::Atlas(texWidth),
_useGL3(useGL3) {
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

bool Atlas::addChild(osg::Node* child) {
	const bool result = osg::Group::addChild(child);

	if(result && _state == State::Packed) {
		if(auto* d0 = dynamic_cast<osgSlug::Drawable*>(child)) {
			d0->compile();
			if(d0->onAtlasAttached) d0->onAtlasAttached(*this);
		}
		else osgx::LambdaVisitor<osg::Drawable>([this](auto& d1) {
			if(auto* sd = dynamic_cast<osgSlug::Drawable*>(&d1)) {
				sd->compile();
				if(sd->onAtlasAttached) sd->onAtlasAttached(*this);
			}
		})(child);
	}

	return result;
}

void Atlas::packTextures() {
	if(!_curveTexture.valid()) {
		if(
			!getCurveTextureData().bytes.size() ||
			!getBandTextureData().bytes.size()
		) throw std::runtime_error("Atlas::build() must be called before Atlas::packTextures()");

		_curveTexture = _makeTexture(getCurveTextureData());
		_bandTexture = _makeTexture(getBandTextureData());

		if(!getGradientTextureData().bytes.empty()) {
			_gradientTexture = _makeTexture(getGradientTextureData());
		}

		if(!getScanlineCurveTextureData().bytes.empty()) {
			_scanlineTexture = _makeTexture(getScanlineCurveTextureData());
		}

#ifdef SLUGHORN_HAS_MSDF
		{
			const auto& msdfData = getMSDFTextureData();

			if(!msdfData.empty() && msdfData.depth > 0) {
				const auto W = static_cast<int>(msdfData.width);
				const auto H = static_cast<int>(msdfData.height);
				const auto numLayers = static_cast<int>(msdfData.depth);
				const size_t floatsPerLayer = static_cast<size_t>(W) * static_cast<size_t>(H) * 3;
				const auto* src = reinterpret_cast<const float*>(msdfData.bytes.data());

				auto tex = new osg::Texture2DArray();

				tex->setTextureSize(W, H, numLayers);
				tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
				tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
				tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
				tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

				for(int l = 0; l < numLayers; l++) {
					auto img = new osg::Image();

					img->allocateImage(W, H, 1, GL_RGB, GL_FLOAT);
					img->setInternalTextureFormat(GL_RGB32F);

					std::memcpy(
						img->data(),
						src + static_cast<size_t>(l) * floatsPerLayer,
						floatsPerLayer * sizeof(float)
					);

					tex->setImage(static_cast<unsigned int>(l), img);
				}

				_msdfTexture = tex;
			}
		}
#endif

		// Build the atlas-level shape SSBO (binding 0). One entry per unique shape;
		// 3 vec4s = 48 bytes per entry: bandXform, shapeData, originData.
		_shapeBuffer = osgx::make_ref<osgx::Vec4Array>();

		uint32_t idx = 0;

		for(const auto& [key, shape] : getShapes()) {
			_shapeIndex[key] = idx++;

			_shapeBuffer->push_back({
				shape.bandScaleX,
				shape.bandScaleY,
				shape.bandOffsetX,
				shape.bandOffsetY
			});

			_shapeBuffer->push_back({
				cv(shape.bandTexX),
				cv(shape.bandTexY),
				cv(shape.bandMaxX),
				cv(shape.bandMaxY)
			});

			_shapeBuffer->push_back({cv(shape.originX), cv(shape.originY), 0_cv, 0_cv});
		}

		_shapeBuffer->setBufferObject(new osg::ShaderStorageBufferObject());
	}

	// Always refresh the StateSet so callers can switch GL3/SSBO mode by re-calling.
	setStateSet(createDefaultStateSet(_useGL3));

	_state = State::Packed;

	// Compile any osgSlug::Drawable children that were added before packTextures() was called.
	//
	// NOTE: We _must_ use `unsigned int` here instead of `size_t` because ... OSG is dumb like
	// that.
	for(unsigned int i = 0; i < getNumChildren(); i++) {
		if(auto* d = dynamic_cast<osgSlug::Drawable*>(getChild(i))) {
			d->compile();
			if(d->onAtlasAttached) d->onAtlasAttached(*this);
		}
	}
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

}
