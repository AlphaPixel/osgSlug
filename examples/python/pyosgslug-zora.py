#!/usr/bin/env python3
#vimrun! ./zora.py

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn
import math
import time
import os
import random

W, H = 800, 600

os.environ.update({
	"OSG_WINDOW": f"50 50 {W} {H}",
	"OSG_THREADING": "SingleThreaded",
	"OSG_GL_CONTEXT_PROFILE_MASK": "1",
	"OSG_GL_VERSION": "4.6",
	"OSG_GL_CONTEXT_VERSION": "4.6",
})

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------

FULLSCREEN_VERT = """
#version 330 core

in vec4 osg_Vertex;
in vec2 osg_MultiTexCoord0;

out vec2 uv;

void main() {
	uv = osg_MultiTexCoord0;
	gl_Position = osg_Vertex;
}
"""

BLUR_FRAG = """
#version 330 core

uniform sampler2D inputTex;
uniform vec2 texelStep;

in vec2 uv;

out vec4 fragColor;

void main() {
	vec2 step = texelStep * 6;

	vec4 sum = vec4(0.0);

	sum += texture(inputTex, uv - 4.0 * step) * 0.0162162162;
	sum += texture(inputTex, uv - 3.0 * step) * 0.0540540541;
	sum += texture(inputTex, uv - 2.0 * step) * 0.1216216216;
	sum += texture(inputTex, uv - 1.0 * step) * 0.1945945946;
	sum += texture(inputTex, uv) * 0.2270270270;
	sum += texture(inputTex, uv + 1.0 * step) * 0.1945945946;
	sum += texture(inputTex, uv + 2.0 * step) * 0.1216216216;
	sum += texture(inputTex, uv + 3.0 * step) * 0.0540540541;
	sum += texture(inputTex, uv + 4.0 * step) * 0.0162162162;

	fragColor = sum;
}
"""

COMPOSITE_FRAG = """
#version 330 core

uniform sampler2D sceneTex;
uniform sampler2D blurTex;

uniform float glowStrength;

in vec2 uv;

out vec4 fragColor;

void main() {
	vec4 scene = texture(sceneTex, uv);
	vec4 blur = texture(blurTex, uv);

	// Blue holographic tint.
	vec3 blueScene = scene.rgb * vec3(0.28, 0.55, 1.0);
	vec3 blueGlow = blur.rgb * vec3(0.08, 0.35, 1.0) * glowStrength;

	// Screen-space scanlines.
	float scan = step(0.35, fract(gl_FragCoord.y * 0.25));
	scan = mix(0.72, 1.0, scan);

	// Tighter vignette for Zora look.
	float d = distance(uv, vec2(0.5));
	float vignette = smoothstep(0.72, 0.12, d);

	vec3 color = (blueScene + blueGlow) * scan * vignette;

	fragColor = vec4(color, 1.0);
}
"""

# effectId=1: rotation at effectParam radians/second around the shape's Centered origin.
VERT_EFFECTS = """
#version 430 core

#pragma osgSlug lib_vertex

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	if(data.effectId == 1) return osgSlug_Vertex_Rotate(data, data.effectParam * data.time);

	return osgSlug_VertexDefault(data);
}
"""

# Concentric ring definitions. Radii and widths are in canvas [0,1] space.
# Speed is in radians/second; sign = CCW (+) or CW (-).
CX, CY = 0.5, 0.5
Z_STEP = 0.05 # world-space depth separation between adjacent rings

# Small accent dots clustered in the gap of ring 0.
# Custom origin pins the rotation pivot to (CX, CY) regardless of cluster position.
# DOT_SPREAD is the total angular span of the cluster; GAP is pi/6 so we stay well inside it.
DOT_ORBITAL_RADIUS = 0.42
DOT_SIZE = 0.007
DOT_COUNT = 5
DOT_SPREAD = math.pi / 10 # ~18 deg total spread — fits inside the 30 deg gap
DOT_SPEED = 0.48 # matches ring 0 so cluster stays in the gap

# Outward pulse cascade: every PULSE_CYCLE seconds, rings flash inner→outer.
# Each entry: (layer_index, start_offset, duration). Durations shrink outward.
PULSE_CYCLE = 3.0
BASE_COLOR = slughorn.Color(0.6, 0.85, 1.0, 1.0)
PULSE_COLOR = slughorn.Color(4.0, 4.0, 4.0, 1.0) # HDR white flash — punches through the blue tint

PULSE_SEQ = [
	(4, 0.00, 0.28, 1.00), # innermost — full HDR blast
	(3, 0.20, 0.22, 0.75),
	(2, 0.36, 0.17, 0.50),
	(1, 0.48, 0.13, 0.28),
	(0, 0.57, 0.09, 0.12), # outermost — faint flicker
]

RINGS = [
	{"radius": 0.42, "width": 0.010, "speed": 0.48},
	{"radius": 0.34, "width": 0.016, "speed": -0.78},
	{"radius": 0.26, "width": 0.010, "speed": 1.20},
	{"radius": 0.18, "width": 0.022, "speed": -0.42},
	{"radius": 0.10, "width": 0.010, "speed": 0.90},
]

# ---------------------------------------------------------------------------
# Helpers (shared with blur4.py)
# ---------------------------------------------------------------------------

def make_color_texture(w, h):
	tex = osg.Texture2D()
	tex.size = (w, h)
	tex.internalFormat = GL_RGBA16F
	tex.filter = (osg.Texture.LINEAR, osg.Texture.LINEAR)
	tex.wrap = (osg.Texture.CLAMP_TO_EDGE, osg.Texture.CLAMP_TO_EDGE)
	return tex


def make_fullscreen_quad():
	geode = osg.Geode()
	geode.name = "fullscreen-quad"

	geode.drawables.append(osg.createTexturedQuadGeometry(
		osg.Vec3(-1.0, -1.0, -1.0),
		osg.Vec3(2.0, 0.0, 0.0),
		osg.Vec3(0.0, 2.0, 0.0),
	))

	return geode


def make_program(name, vert, frag):
	return osg.Program(name=name, shaders=(
		osg.Shader(osg.Shader.VERTEX, vert),
		osg.Shader(osg.Shader.FRAGMENT, frag),
	))


def make_scene_rtt_pass(output_tex, scene, w, h, name="Scene RTT"):
	cam = osg.Camera()
	cam.name = name

	cam.renderOrder = (osg.Camera.PRE_RENDER, 0)
	cam.renderTargetImplementation = osg.Camera.FRAME_BUFFER_OBJECT

	cam.clearMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
	cam.clearColor = osg.Vec4(0.0, 0.0, 0.0, 1.0)

	cam.viewport = osg.Viewport(0, 0, w, h)
	cam.attach(osg.Camera.COLOR_BUFFER, output_tex)

	cam.children.append(scene)

	return cam


def make_fullscreen_rtt_pass(
	input_tex,
	output_tex,
	frag_shader,
	w,
	h,
	name="Post RTT",
	order=1,
	extra_uniforms=None,
):
	cam = osg.Camera()
	cam.name = name

	cam.renderOrder = (osg.Camera.PRE_RENDER, order)
	cam.dataVariance = osg.Object.DYNAMIC
	cam.renderTargetImplementation = osg.Camera.FRAME_BUFFER_OBJECT

	cam.clearMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
	cam.clearColor = osg.Vec4(0.0, 0.0, 0.0, 1.0)
	cam.viewport = osg.Viewport(0, 0, w, h)

	cam.projectionMatrix = osg.Matrix.identity()
	cam.viewMatrix = osg.Matrix.identity()
	cam.referenceFrame = osg.Transform.ABSOLUTE_RF
	cam.allowEventFocus = False

	cam.attach(osg.Camera.COLOR_BUFFER, output_tex)

	prog = make_program(f"{name}_program", FULLSCREEN_VERT, frag_shader)

	# cam.stateSet.setAttributeAndModes(
	cam.stateSet.attributes.append((
		prog,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	))

	# cam.stateSet.setTextureAttributeAndModes(
	cam.stateSet.textureAttributes[0] = (
		input_tex,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	)

	cam.stateSet.uniforms["inputTex"] = 0

	if extra_uniforms:
		for k, v in extra_uniforms.items():
			cam.stateSet.uniforms[k] = v

	cam.children.append(make_fullscreen_quad())

	return cam


def make_blur_pass(input_tex, output_tex, w, h, direction, name, order):
	if direction == "horizontal":
		texel_step = osg.Vec2(1.0 / float(w), 0.0)
	elif direction == "vertical":
		texel_step = osg.Vec2(0.0, 1.0 / float(h))
	else:
		raise ValueError(f"invalid blur direction: {direction!r}")

	return make_fullscreen_rtt_pass(
		input_tex=input_tex,
		output_tex=output_tex,
		frag_shader=BLUR_FRAG,
		w=w,
		h=h,
		name=name,
		order=order,
		extra_uniforms={
			"texelStep": texel_step,
		},
	)


def make_composite_hud(scene_tex, blur_tex, w, h):
	cam = osg.Camera()
	cam.name = "Composite HUD"

	cam.renderOrder = osg.Camera.POST_RENDER
	cam.clearMask = GL_DEPTH_BUFFER_BIT
	cam.viewport = osg.Viewport(0, 0, w, h)

	cam.projectionMatrix = osg.Matrix.identity()
	cam.viewMatrix = osg.Matrix.identity()
	cam.referenceFrame = osg.Transform.ABSOLUTE_RF
	cam.allowEventFocus = False

	prog = make_program("composite_hud_program", FULLSCREEN_VERT, COMPOSITE_FRAG)

	# cam.stateSet.setAttributeAndModes(
	cam.stateSet.attributes.append((
		prog,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	))

	# cam.stateSet.setTextureAttributeAndModes(
	cam.stateSet.textureAttributes[0] = (
		scene_tex,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	)
	# cam.stateSet.setTextureAttributeAndModes(
	cam.stateSet.textureAttributes[1] = (
		blur_tex,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	)

	cam.stateSet.uniforms["sceneTex"] = 0
	cam.stateSet.uniforms["blurTex"] = 1
	cam.stateSet.uniforms["glowStrength"] = 2.5

	cam.children.append(make_fullscreen_quad())

	return cam

# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------

def create_scene():
	Origin = slughorn.ShapeInfo.Origin
	# centered = Origin(Origin.Type.Centered)  # fun variation: bbox center drifts from circle center on gapped arcs
	ring_pivot = Origin(CX, CY)  # Pivot(CX, CY): geometric circle center, immune to bbox asymmetry from the gap

	# Build ring geometry in slughorn.
	slug_atlas = slughorn.Atlas()
	canvas = slughorn.canvas.Canvas(slug_atlas, slughorn.KeyIterator())
	canvas.decomposer().tolerance = slughorn.TOLERANCE_BALANCED

	GAP = math.pi / 6 # 30° gap so rotation is visible

	ring_layers = []

	for i, ring in enumerate(RINGS):
		canvas.begin_path()
		canvas.arc(CX, CY, ring["radius"], GAP / 2, 2 * math.pi - GAP / 2)
		ring_layers.append(canvas.stroke(
			ring["width"],
			slughorn.Color(0.6, 0.85, 1.0, 1.0),
			1.0,
			slughorn.Key(f"ring_{i}"),
			ring_pivot,
		))

	# Cluster of dots inside the gap, anchored at ring-0's gap angle (0 rad).
	# Angles are evenly spread over DOT_SPREAD centered on 0.
	dot_path = slughorn.canvas.Path()
	pivot = slughorn.ShapeInfo.Origin(CX, CY) # Custom: pin rotation to ring center

	for j in range(DOT_COUNT):
		theta = -DOT_SPREAD / 2 + j * DOT_SPREAD / (DOT_COUNT - 1)
		dot_path.circle(
			CX + DOT_ORBITAL_RADIUS * math.cos(theta),
			CY + DOT_ORBITAL_RADIUS * math.sin(theta),
			DOT_SIZE,
		)

	dot_layer = canvas.fill(dot_path, slughorn.Color(0.6, 0.85, 1.0, 1.0), 1.0, slughorn.Key("dots"), pivot)

	slug_atlas.build()

	atlas = osgSlug.Atlas(slug_atlas)

	atlas.packTextures()

	sd = osgSlug.ShapeDrawable()
	# sd.atlas = atlas

	for i, (ring, layer) in enumerate(zip(RINGS, ring_layers)):
		layer.transform.z = i * Z_STEP
		layer.effectId = 1
		sd.addLayer(layer)

	dot_layer.transform.z = 0.5 * Z_STEP
	dot_layer.effectId = 1
	sd.addLayer(dot_layer)

	sd.stateSet = atlas.createHookStateSet({osgSlug.VertexHook: VERT_EFFECTS})

	def on_attached(atlas):
		for i, ring in enumerate(RINGS):
			sd.layers[i].effectParam = ring["speed"]

		sd.layers[len(RINGS)].effectParam = DOT_SPEED

	sd.onAtlasAttached = on_attached

	atlas.children.append(sd)

	return atlas, sd

if __name__ == "__main__":
	sceneColor = make_color_texture(W, H)
	blurA = make_color_texture(W, H)
	blurB = make_color_texture(W, H)

	sceneColor.dataVariance = osg.Object.DYNAMIC
	blurA.dataVariance = osg.Object.DYNAMIC
	blurB.dataVariance = osg.Object.DYNAMIC

	scene_geode, sd = create_scene()

	scene_pass = make_scene_rtt_pass(
		output_tex=sceneColor,
		scene=scene_geode,
		w=W, h=H,
		name="Scene RTT",
	)

	blur_h_pass = make_blur_pass(
		input_tex=sceneColor, output_tex=blurA,
		w=W, h=H, direction="horizontal",
		name="Blur Horizontal RTT", order=1,
	)

	blur_v_pass = make_blur_pass(
		input_tex=blurA, output_tex=blurB,
		w=W, h=H, direction="vertical",
		name="Blur Vertical RTT", order=2,
	)

	hud = make_composite_hud(
		scene_tex=sceneColor, blur_tex=blurB,
		w=W, h=H,
	)

	root = osg.Group()
	root.children.extend((scene_pass, blur_h_pass, blur_v_pass, hud))

	viewer = osgViewer.Viewer()
	viewer.sceneData = root
	viewer.cameraManipulator = osgGA.TrackballManipulator()
	viewer.camera.clearColor = osg.Vec4(0.0, 0.0, 0.0, 1.0)

	t0 = time.time()

	while not viewer.done:
		phase = (time.time() - t0) % PULSE_CYCLE

		for layer_idx, start, dur, peak in PULSE_SEQ:
			if start <= phase < start + dur:
				k = math.sin((phase - start) / dur * math.pi) * peak
				r = BASE_COLOR.r + (PULSE_COLOR.r - BASE_COLOR.r) * k
				g = BASE_COLOR.g + (PULSE_COLOR.g - BASE_COLOR.g) * k
				b = BASE_COLOR.b + (PULSE_COLOR.b - BASE_COLOR.b) * k
				sd.layers[layer_idx].color = slughorn.Color(r, g, b, 1.0)
			else:
				sd.layers[layer_idx].color = BASE_COLOR

		viewer.frame()
