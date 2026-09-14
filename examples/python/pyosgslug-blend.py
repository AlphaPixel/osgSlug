#!/usr/bin/env python3
#vimrun! ./pyosgslug-blend.py

# BlendMode playground: three overlapping filled circles (a Venn-diagram arrangement), each with
# independently controllable RGBA + BlendMode via an ImGui panel, plus a live clear-color control.
# Companion to pyosgslug-text-decal.py's --blend-mode flag - built to develop intuition for how
# slughorn.BlendMode actually behaves before tackling the harder composite-shape/decal work (see
# slughorn's ai/context-todo-decal.md). Deliberately NOT decal-based: plain osgSlug.ShapeDrawable,
# no wall texture, no erosion, no --emoji - just three shapes and blend modes, isolated from
# everything else this session touched.
#
# Color/alpha changes are live (ShapeDrawable.layers[i].color, no rebuild needed). BlendMode
# changes are NOT live-updatable - it's baked into RenderGroup splitting at compile() time (see
# ShapeDrawable.cpp's drawImplementation()), so switching a circle's blend mode rebuilds the whole
# scene from scratch instead. Cheap here (3 circles) - and a hands-on illustration of exactly the
# static-atlas-vs-live-data boundary the rest of this session's decal work keeps running into.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn
import osgx

from pyosgslug_example import make_trackball, window_size

# The 11 Photoshop-style modes (slughorn.hpp's BlendMode 20-30) plus SrcOver as "Normal" - the
# actual "how does this paint interact with what's under it" modes. Deliberately excludes the pure
# Porter-Duff compositing operators (Src/Dst/*In/*Out/*Atop/Xor/Clear) - those are masking algebra,
# not blend intuition, and would roughly double the radio button count for little pedagogical value
# in a playground meant to build intuition, not exhaustively cover the enum.
BLEND_MODES = {
	"Normal": slughorn.BlendMode.SrcOver,
	"Multiply": slughorn.BlendMode.Multiply,
	"Screen": slughorn.BlendMode.Screen,
	"Overlay": slughorn.BlendMode.Overlay,
	"Darken": slughorn.BlendMode.Darken,
	"Lighten": slughorn.BlendMode.Lighten,
	"ColorDodge": slughorn.BlendMode.ColorDodge,
	"ColorBurn": slughorn.BlendMode.ColorBurn,
	"HardLight": slughorn.BlendMode.HardLight,
	"SoftLight": slughorn.BlendMode.SoftLight,
	"Difference": slughorn.BlendMode.Difference,
	"Exclusion": slughorn.BlendMode.Exclusion,
}
BLEND_MODE_NAMES = list(BLEND_MODES)

# Classic Venn-diagram arrangement: three same-size circles, each pair overlapping by about a
# third of the radius. Canvas coordinates only - make_trackball() auto-frames whatever comes out,
# and slughorn's canvas convention is already Y-up to match it.
CIRCLE_RADIUS = 0.45
CIRCLE_CENTERS = (
	(0.55, 0.75),
	(0.95, 0.75),
	(0.75, 0.42),
)

def build_content(state):
	source_atlas = slughorn.Atlas()
	canvas = slughorn.canvas.Canvas(source_atlas)

	for cx, cy in CIRCLE_CENTERS:
		canvas.begin_path()
		canvas.circle(cx, cy, CIRCLE_RADIUS)
		# fill()'s color argument doesn't matter here - overwritten per-layer below from state,
		# same as pyosgslug-text-decal.py's build_decal_atlas() pattern (mutate comp.layers after
		# finalize(), before atlas.build()).
		canvas.fill(slughorn.Color(1, 1, 1, 1))

	comp = canvas.finalize()

	for layer, circle in zip(comp.layers, state):
		layer.color = slughorn.Color(*circle["color"])
		layer.blendMode = BLEND_MODES[circle["blend_mode"]]

	source_atlas.build()

	atlas = osgSlug.Atlas.fromAtlas(source_atlas)

	atlas.packTextures()

	drawable = osgSlug.ShapeDrawable()

	drawable.addCompositeShape(comp)
	atlas.children.append(drawable)

	return atlas, drawable

def build_scene(w, h):
	state = [
		{"color": [1.0, 0.15, 0.15, 0.75], "blend_mode": "Normal"},
		{"color": [0.15, 0.85, 0.2, 0.75], "blend_mode": "Normal"},
		{"color": [0.15, 0.35, 1.0, 0.75], "blend_mode": "Normal"},
	]

	root = osg.Group(name="blend-playground")
	content, drawable = build_content(state)

	root.children.append(content)

	return root, state, drawable

def make_panel_fn(viewer, root, state, drawable):
	# drawable is captured mutably via a one-element list ("box") rather than a plain closure
	# variable - the BlendMode rebuild path below needs to REBIND it to a freshly-built
	# ShapeDrawable, and a bare `nonlocal` reassignment from inside the returned closure would
	# work too, but this keeps the box explicit at the one call site that actually mutates it.
	drawable_box = [drawable]
	clear_color = [0.1, 0.1, 0.1]

	def draw_panel(render_info):
		rebuild_needed = False

		for i, circle in enumerate(state):
			osgx.imgui.text(f"Circle {i}")

			changed, r, g, b = osgx.imgui.color_edit3(f"Color##{i}", *circle["color"][:3])

			if changed:
				circle["color"][0:3] = [r, g, b]
				drawable_box[0].layers[i].color = slughorn.Color(*circle["color"])

			changed, a = osgx.imgui.slider_float(f"Alpha##{i}", circle["color"][3], 0.0, 1.0)

			if changed:
				circle["color"][3] = a
				drawable_box[0].layers[i].color = slughorn.Color(*circle["color"])

			changed, idx = osgx.imgui.combo(
				f"Blend Mode##{i}",
				BLEND_MODE_NAMES.index(circle["blend_mode"]),
				BLEND_MODE_NAMES
			)

			if changed:
				circle["blend_mode"] = BLEND_MODE_NAMES[idx]
				rebuild_needed = True

			osgx.imgui.separator()

		osgx.imgui.text("Background")

		changed, r, g, b = osgx.imgui.color_edit3("Clear Color", *clear_color)

		if changed:
			clear_color[0:3] = [r, g, b]
			viewer.camera.clearColor = osg.Vec4(r, g, b, 1.0)

		if rebuild_needed:
			content, new_drawable = build_content(state)

			root.children.clear()
			root.children.append(content)
			drawable_box[0] = new_drawable

	return draw_panel

if __name__ == "__main__":
	W, H = window_size()
	viewer = osgViewer.Viewer()
	root, state, drawable = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.1, 0.1, 0.1, 1.0)

	# Widget pushes itself onto viewer's event handler list automatically - see osgx-imgui.cpp's
	# example for the same pattern in C++. OSG_THREADING=SingleThreaded is already the default
	# pyosgslug_example.py sets (Dear ImGui's single global context isn't safe to touch from more
	# than one OSG draw thread).
	gui = osgx.imgui.Widget(viewer)

	gui.addSection("Blend Playground", make_panel_fn(viewer, root, state, drawable))

	while not viewer.done:
		viewer.frame()
