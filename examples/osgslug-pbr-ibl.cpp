//vimrun! ./osgslug-pbr-ibl --trackball --ktx2 /home/cubicool/dev/OpenSceneGraph.py/examples/pyosg-lighting/data/papermill.ktx2

// Proof-of-concept for slughorn/ai/context-todo-lighting.md Idea 1: an osgSlug shape shaded as
// polished chrome. Two light sources, both physically-based GGX:
//
// - IBL: a real prefiltered environment cubemap via the split-sum technique (Karis 2013),
//   using osgx::pbr (BRDF math) and osgx::ibl (cubemap load + BRDF LUT bake) from
//   ~/dev/osgdebug/osgx.hpp.
// - Direct: a small rig of animated point lights ("spot lights" thrown into the scene, see
//   LightRigCallback) whose highlights slide across the dome per-frame -- the motion is the
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
// declared in makeChromeFrag() and filled by LightRigCallback below.
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

	// The full osgx::pbr BRDF toolkit: D_GGX/G_Schlick/G_Smith for the direct-light loop,
	// F_Schlick for per-light Fresnel. (F_Schlick_roughness comes along too but is unused --
	// the split-sum IBL combine below folds Fresnel into the baked brdfLUT instead.)
	src += osgx::pbr::snippets();

	// Khronos PBR Neutral tonemapping, ported verbatim from 09-ibl.py's tonemapPBRNeutral().
	// Not yet promoted to osgx:: -- this is a display/output step, not BRDF or light-source
	// math, so it doesn't obviously belong in either osgx::pbr or osgx::ibl as they're
	// currently scoped. Promote it if a second consumer shows up.
	src += R"GLSL(
vec3 osgx_TonemapPBRNeutral(vec3 color) {
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if(peak >= startCompression) {
		float d = 1.0 - startCompression;
		float newPeak = 1.0 - d * d / (peak + d - startCompression);
		color *= newPeak / peak;
		float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
		color = mix(color, vec3(newPeak), g);
	}
	return clamp(color, 0.0, 1.0);
}
)GLSL";

	src += R"GLSL(
uniform samplerCube envMap; // unit 5 -- GGX-prefiltered cubemap (osgx::ibl::loadPrefilterCubemap)
uniform sampler2D brdfLUT; // unit 6 -- split-sum LUT (osgx::ibl::makeBRDFLUTCamera)
uniform mat4 osg_ViewMatrixInverse;
uniform vec3 badgeNormalWorld;
uniform float envMaxMip;
uniform float iblIntensity;

// Direct-light rig, animated per-frame by LightRigCallback (see C++ below).
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
	vec3 N = Nz;

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
	if(data.msdfSd >= 0.0) {
		const float BEVEL_WIDTH = 0.5; // msdfSd units: 0.5 = edge .. 1.0 = deep interior
		// Controls how far N tilts at the rim -- directly controls how close NdotV gets to 0
		// there, which drives how bright/edgy the rim's reflections get.
		const float BEVEL_STRENGTH = 0.7;

		float bevel = 1.0 - clamp((data.msdfSd - 0.5) / BEVEL_WIDTH, 0.0, 1.0);

		vec2 grad;

		if(BROKEN_GRADIENT) {
			// BUG.md's confirmed root cause, kept only for A/B (--broken): per-2x2-quad
			// constant derivatives quantize the direction -- and therefore N and R -- into
			// pixel-scale blocks. Points toward DECREASING sd already (screen space).
			grad = -vec2(dFdx(data.msdfSd), dFdy(data.msdfSd));
		}
		else {
			// THE FIX: em-space central difference of the float MSDF tile itself
			// (osgSlug_MSDFGradient, Atlas.shaders.cpp) -- per-pixel smooth. The gradient
			// points toward the interior; the bevel wants interior->edge, hence the negation.
			grad = -osgSlug_MSDFGradient(data.emCoord);
		}

		float gradLen = length(grad);

		if(gradLen > 0.0001 && bevel > 0.0001) {
			vec2 edgeDir = grad / gradLen; // points from interior toward the edge
			vec3 camRight = normalize(osg_ViewMatrixInverse[0].xyz);
			vec3 camUp = normalize(osg_ViewMatrixInverse[1].xyz);

			N = normalize(Nz + (camRight * edgeDir.x + camUp * edgeDir.y) * bevel * BEVEL_STRENGTH);
		}
	}

	// V: the camera's constant world-space back axis (osg_ViewMatrixInverse[2], same
	// convention as camRight/camUp above). Correct and pan-invariant for this
	// near-orthographic 2D-pan setup -- a per-object eye-minus-center approximation drifted
	// visibly under Ortho2DManipulator's pan (see BUG.md point 4).
	vec3 V = normalize(osg_ViewMatrixInverse[2].xyz);
	vec3 R = reflect(-V, N);
	float NdotV = max(dot(N, V), 0.0);

	vec3 F0 = mix(vec3(0.04), data.layerColor.rgb, metallic);

	// ---- IBL specular: split-sum (Karis 2013) -- prefilt * (F0 * scale + bias) ---- //

	// OSG world space is Z-up; the baked cubemap's faces are Y-up. Without this remap we
	// sample a direction that doesn't correspond to R at all -- see 09-ibl.py's identical
	// r_gl = vec3(R.x, R.z, -R.y) before its own textureLod(envMap, ...) call.
	vec3 R_gl = vec3(R.x, R.z, -R.y);

	vec3 prefilt = textureLod(envMap, R_gl, baseRoughness * envMaxMip).rgb;
	vec2 brdf = texture(brdfLUT, vec2(NdotV, baseRoughness)).rg;
	vec3 spec = prefilt * (F0 * brdf.x + brdf.y);

	// ---- Direct specular: the spot-light rig, full GGX per light ---- //

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
		float NdotL = dot(N, L);

		if(NdotL <= 0.0) continue;

		vec3 H = normalize(L + V);
		float NdotH = max(dot(N, H), 0.0);
		float HdotV = max(dot(H, V), 0.0);

		float D = osgx_D_GGX(NdotH, lightRoughness);
		float G = osgx_G_Smith(NdotV, NdotL, lightRoughness);
		vec3 F = osgx_F_Schlick(HdotV, F0);

		// Cook-Torrance specular; no diffuse term -- metallic=1 has kD = 0 by definition,
		// and this example is chrome. Add the kD * albedo/PI term here if a dielectric
		// material ever needs it.
		vec3 specL = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

		direct += specL * lightColor[i] * (lightPosIntensity[i].w / dist2) * NdotL;
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
// Per-frame light rig: MAX_LIGHTS point lights orbiting in front of the badge. Positions are
// written into the lightPosIntensity uniform array each update traversal; colors and count are
// static, set once in main(). Installed as the badge MatrixTransform's update callback.
// ================================================================================================

struct LightRigCallback: public osg::NodeCallback {
	osg::ref_ptr<osg::StateSet> ss;
	osg::Vec3 center{0.5f, 0.5f, 0.0f}; // canvas.circle(0.5, 0.5, ...) -- badge center
	float intensity = 1.0f; // global scale (--light-intensity)

	void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
		float t = nv->getFrameStamp() ? float(nv->getFrameStamp()->getSimulationTime()) : 0.0f;

		// (orbit radius, height above the badge plane, angular speed, phase, intensity)
		static constexpr struct {
			float radius, z, speed, phase, intensity;
		} ORBITS[3] = {
			{0.55f, 0.70f, 0.50f, 0.0f, 1.00f},
			{0.70f, 0.90f, -0.33f, 2.1f, 0.75f},
			{0.45f, 0.50f, 0.80f, 4.2f, 0.50f},
		};

		auto* lp = ss->getUniform("lightPosIntensity");

		for(int i = 0; i < 3; i++) {
			const auto& o = ORBITS[i];
			float a = t * o.speed + o.phase;

			lp->setElement(static_cast<unsigned int>(i), osg::Vec4(
				center.x() + std::cos(a) * o.radius,
				center.y() + std::sin(a) * o.radius,
				center.z() + o.z,
				o.intensity * intensity
			));
		}

		traverse(node, nv);
	}
};

// ================================================================================================
// main
// ================================================================================================

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

	canvas.circle(0.5_cv, 0.5_cv, 0.48_cv);
	canvas.fill({1.0_cv, 0.86_cv, 0.57_cv, 1.0_cv}); // warm gold tint -- F0 for metallic=1

	auto badge = canvas.finalize();
	auto badgeKey = badge.layers[0].key;

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
	atlas->registerMSDF(badgeKey, MSDF_RANGE);
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

	auto rig = osgx::make_ref<LightRigCallback>();

	rig->ss = ss;
	rig->intensity = lightIntensity;
	badgeXform->setUpdateCallback(rig);

	auto root = osgx::make_ref<osg::Group>();

	root->addChild(bakeCam);
	root->addChild(badgeXform);

	return example::run(viewer, args, root);
}
