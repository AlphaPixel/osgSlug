//vimrun! ./osgslug-pbr-ibl --trackball --ktx2 /home/cubicool/dev/OpenSceneGraph.py/examples/pyosg-lighting/data/papermill.ktx2

// Proof-of-concept for slughorn/ai/context-todo-lighting.md Idea 1: an osgSlug shape shaded as
// polished chrome. Two light sources, both physically-based GGX:
//
// - IBL: a real prefiltered environment cubemap via the split-sum technique (Karis 2013),
//   using osgx::pbr (BRDF math) and osgx::ibl (cubemap load + BRDF LUT bake) from
//   ~/dev/osgdebug/osgx.hpp.
// - Direct: a small rig of animated point lights ("spot lights" thrown into the scene, see
//   osgx::OrbitLightRig) whose highlights slide across the dome per-frame -- the motion is the
//   confirmation that N, V, and the specular math are wired correctly, not just a static
//   flat-shaded color.
//
// The dome normal comes from the shape's MSDF distance field via osgSlug_MSDFBevelNormal(), which
// uses an em-space central difference of the float MSDF tile. That keeps N and its reflection
// vector smooth at the silhouette instead of quantized by screen-space hardware derivatives.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

#include <osgx/Gizmos.hpp>

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

static std::string makeChromeFrag() {
	std::string src = R"GLSL(
#version 430 core
#pragma osgSlug lib_fragment

// 430, not 330: `#pragma osgx::pbr *` pulls in LIGHT_UNIFORMS, which declares the osgx_lights
// SSBO (`buffer osgx_LightBuffer`) -- SSBOs require GLSL 430+, matching osgSlug's own
// SHADER_VERT/SHADER_FRAG (Atlas.shaders.cpp).
const float PI = 3.14159265359;
#pragma osgx::pbr *
uniform samplerCube envMap; // unit 5 -- GGX-prefiltered cubemap (osgx::loadPrefilterCubemap)
uniform sampler2D brdfLUT; // unit 6 -- split-sum LUT (osgx::makeBRDFLUTCamera)
uniform mat4 osg_ViewMatrixInverse;
uniform vec3 badgeNormalWorld;
uniform float envMaxMip;
uniform float iblIntensity;

// Direct-light rig, animated per-frame by osgx::OrbitLightRig (osgx.hpp). osgx_lightCount/
// osgx_lights come from LIGHT_UNIFORMS (already spliced in via `#pragma osgx::pbr *` above) --
// the same SSBO-backed osgx::LightSet that OrbitLightRig writes position/intensity into
// every frame, so this loop stays in sync with it instead of hand-copying a shadow uniform API.

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

	vec3 N = osgSlug_MSDFBevelNormal(
		data.emCoord, data.msdfSd, Nz, camRight, camUp, BEVEL_WIDTH, BEVEL_STRENGTH
	);

	// V: the camera's constant world-space back axis (osg_ViewMatrixInverse[2], same
	// convention as camRight/camUp above). Correct and pan-invariant for this
	// near-orthographic 2D-pan setup -- a per-object eye-minus-center approximation drifted
	// visibly under Ortho2DManipulator's pan (see BUG.md point 4).
	vec3 V = normalize(osg_ViewMatrixInverse[2].xyz);
	float NdotV = max(dot(N, V), 0.0);

	vec3 F0 = mix(vec3(0.04), data.layerColor.rgb, metallic);

	// ---- IBL specular: split-sum (Karis 2013), via osgx::IBL_SPECULAR ---- //

	vec3 spec = osgx_IBLSpecular(N, V, F0, baseRoughness, envMap, brdfLUT, envMaxMip);

	// ---- Direct specular: the spot-light rig, full GGX per light, via osgx::DIRECT_SPECULAR ---- //

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

	for(int i = 0; i < osgx_lightCount; i++) {
		vec3 L;
		vec3 radiance = osgx_PointLightRadiance(osgx_lights[i].posIntensity, osgx_lights[i].color, P, L);

		// No diffuse term -- metallic=1 has kD = 0 by definition, and this example is chrome.
		direct += osgx_DirectSpecular(N, V, L, NdotV, lightRoughness, F0) * radiance;
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

	osgx::registerPBRShaderLibs();
	osgx::registerIBLShaderLibs();

	return osgx::resolveShaderLibs(src);
}

// ================================================================================================
// main
// ================================================================================================

// Per-frame light rig: a handful of point lights orbiting in front of the badge -- now
// osgx::OrbitLightRig (osgx.hpp), generalized out of this example so
// osgslug-pbr-ibl-text.cpp can reuse it. See ai/context-todo-lighting.md.

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Chrome badge: IBL environment + animated GGX spot lights", {
		{"--ktx2 <path>", "Pre-baked GGX-prefiltered cubemap (.ktx2)"},
		{"--roughness <float>", "Badge roughness, 0..1 (default: 0.08)"},
		{"--metallic <float>", "Badge metallic, 0..1 (default: 1.0)"},
		{"--light-intensity <float>", "Global scale for the spot-light rig (default: 0.2; 0 = IBL only)"},
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

	auto cubemap = osgx::loadPrefilterCubemap(ktx2Path);

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
	auto bakeCam = osgx::makeBRDFLUTCamera(512, lut);

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

	sd->setHooks({{osgSlug::Atlas::FragmentHook, makeChromeFrag()}});

	auto* ss = sd->getOrCreateStateSet();

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

	auto lights = osgx::make_ref<osgx::LightSet>();

	ss->setAttributeAndModes(lights);

	// Theatrical gels: warm key, cool fill, magenta accent. Position/intensity get overwritten
	// every frame by OrbitLightRig below; only color is fixed here.
	lights->setPoint(0, osg::Vec3(0.0f, 0.0f, 1.0f), osg::Vec3(1.00f, 0.95f, 0.80f), 0.0f);
	lights->setPoint(1, osg::Vec3(0.0f, 0.0f, 1.0f), osg::Vec3(0.55f, 0.70f, 1.00f), 0.0f);
	lights->setPoint(2, osg::Vec3(0.0f, 0.0f, 1.0f), osg::Vec3(1.00f, 0.45f, 0.70f), 0.0f);
	lights->setCount(3);

	atlas->addChild(sd);

	auto badgeXform = osgx::make_ref<osg::MatrixTransform>();

	badgeXform->addChild(atlas);

	auto rig = osgx::make_ref<osgx::OrbitLightRig>();

	rig->lights = lights;
	rig->center = osg::Vec3(0.5f, 0.5f, 0.0f); // canvas.circle(0.5, 0.5, ...) -- badge center
	rig->intensity = lightIntensity;
	badgeXform->setUpdateCallback(rig);

	// Depth-tested wireframe markers at each orbiting light's live position -- rebuilt every
	// frame straight from `lights`, so they track OrbitLightRig's animation for free. No
	// directional lights here (all three are setPoint()), so gizmos->getOverlay() draws nothing, but
	// costs nothing to add either. Sibling of `sd` under `atlas`, not a child of it, so the
	// markers don't inherit the chrome StateSet.
	auto gizmos = osgx::make_ref<osgx::LightGizmos>(*lights, atlas);

	atlas->addChild(gizmos->getMarkers());

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(bakeCam);
	root->addChild(badgeXform);
	root->addChild(gizmos->getOverlay());

	return example::run(viewer, args, root);
}
