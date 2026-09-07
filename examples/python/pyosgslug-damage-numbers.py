#!/usr/bin/env python3
#vimrun! ./pyosgslug-damage-numbers.py [--billboard] [font.ttf]

# Damage-number proof: 1 emits a glancing 1-2, 2 a normal 3-17, and 3 a critical 18-20. Each
# number is a real osgSlug.Text child of the cube's MatrixTransform, so it inherits every model
# transform. Its rise and fade are driven by osg_SimulationTime in shader hooks; Python only
# creates the text and removes it after its GPU animation has finished.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

import argparse
import random

from pyosgslug_example import make_trackball, window_size

FONT_PATH = "font/UbuntuMono-R.ttf"
FONT_SIZE = 0.65
DAMAGE_LIFETIME = 0.85
DAMAGE_START_Z_OFFSET = -0.5

DAMAGE_NORMAL = 0
DAMAGE_GLANCING = 1
DAMAGE_CRITICAL = 2

DAMAGE_STYLES = {
	DAMAGE_GLANCING: ((1, 2), FONT_SIZE * 0.75, slughorn.Color(0.52, 0.65, 0.82, 1.0)),
	DAMAGE_NORMAL: ((3, 17), FONT_SIZE, slughorn.Color(1.0, 0.78, 0.18, 1.0)),
	DAMAGE_CRITICAL: ((18, 20), FONT_SIZE * 1.35, slughorn.Color(1.0, 0.30, 0.08, 1.0)),
}

# One program is shared by all damage numbers. u_damageStartTime is set once on each Text node's
# StateSet, so neither the rise nor the fade needs a CPU-to-GPU update after spawning.
DAMAGE_VERTEX_HOOK = """
#version 430 core

#pragma osgSlug lib_vertex

uniform float u_damageStartTime;

osgSlug_VertexResult osgSlug_Vertex(osgSlug_VertexData data) {
	osgSlug_VertexResult r = osgSlug_VertexDefault(data);
	float age = max(data.time - u_damageStartTime, 0.0);
	float progress = clamp(age / 0.85, 0.0, 1.0);

	if(data.effectId == 1) r.pos.y -= 0.35 * progress;

	else if(data.effectId == 2) {
		r.pos.y += 1.25 * progress;
		r.pos.x += sin(age * 28.0 + data.effectParam) * 0.045 * (1.0 - progress);
	}

	else r.pos.y += 0.80 * progress;

	return r;
}
"""

DAMAGE_FRAGMENT_HOOK = """
#version 430 core

#pragma osgSlug lib_fragment
#pragma osgSlug lib_fragment_em

uniform float u_damageStartTime;

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	float fade = osgSlug_Fragment_FadeOut(
		data.time,
		u_damageStartTime + 0.15,
		u_damageStartTime + 0.85
	);

	vec3 color = data.layerColor.rgb;

	if(data.effectId == 2) {
		float age = max(data.time - u_damageStartTime, 0.0);
		float flash = (0.35 + 0.65 * abs(sin(age * 24.0))) * exp(-age * 3.0);
		color = mix(color, vec3(1.0), flash);
	}

	return vec4(color, data.fill * data.layerColor.a * fade);
}
"""

class DamageNumberController(osgGA.GUIEventHandler):
	def __init__(self, viewer, model, target_bounds, atlas, metrics, billboard):
		super().__init__()

		self.viewer = viewer
		self.model = model
		self.target_bounds = target_bounds
		self.atlas = atlas
		self.metrics = metrics
		self.billboard = billboard
		self.live = []

	def spawn(self, style):
		amount_range, font_size, color = DAMAGE_STYLES[style]
		amount = random.randint(*amount_range)
		text = osgSlug.Text(self.atlas, font_size)

		text.fontMetrics = self.metrics
		text.setHooks({
			osgSlug.Hook.VertexHook: DAMAGE_VERTEX_HOOK,
			osgSlug.Hook.FragmentHook: DAMAGE_FRAGMENT_HOOK,
		})
		text.effectId = style
		text.effectParam = random.uniform(0.0, 6.283185307179586)

		# Phase 2: compensate for the model's rotation so the number faces the screen, but leave
		# autoScaleToScreen off so its size remains relative to the model.
		if self.billboard:
			text.autoRotateMode = osg.AutoTransform.AutoRotateMode.ROTATE_TO_SCREEN
			text.autoScaleToScreen = False

		text.addText(str(amount), color)
		text.compile()

		# Text is authored from its baseline's left edge. Center it over the target's local X bounds
		# and start from the top Y edge. DAMAGE_START_Z_OFFSET is deliberately artistic depth tuning
		# for this cube. The vertex hook subsequently lifts it along +Y; all of this remains
		# model-local and inherits transforms.
		bounds = text.boundingBox
		anchor = self.target_bounds.center
		local = osg.MatrixTransform()
		local.matrix = osg.Matrix.translate(osg.Vec3(
			anchor.x - bounds.center.x,
			self.target_bounds.yMax + 0.20,
			self.target_bounds.zMax + DAMAGE_START_Z_OFFSET
		))

		start_time = float(self.viewer.frameStamp.simulationTime)

		text.stateSet.uniforms["u_damageStartTime"] = start_time
		local.children.append(text)
		self.model.children.append(local)
		self.live.append((start_time, local))

	def handle(self, ea, aa):
		if ea.type == osgGA.GUIEventAdapter.KEYDOWN:
			styles = {
				ord("1"): DAMAGE_GLANCING,
				ord("2"): DAMAGE_NORMAL,
				ord("3"): DAMAGE_CRITICAL,
			}
			style = styles.get(ea.key)

			if style is None:
				return False

			self.spawn(style)

			return True

		if ea.type != osgGA.GUIEventAdapter.FRAME:
			return False

		now = float(self.viewer.frameStamp.simulationTime)
		# now = float(aa.viewer.frameStamp.simulationTime)

		for start_time, node in self.live[:]:
			if now - start_time >= DAMAGE_LIFETIME:
				self.model.children.remove(node)
				self.live.remove((start_time, node))

		return False

def build_scene(w, h, font_path=FONT_PATH):
	font_atlas = slughorn.Atlas()
	config = slughorn.freetype.LoadConfig()

	if not slughorn.freetype.load_ascii_font(font_path, font_atlas, config):
		raise RuntimeError(f"Couldn't load font: {font_path}")

	font_atlas.build()

	atlas = osgSlug.Atlas.fromAtlas(font_atlas)
	root = osg.Group()
	model = osg.MatrixTransform(name="damage-number-cube")
	geode = osg.Geode(name="cube-geometry")
	cube = osg.ShapeDrawable(osg.Box(osg.Vec3(), 2.0, 2.0, 2.0))

	cube.color = osg.Vec4(0.22, 0.48, 0.86, 1.0)
	geode.drawables.append(cube)
	model.children.append(geode)
	root.children.append(model)

	return root, model, cube.computeBoundingBox(), atlas, config.metrics

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="GPU-animated osgSlug damage-number proof.")
	parser.add_argument("font", nargs="?", default=FONT_PATH, help="TTF/OTF font path")
	parser.add_argument(
		"--billboard",
		action="store_true",
		help="Phase 2: rotate damage numbers to face the screen without screen scaling"
	)
	args = parser.parse_args()

	W, H = window_size()
	viewer = osgViewer.Viewer()
	root, model, target_bounds, atlas, metrics = build_scene(W, H, args.font)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.06, 0.07, 0.12, 1.0)
	viewer.eventHandlers.append(
		DamageNumberController(viewer, model, target_bounds, atlas, metrics, args.billboard)
	)

	while not viewer.done:
		viewer.frame()
