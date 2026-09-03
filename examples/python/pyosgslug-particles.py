#!/usr/bin/env python3
#vimrun! ./pyosgslug-particles.py

# ASCII "glyph particles" demo -- the gl_InstanceID particle-fountain example requested on
# Slack. Loads the printable ASCII range of a font into an Atlas, then uses
# osgSlug.PathDrawable(PathMode.Stamp) with setShapeKeys() to stamp a DIFFERENT random glyph at
# each of N instanced quads in a single draw call (one small "shape table" SSBO of em-space
# bounds/band data per glyph, one "points" SSBO of per-particle position/angle/glyph-index --
# see setShapeKeys()'s doc comment in PathDrawable.hpp for why two buffers are needed).
#
# Phase 1, deliberately simple: physics (position/velocity/respawn) run on the CPU in
# ParticleField.__call__() below, an ordinary osg update callback attached directly to the
# PathDrawable -- exactly like SpinCallback in pyosgslug-simple.py. Every tick it rebuilds the
# points list and calls setPoints() again; PathDrawable's own live-update path (see its .hpp
# comment) re-uploads only the changed SSBO bytes, no recompile. GPU-side physics (baking
# spawn-time + velocity into the SSBO and letting the vertex shader compute position from
# osg_SimulationTime) is a natural Phase 2, not attempted here.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

import math
import random
import time

FONT_PATH = "font/UbuntuMono-R.ttf"

PARTICLE_COUNT = 300
FIELD_RADIUS = 380.0 # particles respawn once they drift this far from the origin
SPEED_RANGE = (60.0, 220.0) # world units/sec
GLYPH_HALF_WIDTH = 22.0 # PathDrawable.setHalfWidth() -- shared quad size for every particle
GLYPH_COLOR = osg.Vec4(1.0, 0.65, 0.15, 1.0) # amber -- Stamp mode has one color for all instances

class Particle:
	def __init__(self, glyph_count):
		self._glyph_count = glyph_count
		self.x = 0.0
		self.y = 0.0
		self.vx = 0.0
		self.vy = 0.0
		self.glyph = 0

		self.spawn(from_center=False)

	# from_center=False scatters the particle anywhere inside the field (used once, at startup,
	# so frame 0 already looks alive instead of a single point building up from the origin).
	# from_center=True is the steady-state respawn -- a fresh particle always starts at the
	# origin and flies outward, like an ember from a fountain.
	def spawn(self, from_center=True):
		angle = random.uniform(0.0, 2.0 * math.pi)
		speed = random.uniform(*SPEED_RANGE)
		radius = 0.0 if from_center else random.uniform(0.0, FIELD_RADIUS)

		self.x = math.cos(angle) * radius
		self.y = math.sin(angle) * radius
		self.vx = math.cos(angle) * speed
		self.vy = math.sin(angle) * speed
		self.glyph = random.randrange(self._glyph_count)

	def step(self, dt):
		self.x += self.vx * dt
		self.y += self.vy * dt

		if self.x * self.x + self.y * self.y > FIELD_RADIUS * FIELD_RADIUS:
			self.spawn()

	# points[i].w -- the shape-table index PATH_STAMP_TABLE_VERT looks each instance's glyph up
	# with. points[i].z -- rotation angle; facing the direction of travel makes the "shooting
	# outward" motion visible even on a still frame.
	def point(self):
		return osg.Vec4(self.x, self.y, math.atan2(self.vy, self.vx), float(self.glyph))

class ParticleField:
	def __init__(self, glyph_count, count=PARTICLE_COUNT):
		self.particles = [Particle(glyph_count) for _ in range(count)]
		self.last = time.time()

	def __call__(self, node, nv):
		now = time.time()
		dt = now - self.last
		self.last = now

		for p in self.particles:
			p.step(dt)

		node.setPoints([p.point() for p in self.particles])

		return True

def build_scene(w, h):
	# Same slughorn.Atlas -> osgSlug.Atlas.fromAtlas() dance as pyosgslug-simple.py --
	# osgSlug.Font can't be driven from pure Python yet (pybind11 holder-type constraint on
	# osgSlug::Atlas's multiple inheritance; see the "Real API gap found" note in
	# pyosgslug-simple.py's own history), so glyphs are loaded via the raw slughorn.freetype
	# module against a plain slughorn.Atlas first.
	a = slughorn.Atlas()
	config = slughorn.freetype.LoadConfig()

	if not slughorn.freetype.load_ascii_font(FONT_PATH, a, config):
		raise RuntimeError(f"Couldn't load font: {FONT_PATH}")

	a.build()

	atlas = osgSlug.Atlas.fromAtlas(a)

	# Printable ASCII minus space (32) -- an all-blank glyph is just a wasted instance. Built as
	# explicit slughorn.Key objects rather than raw ints: Key does have an implicit uint32_t
	# constructor (see pyosgslug-simple.py's ord("F") usage against a single Key-typed arg), but
	# this is the first place a *list* of them crosses into C++ (PathDrawable::setShapeKeys()
	# takes std::vector<slughorn::Key>) -- being explicit here sidesteps needing to confirm that
	# conversion also applies element-wise inside pybind11's std::vector caster.
	shape_keys = [slughorn.Key(c) for c in range(33, 127)]

	pd = osgSlug.PathDrawable(osgSlug.PathMode.Stamp)

	pd.setShapeKeys(shape_keys)
	pd.setColor(GLYPH_COLOR)
	pd.setHalfWidth(GLYPH_HALF_WIDTH)

	field = ParticleField(len(shape_keys))

	# Stage initial points BEFORE the drawable has an Atlas parent (compile() requires >= 2
	# points already present) -- see setPoints()'s doc comment in PathDrawable.hpp.
	pd.setPoints([p.point() for p in field.particles])
	pd.updateCallback = field

	# Triggers compile() (Atlas.addChild()'s auto-compile override) now that points/shapeKeys/
	# color/halfWidth are all staged. The update callback's first tick re-calls setPoints() on
	# the now-compiled drawable, which is what actually reveals the instances (setRevealCount()
	# defaults to 0 -- see PathDrawable.hpp's setPoints() comment).
	atlas.children.append(pd)

	return atlas

if __name__ == "__main__":
	from pyosgslug_example import window_size, make_trackball

	W, H = window_size()

	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.05, 0.05, 0.08, 1.0)

	while not viewer.done:
		viewer.frame()
