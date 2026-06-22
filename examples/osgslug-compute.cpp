//vimrun! ./osgslug-compute --clear-color 0.2,0.2,0.3,1.0

#include "osgslug-example.hpp"

#include "slughorn/canvas.hpp"

OSGSLUG_DISABLE_WARNINGS

#include <osg/DispatchCompute>

OSGSLUG_ENABLE_WARNINGS

// TODO: We should be able to SOMEHOW get the `osgSlug::Atlas::SHADER_TYPES` GLSL blurb into this
// inline shader source as well; perhaps we could setup format flags or some kind of helper methods?
static const char* COMPUTE_SHADER = R"(
#version 430 core
layout(local_size_x = 2) in;

struct LayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
};

layout(std430, binding = 1) buffer LayerBuffer {
	LayerData layers[];
};

uniform float osg_SimulationTime;

void main() {
	float t = osg_SimulationTime;
	uint i = gl_GlobalInvocationID.x;
	float phase = float(i) * 1.047;

	layers[i].color = vec4(
		0.5 + 0.5 * sin(t + phase),
		0.5 + 0.5 * sin(t + phase + 2.094),
		0.5 + 0.5 * sin(t + phase + 4.189),
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
	// canvas.scale(1_cv / 10_cv, 1_cv / 10_cv);
	canvas.moveTo(0_cv, 0_cv);
	canvas.lineTo(0_cv, 0.5_cv);
	canvas.lineTo(0.5_cv, 1_cv);
	canvas.lineTo(1_cv, 0.5_cv);
	canvas.lineTo(1_cv, 0_cv);
	canvas.closePath();
	canvas.fill({1_cv, 1_cv, 1_cv, 0.5_cv});

	// auto shape = canvas.fill({});
	auto compositeShape = canvas.finalize();

	atlas->build();
	atlas->packTextures();

	auto sd = osgx::make_ref<osgSlug::SSBOShapeDrawable>();

	// sd->addLayer({shape, {1_cv, 0.5_cv, 0_cv, 0.5_cv}});
	sd->addCompositeShape(compositeShape);
	sd->getOrCreateStateSet()->setRenderBinDetails(1, "RenderBin");

	// atlas->addChild triggers compile immediately (atlas is Packed); getLayerBuffer is valid after.
	atlas->addChild(sd);

	// Grab the layer SSBO handle; the compute shader writes into this buffer!
	// Layer 0 anchors the binding; totalSize covers the full contiguous buffer.
	auto* layerBuf = sd->getLayerBuffer(0);
	const auto layerTotalSize = static_cast<GLsizeiptr>(
		sd->getLayers().size() * 4 * sizeof(osg::Vec4)
	);
	auto computeProgram = osgx::make_ref<osg::Program>();

	computeProgram->addShader(new osg::Shader(osg::Shader::COMPUTE, COMPUTE_SHADER));

	// One workgroup of one thread/one layer.
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
