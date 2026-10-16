#!/usr/bin/env python3
#vimrun! ./pyosgslug-pen.py

# "Funsies" warm-up experiment - riffs on Glenn's Slack ask for "a vector-upload + CS coverage
# generator" to replace CPU Blend2D raster+upload for texture splatting (see slughorn memory
# project_glenn_texture_splatting.md for the full conversation). A shape used as a literal GPU
# "pen": PathDrawable(PathMode.Stamp) stamps it wherever the cursor moves. Terminology note: this
# codebase already used "splatting" for something else (see project_splatting.md) - Jeremy calls
# this technique "Stamp" internally; don't conflate the two.
#
# Mouse tracking goes through osgx.CursorState/CursorHandler/CursorCallback - the real, current
# primitive for this (see osgx/examples/osgx-picking.cpp's own smoke test, the reference this
# follows) - plus osgx.unprojectToPlane() against the z=0 world plane. Deliberately NOT
# pyosgslug-cone-widget.py's hand-rolled make_unprojector()/ndc_from_event(): that predates
# osgx.Projection.hpp, is exactly what unprojectToPlane() now replaces, and is flagged broken -
# don't copy its pattern.
#
# The pen keeps only its most recent MAX_STAMPS positions live - a FIFO ring buffer (Jeremy's own
# "max N SSBO slots" idea), a collections.deque(maxlen=...) rebuilt into PathDrawable.setPoints()
# every time the cursor has moved STAMP_SPACING world units from the last stamp, so the trail
# doesn't stamp every single pixel of travel.
#
# FUTURE IMPROVEMENT - known limitation, confirmed 2026-10-13 first run: the trail goes visibly
# dotted/gappy at higher cursor speed instead of staying a solid overlapping ribbon. Root cause:
# PenTrail.offer() is only ever called once per rendered frame (from CursorCallback, an update-
# traversal callback), not once per real OS mouse event. CursorState itself updates on every real
# MOVE/DRAG event losslessly, but a fast flick can generate several real events between two
# frames, and this code only ever sees the straight-line hop between the frame-start and frame-end
# positions - if that hop exceeds STAMP_SPACING, exactly one stamp lands at the far end, leaving
# the true distance traveled unstamped. Slower movement keeps consecutive frame samples close
# together and looks continuous; faster movement doesn't.
#   - Basic fix (bounded, not hard): react to every raw MOVE/DRAG event directly via a plain
#     GUIEventHandler instead of CursorCallback's once-per-frame sampling, then linearly
#     interpolate between the previous raw point and the new one and drop stamps at fixed
#     STAMP_SPACING intervals ALONG that segment - standard arc-length resampling of a polyline.
#   - Harder fix (real design work, not a quick follow-up): smooth a short rolling window of
#     recent raw points (Catmull-Rom or similar) before resampling, and vary stamp density/size by
#     velocity, so "wonky"/jittery input doesn't just resample into a jagged zigzag - this is
#     genuine paint/ink-engine territory.
#   - Reuse angle: slughorn already has arc-length path sampling (slughorn::canvas::Path::sample(u)
#     - see examples/tmp/osgslug-tmp.map.cpp, which walks a pre-baked SVG path with it), just not
#     wired up for a live, growing, raw-input point stream. Worth starting from if this ever
#     becomes real scoped work rather than a funsies detour.

import collections
import math

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import osgx
import slughorn

MAX_STAMPS = 64 # FIFO ring buffer size - oldest stamp drops as a new one is pushed
STAMP_SPACING = 8.0 # world units of cursor travel before a new stamp is pushed
STAMP_HALF_WIDTH = 14.0 # PathDrawable.setHalfWidth()
INITIAL_BOUND = 400.0 # flat XY half-extent - avoids a degenerate bound before the first stamp

PEN_COLOR = osg.Vec4(0.3, 0.75, 1.0, 0.85)

def build_pen_shape(atlas_source):
	"""Bakes a small leaf/teardrop shape into `atlas_source`, authored pointing "up" (+Y local,
	Canvas's Y-up convention) - deliberately NOT circular, so Stamp mode's per-point rotation
	(points[i].z) is visible in the trail instead of invisible on a rotationally-symmetric
	shape. Returns the slughorn.Key setShapeKeys() needs; the Layer canvas.fill() also returns
	is unused - PathDrawable.setColor() below, not the baked layer color, drives Stamp mode's
	actual render color (see pyosgslug-particles.py's own setColor() usage)."""
	canvas = slughorn.canvas.Canvas(atlas_source)
	origin = slughorn.ShapeInfo.Origin(0.0, 0.0)
	key = slughorn.Key("pen")

	canvas.begin_path()
	canvas.move_to(0.0, 1.0)
	canvas.line_to(-0.55, -0.5)
	canvas.line_to(0.0, -0.15)
	canvas.line_to(0.55, -0.5)
	canvas.close_path()
	canvas.fill(slughorn.Color(1.0, 1.0, 1.0, 1.0), 1.0, key, origin)

	return key

class PenTrail:
	"""FIFO ring buffer of the pen's last `max_stamps` world-space stamps."""
	def __init__(self, max_stamps):
		self._points = collections.deque(maxlen=max_stamps)
		self._last = None

	def offer(self, pos):
		"""pos: osg.Vec3d, a world-space point on the z=0 plane. Pushes a new stamp only once
		the cursor has moved STAMP_SPACING world units since the last one. Returns True if the
		trail actually changed (caller should re-upload via setPoints())."""
		if self._last is not None:
			dx = pos.x - self._last.x
			dy = pos.y - self._last.y

			if dx * dx + dy * dy < STAMP_SPACING * STAMP_SPACING:
				return False

			# atan2(dy, dx) measures from +X; the shape is baked pointing +Y, so subtract a
			# quarter turn to align its baked "forward" with the real direction of travel.
			heading = math.atan2(dy, dx) - math.pi * 0.5

		else:
			heading = 0.0

		self._points.append(osg.Vec4(pos.x, pos.y, heading, 0.0))
		self._last = pos

		return True

	def points(self):
		return list(self._points)

class FreezeHandler(osgGA.GUIEventHandler):
	"""'f' toggles `state["frozen"]`, gating on_cursor_move() below so the trail stops changing -
	meant to be combined with pyosgslug_example's existing F12 ManipulatorToggleHandler (wired by
	configure_viewer() already) to swap from the top-down Ortho2DManipulator to the 3D trackball
	and zoom freely into the frozen stamps. The whole point: this is vector SDF content, not a
	baked bitmap splat texture - the stamps stay perfectly crisp at any zoom level, which is
	exactly the thing a screenshot can't show off but live zooming can."""
	def __init__(self, state):
		super().__init__()

		self.state = state

	def handle(self, ea, aa):
		if ea.handled or ea.type != osgGA.GUIEventAdapter.KEYDOWN:
			return False

		if ea.key not in (ord("f"), ord("F")):
			return False

		self.state["frozen"] = not self.state["frozen"]

		print(
			"Pen FROZEN - press F12 for the trackball and zoom in to inspect the stamps"
			if self.state["frozen"] else
			"Pen LIVE"
		)

		return True

def build_scene():
	atlas_source = slughorn.Atlas()
	pen_key = build_pen_shape(atlas_source)

	atlas_source.build()

	atlas = osgSlug.Atlas(atlas_source)

	atlas.packTextures()

	pd = osgSlug.PathDrawable(osgSlug.PathMode.Stamp)

	# setShapeKeys() with a single-entry table, not setShapeKey() - PATH_STAMP_VERT (what
	# setShapeKey() links) has a known pre-existing aspect-ratio bug (flat square footprint vs.
	# anisotropic rate metadata); PATH_STAMP_TABLE_VERT (setShapeKeys()) doesn't. See slughorn
	# memory project_stamp_fwidth_aa_bug.md.
	pd.setShapeKeys([pen_key])
	pd.setColor(PEN_COLOR)
	pd.setHalfWidth(STAMP_HALF_WIDTH)

	trail = PenTrail(MAX_STAMPS)

	# compile() requires >= 2 points already present - duplicate the origin once, same trick
	# pyosgslug-pathdrawable.py uses for its own single-glyph case. The real trail replaces this
	# wholesale on the first actual cursor move.
	seed = osg.Vec4(0.0, 0.0, 0.0, 0.0)

	pd.setPoints([seed, seed])

	atlas.children.append(pd) # triggers compile()

	# compile() derives a bound from whatever points are staged at the time - just the seed
	# point here, a degenerate zero-size box. Widen it by hand so Ortho2DManipulator's home
	# framing isn't zoomed into nothing before the first real stamp lands.
	pd.initialBound = osg.BoundingBox(
		-INITIAL_BOUND, -INITIAL_BOUND, -1.0, INITIAL_BOUND, INITIAL_BOUND, 1.0
	)

	return atlas, pd, trail

if __name__ == "__main__":
	from pyosgslug_example import configure_viewer, parse_args

	args = parse_args(description="Mouse-driven PathDrawable(Stamp) pen - a shape that follows the cursor")
	atlas, pd, trail = build_scene()

	viewer = osgViewer.Viewer()
	viewer.sceneData = atlas
	viewer.camera.clearColor = osg.Vec4(0.05, 0.05, 0.08, 1.0)

	configure_viewer(viewer, atlas, args) # defaults to Ortho2DManipulator - matches the XY pen plane

	cursor_state = osgx.CursorState()
	plane = osg.Plane(osg.Vec3d(0.0, 0.0, 1.0), 0.0) # z=0, the plane the pen draws on
	state = {"frozen": False}

	def on_cursor_move(x, y):
		if state["frozen"]:
			return

		if not cursor_state.inWindow:
			return

		viewport = viewer.camera.viewport

		if viewport is None:
			return

		hit = osgx.unprojectToPlane(
			viewer.camera, viewport, x, y, cursor_state.yIncreasingDownwards, plane
		)

		if hit is None:
			return

		if trail.offer(hit):
			pd.setPoints(trail.points())

	viewer.eventHandlers.append(osgx.CursorHandler(cursor_state))
	viewer.eventHandlers.append(FreezeHandler(state))
	atlas.updateCallback = osgx.CursorCallback(cursor_state, on_cursor_move)

	print("Move the mouse to draw with the pen - trail keeps only the most recent", MAX_STAMPS, "stamps")
	print("'f' freezes the trail - press F12 to switch to the trackball and zoom in to inspect it")

	while not viewer.done:
		viewer.frame()
