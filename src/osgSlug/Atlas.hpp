#pragma once

#include "Types.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Referenced>
#include <osg/Texture2D>

OSGSLUG_ENABLE_WARNINGS

#include <filesystem>

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
	Atlas() = default;
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

	osg::StateSet* createDefaultStateSet() const;

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
};

}
