#!/usr/bin/env python3
#vimrun! ./pyosgslug-pathcompare.py

# Caps/joins test bench: ShapeDrawable (canvas.stroke() baked outline, the reference) drawn next to
# PathDrawable(Sluggit) for the same centerline, so any difference is visible by eye.
#
#   top    - ShapeDrawable, canvas.stroke() baked with the selected join/cap
#   bottom - PathDrawable(Sluggit) - the same centerline via setPaths()
#
# CASES below are the stroke situations caps/joins have to survive, from the easy one (every miter
# ratio under the limit) to the ones known to break mitered quads: a turn past the miter limit, a
# near-reversal, a segment shorter than the stroke width, collinear and duplicate points, and two
# separate subpaths sharing an endpoint (how real MVT data splits one road). Every case x join x cap
# reference shape is baked up front, so switching only toggles node masks and calls setPaths().
#
# Controls:
#   c        next case
#   j        next join
#   k        next cap
#   r        rotate everything by ROTATE_STEP_DEG (wraps at 360)
#   F12      toggle Ortho2D <-> 3D trackball
#   [ / ]    pixel-peep zoom (1/2/4/8/16/32x), anchored on the mouse position
#   g        toggle the pixel grid overlay (zoom >= 4x)
#   f        freeze the pixel-peep anchor
#   F1-F7    osgSlug_debugMode (checkerboard / band edges / quad border / heatmap / ...)
#
# --case NAME, --join NAME, --cap NAME and --debug N pick the starting state, so a headless run
# (--headless, see pyosgslug_example.py) can capture any combination.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import argparse
import math

import osgSlug
import slughorn

WIDTH = 20.0 # world units, full stroke width
LENGTH = 160.0 # world units, scale applied to every CASES polyline
PARALLEL_CLEARANCE = 16.0 # guaranteed world-unit edge-to-edge gap between the two strokes
ROTATE_STEP_DEG = 15.0
MITER_LIMIT = 4.0 # canvas.stroke()'s default, and PathDrawable's MAX_MITER

# Unit-space subpaths (scaled by LENGTH), all within x in [-0.5, 0.5]. WIDTH / LENGTH = 0.125, so a
# half-width is 0.0625 in these units.
CASES = (
	# Turns of ~45, ~98 and ~75 degrees; every miter ratio stays under MITER_LIMIT.
	("gentle", (((-0.5, -0.1), (-0.15, -0.1), (0.05, 0.1), (0.2, -0.1), (0.45, 0.0)),)),
	# One 20 degree interior angle: miter ratio 1 / sin(10 deg) = 5.76, past MITER_LIMIT, so the
	# reference bevels there.
	("sharp", (((-0.5, -0.05), (0.3, -0.05), (-0.17, 0.121)),)),
	# A 3 degree interior angle - the stroke folds back over itself.
	("hairpin", (((-0.5, 0.0), (0.35, 0.0), (-0.349, 0.037)),)),
	# Two 90 degree turns in opposite directions (an S-step) joined by a 0.05 segment, shorter than
	# the 0.125 stroke width. The two bisectors are parallel, so the quads still partition.
	("short", (((-0.5, -0.05), (0.0, -0.05), (0.0, 0.0), (0.5, 0.0)),)),
	# Two 90 degree turns the SAME way around a 0.08 segment (narrower than the stroke): a U whose
	# arms overlap, and whose two inner miter corners pass each other, so the bisectors cross.
	("uturn", (((-0.5, -0.05), (0.3, -0.05), (0.3, 0.03), (-0.5, 0.03)),)),
	# A zero-angle joint, which must be seamless.
	("collinear", (((-0.5, 0.0), (0.0, 0.0), (0.5, 0.0)),)),
	# A zero-length segment (repeated point), common in real tile data.
	("duplicate", (((-0.5, 0.0), (0.0, 0.0), (0.0, 0.0), (0.45, 0.12)),)),
	# Two subpaths sharing an endpoint at an angle - one road split in two, as MVT delivers it.
	("split", (((-0.5, 0.0), (0.0, 0.0)), ((0.0, 0.0), (0.45, 0.12)))),
)

CASES_HALF_Y = max(abs(y) for _, subpaths in CASES for sub in subpaths for _, y in sub)

JOINS = (slughorn.canvas.LineJoin.MITER, slughorn.canvas.LineJoin.BEVEL, slughorn.canvas.LineJoin.ROUND)
CAPS = (slughorn.canvas.LineCap.BUTT, slughorn.canvas.LineCap.SQUARE, slughorn.canvas.LineCap.ROUND)

# Vertical distance between the two strokes' centerlines.
OFFSET = 2.0 * CASES_HALF_Y * LENGTH + WIDTH + PARALLEL_CLEARANCE

# Sluggit's caller-provided profile shape - a bare unit square, same convention as
# osgslug-tmp.ink.cpp's "_path_sq". NOT the baked stroke-outline shape ShapeDrawable uses below.
SQUARE_SHAPE_KEY = "_path_sq"

# One shared near-white for both, so the comparison is about shape and AA, not color. slughorn.Color
# for the Layer, osg.Vec4 for PathDrawable.setColor() - same value.
SHAPE_COLOR = slughorn.Color(0.95, 0.95, 0.95, 1.0)
PATH_COLOR = osg.Vec4(0.95, 0.95, 0.95, 1.0)

BACKGROUND = osg.Vec4(0.05, 0.05, 0.08, 1.0)

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

# Single-panel pixel-zoom magnifier - the useful half of osgslug-compare.cpp's PixelPeep /
# COMPOSITE_FRAG, with the multi-panel grid-cell math dropped (one panel = the whole window).
MAGNIFIER_FRAG = """
#version 330 core

uniform sampler2D sceneTex;
uniform float peepZoom;
uniform vec2 peepCenter;
uniform vec2 peepTexels;
uniform float peepGridOn;

in vec2 uv;

out vec4 fragColor;

void main() {
	vec2 st = peepCenter + (uv - peepCenter) / peepZoom;

	fragColor = texture(sceneTex, st);

	if(peepGridOn > 0.5) {
		vec2 p = st * peepTexels;
		vec2 f = fract(p);
		vec2 w = fwidth(p);

		fragColor.rgb = mix(fragColor.rgb, vec3(0.5), 0.5 * max(step(f.x, w.x), step(f.y, w.y)));
	}
}
"""

# ---------------------------------------------------------------------------
# RTT plumbing - mirrors pyosgslug-zora.py's make_scene_rtt_pass()/make_composite_hud() pattern.
# ---------------------------------------------------------------------------

def make_program(name, vert, frag):
	return osg.Program(name=name, shaders=(
		osg.Shader(osg.Shader.VERTEX, vert),
		osg.Shader(osg.Shader.FRAGMENT, frag),
	))

def make_fullscreen_quad():
	geode = osg.Geode()
	geode.name = "fullscreen-quad"

	geode.drawables.append(osg.createTexturedQuadGeometry(
		osg.Vec3(-1.0, -1.0, -1.0),
		osg.Vec3(2.0, 0.0, 0.0),
		osg.Vec3(0.0, 2.0, 0.0),
	))

	return geode

def make_scene_texture(w, h):
	tex = osg.Texture2D()
	tex.size = (w, h)
	tex.internalFormat = GL_RGBA8
	# NEAREST at 1x is an exact 1:1 copy; at Nx it is the whole point of pixel-peep.
	tex.filter = (osg.Texture.NEAREST, osg.Texture.NEAREST)
	tex.wrap = (osg.Texture.CLAMP_TO_EDGE, osg.Texture.CLAMP_TO_EDGE)

	return tex

def make_scene_pass(output_tex, scene, w, h):
	cam = osg.Camera()
	cam.name = "Scene RTT"

	cam.renderOrder = (osg.Camera.PRE_RENDER, 0)
	cam.renderTargetImplementation = osg.Camera.FRAME_BUFFER_OBJECT

	cam.clearMask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
	cam.clearColor = BACKGROUND

	cam.viewport = osg.Viewport(0, 0, w, h)
	cam.attach(osg.Camera.COLOR_BUFFER, output_tex)

	cam.children.append(scene)

	return cam

# POST_RENDER with no attach() - draws straight to the real window, same as zora.py's
# make_composite_hud().
def make_magnifier_pass(scene_tex, w, h):
	cam = osg.Camera()
	cam.name = "Pixel-Peep Magnifier"

	cam.renderOrder = osg.Camera.POST_RENDER
	cam.clearMask = GL_DEPTH_BUFFER_BIT
	cam.viewport = osg.Viewport(0, 0, w, h)

	cam.projectionMatrix = osg.Matrix.identity()
	cam.viewMatrix = osg.Matrix.identity()
	cam.referenceFrame = osg.Transform.ABSOLUTE_RF
	cam.allowEventFocus = False

	prog = make_program("magnifier_program", FULLSCREEN_VERT, MAGNIFIER_FRAG)

	cam.stateSet.attributes.append((prog, osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE))
	cam.stateSet.textureAttributes[0] = (
		scene_tex,
		osg.StateAttribute.ON | osg.StateAttribute.OVERRIDE,
	)
	cam.stateSet.uniforms["sceneTex"] = 0
	cam.stateSet.uniforms["peepZoom"] = 1.0
	cam.stateSet.uniforms["peepCenter"] = osg.Vec2(0.5, 0.5)
	cam.stateSet.uniforms["peepTexels"] = osg.Vec2(float(w), float(h))
	cam.stateSet.uniforms["peepGridOn"] = 0.0

	cam.children.append(make_fullscreen_quad())

	return cam

# Nearest-neighbor pixel-zoom magnifier handler - mouse MOVE re-anchors it, '[' / ']' step zoom,
# 'g' toggles the grid overlay, 'f' freezes the anchor. Single-panel equivalent of
# osgslug-compare.cpp's PixelPeep (its multi-panel grid-cell math doesn't apply - one panel is
# the whole window here).
class PixelPeep(osgGA.GUIEventHandler):
	ZOOMS = (1, 2, 4, 8, 16, 32)
	GRID_MIN_ZOOM = 4

	def __init__(self, state_set):
		super().__init__()

		self.level = 0
		self.show_grid = True
		self.frozen = False
		self.anchor = osg.Vec2(0.5, 0.5)

		self.zoom_u = state_set.uniforms["peepZoom"]
		self.center_u = state_set.uniforms["peepCenter"]
		self.grid_u = state_set.uniforms["peepGridOn"]

	def zoom(self):
		return self.ZOOMS[self.level]

	def apply(self):
		z = self.zoom()

		self.zoom_u.value = float(z)
		self.center_u.value = self.anchor
		self.grid_u.value = 1.0 if (self.show_grid and z >= self.GRID_MIN_ZOOM) else 0.0

		print(f"pixel-peep {z}x{' (frozen)' if self.frozen else ''}")

	def handle(self, ea, aa):
		if ea.handled:
			return False

		if ea.type == osgGA.GUIEventAdapter.MOVE:
			if self.frozen:
				return False

			view = aa.view

			if view is None:
				return False

			viewport = view.camera.viewport

			if viewport is None:
				return False

			u = (ea.x - viewport.x) / viewport.width
			v = (ea.y - viewport.y) / viewport.height

			if ea.mouseYOrientation == osgGA.GUIEventAdapter.Y_INCREASING_DOWNWARDS:
				v = 1.0 - v

			self.anchor = osg.Vec2(min(max(u, 0.0), 1.0), min(max(v, 0.0), 1.0))
			self.center_u.value = self.anchor

			return False

		if ea.type != osgGA.GUIEventAdapter.KEYDOWN:
			return False

		if ea.key == ord("["):
			if self.level > 0:
				self.level -= 1
		elif ea.key == ord("]"):
			if self.level + 1 < len(self.ZOOMS):
				self.level += 1
		elif ea.key == ord("g"):
			self.show_grid = not self.show_grid
		elif ea.key == ord("f"):
			self.frozen = not self.frozen
		else:
			return False

		self.apply()

		return True

# 'r' rotates the whole triplet's MatrixTransform by ROTATE_STEP_DEG, wrapping at 360 - the live
# equivalent of the old static angle-sweep grid.
class RotateHandler(osgGA.GUIEventHandler):
	def __init__(self, transform, step_deg):
		super().__init__()

		self.transform = transform
		self.step_deg = step_deg
		self.angle_deg = 0.0

	def handle(self, ea, aa):
		if ea.handled or ea.type != osgGA.GUIEventAdapter.KEYDOWN or ea.key != ord("r"):
			return False

		self.angle_deg = (self.angle_deg + self.step_deg) % 360.0
		self.transform.matrix = osg.Matrix.rotate(
			math.radians(self.angle_deg), osg.Vec3(0.0, 0.0, 1.0)
		)

		print(f"scene rotation: {self.angle_deg:.0f} deg")

		return True

# ---------------------------------------------------------------------------
# Atlas / shape setup
# ---------------------------------------------------------------------------

# Bakes one reference stroke per (case, join, cap) plus Sluggit's unit-square profile shape.
# Returns (osgSlug.Atlas, {(case, join, cap): Layer}); a combination canvas.stroke() could not
# bake maps to None. Shapes are authored at unit size and reach world units through the Layer's
# scale=LENGTH - large canvas-space coordinates lose precision, so keep them small.
def build_atlas():
	a = slughorn.Atlas()
	cvs = slughorn.canvas.Canvas(a)
	layers = {}

	# auto_metrics stays ON for the strokes: it derives the shape metrics from the real outline's
	# bounding box, and the returned Layer's transform compensates for the local-origin shift it
	# applies. That Layer must be reused as-is - a fresh slughorn.Layer(key, ...) has a zero
	# transform and renders shifted.
	for name, subpaths in CASES:
		for join in JOINS:
			for cap in CAPS:
				cvs.begin_path()

				for sub in subpaths:
					cvs.move_to(*sub[0])

					for x, y in sub[1:]:
						cvs.line_to(x, y)

				try:
					layers[(name, join, cap)] = cvs.stroke(
						WIDTH / LENGTH,
						SHAPE_COLOR,
						scale=LENGTH,
						join=join,
						cap=cap,
						miter_limit=MITER_LIMIT
					)

				except Exception as e:
					print(f"canvas.stroke() failed for {name}/{join}/{cap}: {e}")

					layers[(name, join, cap)] = None

	# Sluggit's profile shape genuinely IS a unit square, so auto_metrics=False (and its
	# hardcoded 0/1/1/1 metrics) is correct here.
	cvs.auto_metrics = False

	cvs.begin_path()
	cvs.rect(0.0, 0.0, 1.0, 1.0)
	cvs.define_shape(SQUARE_SHAPE_KEY)

	a.build()

	return osgSlug.Atlas.fromAtlas(a), layers

# ---------------------------------------------------------------------------
# Scene
# ---------------------------------------------------------------------------

def case_points(subpaths):
	return [[osg.Vec4(x * LENGTH, y * LENGTH, 0.0, 0.0) for x, y in sub] for sub in subpaths]

# Everything lives in local, unrotated space under one MatrixTransform that RotateHandler alone
# controls, the pair centered on its origin. Every reference stroke gets its own ShapeDrawable at
# +OFFSET / 2, all hidden but the selected one; the single Sluggit PathDrawable sits at -OFFSET / 2
# and is re-fed per case.
class Bench:
	def __init__(self, atlas, layers):
		self.rotate_mt = osg.MatrixTransform()
		self.rotate_mt.matrix = osg.Matrix.identity()

		self.references = {}

		for combo, layer in layers.items():
			if layer is None:
				continue

			sd = osgSlug.ShapeDrawable()

			sd.addLayer(layer)

			mt = osg.MatrixTransform()

			mt.matrix = osg.Matrix.translate(0.0, OFFSET * 0.5, 0.0)
			mt.nodeMask = 0
			mt.children.append(sd)

			self.rotate_mt.children.append(mt)

			self.references[combo] = mt

		self.sluggit = osgSlug.PathDrawable(osgSlug.PathMode.Sluggit)

		self.sluggit.setShapeKey(SQUARE_SHAPE_KEY)
		self.sluggit.color = PATH_COLOR
		self.sluggit.halfWidth = WIDTH * 0.5
		self.sluggit.miterLimit = MITER_LIMIT
		self.sluggit.setPaths(case_points(CASES[0][1]))

		sluggit_mt = osg.MatrixTransform()

		sluggit_mt.matrix = osg.Matrix.translate(0.0, -OFFSET * 0.5, 0.0)
		sluggit_mt.children.append(self.sluggit)

		self.rotate_mt.children.append(sluggit_mt)

		atlas.children.append(self.rotate_mt)

		self.case = 0
		self.join = 0
		self.cap = 0
		self.shown = None

		self.apply()

	def apply(self):
		name, subpaths = CASES[self.case]
		combo = (name, JOINS[self.join], CAPS[self.cap])

		if self.shown is not None:
			self.shown.nodeMask = 0

		self.shown = self.references.get(combo)

		if self.shown is not None:
			self.shown.nodeMask = 0xffffffff

		self.sluggit.join = JOINS[self.join]
		self.sluggit.cap = CAPS[self.cap]
		self.sluggit.setPaths(case_points(subpaths))

		baked = "" if self.shown is not None else "  (no reference - canvas.stroke() failed)"

		print(f"case={name}  join={JOINS[self.join].name}  cap={CAPS[self.cap].name}{baked}")

# 'c' / 'j' / 'k' step the Bench's case / join / cap.
class BenchHandler(osgGA.GUIEventHandler):
	def __init__(self, bench):
		super().__init__()

		self.bench = bench

	def handle(self, ea, aa):
		if ea.handled or ea.type != osgGA.GUIEventAdapter.KEYDOWN:
			return False

		b = self.bench

		if ea.key == ord("c"):
			b.case = (b.case + 1) % len(CASES)
		elif ea.key == ord("j"):
			b.join = (b.join + 1) % len(JOINS)
		elif ea.key == ord("k"):
			b.cap = (b.cap + 1) % len(CAPS)
		else:
			return False

		b.apply()

		return True

if __name__ == "__main__":
	from pyosgslug_example import DebugModeHandler, configure_viewer, parse_args, window_size

	bench_parser = argparse.ArgumentParser(add_help=False)

	bench_parser.add_argument("--case", choices=[name for name, _ in CASES], default=CASES[0][0])
	bench_parser.add_argument("--join", choices=[j.name.lower() for j in JOINS], default="miter")
	bench_parser.add_argument("--cap", choices=[c.name.lower() for c in CAPS], default="butt")
	bench_parser.add_argument("--debug", type=int, default=0, help="initial osgSlug_debugMode (F1-F7)")

	bench_args, rest = bench_parser.parse_known_args()

	args = parse_args(
		description="Caps/joins test bench: baked ShapeDrawable vs PathDrawable(Sluggit)",
		argv=rest
	)

	atlas, layers = build_atlas()

	bench = Bench(atlas, layers)

	bench.case = [name for name, _ in CASES].index(bench_args.case)
	bench.join = [j.name.lower() for j in JOINS].index(bench_args.join)
	bench.cap = [c.name.lower() for c in CAPS].index(bench_args.cap)

	bench.apply()

	W, H = window_size()

	scene_tex = make_scene_texture(W, H)
	scene_pass = make_scene_pass(scene_tex, atlas, W, H)
	magnifier_pass = make_magnifier_pass(scene_tex, W, H)

	root = osg.Group()
	root.children.extend((scene_pass, magnifier_pass))

	viewer = osgViewer.Viewer()
	viewer.sceneData = root
	viewer.camera.clearColor = BACKGROUND

	configure_viewer(viewer, atlas, args)

	# configure_viewer()'s default Ortho2DManipulator frames from viewer.sceneData's bound (root,
	# polluted by the magnifier's NDC quad), not atlas's - size it explicitly instead. A bounding
	# CIRCLE, not a box, since 'r' can rotate the bench to any angle at runtime. Only applies to the
	# initial view: F12 back from the trackball rebuilds a fresh default manipulator - scroll to
	# zoom out again if that happens.
	if not args.trackball:
		half_x = LENGTH * 0.5 + WIDTH
		half_y = OFFSET * 0.5 + CASES_HALF_Y * LENGTH + WIDTH * 0.5
		radius = math.hypot(half_x, half_y)
		aspect = W / H if H else 1.0

		viewer.cameraManipulator.center = osg.Vec3d(0.0, 0.0, 0.0)
		viewer.cameraManipulator.halfExtentY = max(radius, radius / aspect) * 1.1

	# This tool inspects each renderer's own analytic AA, not hardware multisampling - off. There
	# is no multisample FBO behind scene_tex, so 'm' has nothing to toggle either way.
	debug_handler = DebugModeHandler(scene_pass.stateSet)
	debug_handler.msaa = False
	scene_pass.stateSet.modes[GL_MULTISAMPLE] = osg.StateAttribute.OFF

	if bench_args.debug:
		debug_handler.set_mode(bench_args.debug)

	viewer.eventHandlers.append(debug_handler)
	viewer.eventHandlers.append(PixelPeep(magnifier_pass.stateSet))
	viewer.eventHandlers.append(RotateHandler(bench.rotate_mt, ROTATE_STEP_DEG))
	viewer.eventHandlers.append(BenchHandler(bench))

	print("top=ShapeDrawable(baked)  bottom=PathDrawable(Sluggit)")
	print("c case  j join  k cap  r rotate  F12 2D/3D  [ / ] zoom  g grid  f freeze peep  F1-F7 debug")
	print("(grid overlay only draws at 4x+ zoom - press ']' a few times before 'g')")

	while not viewer.done:
		viewer.frame()
