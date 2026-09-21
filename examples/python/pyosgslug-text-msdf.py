#!/usr/bin/env python3
#vimrun! ./pyosgslug-text-msdf.py

# Python counterpart to (the now largely superseded) examples/tmp/osgslug-tmp.msdf.cpp.
#
# That scratchpad file built a fully standalone MSDF-only osg.Program (its own VERTEX_MSDF/
# FRAGMENT_MSDF shaders, bypassing Slug's analytic Bezier coverage entirely) to compare Slug
# vs. MSDF rendering side by side. Since it was written, MSDF field access became first-class
# on the STANDARD Slug pipeline: osgSlug_SDF_Sample()/osgSlug_SDF_Gradient()/osgSlug_Effect_GlowSDF()
# (src/Atlas.shaders.cpp) are ordinary fragment_lib helpers any FragmentExtHook can call on a
# normal ShapeDrawable - no separate program, no SSBO/StateSet plumbing to hand-roll. This file
# only exercises that path. (The standalone-program approach is still a real gap for Python -
# Atlas.getShapeBuffer()/SHADER_TYPES/ProgramSpec aren't bound yet - revisit separately if a use
# case needs bypassing Slug coverage entirely, e.g. very large glyph counts.)
#
# EFFECTS below is a name -> hook-source registry, grown one entry at a time as more
# MSDF-driven effects get ported; --effect picks which one runs.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import slughorn

import argparse

from pyosgslug_example import make_trackball, window_size

FONT_PATH = "font/UbuntuMono-R.ttf"

# em-space SDF spread requested for every glyph's MSDF tile. Also used as each layer's `bleed`
# (extra content margin) below - bleed must stay <= this range, the tile's own safe margin
# (see Atlas::SDF::Tile in slughorn/slughorn.hpp), or the glow would try to sample
# past the tile's baked-in exterior padding.
MSDF_RANGE = 0.2

# ------------------------------------------------------------------------------------------------
# "glow" - osgSlug_Effect_GlowSDF() (Atlas.shaders.cpp) already implements the exterior-glow
# math (seam-hiding ramp + outer fade, both keyed off the baked signed distance); this hook just
# wires it into the FragmentExt slot, which is what lets it paint OUTSIDE the glyph's Slug
# coverage - contrast with a plain FragmentHook, which only ever sees fragments Slug already
# considers covered.
#
# `fragment_emcoord` (not `fragment`): FragmentExtHook never defines osgSlug_FragmentEmCoord
# itself, see Atlas.hpp's SHADER_FRAGMENT_EMCOORD comment. `fragment_lib` opts into the
# osgSlug_Effect_* prototypes.
#
# Shadow color is hardcoded black rather than reused from data.layerColor - a drop shadow reads
# as a shadow specifically because it's independent of the glyph's own fill color, not a tinted
# echo of it. Only alpha carries over, so the shadow still respects the layer's own opacity.
# ------------------------------------------------------------------------------------------------
GLOW_HOOK = """
#version 430 core

#pragma osgSlug fragment_emcoord,fragment_lib

vec4 osgSlug_FragmentExt(osgSlug_FragmentExtData data, out int blendMode) {
	vec4 shadowColor = vec4(1.0, 1.0, 0.4, data.layerColor.a);

	return osgSlug_Effect_GlowSDF(
		data.sd, data.sdfRange, shadowColor, data.effectParam, blendMode
	);
}
"""

EFFECTS = {
	"glow": GLOW_HOOK,
}

# Dump every baked tile in `atlas` (a slughorn.Atlas; fromAtlas() below only copies it into an
# osgSlug.Atlas, so `atlas` is still valid whether this runs before or after that) to
# <output_dir>/<key>.png, cropped out of the one shared SDF texture.
#
# Reads the atlas's already-baked float texture data directly (Atlas.sdf.texture - a flat
# memoryview, tiles located via Shape.sdf) rather than re-rendering, so this shows exactly what
# the GPU would sample. writeImageFile can't take that float data as-is - PNG (libpng) has no
# float pixel format, confirmed empirically: handing it a GL_FLOAT osg.Image fails outright
# (returns False, "write error"), no silent downcast - so the clamp/scale to GL_UNSIGNED_BYTE
# below is required, not optional polish.
def dump_sdf(atlas, output_dir):
	import array

	from pathlib import Path

	tex = atlas.sdf.texture

	if atlas.sdf.empty:
		print("No SDF tiles were registered; nothing to dump.")
		return

	channels = 3 if tex.format == "RGB32F" else 1
	floats = array.array("f")

	floats.frombytes(bytes(memoryview(tex.bytes)))

	out_dir = Path(output_dir)

	out_dir.mkdir(parents=True, exist_ok=True)

	count = 0

	for key, shape in atlas.get_shapes().items():
		tile = shape.sdf

		if tile is None:
			continue

		# The texture stores row 0 = bottom of the shape - the same convention as osg::Image, whose
		# PNG writer already emits the bottom row last, so rows are copied straight across.
		rgb = bytearray()

		for row in range(tile.h):
			for col in range(tile.w):
				base = ((tile.y + row) * tex.width + tile.x + col) * channels

				for c in range(3):
					v = floats[base + (c if channels == 3 else 0)]

					rgb.append(int(min(max(v, 0.0), 1.0) * 255.0 + 0.5))

		type_name = key.type.name if hasattr(key.type, "name") else str(key.type)
		name = f"U+{key.codepoint:04X}" if type_name == "Codepoint" else key.name.replace("/", "_")

		img = osg.Image()
		img.allocateImage(tile.w, tile.h, 1, GL_RGB, GL_UNSIGNED_BYTE)

		memoryview(img).cast("B")[:] = bytes(rgb)

		if not osgDB.writeImageFile(img, str(out_dir / f"{name}.png")):
			raise RuntimeError(f"osgDB.writeImageFile failed for SDF tile {name}")

		count += 1

	print(f"Saved {count} SDF tile(s) -> {out_dir}/")

def build_scene(text, font_path, effect, effect_param, dump_sdf_dir=None):
	a = slughorn.Atlas()
	config = slughorn.freetype.LoadConfig()

	if not slughorn.freetype.load_ascii_font(font_path, a, config):
		raise RuntimeError(f"Couldn't load font: {font_path}")

	canvas = slughorn.canvas.Canvas(a)

	# Every text() commit below also queues a tile request (Atlas.request_sdf()) for the
	# glyph it just registered - rendered in a.build() below, no separate registration loop.
	canvas.set_sdf(True, MSDF_RANGE)
	canvas.text(
		text, 0.25, 0.5, 0.4,
		slughorn.Color(1, 1, 1, 1),
		config.metrics,
		anchor_y=slughorn.canvas.TextAnchorY.BASELINE,
		align_x=slughorn.canvas.TextAlignX.CENTER,
	)

	comp = canvas.finalize()

	for layer in comp.layers:
		layer.bleed = MSDF_RANGE
		layer.effectParam = effect_param

	a.set_sdf(slughorn.SDF.Config(tile_size=256))
	a.build()

	if dump_sdf_dir is not None:
		dump_sdf(a, dump_sdf_dir)

		return None

	atlas = osgSlug.Atlas.fromAtlas(a)

	atlas.packTextures()

	sd = osgSlug.ShapeDrawable()

	sd.addCompositeShape(comp)
	sd.setHooks({osgSlug.Hook.FragmentExtHook: EFFECTS[effect]})

	# No Geode wrapper needed (osgSlug's Drawable IS an osg.Node in this OSG fork), and no manual
	# sd.compile() either - atlas is already Packed at this point, so Atlas.addChild's override
	# compiles any osgSlug.Drawable child automatically.
	atlas.children.append(sd)

	return atlas

if __name__ == "__main__":
	parser = argparse.ArgumentParser(description="osgSlug MSDF-driven text effects")
	parser.add_argument("font", nargs="?", default=FONT_PATH, help="TTF/OTF font path")
	parser.add_argument("text", nargs="?", default="slughorn", help="text to render")
	parser.add_argument("--effect", choices=sorted(EFFECTS), default="glow",
		help="which MSDF-driven effect to apply")
	parser.add_argument("--effect-param", type=float, default=0.0,
		help="per-effect tuning knob, em-units (0 = effect's own default; glow: outer fade width)")
	parser.add_argument("--dump-sdf", metavar="DIR",
		help="Dump every baked tile to <DIR>/<key>.png instead of opening a viewer window")
	args = parser.parse_args()

	if args.dump_sdf:
		build_scene(args.text, args.font, args.effect, args.effect_param, dump_sdf_dir=args.dump_sdf)

		raise SystemExit(0)

	W, H = window_size()
	viewer = osgViewer.Viewer()
	root = build_scene(args.text, args.font, args.effect, args.effect_param)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.0, 0.0, 0.0, 1.0)

	while not viewer.done:
		viewer.frame()
