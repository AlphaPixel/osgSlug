//vimrun! ./osgslug-mask --clear-color 0.1,0.2,0.3

// Shape masking demo - exercises slughorn::Mask on CompositeShape.
//
// --type msdf|circle|rect|capsule|arc|arcband mask approach (default: msdf)
// --invert knockout: discard inside the mask
// --debug-msdf show raw MSDF tile RGB (msdf type only)
//
// Scene: a two-layer composite (orange rect + yellow rect), masked as a whole by a shape at
// center. Both layers share one CompositeShape.mask, so both get masked together -- this is
// the regression test for the bug that motivated the RenderMask/RenderGroup rework: masking
// used to piggyback on a single layer's effectId, so a second layer in the same composite
// rendered flat/unmasked. Now osgSlug_mask is populated automatically per masked RenderGroup
// (see ShapeDrawable::compile(), ShapeDrawable::drawImplementation()'s applyMask()) --
// nothing in this file uploads osgSlug_mask.* by hand anymore.
//
// Masking is now fully automatic and needs NO custom FragmentHook at all (contrast with the
// old HOOK_MASK_BODY this file used to define) -- osgSlug_FragmentMask(), an always-linked
// early hook, evaluates and discards BEFORE slug_Render runs, so a mask that only reveals a
// fraction of a shape skips Slug's curve-band loop entirely for the rest. See
// ai/context-todo-mask.md, "osgSlug_FragmentMask() early hook."
//
// All --type values produce a visually comparable result; the differences are in edge
// quality, cost, and whether the mask shape was pre-baked (msdf) or evaluated analytically
// each fragment.
//
// Coordinate recovery (only needed by --debug-msdf's custom hook below; the automatic path
// does this internally): data.emCoord is shape-local, (0,0) = this layer's own canvas bbox
// min. canvasCoord = data.emCoord + layerOrigin, where layerOrigin is this fragment's own
// layer's transform.xy, read per-layer from the LayerBuffer SSBO -- each layer has its own
// origin, not one shared per mask (see RenderMask.hpp). Mask params are in canvas space.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

static constexpr float MASK_CX = 0.5f;
static constexpr float MASK_CY = 0.5f;
static constexpr float MSDF_RANGE = 0.025f;

// ================================================================================================
// GLSL - --debug-msdf only: shows the raw baked MSDF tile RGB (median-of-three inputs, before
// reconstruction) in place of the normal fill color, for fragments the mask (and Slug's own
// coverage) already let through. osgSlug_Mask_DebugMSDF is the opt-in helper the automatic
// osgSlug_FragmentMask pipeline itself never calls (its early hook only returns a coverage
// float -- no room for a raw-tile preview); see that helper's comment in SHADER_LIB_MASK for
// why. Unlike the OLD --debug-msdf (which showed the tile across the whole shape, mask or no
// mask, since masking used to gate osgSlug_Fragment's output rather than discard beforehand),
// this now only shows tile pixels the mask has already revealed -- arguably the more useful
// view, since it overlays the tile exactly where it's actually affecting the render.
// ================================================================================================

// Forward-declares (does NOT `#pragma osgSlug lib_mask`): that library's function BODIES are
// already linked in via the always-present MaskHook shader object (SHADER_MASK_FRAGMENT_HOOK),
// and GLSL rejects the same function being defined twice across shader objects linked into one
// Program. Pulling in lib_mask a second time here to reach these two helpers is exactly the
// trap that broke this file's first draft -- forward-declare-and-call instead, the same way
// SHADER_FRAG itself reaches osgSlug_Fragment/osgSlug_FragmentExt/osgSlug_FragmentMask.
//
// This hook occupies the FragmentHook slot, which REPLACES the entire default shader object --
// so it must define BOTH functions that slot's default (SHADER_NOOP_FRAGMENT_HOOK) normally
// provides, not just osgSlug_Fragment. osgSlug_FragEmCoord is the one easy to forget (main()
// calls it unconditionally, before osgSlug_Fragment even runs) since most hooks never need to
// touch it -- passthrough here, identical to the noop default.
static const std::string HOOK_DEBUG_MSDF = R"(
#version 430 core
#pragma osgSlug lib_fragment

vec2 osgSlug_Mask_LayerOrigin();
vec3 osgSlug_Mask_DebugMSDF(vec2 canvasCoord);

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	vec3 msd = osgSlug_Mask_DebugMSDF(data.emCoord + osgSlug_Mask_LayerOrigin());

	if(msd.r < -0.5) return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);

	return vec4(msd, data.fill * data.layerColor.a);
}
)";

// ================================================================================================
// Helpers
// ================================================================================================

static slughorn::Mask::Type maskTypeFromString(const std::string& s) {
	if(s == "circle") return slughorn::Mask::Type::Circle;
	if(s == "rect") return slughorn::Mask::Type::Rect;
	if(s == "capsule") return slughorn::Mask::Type::Capsule;
	if(s == "arc") return slughorn::Mask::Type::Arc;
	if(s == "arcband") return slughorn::Mask::Type::ArcBand;

	return slughorn::Mask::Type::MSDF;
}

// ================================================================================================
// main
// ================================================================================================

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Demonstrates masking", {
		{
			"--type <string>",
			"Example to run; one of:\n"
			"\tmsdf (DEFAULT)\n"
			"\tcircle\n"
			"\trect\n"
			"\tcapsule\n"
			"\tarc\n"
			"\tarcband\n"
		}
	})) return 0;

	std::string typeName = "msdf";

	while(args.read("--type", typeName)) {
		if(!example::validateArgument(args, "--type", typeName, {
			"msdf",
			"circle",
			"rect",
			"capsule",
			"arc",
			"arcband"
		})) return example::fail(args, 1);
	}

	bool invert = false;
	bool debugMSDF = false;

	if(args.read("--invert")) invert = true;
	if(args.read("--debug-msdf")) debugMSDF = true;

	const auto maskType = maskTypeFromString(typeName);

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	slughorn::canvas::Canvas canvas(*atlas);

/*
	// Two layers in one composite -- both must get masked together (see file header comment).
	// beginPath() between the two is required: fill() doesn't clear the internal path, so
	// without it the second rect's curves accumulate onto the first's (one merged shape
	// instead of two), which is what made both squares render as a single color.
	canvas.rect(0_cv, 0.0_cv, 0.5_cv, 0.5_cv);
	canvas.fill({1.0_cv, 0.6_cv, 0.1_cv, 1.0_cv});
	canvas.beginPath();
	canvas.rect(0.5_cv, 0.5_cv, 0.5_cv, 0.5_cv);
	canvas.fill({1_cv, 1_cv, 0.1_cv, 1.0_cv});
*/

	// Four separate CompositeShapes, one per quad, each with its own single rect layer and
	// its own distinct Mask -- see the file header comment for why this is a genuinely new
	// code path versus arc/capsule, and why these are the four newest procedural
	// Mask::Types rather than the original six (those stay in osgslug-mask.cpp).
	// beginPath() before each rect() (after the first) is required: fill() doesn't clear
	// the internal path, so without it each rect's curves would accumulate onto the last.
	canvas.rect(0.0_cv, 0.0_cv, 0.5_cv, 0.5_cv).fill({1.0_cv, 0.6_cv, 0.1_cv, 1.0_cv}); // bottom-left, orange
	canvas.beginPath().rect(0.5_cv, 0.0_cv, 0.5_cv, 0.5_cv).fill({0.9_cv, 0.2_cv, 0.6_cv, 1.0_cv}); // bottom-right, magenta
	canvas.beginPath().rect(0.0_cv, 0.5_cv, 0.5_cv, 0.5_cv).fill({0.1_cv, 0.8_cv, 0.9_cv, 1.0_cv}); // top-left, cyan
	canvas.beginPath().rect(0.5_cv, 0.5_cv, 0.5_cv, 0.5_cv).fill({1.0_cv, 1.0_cv, 0.1_cv, 1.0_cv}); // top-right, yellow

	// Mask: canvas.mask() authoring sugar (slughorn/canvas.hpp) -- both forms stage the mask
	// onto the CompositeShape finalize() is about to produce, so it lands on rectComp directly
	// with no separate composite/key-extraction dance. MSDF form commits the accumulated path
	// (the circle drawn just below) as a baked mask shape, deriving cx/cy/r from its own
	// canvas-space bbox; procedural forms need no atlas registration at all.
	if(maskType == slughorn::Mask::Type::MSDF) {
		canvas.beginPath();
		canvas.circle(MASK_CX, MASK_CY, 0.25f);
		canvas.mask(MSDF_RANGE, invert);
	}

	else {
		switch(maskType) {
		case slughorn::Mask::Type::Circle:
			canvas.mask(slughorn::Mask::circle(MASK_CX, MASK_CY, 0.25f, invert));
			break;

		case slughorn::Mask::Type::Rect:
			canvas.mask(slughorn::Mask::rect(0.25f, 0.25f, 0.5f, 0.5f, invert));
			break;

		case slughorn::Mask::Type::Capsule:
			canvas.mask(slughorn::Mask::capsule(0.2f, 0.5f, 0.8f, 0.5f, 0.15f, invert));
			break;

		case slughorn::Mask::Type::Arc:
			canvas.mask(slughorn::Mask::arc(MASK_CX, MASK_CY, 0.35f, -2.2f, 2.2f, invert));
			break;

		case slughorn::Mask::Type::ArcBand:
			canvas.mask(slughorn::Mask::arcBand(MASK_CX, MASK_CY, 0.35f, -2.2f, 2.2f, 0.05f, invert));
			break;

		default:
			break;
		}
	}

	auto rectComp = canvas.finalize();

	// canvas.mask()'s MSDF branch already called atlas->requestMSDF() itself above -- requestMSDF()
	// (unlike the old registerMSDF()) is safe to call pre-build, so build() alone renders the
	// queued tile; no post-build registration step needed here at all.
	atlas->setMSDFTileSize(128);
	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addCompositeShape(rectComp);

	// osgSlug_mask itself (type/invert/params/params2/msdfLayer) is populated automatically per
	// masked RenderGroup -- see ShapeDrawable::compile() and
	// ShapeDrawable::drawImplementation()'s applyMask(). No StateSet override needed for the
	// normal path at all: sd inherits the Atlas's own default StateSet (which already links the
	// automatic masking hook), so masking works the instant CompositeShape.mask is set.
	if(debugMSDF) sd->setHooks({{osgSlug::Atlas::FragmentHook, HOOK_DEBUG_MSDF}});

	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
