//vimrun! ./osgslug-mask --clear-color 0.06,0.06,0.10,1.0

// Shape masking demo - exercises slughorn::Mask on CompositeShape.
//
// --type msdf|circle|rect|capsule|arc|arcband mask approach (default: msdf)
// --invert knockout: discard inside the mask
// --debug-msdf show raw MSDF tile RGB (msdf type only)
//
// Scene: orange rect covering most of the canvas (effectId=1) masked by a shape at center.
// All types produce a visually comparable result; the differences are in edge quality, cost,
// and whether the mask shape was pre-baked (msdf) or evaluated analytically each fragment.
//
// Coordinate recovery in the hook:
// data.emCoord - shape-local: (0,0) = content shape's canvas bbox min
// canvasCoord = data.emCoord + osgSlug_mask.contentOrigin (absolute canvas space)
// Mask params are in canvas space.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

static constexpr float RECT_X = 0.05f;
static constexpr float RECT_Y = 0.05f;
static constexpr float MASK_CX = 0.5f;
static constexpr float MASK_CY = 0.5f;
static constexpr float MSDF_RANGE = 0.025f;

// ================================================================================================
// GLSL - shared preamble (includes lib_fragment which defines osgSlug_MaskData + osgSlug_mask)
// ================================================================================================

/* static const std::string HOOK_COMMON = R"(
#version 330 core
#pragma osgSlug lib_fragment
#pragma osgSlug lib_mask

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}
)"; */

// ================================================================================================
// GLSL - mask hook: delegates entirely to osgSlug_Mask_Evaluate from lib_mask
// ================================================================================================

static const std::string HOOK_MASK_BODY = R"(
#version 330 core
#pragma osgSlug lib_fragment
#pragma osgSlug lib_mask

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	if(data.effectId != 1) return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);

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
			"msdf (DEFAULT)",
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

	// Content: orange rect, effectId=1 triggers the mask hook.
	canvas.rect(RECT_X, RECT_Y, 1.0f - 2.0f * RECT_X, 1.0f - 2.0f * RECT_Y);
	canvas.fill({1.0_cv, 0.6_cv, 0.1_cv, 1.0_cv});
	auto rectComp = canvas.finalize();
	rectComp.layers[0].effectId = 1;

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

	// auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::FragmentHook, HOOK_COMMON + HOOK_MASK_BODY}});
	auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::FragmentHook, HOOK_MASK_BODY}});

	// osgSlug_mask struct members - same layout for all types.
	const auto& cl = rectComp.layers[0];
	const auto& mk = *rectComp.mask;

	ss->addUniform(new osg::Uniform("osgSlug_mask.type", static_cast<int>(maskType)));
	ss->addUniform(new osg::Uniform("osgSlug_mask.invert", invert));
	ss->addUniform(new osg::Uniform("osgSlug_mask.debug", debugMSDF));
	ss->addUniform(new osg::Uniform("osgSlug_mask.contentOrigin", osg::Vec2f(cl.transform.x, cl.transform.y)));
	ss->addUniform(new osg::Uniform("osgSlug_mask.params", osg::Vec4f(mk.params[0], mk.params[1], mk.params[2], mk.params[3])));
	ss->addUniform(new osg::Uniform("osgSlug_mask.params2", osg::Vec2f(mk.params[4], mk.params[5])));

	if(maskType == slughorn::Mask::Type::MSDF) {
		const auto maskShape = atlas->getShape(maskKey).value();

		ss->addUniform(new osg::Uniform("osgSlug_mask.msdfLayer", maskShape.msdfLayer));
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
