#!/usr/bin/env python3
#vimrun! ./pyosgslug-text-on-path.py

# Python counterpart to examples/tmp/osgslug-tmp.textpath.cpp. Draws a stroked S-curve and
# CPU-bakes glyph placement/rotation along it through Canvas.text_on_path() - no vertex hook is
# involved because the glyphs already arrive as individually transformed layers.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

FONT_PATH = "font/UbuntuMono-R.ttf"
TEXT = "This  is  some  text  that  follows  a  path..."

def build_scene(w, h):
	# The Python bindings expose font loading on slughorn.Atlas rather than osgSlug::Font. Load
	# exactly the glyphs this example needs, then use the resulting metrics for text_on_path().
	a = slughorn.Atlas()
	metrics = slughorn.freetype.load_font_metrics(FONT_PATH)
	codepoints = sorted({ord(c) for c in TEXT})

	if not slughorn.freetype.load_font_glyphs(FONT_PATH, codepoints, a):
		raise RuntimeError(f"Couldn't load font: {FONT_PATH}")

	canvas = slughorn.canvas.Canvas(a)
	path = slughorn.canvas.Path()

	# S-curve: two opposing quadratic arcs meeting at the midpoint. Keep the same canvas-space
	# coordinates and stroke width as the C++ source so the two examples remain comparable.
	path.move_to(0.2, 0.1)
	path.quad_to(0.9, 0.1, 0.5, 0.5)
	path.quad_to(0.1, 0.9, 0.8, 0.9)

	canvas.add_path(path)
	canvas.stroke(0.001, slughorn.Color(1.0, 0.5, 0.0, 1.0))
	canvas.text_on_path(
		path,
		TEXT,
		0.05,
		0.0,
		slughorn.Color(1.0, 1.0, 1.0, 1.0),
		metrics
	)

	composite = canvas.finalize()

	a.build()
	atlas = osgSlug.Atlas.fromAtlas(a)

	sd = osgSlug.ShapeDrawable()

	sd.addCompositeShape(composite)
	atlas.children.append(sd)

	return atlas

if __name__ == "__main__":
	from pyosgslug_example import window_size, make_trackball

	W, H = window_size()
	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.2, 0.2, 0.3, 1.0)

	while not viewer.done:
		viewer.frame()
