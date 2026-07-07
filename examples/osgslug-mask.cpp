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
// All --type values produce a visually comparable result; the differences are in edge
// quality, cost, and whether the mask shape was pre-baked (msdf) or evaluated analytically
// each fragment.
//
// Coordinate recovery in the hook:
// data.emCoord - shape-local: (0,0) = this layer's own canvas bbox min
// canvasCoord = data.emCoord + layerOrigin, where layerOrigin is this fragment's own layer's
// transform.xy, read per-layer from the LayerBuffer SSBO (see osgSlug_Mask_Evaluate) -- each
// layer has its own origin, not one shared per mask (see RenderMask.hpp).
// Mask params are in canvas space.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

static constexpr float MASK_CX = 0.5f;
static constexpr float MASK_CY = 0.5f;
static constexpr float MSDF_RANGE = 0.025f;

// ================================================================================================
// GLSL - mask hook: every fragment this StateSet renders goes through the same masked
// RenderGroup (this demo has exactly one masked composite, no unmasked layers mixed in), so
// the hook delegates unconditionally -- no per-layer effectId branch needed anymore.
// ================================================================================================

static const std::string HOOK_MASK_BODY = R"(
#version 430 core
#pragma osgSlug lib_fragment
#pragma osgSlug lib_mask

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	return osgSlug_Mask_Evaluate(data);
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

	// Two layers in one composite -- both must get masked together (see file header comment).
	// beginPath() between the two is required: fill() doesn't clear the internal path, so
	// without it the second rect's curves accumulate onto the first's (one merged shape
	// instead of two), which is what made both squares render as a single color.
	canvas.rect(0_cv, 0.0_cv, 0.5_cv, 0.5_cv);
	canvas.fill({1.0_cv, 0.6_cv, 0.1_cv, 1.0_cv});
	canvas.beginPath();
	canvas.rect(0.5_cv, 0.5_cv, 0.5_cv, 0.5_cv);
	canvas.fill({1_cv, 1_cv, 0.1_cv, 1.0_cv});

	auto rectComp = canvas.finalize();

	// Mask: MSDF type bakes a circle shape into the atlas; procedural types use only params.
	slughorn::Key maskKey{};

	if(maskType == slughorn::Mask::Type::MSDF) {
		canvas.circle(MASK_CX, MASK_CY, 0.25f);
		canvas.fill({1.0_cv, 1.0_cv, 1.0_cv, 1.0_cv});

		maskKey = canvas.finalize().layers[0].key;

		// Store cx/cy/r/range in params; GLSL derives the tile bbox from these at runtime.
		auto mk = slughorn::Mask::msdf(maskKey, invert);

		mk.params[0] = MASK_CX; mk.params[1] = MASK_CY;
		mk.params[2] = 0.25f; mk.params[3] = MSDF_RANGE;
		rectComp.mask = mk;
	}

	else {
		switch(maskType) {
		case slughorn::Mask::Type::Circle:
			rectComp.mask = slughorn::Mask::circle(MASK_CX, MASK_CY, 0.25f, invert);
			break;

		case slughorn::Mask::Type::Rect:
			rectComp.mask = slughorn::Mask::rect(0.25f, 0.25f, 0.5f, 0.5f, invert);
			break;

		case slughorn::Mask::Type::Capsule:
			rectComp.mask = slughorn::Mask::capsule(0.2f, 0.5f, 0.8f, 0.5f, 0.15f, invert);
			break;

		case slughorn::Mask::Type::Arc:
			rectComp.mask = slughorn::Mask::arc(MASK_CX, MASK_CY, 0.35f, -2.2f, 2.2f, invert);
			break;

		case slughorn::Mask::Type::ArcBand:
			rectComp.mask = slughorn::Mask::arcBand(MASK_CX, MASK_CY, 0.35f, -2.2f, 2.2f, 0.05f, invert);
			break;

		default:
			break;
		}
	}

	atlas->setMSDFTileSize(128);
	atlas->build();

	if(maskType == slughorn::Mask::Type::MSDF) atlas->registerMSDF(maskKey, MSDF_RANGE);

	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addCompositeShape(rectComp);

	// osgSlug_mask itself (type/invert/params/params2/msdfLayer) is populated automatically per
	// masked RenderGroup -- see ShapeDrawable::compile() and
	// ShapeDrawable::drawImplementation()'s applyMask(). --debug-msdf is the one field that
	// isn't authoring data (not part of slughorn::Mask), so it's set directly on the RenderMask.
	// (getLayerMask() is SSBO-specific; this demo's masking only exists on that backend.)
	if(auto* ssbo = dynamic_cast<osgSlug::ShapeDrawable*>(sd.get())) {
		if(auto* mask = ssbo->getLayerMask(0)) mask->setDebug(debugMSDF);
	}

	auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::FragmentHook, HOOK_MASK_BODY}});

	if(maskType == slughorn::Mask::Type::MSDF) {
		ss->addUniform(new osg::Uniform("osgSlug_maskMsdfTexture", 7));

		if(auto* msdfTex = atlas->getMSDFTexture()) {
			ss->setTextureAttributeAndModes(7, msdfTex, osg::StateAttribute::ON);
		}

		else {
			OSG_WARN << "osgslug-mask: no MSDF texture after packTextures()" << std::endl;
		}
	}

	sd->setStateSet(ss);
	atlas->addChild(sd);

	return example::run(viewer, args, atlas);
}
