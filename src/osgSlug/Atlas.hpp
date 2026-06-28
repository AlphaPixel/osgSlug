#pragma once

#include "Types.hpp"

#include "slughorn/serial.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/Group>
#include <osg/Shader>
#include <osg/Texture2D>
#include <osg/Texture2DArray>
#include <osg/BufferObject>
#include <osg/BufferIndexBinding>

OSGSLUG_ENABLE_WARNINGS

#ifdef SLUGHORN_HAS_MSDF
#include "slughorn/render.hpp"
#endif

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace osgSlug {

class Drawable; // forward declaration; full type needed only in Atlas.cpp

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
class Atlas: public osg::Group, public slughorn::Atlas {
public:
	enum class State { Empty, Built, Packed };

	// useGL3: selects the GL3 attrib-based vertex path instead of the SSBO (GL4) path.
	// An application uses one or the other for its lifetime; mixing is not supported.
	// texWidth: texture atlas width; rarely needs changing from the default.
	Atlas(bool useGL3=false, uint32_t texWidth=slughorn::Atlas::DEFAULT_TEXTURE_WIDTH);
	explicit Atlas(const slughorn::Atlas& src);

	static osg::ref_ptr<Atlas> read(std::filesystem::path path);
	static osg::ref_ptr<Atlas> read(std::ifstream& ifs);

	bool getUseGL3() const { return _useGL3; }
	State getState() const { return _state; }

	// Pack the raw TextureData buffers produced by build() into OSG texture objects, and set the
	// Atlas Group's StateSet with the default shader program (SSBO or GL3), all textures, uniforms,
	// blend state, and SSBO binding 0. Must be called after build(). Texture packing is idempotent
	// for the textures; the StateSet program is always refreshed.
	// After packing, _state becomes Packed and any existing osgSlug::Drawable children are
	// compiled automatically.
	void packTextures();

	// Overrides osg::Group::addChild. When the atlas is already Packed, any osgSlug::Drawable
	// child that hasn't been compiled yet is compiled immediately so callers don't need to call
	// setAtlas() + compile() manually.
	bool addChild(osg::Node* child) override;

	// Valid after packTextures().
	osg::Texture2D* getCurveTexture() const { return _curveTexture.get(); }
	osg::Texture2D* getBandTexture() const { return _bandTexture.get(); }
	osg::Texture2D* getGradientTexture() const { return _gradientTexture.get(); }
	osg::Texture2D* getScanlineTexture() const { return _scanlineTexture.get(); }

	// Valid after packTextures(); null when no shapes have MSDF registered.
	osg::Texture2DArray* getMSDFTexture() const { return _msdfTexture.get(); }

	// Atlas-level shape SSBO (binding 0). Valid after packTextures().
	// Returns the 0-based index of key in the shape buffer, or throws if not found.
	uint32_t getShapeIndex(const slughorn::Key& key) const;
	osgx::Vec4Array* getShapeBuffer() const { return _shapeBuffer.get(); }

	// Vertex hook no-op; osgSlug_Vertex() passes pos through unchanged.
	static const std::string SHADER_NOOP_VERTEX_HOOK;
	// Fragment hook no-op; osgSlug_Fragment() returns coverage unchanged.
	static const std::string SHADER_NOOP_FRAGMENT_HOOK;
	// Default for osgSlug_FragmentExt's pre-discard hook. Always linked as its own shader unit,
	// independent of fragEffects, so existing custom fragEffects units never need to know this hook
	// exists.
	static const std::string SHADER_NOOP_FRAGMENT_EXT_HOOK;
	static const std::string SHADER_ATLAS_TYPES; // AtlasShapeData + binding 0 only
	static const std::string SHADER_TYPES; // SHADER_ATLAS_TYPES + LayerData + binding 1
	static const std::string SHADER_LIB_VERTEX;
	static const std::string SHADER_LIB_FRAGMENT;
	static const std::string SHADER_LIB_SCANLINE; // evaluate_bezier + intersect_monotonic + scanline_sweep
	static const std::string SHADER_VERT; // main SSBO vertex shader (embedded)
	static const std::string SHADER_VERT_GL3; // GL3 attrib-based vertex shader (embedded)
	static const std::string SHADER_VERT_DECAL; // tangent-plane decal vertex shader (embedded)
	static const std::string SHADER_FRAG; // main fragment shader (embedded, resolved)
	static const std::string SHADER_SCANLINE_VERT; // ScanlineDrawable vertex shader
	static const std::string SHADER_SCANLINE_FRAG; // ScanlineDrawable fragment shader (resolved)

	enum Hook { VertexHook, FragmentHook, FragmentExtHook };

	using HookList = std::vector<std::pair<Hook, std::string>>;

	osg::StateSet* createDefaultStateSet(bool useGL3=false, HookList hooks={}) const;

	// Returns a lightweight StateSet carrying only the shader program (with hooks applied).
	// No textures, uniforms, or blend state - those are inherited from the Atlas Group's StateSet.
	// Set this on a child drawable to override just the program (e.g. for vertex animation hooks).
	// Call BEFORE compile() so compile()'s getOrCreateStateSet() merges into the hook StateSet
	// rather than replacing it.
	osg::StateSet* createHookStateSet(HookList hooks={}) const;

	// Returns a Program that uses the tangent-plane decal vertex shader.
	// Set this on an SSBODecalDrawable's StateSet to override the parent Geode's program.
	// (SSBODecalDrawable::compile() calls this automatically.)
	osg::Program* createDecalProgram(HookList hooks={}) const;

	static osg::ref_ptr<Atlas> fromAtlas(const slughorn::Atlas& src, bool useGL3=false) {
		osg::ref_ptr<Atlas> atlas = new osgSlug::Atlas(useGL3);

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

	bool _useGL3 = false;
	State _state = State::Empty;

	osg::ref_ptr<osg::Texture2D> _curveTexture;
	osg::ref_ptr<osg::Texture2D> _bandTexture;
	osg::ref_ptr<osg::Texture2D> _gradientTexture; // null when no gradients registered
	osg::ref_ptr<osg::Texture2D> _scanlineTexture; // Scanline Sweeper curve data (RGBA32F)

	osg::ref_ptr<osg::Texture2DArray> _msdfTexture; // null when no shapes have MSDF registered

	osg::ref_ptr<osgx::Vec4Array> _shapeBuffer; // atlas shape SSBO, binding 0
	std::unordered_map<slughorn::Key, uint32_t, slughorn::KeyHash> _shapeIndex;
};

}
