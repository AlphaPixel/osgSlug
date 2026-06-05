#pragma once

#include "Types.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Referenced>
#include <osg/Texture2D>
#include <osg/BufferObject>
#include <osg/BufferIndexBinding>

OSGSLUG_ENABLE_WARNINGS

#include <filesystem>
#include <unordered_map>

namespace osgSlug {

// ================================================================================================
// osgSlug::Atlas
//
// Thin OSG adapter over slughorn::Atlas.
//
// Workflow:
//
// 1. Call addShape() -> build() via the slughorn::Atlas base (unchanged).
// 2. Call packTextures() to upload the raw buffers into osg::Texture2D.
// 3. Call getCurveTexture() / getBandTexture() if you need access to the data (unlikely).
//
// The split between build() and packTextures() is intentional: build() is pure C++ and can run on
// any thread; packTextures() touches OSG and should run on the OSG main/draw thread (or before the
// scene-graph is live).
// ================================================================================================
class Atlas: public osg::Referenced, public slughorn::Atlas {
public:
	// Atlas() = default;
	Atlas(uint32_t texWidth=slughorn::Atlas::DEFAULT_TEXTURE_WIDTH);
	explicit Atlas(const slughorn::Atlas& src);

	static osg::ref_ptr<Atlas> read(std::filesystem::path path);
	static osg::ref_ptr<Atlas> read(std::ifstream& ifs);

	// Pack the raw TextureData buffers produced by build() into OSG texture objects. Must be called
	// after build() and before getCurveTexture() / getBandTexture(). This function is idempotent
	// (safe to call more than once); I love that word.
	void packTextures();

	// Valid after packTextures().
	osg::Texture2D* getCurveTexture() const { return _curveTexture.get(); }
	osg::Texture2D* getBandTexture() const { return _bandTexture.get(); }
	osg::Texture2D* getGradientTexture() const { return _gradientTexture.get(); }

	// Atlas-level shape SSBO (binding 0). Valid after packTextures().
	// Returns the 0-based index of key in the shape buffer, or throws if not found.
	uint32_t getShapeIndex(const slughorn::Key& key) const;
	osgx::Vec4Array* getShapeBuffer() const { return _shapeBuffer.get(); }

	static const std::string SHADER_NOOP_VERTEX;
	static const std::string SHADER_NOOP_FRAGMENT;
	static const std::string SHADER_ATLAS_TYPES; // AtlasShapeData + binding 0 only
	static const std::string SHADER_TYPES; // SHADER_ATLAS_TYPES + LayerData + binding 1
	static const std::string SHADER_LIB_VERTEX;
	static const std::string SHADER_LIB_FRAGMENT;

	osg::StateSet* createDefaultStateSet(
		bool useGL3=false,
		const std::string& vertEffects=SHADER_NOOP_VERTEX,
		const std::string& fragEffects=SHADER_NOOP_FRAGMENT
	) const;

	// Returns a Program that uses the tangent-plane decal vertex shader.
	// Set this on an SSBODecalDrawable's StateSet to override the parent Geode's program.
	// (SSBODecalDrawable::compile() calls this automatically.)
	osg::Program* createDecalProgram(
		const std::string& vertEffects=SHADER_NOOP_VERTEX,
		const std::string& fragEffects=SHADER_NOOP_FRAGMENT
	) const;

	static osg::ref_ptr<Atlas> fromAtlas(const slughorn::Atlas& src) {
		osg::ref_ptr<Atlas> atlas = new osgSlug::Atlas();

		static_cast<slughorn::Atlas&>(*atlas) = src;

		atlas->packTextures();

		return atlas;
	}

protected:
	virtual ~Atlas() = default;

	template<typename... Args>
	static osg::ref_ptr<Atlas> fromAtlas(Args&&... args) {
		return new osgSlug::Atlas(slughorn::serial::read(std::forward<Args>(args)...));
	}

private:
	static osg::ref_ptr<osg::Texture2D> _makeTexture(const slughorn::Atlas::TextureData& data);

	osg::ref_ptr<osg::Texture2D> _curveTexture;
	osg::ref_ptr<osg::Texture2D> _bandTexture;
	osg::ref_ptr<osg::Texture2D> _gradientTexture; // null when no gradients registered

	osg::ref_ptr<osgx::Vec4Array> _shapeBuffer; // atlas shape SSBO, binding 0
	std::unordered_map<slughorn::Key, uint32_t, slughorn::KeyHash> _shapeIndex;
};

}
