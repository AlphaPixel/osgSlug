// vimrun! ./osgslug-rtt

#include "osgslug-example.hpp"
#include "slughorn/canvas.hpp"

OSGSLUG_DISABLE_WARNINGS
#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Texture2D>
OSGSLUG_ENABLE_WARNINGS

static constexpr int RTT_W = 512;
static constexpr int RTT_H = 512;

static const char* QUAD_VERT = R"(
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec2 uv;

void main() {
	uv = osg_MultiTexCoord0;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
})";

static const char* QUAD_FRAG = R"(
#version 330 core

uniform sampler2D colorTex;

in vec2 uv;

out vec4 color;

void main() {
	color = texture(colorTex, uv);
})";

osg::ref_ptr<osg::Texture2D> createRTTTexture() {
	auto tex = osgx::make_ref<osg::Texture2D>();

	tex->setTextureSize(RTT_W, RTT_H);
	tex->setInternalFormat(GL_RGBA);
	tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
	tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
	tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
	tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

	return tex;
}

// ABSOLUTE_RF + fixed Ortho2D so the RTT view is independent of the main
// camera — the Ortho2DManipulator moves the quad, not what the RTT sees.
osg::ref_ptr<osg::Camera> createRTTCamera(osg::Texture2D* colorTex) {
	auto cam = osgx::make_ref<osg::Camera>();

	cam->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
	cam->setProjectionMatrixAsOrtho2D(-0.05, 1.05, -0.05, 1.05);
	cam->setViewMatrix(osg::Matrix::identity());
	cam->setRenderOrder(osg::Camera::PRE_RENDER);
	cam->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
	cam->setClearMask(GL_COLOR_BUFFER_BIT);
	cam->setClearColor(osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
	cam->setViewport(0, 0, RTT_W, RTT_H);
	cam->attach(osg::Camera::COLOR_BUFFER, colorTex);
	cam->setName("RTT Camera");

	return cam;
}

// Plain scene-graph quad — no HUD camera needed.
// The Ortho2DManipulator pans/zooms it like any other 2D scene node.
osg::ref_ptr<osg::Geometry> createRTTQuad(osg::Texture2D* colorTex) {
	auto quad = osg::ref_ptr<osg::Geometry>(osg::createTexturedQuadGeometry(
		osg::Vec3(0.0f, 0.0f, 0.0f),
		osg::Vec3(1.0f, 0.0f, 0.0f),
		osg::Vec3(0.0f, 1.0f, 0.0f)
	));

	auto* ss = quad->getOrCreateStateSet();
	ss->setTextureAttributeAndModes(0, colorTex, osg::StateAttribute::ON);
	ss->addUniform(new osg::Uniform("colorTex", 0));
	ss->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);

	auto* prog = new osg::Program();
	prog->addShader(new osg::Shader(osg::Shader::VERTEX,   QUAD_VERT));
	prog->addShader(new osg::Shader(osg::Shader::FRAGMENT, QUAD_FRAG));
	ss->setAttributeAndModes(prog);

	return quad;
}

int main(int argc, char** argv) {
	osg::ArgumentParser args(&argc, argv);
	osgViewer::Viewer viewer(args);

	if(!example::setupArguments(args, "RTT: osgSlug -> FBO -> scene quad (x=freeze/thaw)")) return 0;

	auto atlas = osgx::make_ref<osgSlug::Atlas>();

	slughorn::canvas::Canvas canvas(*atlas, 0xE0000);

	canvas
		.beginPath()
		.moveTo(0.1_cv, 0.1_cv)
		.lineTo(0.5_cv, 0.5_cv)
		.lineTo(0.9_cv, 0.1_cv)
		.strokePath(0.35_cv)
	;

	auto layer = canvas.fill({0.3_cv, 0.7_cv, 1.0_cv, 1.0_cv});

	atlas->build();
	atlas->packTextures();

	auto sd = example::makeShapeDrawable();
	sd->addLayer(layer);

	atlas->addChild(sd);

	// content is what we mask out when frozen (atlas stays alive for state).
	auto content = osgx::make_ref<osg::Group>();
	content->addChild(atlas);

	auto colorTex = createRTTTexture();
	auto rttCam = createRTTCamera(colorTex);
	rttCam->addChild(content);

	auto root = osgx::make_ref<osg::Group>();
	root->addChild(rttCam);
	root->addChild(createRTTQuad(colorTex));

	viewer.setSceneData(root);
	viewer.getCamera()->setClearColor(osg::Vec4(0.2f, 0.2f, 0.2f, 1.0f));

	auto setRTTFrozen = [content, rttCam](bool frozen) {
		content->setNodeMask(frozen ? 0u : ~0u);
		rttCam->setClearMask(frozen ? 0u : static_cast<GLbitfield>(GL_COLOR_BUFFER_BIT));

		OSG_NOTICE
			<< (frozen
				? "RTT frozen; manipulator driving textured quad"
				: "RTT live; manipulator driving RTT camera")
			<< std::endl
		;
	};

	auto manipulators = osgx::make_ref<osgx::MultiViewManipulator>();

	manipulators->addTarget(
		"RTT camera",
		new osgx::Ortho2DManipulator(),
		rttCam.get(),
		content.get(),
		[setRTTFrozen](bool active) { if(active) setRTTFrozen(false); }
	);

	manipulators->addTarget(
		"textured quad",
		example::makeTrackball(root.get()),
		nullptr,
		root.get(),
		[setRTTFrozen](bool active) { if(active) setRTTFrozen(true); }
	);

	viewer.setCameraManipulator(manipulators);
	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
	viewer.addEventHandler(new example::DebugModeHandler(viewer.getCamera()->getOrCreateStateSet()));
	viewer.setUpViewInWindow(50, 50, 800, 600);

	return viewer.run();
}
