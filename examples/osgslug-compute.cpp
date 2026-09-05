//vimrun! ./osgslug-compute --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/DispatchCompute>

OSGSLUG_ENABLE_WARNINGS

// CC0: Clearly a bug -- A "Happy Accident" Shader
// Original (golfed ShaderToy): https://twigl.app?ol=true&ss=-OUOudmBPJ57CIb7rAxS
// Attribution: @byt3_m3chanic, @FabriceNeyret2, @iq, @shane, @XorDev + many more
//
// Adapted as an osgSlug FragmentHook that activates on RAYMARCHER_EFFECT_ID.
// The circle shape uses this as its fill; the pentagon keeps the compute color animation.

static constexpr uint32_t RAYMARCHER_EFFECT_ID = 10;

static const char* FRAGMENT_HOOK = R"(
#version 430 core

#pragma osgSlug lib_fragment

uniform float osg_SimulationTime;
uniform vec2 iResolution;

vec2 osgSlug_FragEmCoord(vec2 emCoord, inout vec2 emsPerPixel, int effectId, float time) {
	return emCoord;
}

// Raymarched fractal tunnel. Uses gl_FragCoord so the effect tiles in screen-space
// and the shape boundary acts as a clipping window into the scene.
vec4 clearlyABug(vec2 C, vec2 r, float t) {
	float i = 0.0, d = 0.0, z = fract(dot(C, sin(C))) - 0.5;
	vec4 O = vec4(0.0), o = vec4(0.0), p = vec4(0.0);

	for(; ++i < 77.0; z += 0.6 * d) {
		p = vec4(z * normalize(vec3(C - 0.5 * r, r.y)), 0.1 * t);
		p.z += t;
		O = p;
		p.xy *= mat2(cos(2.0 + O.z + vec4(0, 11, 33, 0)));
		p.xy *= mat2(cos(O + vec4(0, 11, 33, 0)));
		O = (1.0 + sin(0.5 * O.z + length(p - O) + vec4(0, 4, 3, 6))) / (0.5 + 2.0 * dot(O.xy, O.xy));
		p = abs(fract(p) - 0.5);
		d = abs(min(length(p.xy) - 0.125, min(p.x, p.y) + 1e-3)) + 1e-3;
		o += O.w / d * O;
	}

	return tanh(o / 2e4);
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	if(data.effectId == 10) {
		vec4 color = clearlyABug(data.uv * iResolution, iResolution, osg_SimulationTime);
		// vec4 color = clearlyABug(gl_FragCoord.xy, iResolution, osg_SimulationTime);
		color.a = data.fill;

		return color;
	}

	return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
}
)";

// TODO: We should be able to SOMEHOW get the `osgSlug::Atlas::SHADER_TYPES` GLSL blurb into this
// inline shader source as well; perhaps we could setup format flags or some kind of helper methods?
static const char* COMPUTE_SHADER = R"(
#version 430 core
layout(local_size_x = 1) in;

// MUST stay member-identical to osgSlug::Atlas::SHADER_TYPES' osgSlug_LayerData - the compute
// program is linked separately, so a mismatch here doesn't fail the link, it just silently
// computes the WRONG per-layer stride and writes into the wrong slot. (This had in fact
// already happened once: this struct was missing transformData.)
struct LayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 transformData;
	vec4 axisX;
	vec4 axisY;
};

layout(std430, binding = 1) buffer LayerBuffer {
	LayerData layers[];
};

uniform float osg_SimulationTime;

void main() {
	float t = osg_SimulationTime;

	// Layer 0 is the circle (uses the raymarcher fill; compute writes are ignored there).
	// Layer 1 is the pentagon; animate its color here.
	layers[1].color = vec4(
		0.5 + 0.5 * sin(t),
		0.5 + 0.5 * sin(t + 2.094),
		0.5 + 0.5 * sin(t + 4.189),
		0.5
	);
}
)";

// Memory barrier before the slug draw so compute writes are visible to the vertex shader.
struct LayerBarrierCallback: public osg::Drawable::DrawCallback {
	void drawImplementation(osg::RenderInfo& ri, const osg::Drawable* d) const override {
		auto* ext = osg::GLExtensions::Get(ri.getState()->getContextID(), true);

		ext->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

		d->drawImplementation(ri);
	}
};

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);

	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "Compute shader SSBO color animation")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas);

	canvas.beginPath();
	canvas.arc(0.5_cv, 0.5_cv, 0.5_cv, 0_cv, 2_cv * 3.14159_cv);
	canvas.fill({1_cv, 1_cv, 1_cv, 1_cv});
	canvas.beginPath();
	canvas.moveTo(0_cv, 0_cv);
	canvas.lineTo(0_cv, 0.5_cv);
	canvas.lineTo(0.5_cv, 1_cv);
	canvas.lineTo(1_cv, 0.5_cv);
	canvas.lineTo(1_cv, 0_cv);
	canvas.closePath();
	canvas.fill({1_cv, 1_cv, 1_cv, 0.5_cv});

	auto compositeShape = canvas.finalize();

	// Circle (layer 0) uses the raymarcher as its fill.
	compositeShape.layers[0].effectId = RAYMARCHER_EFFECT_ID;

	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::ShapeDrawable>();

	sd->addCompositeShape(compositeShape);

	sd->setHooks({{osgSlug::Atlas::FragmentHook, FRAGMENT_HOOK}});
	sd->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

	// atlas->addChild triggers compile immediately (atlas is Packed); getLayerBuffer is valid after.
	atlas->addChild(sd);

	// Window dimensions for the raymarcher's iResolution (matches example::run default).
	sd->getOrCreateStateSet()->addUniform(new osg::Uniform("iResolution", osg::Vec2f(800.0f, 600.0f)));

	// Grab the layer SSBO handle; the compute shader writes into this buffer!
	// Layer 0 anchors the binding; totalSize covers the full contiguous buffer.
	auto* layerBuf = sd->getLayerBuffer(0);
	const auto layerTotalSize = static_cast<GLsizeiptr>(
		sd->getLayers().size() * 4 * sizeof(osg::Vec4)
	);
	auto computeProgram = osgx::make_ref<osg::Program>();

	computeProgram->addShader(new osg::Shader(osg::Shader::COMPUTE, COMPUTE_SHADER));

	// One workgroup of one thread -- animates the pentagon (layer 1).
	auto dispatch = osgx::make_ref<osg::DispatchCompute>(1, 1, 1);
	auto* dss = dispatch->getOrCreateStateSet();

	dss->setAttributeAndModes(computeProgram, osg::StateAttribute::ON);
	dss->setAttributeAndModes(
		new osg::ShaderStorageBufferBinding(1, layerBuf, 0, layerTotalSize),
		osg::StateAttribute::ON
	);

	// Render bin 0 < 1 ensures dispatch precedes the geometry draw.
	dss->setRenderBinDetails(0, "RenderBin");

	// Barrier: compute writes must be visible before the vertex shader reads binding 1.
	sd->setDrawCallback(new LayerBarrierCallback());

	auto scene = osgx::make_ref<osg::Group>();

	scene->addChild(dispatch);
	scene->addChild(atlas);

	return example::run(viewer, args, scene);
}
