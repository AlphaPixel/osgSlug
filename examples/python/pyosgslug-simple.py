#!/usr/bin/env python3
#vimrun! ./pyosgslug-simple.py

# Python counterpart to examples/osgslug-simple.cpp. Mirrors that example's atlas/font/text/
# shape/spin setup. Run from the BUILD dir so the relative FONT_PATH below resolves (font/ is
# copied there by CMake).
#
# build_scene(w, h) is the same runner contract OpenSceneGraph.py's own sandbox examples use
# (see pyosg-voxelize2d.py) -- a pure scene-assembly entrypoint, no viewer/window side effects,
# so this file is ready to be picked up by an osgSlug.examples runner later without a rewrite.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

import time

FONT_PATH = "font/UbuntuMono-R.ttf"

# No osg.NodeCallback subclass needed -- any callable (plain class with __call__(node, nv), or
# even a lambda) works as an updateCallback, same as pyosg-voxelize2d.py's own SpinCallback.
# Rotates the MatrixTransform about its bounding-sphere center, then slides the whole thing off
# to the side (+726 on X) so it doesn't sit on top of the text -- mirrors SpinCallback in
# osgslug-simple.cpp, but keyed off wall-clock time like every other OSG.py example instead of
# frameStamp.simulationTime (no example in this codebase reaches for the latter from inside a
# callback -- time.time() is the established idiom here).
class SpinCallback:
	def __init__(self):
		self.center = None
		self.t0 = time.time()

	def __call__(self, node, nv):
		if self.center is None:
			bound = node.bound
			self.center = osg.Vec3d(bound.center) if bound.valid() else osg.Vec3d(0, 0, 0)

		angle = time.time() - self.t0 # radians/sec

		R = (
			osg.Matrix.translate(-self.center) *
			osg.Matrix.rotate(angle, osg.Vec3(0.0, 0.0, 1.0)) *
			osg.Matrix.translate(self.center)
		)

		R *= osg.Matrix.translate(726.0, 0.0, 1.0)

		node.matrix = R

		return True

def build_scene(w, h):
	# The authoring-side atlas: slughorn.Atlas owns add_shape()/build(), neither of which is
	# exposed on osgSlug.Atlas (its Python binding only lists osg.Group as a base, so the
	# slughorn.Atlas methods osgSlug::Atlas also inherits in C++ aren't reachable here). Build
	# and pack this one first, then hand it to osgSlug.Atlas.fromAtlas() below.
	a = slughorn.Atlas()

	# Manually inject raw quadratic curves as codepoint 'F' -- the exact experiment that started
	# slughorn/osgSlug. A triangle with one curved side, split into a 2x5 (arbitrary) band
	# arrangement. ord("F") matters here, NOT the string "F" -- a str key creates a *named* key,
	# which wouldn't collide with (and override) the font's own 'F' glyph below.
	tri = slughorn.ShapeInfo()

	tri.num_bands_x = 2
	tri.num_bands_y = 5
	tri.curves = [
		slughorn.Curve(0.0, 0.0, 0.5, 0.35, 1.0, 0.0), # bottom
		slughorn.Curve(1.0, 0.0, 0.75, 0.35, 0.5, 0.7), # right
		slughorn.Curve(0.5, 0.7, 0.25, 0.35, 0.0, 0.0), # left
	]

	a.add_shape(ord("F"), tri)

	# Feed the font in; since 'F' is already claimed above, load_ascii_font leaves it alone.
	config = slughorn.freetype.LoadConfig()

	if not slughorn.freetype.load_ascii_font(FONT_PATH, a, config):
		raise RuntimeError(f"Couldn't load font: {FONT_PATH}")

	a.build()

	# fromAtlas() copies `a` into a new osgSlug.Atlas and packs it -- build() must already have
	# been called, packTextures() has not.
	atlas = osgSlug.Atlas.fromAtlas(a)

	text = osgSlug.Text(atlas, 100.0)

	text.fontMetrics = config.metrics

	text.addText("Line 0: ABCDEFGabcdefg\n", slughorn.Color(1, 0.5, 0, 1))
	text.addText("Line 1: 1234568790\n")
	text.addText("You can also")
	text.addText(" mix", slughorn.Color(0.5, 0.7, 0.9, 1))
	text.addText(" colors ", slughorn.Color(0.7, 0.9, 0.5, 1))
	text.addText("in the same line!")
	text.compile()

	sd = osgSlug.ShapeDrawable()

	sd.addLayer(slughorn.Layer(ord("F"), slughorn.Color(1, 1, 0, 0.5), scale=100.0))

	mt = osg.MatrixTransform()

	mt.children.append(sd)
	mt.updateCallback = SpinCallback()

	# Append `mt`, not `sd` directly -- no Geode wrapper needed either way (osgSlug's Drawable IS
	# an osg.Node in this OSG fork), and no manual sd.compile() needed: `atlas` is already Packed
	# at this point, so Atlas.addChild's override walks the newly-added subtree and compiles any
	# osgSlug.Drawable it finds automatically, nested MatrixTransform and all. Calling
	# sd.compile() before this line would just print "no Atlas parent" and no-op -- see
	# pyosgslug-template.py for that exact mistake.
	atlas.children.append(mt)

	# Text isn't an osgSlug.Drawable, so the auto-compile hook above doesn't apply to it --
	# compile() must be called explicitly regardless of parenting order.
	atlas.children.append(text)

	return atlas

if __name__ == "__main__":
	from pyosgslug_example import window_size, make_trackball

	W, H = window_size()

	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.2, 0.2, 0.2, 1.0)

	while not viewer.done:
		viewer.frame()
