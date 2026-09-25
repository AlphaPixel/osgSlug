#include "osgSlug/Library.hpp"
#include "osgSlug/Atlas.hpp"

namespace osgSlug {

// The osgx::Library base constructor has already registered osgx's catalogs (pbr, light, ibl,
// environment, picking, sdf, ...), which osgSlug's shaders also pull in.
Library::Library(osg::ArgumentParser* arguments, const osgx::LibraryOptions& options):
osgx::Library(arguments, options) {
	auto& slots = bindings();

	slots.declare(osgx::Bindings::Type::UBO, "osgSlug::mask");

	slots.declare(osgx::Bindings::Type::SSBO, "osgSlug::atlas.shapes");
	slots.declare(osgx::Bindings::Type::SSBO, "osgSlug::atlas.sdfTiles");
	slots.declare(osgx::Bindings::Type::SSBO, "osgSlug::layers");
	slots.declare(osgx::Bindings::Type::SSBO, "osgSlug::path.points");
	slots.declare(osgx::Bindings::Type::SSBO, "osgSlug::path.shapes");

	// Preferred indices are the units these textures used before they were slots.
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::atlas.curve", 0);
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::atlas.band", 1);
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::atlas.gradient", 2);
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::atlas.sdf", 3);
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::effect", 4);
	slots.declare(osgx::Bindings::Type::TextureUnit, "osgSlug::scanline", 0);

	// "fragment_emcoord" is struct/interface content only (osgSlug_FragmentData, geom/fx blocks,
	// etc.) - it MUST stay body-free, since SHADER_FRAG, both SHADER_MASK_FRAGMENT_HOOK* units,
	// SHADER_NOOP_FRAGMENT_EXT_HOOK, and whichever FragmentHook is active all pull it in and get
	// linked into the same Program; GLSL only allows ONE of several linked shader objects to
	// provide a given function's body. "fragment" (interface + a real default
	// osgSlug_FragmentEmCoord body) is therefore reserved for exactly one of those - the active
	// FragmentHook - to ever pull in. Same story for vertex: "vertex" is interface-only (safe
	// everywhere), "vertex_main" carries real osgSlug_VertexDefault/_Rotate/_Scale bodies and is
	// pulled only by the two main vertex units (SHADER_VERT/SHADER_VERT_DECAL), never a hook. See
	// Atlas.hpp's declaration comments for the full per-pragma safety reasoning.
	const osgx::ShaderLib libs[] = {
		{"vertex", {}, Atlas::SHADER_VERTEX},
		{"vertex_lib", {}, Atlas::SHADER_LIB_VERTEX},
		{"vertex_main", {}, Atlas::SHADER_VERTEX_MAIN},
		{"fragment_emcoord", {}, Atlas::SHADER_FRAGMENT_EMCOORD},
		{"fragment", {}, Atlas::SHADER_FRAGMENT},
		{"fragment_lib", {}, Atlas::SHADER_LIB_FRAGMENT},
		{"coverage_lib", {}, Atlas::SHADER_LIB_COVERAGE},
		{"scanline_lib", {}, Atlas::SHADER_LIB_SCANLINE},
		{"mask_lib", {}, Atlas::SHADER_LIB_MASK}
	};

	osgx::registerShaderLibs("osgSlug", libs);
}

Library initialize(osg::ArgumentParser& arguments, const osgx::LibraryOptions& options) {
	return Library(&arguments, options);
}

Library initialize(const osgx::LibraryOptions& options) {
	return Library(nullptr, options);
}

}
