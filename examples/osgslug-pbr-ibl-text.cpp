//vimrun! ./osgslug-pbr-ibl-text --trackball --ktx2 /home/cubicool/dev/OpenSceneGraph.py/examples/pyosg-lighting/data/papermill.ktx2

// Same chrome/IBL technique as osgslug-pbr-ibl.cpp (the circle badge), applied to real text
// glyphs instead of a single primitive -- see ai/context-todo-lighting.md's "Ultimate Goal".
// Shares the promoted osgx::pbr GLSL (DIRECT_SPECULAR/IBL_SPECULAR/TONEMAP_PBR_NEUTRAL) and
// osgx::pbr::OrbitLightRig with the badge example; only the shape-specific bits (glyph loading,
// per-layer material/MSDF registration, light-rig centering on the text's bounding box) differ.
//
// The bevel/normal reconstruction (osgSlug_MSDFBevelNormal, Atlas.shaders.cpp) is untested so
// far on thin glyph strokes and sharp corners -- the badge example only ever exercised it on a
// single filled circle. Expect this to be the first place new bevel/MSDF-range tuning surfaces.

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

#include "osgSlug/Font.hpp"

#include <osg/TextureCubeMap>

// ================================================================================================
// GLSL - chrome FragmentHook (identical material model to osgslug-pbr-ibl.cpp)
// ================================================================================================

// effectData.w packing: low byte = roughness*255, high byte = metallic*255.
static float packMaterial(float roughness, float metallic) {
	return float(int(roughness * 255.0f)) + float(int(metallic * 255.0f)) * 256.0f;
}

// MSDF range for the bevel: em-space half-bandwidth the tile encodes around the edge. Glyphs
// are much smaller/thinner than the badge circle, so this starts conservative (a real rim, not
// a full-dome sweep) -- widen it per-glyph if the flat-looking interior needs more curve.
static constexpr float MSDF_RANGE = 0.12f;

static constexpr int MAX_LIGHTS = 4;

static std::string makeChromeFrag() {
	std::string src = R"GLSL(
#version 330 core
#pragma osgSlug lib_fragment

const float PI = 3.14159265359;
#pragma osgx::pbr *

)GLSL";

	src += R"GLSL(
uniform samplerCube envMap; // unit 5 -- GGX-prefiltered cubemap (osgx::ibl::loadPrefilterCubemap)
uniform sampler2D brdfLUT; // unit 6 -- split-sum LUT (osgx::ibl::makeBRDFLUTCamera)
uniform mat4 osg_ViewMatrixInverse;
uniform vec3 textNormalWorld;
uniform float envMaxMip;
uniform float iblIntensity;

// Direct-light rig, animated per-frame by osgx::pbr::OrbitLightRig (osgx.hpp).
const int MAX_LIGHTS = 3;
uniform int lightCount;
uniform vec4 lightPosIntensity[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	// Only glyph layers (effectId=1, set in main()) get the chrome treatment.
	if(data.effectId != 1) return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);

	float metallic = floor(data.effectParam / 256.0) / 255.0;
	float baseRoughness = mod(data.effectParam, 256.0) / 255.0;

	vec3 Nz = normalize(textNormalWorld);
	vec3 camRight = normalize(osg_ViewMatrixInverse[0].xyz);
	vec3 camUp = normalize(osg_ViewMatrixInverse[1].xyz);

	const float BEVEL_WIDTH = 0.5; // msdfSd units: 0.5 = edge .. 1.0 = deep interior
	const float BEVEL_STRENGTH = 0.7;

	// osgSlug_MSDFBevelNormal (Atlas.shaders.cpp) -- same helper the badge example uses, now
	// shared instead of duplicated. Uses the em-space MSDF gradient (osgSlug_MSDFGradient),
	// never dFdx/dFdy(msdfSd) -- see BUG.md.
	vec3 N = osgSlug_MSDFBevelNormal(
		data.emCoord, data.msdfSd, Nz, camRight, camUp, BEVEL_WIDTH, BEVEL_STRENGTH
	);

	vec3 V = normalize(osg_ViewMatrixInverse[2].xyz);
	float NdotV = max(dot(N, V), 0.0);

	vec3 F0 = mix(vec3(0.04), data.layerColor.rgb, metallic);

	vec3 spec = osgx_IBLSpecular(N, V, F0, baseRoughness, envMap, brdfLUT, envMaxMip);

	const float SPOT_ROUGHNESS_FLOOR = 0.25;
	float lightRoughness = max(baseRoughness, SPOT_ROUGHNESS_FLOOR);

	// Per-pixel world position: text lies in the z=0 plane with em == world (identity
	// MatrixTransform, un-tilted) -- same simplification as the badge example.
	vec3 P = vec3(data.emCoord, 0.0);

	vec3 direct = vec3(0.0);

	for(int i = 0; i < lightCount; i++) {
		vec3 toL = lightPosIntensity[i].xyz - P;
		float dist2 = dot(toL, toL);
		vec3 L = toL * inversesqrt(dist2);

		direct += osgx_DirectSpecular(N, V, L, NdotV, lightRoughness, F0)
			* lightColor[i] * (lightPosIntensity[i].w / dist2);
	}

	vec3 color = spec * iblIntensity + direct + data.layerColor.rgb * 0.02;

	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	return vec4(color, data.fill * data.layerColor.a);
}
)GLSL";

	osgx::pbr::registerShaderLibs();
	osgx::ibl::registerShaderLibs();

	return osgx::resolveShaderLibs(src);
}

// ================================================================================================
// main
// ================================================================================================

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Chrome text: IBL environment + animated GGX spot lights", {
		{"--ktx2 <path>", "Pre-baked GGX-prefiltered cubemap (.ktx2)"},
		{"--font <path>", "TTF/OTF font to load (default: font/UbuntuMono-R.ttf)"},
		{"--text <string>", "Text to render (default: \"CHROME\")"},
		{"--font-size <float>", "Glyph size in canvas units (default: 0.2)"},
		{"--roughness <float>", "Text roughness, 0..1 (default: 0.08)"},
		{"--metallic <float>", "Text metallic, 0..1 (default: 1.0)"},
		{"--light-intensity <float>", "Global scale for the spot-light rig (default: 0.2; 0 = IBL only)"},
		{"--light-orbit-radius-scale <float>", "Scale for the spot-light orbit radii (default: 0.6)"},
		{"--light-orbit-height-scale <float>", "Scale for how far above the text plane the lights hover; smaller brings them close to the surface for a wide, grazing sweep -- this is the main knob for making the orbit read as motion (default: 0.15; 1.0 = original badge-sized height)"},
		{"--light-orbit-speed-scale <float>", "Scale for the spot-light orbit angular speed (default: 1.5)"},
	})) return 0;

	std::string ktx2Path;
	std::string fontPath = "font/UbuntuMono-R.ttf";
	std::string text = "CHROME";
	float fontSize = 0.2f;
	float roughness = 0.08f;
	float metallic = 1.0f;
	float lightIntensity = 0.2f;
	float lightOrbitRadiusScale = 0.6f;
	float lightOrbitHeightScale = 0.15f;
	float lightOrbitSpeedScale = 1.5f;

	if(!args.read("--ktx2", ktx2Path)) return example::fail(
		args, 1, "--ktx2 <path> is required"
	);

	args.read("--font", fontPath);
	args.read("--text", text);
	args.read("--font-size", fontSize);
	args.read("--roughness", roughness);
	args.read("--metallic", metallic);
	args.read("--light-intensity", lightIntensity);
	args.read("--light-orbit-radius-scale", lightOrbitRadiusScale);
	args.read("--light-orbit-height-scale", lightOrbitHeightScale);
	args.read("--light-orbit-speed-scale", lightOrbitSpeedScale);

	auto cubemap = osgx::ibl::loadPrefilterCubemap(ktx2Path);

	if(!cubemap) return example::fail(args, 1, "failed to load --ktx2 " + ktx2Path);

	float maxMip = 0.0f;

	if(auto* img = cubemap->getImage(0)) {
		maxMip = float(std::max(0, int(img->getNumMipmapLevels()) - 1));
	}

	else OSG_WARN << "osgslug-pbr-ibl-text: cubemap->getImage(0) returned null" << std::endl;

	auto lut = osgx::make_ref<osg::Texture2D>();
	auto bakeCam = osgx::ibl::makeBRDFLUTCamera(512, lut);

	// ---- Text shape: chrome material via effectData.w on every glyph layer ---- //

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	auto font = osgx::make_ref<osgSlug::Font>(fontPath, atlas);

	if(!font->load()) return example::fail(args, 1, "failed to load --font " + fontPath);

	slughorn::canvas::Canvas canvas(*atlas);

	slughorn::canvas::Path baseline;

	// 1.0 em per glyph is a generous upper bound on advance (UbuntuMono's real advance is
	// tighter) -- textOnPath silently drops glyphs that would extend past the path end, so
	// erring long is the safe direction.
	baseline.moveTo(0.0_cv, 0.5_cv);
	baseline.lineTo(float(text.size()) * fontSize * 1.0_cv, 0.5_cv);

	// setMSDF() requests each glyph's MSDF tile as textOnPath() commits it -- requestMSDF() is
	// idempotent, so repeated glyphs (shared shape key) cost nothing extra; no need to
	// deduplicate keys or batch-register after the fact like the old registerMSDF() did.
	canvas.setMSDF(true, MSDF_RANGE);

	canvas.textOnPath(
		baseline,
		text,
		fontSize,
		0_cv,
		{0.9_cv, 0.9_cv, 0.92_cv, 1.0_cv}, // neutral silver -- F0 for metallic=1, true chrome
		font->metrics()
	);

	auto textShape = canvas.finalize();

	for(auto& layer : textShape.layers) {
		layer.effectParam = packMaterial(roughness, metallic);
		layer.effectId = 1;
	}

	atlas->setMSDFTileSize(64);
	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();

	sd->addCompositeShape(textShape);

	auto* ss = atlas->createHookStateSet({{osgSlug::Atlas::FragmentHook, makeChromeFrag()}});

	// GL_TEXTURE_CUBE_MAP_SEAMLESS -- avoids visible seams at cube edges, especially at the
	// blurrier (high-roughness) mip levels.
	ss->setMode(0x884F, osg::StateAttribute::ON);

	ss->addUniform(new osg::Uniform("envMap", 5));
	ss->setTextureAttributeAndModes(5, cubemap, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("brdfLUT", 6));
	ss->setTextureAttributeAndModes(6, lut, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("envMaxMip", maxMip));
	ss->addUniform(new osg::Uniform("iblIntensity", 1.0f));
	ss->addUniform(new osg::Uniform("textNormalWorld", osg::Vec3(0.0f, 0.0f, 1.0f)));

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

	auto textXform = osgx::make_ref<osg::MatrixTransform>();

	textXform->addChild(atlas);

	auto rig = osgx::make_ref<osgx::pbr::OrbitLightRig>();

	rig->ss = ss;

	// Center the light rig on the text's own bounding box rather than the badge's fixed
	// (0.5, 0.5) -- string length/font size vary per --text/--font-size.
	if(auto bbox = textShape.boundingBox(*atlas)) {
		rig->center = osg::Vec3(
			float((bbox->x0 + bbox->x1) * 0.5_cv),
			float((bbox->y0 + bbox->y1) * 0.5_cv),
			0.0f
		);
	}

	// Default orbits (osgx.hpp) are sized/paced for the badge example. What actually makes an
	// orbiting light read as visible motion is the *ratio* of radius to height -- a light held
	// far above the surface barely changes direction as it circles (a narrow cone, near-static
	// highlight); pulling it down close to the plane widens that cone toward grazing angles, so
	// the same orbit sweeps the specular highlight much further and brighter (1/dist^2) as it
	// passes close. Speed is bumped too so the sweep repeats often enough to read in a short clip.
	for(auto& orbit : rig->orbits) {
		orbit.radius *= lightOrbitRadiusScale;
		orbit.height *= lightOrbitHeightScale;
		orbit.speed *= lightOrbitSpeedScale;
	}

	rig->intensity = lightIntensity;
	textXform->setUpdateCallback(rig);

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(bakeCam);
	root->addChild(textXform);

	return example::run(viewer, args, root);
}
