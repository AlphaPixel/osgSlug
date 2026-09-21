#include "osgSlug/Atlas.hpp"
#include "osgSlug/Drawable.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Image>
#include <osgUtil/CullVisitor>
#include <osgx/SDF.hpp>

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
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881A
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RG_INTEGER
#define GL_RG_INTEGER 0x8228
#endif
#ifndef GL_RG16UI
#define GL_RG16UI 0x823A
#endif

namespace osgSlug {

namespace {

// Per-cull update of the osgSlug_viewport uniform on the Atlas's own StateSet; see the
// setCullCallback() call in Atlas::packTextures() for why this exists.
struct ViewportUniformCallback: public osg::NodeCallback {
	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		auto* cv = nv ? nv->asCullVisitor() : nullptr;
		const auto* vp = cv && cv->getCurrentCamera() ? cv->getCurrentCamera()->getViewport() : nullptr;

		if(vp) {
			auto* ss = node->getStateSet();

			if(auto* u = ss ? ss->getUniform("osgSlug_viewport") : nullptr; u) u->set(osg::Vec2(
				static_cast<float>(vp->width()),
				static_cast<float>(vp->height())
			));
		}

		traverse(node, nv);
	}
};

} // anonymous namespace

Atlas::Atlas(uint32_t texWidth):
slughorn::Atlas(texWidth),
_nullMask(RenderMask::createNull(RENDER_MASK_UBO_BINDING)) {
}

Atlas::Atlas(const slughorn::Atlas& src):
_nullMask(RenderMask::createNull(RENDER_MASK_UBO_BINDING)) {
	static_cast<slughorn::Atlas&>(*this) = src;

	packTextures();
}

Atlas::~Atlas() = default;

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

		{
			const auto& sdf = getSDF();

			if(!sdf.texture.empty()) {
				const bool single = (sdf.config.type == slughorn::Atlas::SDF::Type::SDF);
				auto img = new osg::Image();

				img->allocateImage(
					static_cast<int>(sdf.texture.width),
					static_cast<int>(sdf.texture.height),
					1,
					single ? GL_RED : GL_RGB,
					GL_FLOAT
				);

				img->setInternalTextureFormat(single ? GL_R32F : GL_RGB32F);

				std::memcpy(img->data(), sdf.texture.bytes.data(), sdf.texture.bytes.size());

				_sdfTexture = osgx::SDF::makeTexture(img);
			}
		}

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

		// The SDF-only tile table (binding 2), kept OUT of the shape record above on purpose: only
		// shapes with a baked tile get an entry, in shape order. Layers reach it through the index
		// each drawable stores in effectData.z. Per tile: rect = (x, y, w, h) in texels;
		// frame = (emOriginX, emOriginY, texelsPerEm, range), so texel = rect.xy + (em - frame.xy) *
		// frame.z.
		_sdfTileBuffer = nullptr;
		_sdfTileIndex.clear();

		for(const auto& [key, shape] : getShapes()) {
			if(!shape.sdf) continue;

			const auto& t = *shape.sdf;

			if(!_sdfTileBuffer) _sdfTileBuffer = osgx::make_ref<osgx::Vec4Array>();

			_sdfTileIndex[key] = static_cast<int>(_sdfTileBuffer->size() / 2);

			_sdfTileBuffer->push_back({cv(t.x), cv(t.y), cv(t.w), cv(t.h)});
			_sdfTileBuffer->push_back({t.emOriginX, t.emOriginY, t.texelsPerEm, t.range});
		}

		if(_sdfTileBuffer) _sdfTileBuffer->setBufferObject(new osg::ShaderStorageBufferObject());
	}

	// Always refresh the StateSet so callers can re-apply hook changes by re-calling.
	setStateSet(createDefaultStateSet());

	// Keep osgSlug_viewport synced to whichever camera is actually rendering us. The GPU-live
	// AA margin (SHADER_VERT) is denominated in PIXELS, so the vertex stage needs the real
	// viewport each frame - the createDefaultStateSet() default is only a pre-first-cull
	// fallback. (Multiple cameras: last cull wins - revisit if a multi-view use appears.)
	setCullCallback(new ViewportUniformCallback());

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

int Atlas::getSDFTileIndex(const slughorn::Key& key) const {
	const auto it = _sdfTileIndex.find(key);

	return it == _sdfTileIndex.end() ? -1 : it->second;
}

osg::ref_ptr<osg::Texture2D> Atlas::_makeTexture(const slughorn::Atlas::TextureData& data) {
	osg::ref_ptr<osg::Image> img = new osg::Image();

	const auto width = static_cast<int>(data.width);
	const auto height = static_cast<int>(data.height);

	if(data.format == slughorn::Atlas::TextureData::Format::RGBA32F) {
		img->allocateImage(width, height, 1, GL_RGBA, GL_FLOAT);
		img->setInternalTextureFormat(GL_RGBA32F_ARB);
	}

	else if(data.format == slughorn::Atlas::TextureData::Format::RGBA16F) {
		// Curve texture. Bytes are already packed as half-floats by Atlas::packTextures();
		// GL_HALF_FLOAT tells GL to upload them as-is, not to convert from GL_FLOAT.
		img->allocateImage(width, height, 1, GL_RGBA, GL_HALF_FLOAT);
		img->setInternalTextureFormat(GL_RGBA16F_ARB);
	}

	else if(data.format == slughorn::Atlas::TextureData::Format::RG16UI) {
		// Band texture (current default). B/A are never consumed by the shader (which only
		// ever reads .xy), so this is lossless, not a tradeoff like RGBA16F above.
		img->allocateImage(width, height, 1, GL_RG, GL_UNSIGNED_SHORT);
		img->setInternalTextureFormat(GL_RG16UI);
		img->setPixelFormat(GL_RG_INTEGER);
	}

	else if(data.format == slughorn::Atlas::TextureData::Format::RGBA16UI) {
		// Legacy band texture format, read-compat only (a .slug/.slugb serialized before
		// 2026-08-29 can still deserialize into this format).
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
