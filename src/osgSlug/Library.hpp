#pragma once

#include "Types.hpp"

namespace osgSlug {

// osgSlug's osgx::Library subclass. Constructing it initializes osgx (the base class) and then
// registers osgSlug's `#pragma osgSlug` shader-lib catalog; destroying it releases both. Create
// exactly one, before building any osgSlug scene, and declare it before the viewer:
//
//   auto lib = osgSlug::initialize(arguments);
//   osgViewer::Viewer viewer(arguments);
//
// osgSlug's shader constants (Atlas::SHADER_*) are unexpanded GLSL; their `#pragma` lines are
// expanded against this catalog when a Program is built, which requires a live Library.
//
// osgSlug declares its own osgx::Bindings slots, pinnable through `options` like osgx's:
//   SSBO: osgSlug::atlas.shapes, osgSlug::atlas.sdfTiles, osgSlug::layers,
//         osgSlug::path.points, osgSlug::path.shapes
//   TextureUnit: osgSlug::atlas.{curve,band,gradient,sdf}, osgSlug::effect, osgSlug::scanline
// osgSlug::effect is the unit a FragmentHook's osgSlug_effectTexture reads; the application binds
// its own texture there (Library::instance().bindings().get("osgSlug::effect")).
class Library: public osgx::Library {
	public:
		explicit Library(
			osg::ArgumentParser* arguments=nullptr,
			const osgx::LibraryOptions& options=osgx::LibraryOptions()
		);
};

// Constructs the Library. Returned by value (guaranteed copy elision), same as osgx::initialize().
Library initialize(
	osg::ArgumentParser& arguments,
	const osgx::LibraryOptions& options=osgx::LibraryOptions()
);
Library initialize(const osgx::LibraryOptions& options=osgx::LibraryOptions());

}
