#!/usr/bin/env python3
#vimrun! ./pyosgslug-pattern.py

# Fill patterns, baked vs GPU, side by side in one ShapeDrawable:
#
#   left  - BAKED: the hexagon tile repeated over a COLS x ROWS grid by the CPU into ONE filled
#           shape. Every repeat is real geometry in the Atlas - the reference.
#   right - GPU: one hexagon registered as a pattern tile (Canvas.define_pattern()) and a plain
#           COLS x ROWS square filled with it (Canvas.fill_pattern()). The Atlas holds one hexagon
#           and one square; the shader repeats the tile.
#
# The two halves must look identical. Both use the same tile cell (0, 0, 1, 1) and the pattern
# is anchored to canvas space, so the GPU grid lines up with the baked one, shifted by GPU_OFFSET.
#
# --tile picks the tile: "hexagon" (default) or "arrow". The hexagon is symmetric, so Reflect
# looks exactly like Repeat; the arrow points right and sits in the upper half of its cell, so a
# mirrored axis shows as alternating arrows. The baked half always stays Repeat/Repeat.
#
# Controls:
#   s        toggle scrolling the GPU pattern (a per-layer setPatternTransform(), no rebuild)
#   r        cycle the GPU pattern's repeat modes (a per-layer setPatternRepeat(), no rebuild)
#   F1-F7    osgSlug_debugMode

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import argparse
import math
import time

import osgSlug
import slughorn

COLS = 5
ROWS = 5
PERIOD = (1.0, 1.0) # tile-to-tile step, in the same units as the tile's own coordinates
HEX_RADIUS = 0.4 # center-to-vertex, < half the period so neighbors don't touch
GPU_OFFSET = COLS + 1.0 # canvas-space X of the GPU half
SCROLL_SPEED = 0.5 # cells per second

PATTERN_COLOR = slughorn.Color(1.0, 0.7, 0.0, 1.0)

Repeat = slughorn.PatternInfo.Repeat

# (repeatX, repeatY) combinations 'r' cycles through.
REPEAT_MODES = (
	(Repeat.Repeat, Repeat.Repeat),
	(Repeat.Reflect, Repeat.Repeat),
	(Repeat.Repeat, Repeat.Reflect),
	(Repeat.Reflect, Repeat.Reflect),
)

# Right-pointing arrow, relative to its cell center (unit cell). Raised above the center, so it is
# asymmetric on both axes.
ARROW = (
	(-0.35, 0.05), (0.1, 0.05), (0.1, -0.1), (0.35, 0.125), (0.1, 0.35), (0.1, 0.2), (-0.35, 0.2)
)

def hexagon(cvs, cx, cy):
	"""One tile: a pointy-top hexagon centered at (cx, cy), as a closed subpath of the canvas's
	current path."""
	for i in range(6):
		angle = math.pi / 6.0 + i * math.pi / 3.0
		x = cx + HEX_RADIUS * math.cos(angle)
		y = cy + HEX_RADIUS * math.sin(angle)

		if i == 0:
			cvs.move_to(x, y)

		else:
			cvs.line_to(x, y)

	cvs.close_path()

def arrow(cvs, cx, cy):
	"""One tile: ARROW around (cx, cy), as a closed subpath of the canvas's current path."""
	for i, (x, y) in enumerate(ARROW):
		if i == 0:
			cvs.move_to(cx + x, cy + y)

		else:
			cvs.line_to(cx + x, cy + y)

	cvs.close_path()

TILES = {"hexagon": hexagon, "arrow": arrow}

def bake_pattern(cvs, tile, cols, rows, period):
	"""Repeats tile(cvs, cx, cy) once per grid cell, centered in each cell, all into the canvas's
	current path - the caller fills it once."""
	px, py = period

	for row in range(rows):
		for col in range(cols):
			tile(cvs, (col + 0.5) * px, (row + 0.5) * py)

def build_scene(tile=hexagon):
	"""Returns (osgSlug.Atlas, ShapeDrawable, the GPU layer's base PatternInfo.transform)."""
	a = slughorn.Atlas()
	cvs = slughorn.canvas.Canvas(a)

	# Layer 0: baked.
	cvs.begin_path()

	bake_pattern(cvs, tile, COLS, ROWS, PERIOD)

	cvs.fill(PATTERN_COLOR)

	# The tile: one tile centered in the cell (0, 0) - (1, 1). define_pattern() registers it as a
	# shape but pushes no layer.
	cvs.begin_path()

	tile(cvs, 0.5 * PERIOD[0], 0.5 * PERIOD[1])

	handle = cvs.define_pattern(0.0, 0.0, PERIOD[0], PERIOD[1])

	# Layer 1: GPU - the region is just a square the size of the baked grid.
	cvs.begin_path()
	cvs.rect(GPU_OFFSET, 0.0, COLS * PERIOD[0], ROWS * PERIOD[1])
	cvs.fill_pattern(handle, PATTERN_COLOR)

	cs = cvs.finalize()

	a.build()

	# fromAtlas() copies `a` into a new osgSlug.Atlas and packs it - no packTextures() needed.
	atlas = osgSlug.Atlas.fromAtlas(a)

	sd = osgSlug.ShapeDrawable()

	sd.addCompositeShape(cs)

	# atlas is already packed, so appending compiles sd automatically.
	atlas.children.append(sd)

	return atlas, sd, a.get_patterns()[0].transform

# 's' toggles scrolling: each frame, the GPU layer's pattern transform is the base one followed by
# a translation in tile space. 'r' cycles REPEAT_MODES. Both write straight into that layer's
# paint slot.
class PatternHandler(osgGA.GUIEventHandler):
	def __init__(self, sd, base):
		super().__init__()

		self.sd = sd
		self.base = base
		self.scrolling = False
		self.t0 = time.time()
		self.mode = 0

	def handle(self, ea, aa):
		if ea.type == osgGA.GUIEventAdapter.KEYDOWN and ea.key == ord("r"):
			self.mode = (self.mode + 1) % len(REPEAT_MODES)

			rx, ry = REPEAT_MODES[self.mode]

			self.sd.layers[1].setPatternRepeat(rx, ry)

			print(f"repeat x={rx.name} y={ry.name}")

			return True

		if ea.type == osgGA.GUIEventAdapter.KEYDOWN and ea.key == ord("s"):
			self.scrolling = not self.scrolling
			self.t0 = time.time()

			if not self.scrolling:
				self.sd.layers[1].setPatternTransform(self.base)

			print(f"scrolling {'on' if self.scrolling else 'off'}")

			return True

		if ea.type == osgGA.GUIEventAdapter.FRAME and self.scrolling:
			dx = (time.time() - self.t0) * SCROLL_SPEED * PERIOD[0]

			self.sd.layers[1].setPatternTransform(slughorn.Matrix.translate(dx, 0.0) * self.base)

		return False

if __name__ == "__main__":
	from pyosgslug_example import DebugModeHandler, configure_viewer, parse_args

	pattern_parser = argparse.ArgumentParser(add_help=False)

	pattern_parser.add_argument("--tile", choices=list(TILES), default="hexagon")

	pattern_args, rest = pattern_parser.parse_known_args()

	args = parse_args(
		description="Fill patterns: CPU-baked (left) vs GPU fill_pattern() (right)",
		argv=rest
	)

	atlas, sd, base = build_scene(TILES[pattern_args.tile])

	viewer = osgViewer.Viewer()
	viewer.sceneData = atlas
	viewer.camera.clearColor = osg.Vec4(0.05, 0.05, 0.08, 1.0)
	viewer.eventHandlers.append(DebugModeHandler(viewer.camera.stateSet))
	viewer.eventHandlers.append(PatternHandler(sd, base))

	configure_viewer(viewer, atlas, args)

	while not viewer.done:
		viewer.frame()
