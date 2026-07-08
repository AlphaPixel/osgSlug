//vimrun! ./osgslug-pbr-ibl --trackball --ktx2 /home/cubicool/dev/OpenSceneGraph.py/examples/pyosg-lighting/data/papermill.ktx2

// Proof-of-concept for slughorn/ai/context-todo-lighting.md Idea 1: an osgSlug shape shaded as
// polished chrome. Two light sources, both physically-based GGX:
//
// - IBL: a real prefiltered environment cubemap via the split-sum technique (Karis 2013),
//   using osgx::pbr (BRDF math) and osgx::ibl (cubemap load + BRDF LUT bake) from
//   ~/dev/osgdebug/osgx.hpp.
// - Direct: a small rig of animated point lights ("spot lights" thrown into the scene, see
//   osgx::pbr::OrbitLightRig) whose highlights slide across the dome per-frame -- the motion is the
//   confirmation that N, V, and the specular math are wired correctly, not just a static
//   flat-shaded color.
//
// The dome normal comes from the shape's MSDF distance field via osgSlug_MSDFGradient() -- an
// em-space central difference of the float MSDF tile. NEVER from dFdx/dFdy(msdfSd): screen-space
// derivatives are constant per 2x2 hardware quad, and a direction built from them quantizes N
// (and every reflection of it) into pixel-scale blocks that sharp lookups amplify into a crunchy
// silhouette. That was BUG.md's boundary artifact (resolved 2026-07-06); run with --broken to
// see it.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

#include <osg/TextureCubeMap>

// ================================================================================================
// GLSL - chrome FragmentHook
// ================================================================================================

// effectData.w packing, per ai/context-todo-lighting.md: low byte = roughness*255,
// high byte = metallic*255.
static float packMaterial(float roughness, float metallic) {
	return float(int(roughness * 255.0f)) + float(int(metallic * 255.0f)) * 256.0f;
}

// MSDF range for the dome: em-space half-bandwidth the tile encodes around the edge. Not used
// for a physically-real distance anywhere (osgSlug_Fragment doesn't get msdfRange, only
// msdfSd) -- the bevel width in the hook is defined directly in msdfSd space instead. Set to
// just under the badge's own radius (0.5, see canvas.circle() below) so msdfSd sweeps its
// whole edge(0.5)->interior(1.0) range across the ENTIRE shape -- reaching 1.0 only right at
// the center -- rather than saturating a few pixels in from the edge.
static constexpr float MSDF_RANGE = 0.45f;

// Number of direct lights the shader loop supports; keep in sync with the uniform arrays
// declared in makeChromeFrag() and filled by osgx::pbr::OrbitLightRig below.
static constexpr int MAX_LIGHTS = 4;

// brokenGradient bakes in the ORIGINAL dFdx/dFdy(msdfSd) bevel direction -- the confirmed root
// cause of BUG.md's boundary artifact -- for A/B against the osgSlug_MSDFGradient() fix.
static std::string makeChromeFrag(bool brokenGradient) {
	std::string src = R"GLSL(
#version 330 core
#pragma osgSlug lib_fragment

const float PI = 3.14159265359;

const bool BROKEN_GRADIENT = )GLSL";

	src += brokenGradient ? "true;\n" : "false;\n";

	// The full osgx::pbr BRDF toolkit: D_GGX/G_Schlick/G_Smith/F_Schlick feed
	// osgx_DirectSpecular below (F_Schlick_roughness comes along too but is unused -- the
	// split-sum IBL combine folds Fresnel into the baked brdfLUT instead). DIRECT_SPECULAR,
	// IBL_SPECULAR, and TONEMAP_PBR_NEUTRAL are the shape-agnostic pieces promoted to osgx::pbr
	// once this example got a second consumer (osgslug-pbr-ibl-text.cpp) -- see
	// ai/context-todo-lighting.md.
	src += osgx::pbr::snippets();
	src += osgx::pbr::DIRECT_SPECULAR;
	src += osgx::pbr::IBL_SPECULAR;
	src += osgx::pbr::TONEMAP_PBR_NEUTRAL;

	src += R"GLSL(
uniform samplerCube envMap; // unit 5 -- GGX-prefiltered cubemap (osgx::ibl::loadPrefilterCubemap)
uniform sampler2D brdfLUT; // unit 6 -- split-sum LUT (osgx::ibl::makeBRDFLUTCamera)
uniform mat4 osg_ViewMatrixInverse;
uniform vec3 badgeNormalWorld;
uniform float envMaxMip;
uniform float iblIntensity;

// Direct-light rig, animated per-frame by osgx::pbr::OrbitLightRig (osgx.hpp).
// lightPosIntensity: xyz = world-space position, w = intensity (already scaled by
// --light-intensity). Unused slots have w = 0.
const int MAX_LIGHTS = 4;
uniform int lightCount;
uniform vec4 lightPosIntensity[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	// Only the badge's own layer (effectId=1, set in main()) gets the chrome treatment --
	// any other layer on this shape (a plain border, etc.) renders flat, same as the noop
	// hook, instead of being unintentionally treated as a zero-roughness mirror too.
	if(data.effectId != 1) return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);

	float metallic = floor(data.effectParam / 256.0) / 255.0;
	float baseRoughness = mod(data.effectParam, 256.0) / 255.0;

	vec3 Nz = normalize(badgeNormalWorld);
	vec3 camRight = normalize(osg_ViewMatrixInverse[0].xyz);
	vec3 camUp = normalize(osg_ViewMatrixInverse[1].xyz);
	vec3 N;

	// Dome normal from the MSDF distance field: flat (Nz) only at the very deepest interior
	// point, curving continuously all the way out to the edge as msdfSd approaches 0.5
	// (msdfSd < 0.0 = no MSDF tile -- stays flat). BEVEL_WIDTH = 0.5 spans msdfSd's entire
	// edge(0.5)->interior(1.0) range, so this reads as a curved dome across the WHOLE shape,
	// not just a rim -- MSDF_RANGE (see main()) is set to roughly the badge's own radius so
	// msdfSd actually reaches 1.0 only near dead center instead of saturating a few pixels in.
	//
	// The tilt direction maps the em-space gradient through camRight/camUp -- a deliberate
	// simplification that assumes a mostly camera-facing, un-tilted badge (em x/y == world
	// x/y == camRight/camUp here).
	const float BEVEL_WIDTH = 0.5; // msdfSd units: 0.5 = edge .. 1.0 = deep interior
	// Controls how far N tilts at the rim -- directly controls how close NdotV gets to 0
	// there, which drives how bright/edgy the rim's reflections get.
	const float BEVEL_STRENGTH = 0.7;

	if(BROKEN_GRADIENT) {
		// BUG.md's confirmed root cause, kept only for A/B (--broken): per-2x2-quad constant
		// derivatives quantize the direction -- and therefore N and R -- into pixel-scale
		// blocks. Duplicates osgSlug_MSDFBevelNormal (Atlas.shaders.cpp) with the broken
		// gradient source, since that helper always uses the correct em-space one.
		N = Nz;

		if(data.msdfSd >= 0.0) {
			float bevel = 1.0 - clamp((data.msdfSd - 0.5) / BEVEL_WIDTH, 0.0, 1.0);
			// Points toward DECREASING sd already (screen space).
			vec2 grad = -vec2(dFdx(data.msdfSd), dFdy(data.msdfSd));
			float gradLen = length(grad);

			if(gradLen > 0.0001 && bevel > 0.0001) {
				vec2 edgeDir = grad / gradLen;

				N = normalize(Nz + (camRight * edgeDir.x + camUp * edgeDir.y) * bevel * BEVEL_STRENGTH);
			}
		}
	}
	else {
		// THE FIX (see BUG.md): osgSlug_MSDFBevelNormal (Atlas.shaders.cpp) uses the em-space
		// MSDF gradient (osgSlug_MSDFGradient), never dFdx/dFdy(msdfSd) -- per-pixel smooth
		// instead of quantized into 2x2 hardware-derivative blocks.
		N = osgSlug_MSDFBevelNormal(
			data.emCoord, data.msdfSd, Nz, camRight, camUp, BEVEL_WIDTH, BEVEL_STRENGTH
		);
	}

	// V: the camera's constant world-space back axis (osg_ViewMatrixInverse[2], same
	// convention as camRight/camUp above). Correct and pan-invariant for this
	// near-orthographic 2D-pan setup -- a per-object eye-minus-center approximation drifted
	// visibly under Ortho2DManipulator's pan (see BUG.md point 4).
	vec3 V = normalize(osg_ViewMatrixInverse[2].xyz);
	float NdotV = max(dot(N, V), 0.0);

	vec3 F0 = mix(vec3(0.04), data.layerColor.rgb, metallic);

	// ---- IBL specular: split-sum (Karis 2013), via osgx::pbr::IBL_SPECULAR ---- //

	vec3 spec = osgx_IBLSpecular(N, V, F0, baseRoughness, envMap, brdfLUT, envMaxMip);

	// ---- Direct specular: the spot-light rig, full GGX per light, via osgx::pbr::DIRECT_SPECULAR ---- //

	// Floor the direct-light roughness: at a true mirror value (0.08) the GGX lobe is
	// sub-pixel -- a singular sparkle that aliases exactly like the bug we just fixed.
	// Treating each spot as a small area light (finite highlight size) is both nicer-looking
	// and stable. The IBL path keeps the raw baseRoughness; its prefiltered mips already
	// integrate the lobe.
	const float SPOT_ROUGHNESS_FLOOR = 0.15;
	float lightRoughness = max(baseRoughness, SPOT_ROUGHNESS_FLOOR);

	// Per-pixel world position: the badge lies in the z=0 plane with em == world (identity
	// MatrixTransform, un-tilted) -- same simplification as the camRight/camUp mapping above.
	vec3 P = vec3(data.emCoord, 0.0);

	vec3 direct = vec3(0.0);

	for(int i = 0; i < lightCount; i++) {
		vec3 toL = lightPosIntensity[i].xyz - P;
		float dist2 = dot(toL, toL);
		vec3 L = toL * inversesqrt(dist2);

		// No diffuse term -- metallic=1 has kD = 0 by definition, and this example is chrome.
		direct += osgx_DirectSpecular(N, V, L, NdotV, lightRoughness, F0)
			* lightColor[i] * (lightPosIntensity[i].w / dist2);
	}

	// No SH diffuse yet (osgx::ibl task 3, still pending) -- a small flat floor keeps the
	// badge from reading as pure black where the environment contributes nothing.
	vec3 color = spec * iblIntensity + direct + data.layerColor.rgb * 0.02;

	// A near-mirror surface reflecting a bright HDR environment routinely exceeds 1.0 in RGB;
	// writing that straight to an LDR framebuffer hard-clips to solid white with no gradient,
	// which reads as a jagged/blown-out edge even though data.fill's alpha coverage is smooth.
	// Same tonemap as 09-ibl.py's tonemapPBRNeutral() (Khronos PBR Neutral -- hue-preserving,
	// no ACES orange shift), plus the same manual gamma since we're not on an sRGB framebuffer.
	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	return vec4(color, data.fill * data.layerColor.a);
}
)GLSL";

	return src;
}

// ================================================================================================
// main
// ================================================================================================

// Per-frame light rig: MAX_LIGHTS point lights orbiting in front of the badge -- now
// osgx::pbr::OrbitLightRig (osgx.hpp), generalized out of this example so
// osgslug-pbr-ibl-text.cpp can reuse it. See ai/context-todo-lighting.md.

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Chrome badge: IBL environment + animated GGX spot lights", {
		{"--ktx2 <path>", "Pre-baked GGX-prefiltered cubemap (.ktx2)"},
		{"--roughness <float>", "Badge roughness, 0..1 (default: 0.08)"},
		{"--metallic <float>", "Badge metallic, 0..1 (default: 1.0)"},
		{"--light-intensity <float>", "Global scale for the spot-light rig (default: 0.2; 0 = IBL only)"},
		{"--broken", "Use the old dFdx/dFdy(msdfSd) bevel direction (BUG.md artifact) for A/B"},
	})) return 0;

	std::string ktx2Path;
	float roughness = 0.08f;
	float metallic = 1.0f;
	float lightIntensity = 0.2f;

	if(!args.read("--ktx2", ktx2Path)) return example::fail(
		args, 1, "--ktx2 <path> is required"
	);

	args.read("--roughness", roughness);
	args.read("--metallic", metallic);
	args.read("--light-intensity", lightIntensity);

	bool broken = args.read("--broken");

	auto cubemap = osgx::ibl::loadPrefilterCubemap(ktx2Path);

	if(!cubemap) return example::fail(args, 1, "failed to load --ktx2 " + ktx2Path);

	float maxMip = 0.0f;

	if(auto* img = cubemap->getImage(0)) {
		maxMip = float(std::max(0, int(img->getNumMipmapLevels()) - 1));

		OSG_NOTICE
			<< "osgslug-pbr-ibl: face0 image " << img->s() << "x" << img->t()
			<< ", numMipmapLevels=" << img->getNumMipmapLevels()
			<< " -> envMaxMip=" << maxMip
			<< std::endl
		;
	}

	else OSG_WARN << "osgslug-pbr-ibl: cubemap->getImage(0) returned null" << std::endl;

	auto lut = osgx::make_ref<osg::Texture2D>();
	auto bakeCam = osgx::ibl::makeBRDFLUTCamera(512, lut);

	// ---- Badge shape: a single filled circle, chrome material via effectData.w ---- //

	auto atlas = osgx::make_ref<osgSlug::Atlas>();
	slughorn::canvas::Canvas canvas(*atlas);

	// setMSDF() (before the commit it should apply to) requests this badge's MSDF tile the
	// moment fill() registers its shape -- requestMSDF() is safe pre-build, so build() alone
	// renders it; no separate post-build registerMSDF()-style call needed below.
	canvas.setMSDF(true, MSDF_RANGE);
	canvas.circle(0.5_cv, 0.5_cv, 0.48_cv);
	canvas.fill({1.0_cv, 0.86_cv, 0.57_cv, 1.0_cv}); // warm gold tint -- F0 for metallic=1

	auto badge = canvas.finalize();

	badge.layers[0].effectParam = packMaterial(roughness, metallic);
	// Marks this layer for the chrome treatment in makeChromeFrag()'s osgSlug_Fragment --
	// any OTHER layer added to this shape (e.g. a plain white border) falls through to flat
	// rendering instead of unintentionally being treated as a zero-roughness mirror too.
	badge.layers[0].effectId = 1;

	// Default MSDF tile size (128) assumes a narrow rim-width range. MSDF_RANGE now spans
	// nearly the whole badge (for the full-dome look), so the same 128 texels have to cover a
	// much wider em-space span -- roughly 2-3 screen pixels per texel at a typical view, which
	// is why the edge showed visible texel-grid artifacts even with no zoom applied. Must be
	// set before build().
	atlas->setMSDFTileSize(128);
	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addCompositeShape(badge);

	auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::FragmentHook, makeChromeFrag(broken)}});

	// GL_TEXTURE_CUBE_MAP_SEAMLESS -- avoids visible seams at cube edges, especially at the
	// blurrier (high-roughness) mip levels.
	ss->setMode(0x884F, osg::StateAttribute::ON);

	ss->addUniform(new osg::Uniform("envMap", 5));
	ss->setTextureAttributeAndModes(5, cubemap, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("brdfLUT", 6));
	ss->setTextureAttributeAndModes(6, lut, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("envMaxMip", maxMip));
	ss->addUniform(new osg::Uniform("iblIntensity", 1.0f));
	ss->addUniform(new osg::Uniform("badgeNormalWorld", osg::Vec3(0.0f, 0.0f, 1.0f)));

	// ---- Direct-light rig ---- //

	auto* lightPos = new osg::Uniform(osg::Uniform::FLOAT_VEC4, "lightPosIntensity", MAX_LIGHTS);
	auto* lightCol = new osg::Uniform(osg::Uniform::FLOAT_VEC3, "lightColor", MAX_LIGHTS);

	// Theatrical gels: warm key, cool fill, magenta accent. Unused slot stays black/zero.
	lightCol->setElement(0, osg::Vec3(1.00f, 0.95f, 0.80f));
	lightCol->setElement(1, osg::Vec3(0.55f, 0.70f, 1.00f));
	lightCol->setElement(2, osg::Vec3(1.00f, 0.45f, 0.70f));

	for(int i = 0; i < MAX_LIGHTS; i++) lightPos->setElement(
		static_cast<unsigned int>(i), osg::Vec4(0.0f, 0.0f, 1.0f, 0.0f)
	);

	ss->addUniform(new osg::Uniform("lightCount", 3));
	ss->addUniform(lightPos);
	ss->addUniform(lightCol);

	sd->setStateSet(ss);
	atlas->addChild(sd);

	auto badgeXform = osgx::make_ref<osg::MatrixTransform>();

	badgeXform->addChild(atlas);

	auto rig = osgx::make_ref<osgx::pbr::OrbitLightRig>();

	rig->ss = ss;
	rig->center = osg::Vec3(0.5f, 0.5f, 0.0f); // canvas.circle(0.5, 0.5, ...) -- badge center
	rig->intensity = lightIntensity;
	badgeXform->setUpdateCallback(rig);

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(bakeCam);
	root->addChild(badgeXform);

	return example::run(viewer, args, root);
}
