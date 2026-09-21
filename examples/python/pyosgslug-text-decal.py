#!/usr/bin/env python3
#vimrun! ./pyosgslug-text-decal.py

# Real successor to examples/tmp/osgslug-tmp.worn.cpp - see ai/context-todo-decal.md (slughorn
# repo) Open Work item 6: "Replace osgslug-tmp.worn.cpp's flat overlay with the text genuinely
# decaled onto the cobblestone quad... the natural regression/acceptance test." worn.cpp never
# actually decaled anything - it was a flat, depth-test-off 2D quad hovering in front of a
# textured wall, alpha-blended with zero lighting response. This uses the REAL DecalDrawable
# mechanism (DecalDrawable.addPlanarDecal(), proven in osgslug-tmp.decalcube.cpp) to place text
# genuinely IN the 3D scene, self-occluding and correctly parallaxing as the camera moves.
#
# Design history (two earlier attempts, both wrong, worth keeping so a future session doesn't
# retry them): attempt 1 made the paint sample the WALL's own normal/ORM textures directly - only
# worked because the wall quad's UVs were hand-matched to the decal's own tangent frame, breaks on
# any real mesh. Attempt 2 built a from-scratch Cook-Torrance relighting model plus a fake
# MSDF-bevel normal for the paint, lit by the same point lights as the wall - real effort, but it
# read as "gemstone/sticker", not paint, and the user's own verdict was that the ORIGINAL flat,
# non-decaled worn.cpp hack still looked more convincing despite being the fake one.
#
# The actual fix (this version): slughorn already has real Photoshop-style blend modes
# (slughorn.BlendMode - Multiply/Overlay/SoftLight/etc, GPU-native via
# GL_KHR_blend_equation_advanced, wired end-to-end through DecalDrawable::compile()'s own
# per-layer render-group splitting - see ShapeDrawable.cpp's applyBlendMode()). Setting
# layer.blendMode instead of the default SrcOver makes the GPU's blend hardware combine the
# paint's color with whatever is ALREADY IN THE FRAMEBUFFER at that pixel - the wall, already
# fully lit - using the real Multiply/Overlay formula. That's the actual Photoshop mechanism (a
# Multiply layer reads the pixel value already there), and it's not privileged access to the
# wall's material the way attempt 1 was: it reads the wall's own FINAL RENDERED color, the exact
# same thing a real deferred decal system (osgEarth::Decals) reconstructs from the depth/color
# buffer. No relighting model needed for the paint at all - it can be flat, and the wall's own
# light/shadow variation shows through it for free.
#
# Erosion effect swap: DECAL_FRAGMENT_HOOK below ports worn.cpp's effectId 4 ("MSDF-gated stencil
# chips"), not effectId 1 (uniform Voronoi cells + a crack overlay, what an earlier version of
# this file had - reads more like cracked tile/glass). effectId 4 is what worn.cpp's own main()
# actually used by default, and is a materially richer technique: MSDF-distance-gated so chips
# concentrate at the edge and fade toward center, modulated by a global noise+Voronoi-epicenter
# "wear map" so damage is UNEVEN across the whole word, using variable-radius Voronoi cells so
# bites are irregular sizes - that combination is what reads as "chipping away" rather than a
# repeating pattern.

import argparse
import os

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

from OpenSceneGraph.examples import polyhaven

import osgSlug
import slughorn
import osgx

from pyosgslug_example import make_trackball, window_size

FONT_PATH = "font/Silkscreen-Bold.ttf"

# Em-space SDF spread for the text's baked tiles - effectId 4's chip erosion (DECAL_FRAGMENT_HOOK
# below) is tile-gated (concentrates at the glyph boundary, per data.sd), so this needs to be
# enabled again (it was dropped when the BlendMode pivot removed the earlier MSDF-bevel normal).
MSDF_RANGE = 0.15

# The wall quad's own world footprint (a plain textured rectangle, its own UV span 0..1 handled
# by osg.createTexturedQuadGeometry() below - nothing decal-specific here).
WALL_ORIGIN = osg.Vec3(0.0, 0.0, 0.0)
WALL_WIDTH = 4.0
WALL_HEIGHT = 2.5
WALL_TANGENT_U = osg.Vec3(WALL_WIDTH, 0.0, 0.0)
WALL_TANGENT_V = osg.Vec3(0.0, WALL_HEIGHT, 0.0)

# The decal's OWN tangent frame is deliberately ISOTROPIC (equal width/height) even though the
# wall itself isn't - canvas.text() authors glyphs in true, undistorted proportions, and mapping
# that onto an anisotropic quad (WALL_WIDTH != WALL_HEIGHT) would stretch every letter sideways.
# Sharing WALL_ORIGIN keeps it centered on the wall; its own size is independent of the wall's.
DECAL_SIZE = 2.6
DECAL_TANGENT_U = osg.Vec3(DECAL_SIZE, 0.0, 0.0)
DECAL_TANGENT_V = osg.Vec3(0.0, DECAL_SIZE, 0.0)

# Same epsilon decalcube.cpp's own SURFACE_PUSH uses, pushed along the wall's own flat normal -
# avoids z-fighting between the decal quad and the wall's coincident surface.
SURFACE_PUSH = 0.002

# Reused verbatim from OpenSceneGraph.examples.polyhaven's own build_texture_root() - the WALL's
# own light rig only now (the paint no longer does any lighting of its own - see the module
# docstring above for why).
LIGHT_POS = (osg.Vec3(-3.0, 4.0, 5.0), osg.Vec3(4.0, -2.0, 2.0))
LIGHT_COLOR = (osg.Vec3(7.0, 6.6, 5.6), osg.Vec3(1.5, 1.8, 4.0))
LIGHT_RADIUS = (28.0, 26.0)
LIGHT_SOURCE_RADIUS = (1.0, 1.0)
SKY_COLOR = osg.Vec3(0.18, 0.20, 0.25)
GROUND_COLOR = osg.Vec3(0.05, 0.04, 0.03)

# Every slughorn.BlendMode value that's actually MEANINGFUL for a decal on an opaque surface
# (see the module docstring above) - deliberately NOT the full 23-value enum. A decal's dst is,
# by construction, an already-fully-resolved opaque render (dst_alpha == 1.0 everywhere the
# decal can legally sit - that's what "decal on a surface" means, not an accident of how the
# wall shader happens to be written). Porter-Duff's *In/*Out/*Atop/Xor family exists specifically
# to mask two independently-shaped, possibly-transparent layers against each other via
# dst_alpha; against a uniformly-opaque dst, half of that family collapses to duplicates of a
# simpler mode and the other half becomes useless:
#   SrcIn  -> identical to Src        SrcAtop -> identical to Normal (SrcOver)
#   DstOver -> identical to Dst       DstAtop -> identical to DstIn
#   Xor    -> identical to DstOut     SrcOut  -> always fully invisible
# Omitted here for exactly that reason. The 11 Photoshop-style modes never mask by dst's alpha
# (they only read dst's COLOR), so all of them stay meaningful regardless.
BLEND_MODES = {
	# SrcOver is slughorn's actual default (slughorn.hpp:525, "must stay 0 forever") - plain alpha
	# compositing, no per-pixel dst readback. Also --emoji's escape hatch from the sibling-layer
	# bleed (see ai/context-todo-decal.md's "NEXT UP" section): with SrcOver, a composite shape's
	# own overlapping layers occlude each other normally again (opaque paint replaces, doesn't
	# mix), at the cost of the wall's highlights/shadows no longer showing through the paint at
	# all - the exact tradeoff Overlay was chosen to avoid for TEXT.
	"normal": slughorn.BlendMode.SrcOver,
	# Replaces the wall outright, ignoring its own alpha for blending (only depth/stencil apply) -
	# premultiplied AA edges show as dark halos rather than blending smoothly into the wall.
	"src": slughorn.BlendMode.Src,
	"dst": slughorn.BlendMode.Dst,
	"clear": slughorn.BlendMode.Clear,
	# Dims the wall by the paint's OWN alpha shape - a "translucent stamp" look.
	"dst-in": slughorn.BlendMode.DstIn,
	# Erases the wall to black by the paint's OWN alpha shape - a "burn/cutout" look.
	"dst-out": slughorn.BlendMode.DstOut,
	"multiply": slughorn.BlendMode.Multiply,
	"screen": slughorn.BlendMode.Screen,
	"overlay": slughorn.BlendMode.Overlay,
	"darken": slughorn.BlendMode.Darken,
	"lighten": slughorn.BlendMode.Lighten,
	"color-dodge": slughorn.BlendMode.ColorDodge,
	"color-burn": slughorn.BlendMode.ColorBurn,
	"hard-light": slughorn.BlendMode.HardLight,
	"soft-light": slughorn.BlendMode.SoftLight,
	"difference": slughorn.BlendMode.Difference,
	"exclusion": slughorn.BlendMode.Exclusion,
}
BLEND_MODE_NAMES = list(BLEND_MODES)

# The wall material is polyhaven.py's own Cook-Torrance shader, completely unmodified - real PBR
# textures (baseColor/normal/ORM), no IBL. See that module for the D_GGX/G_Schlick/F_Schlick/
# sphereLightDir math.
WALL_VERTEX_SHADER = polyhaven.VERTEX_SHADER
WALL_FRAGMENT_SHADER = polyhaven.FRAGMENT_SHADER

# The paint's own fragment hook: pure alpha-space content, no lighting/normal work at all -
# layer.blendMode (set in build_decal_atlas() below) is what makes this integrate with the wall,
# not this shader. Ported from osgslug-tmp.worn.cpp's effectId 4 ("MSDF-gated stencil chips") -
# that file's ACTUAL default (`layer.effectId = 4` in its own main()), not effectId 1's uniform
# Voronoi cells + crack overlay (what an earlier version of this file had ported instead - reads
# more like cracked tile/glass than eroding paint). Deliberately drops the one part of effectId 4
# that samples the wall's own diffuse texture for extra "surface wear" - fine for worn.cpp's flat,
# fixed-UV 2D overlay, but this decal is meant to work on any surface without reading its
# material (see the module docstring's BlendMode design note) - the noise-driven wear map alone
# already gives the uneven, irregular chip pattern that's the actual point of this effect.
DECAL_FRAGMENT_HOOK = """
#version 430 core

#pragma osgSlug fragment

vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return fract(sin(p) * 43758.5453);
}

float hash1(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 smoothNoise2(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);

	vec2 a = hash2(i),                  b = hash2(i + vec2(1.0, 0.0));
	vec2 c = hash2(i + vec2(0.0, 1.0)), d = hash2(i + vec2(1.0, 1.0));

	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float noise1(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);

	float a = hash1(i);
	float b = hash1(i + vec2(1.0, 0.0));
	float c = hash1(i + vec2(0.0, 1.0));
	float d = hash1(i + vec2(1.0, 1.0));

	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
	float v = 0.0;
	float a = 0.5;

	v += a * noise1(p); p *= 2.03; a *= 0.5;
	v += a * noise1(p); p *= 2.01; a *= 0.5;
	v += a * noise1(p); p *= 2.07; a *= 0.5;
	v += a * noise1(p); p *= 2.11; a *= 0.5;

	return v / 0.9375;
}

// Distance to nearest Voronoi cell + a per-cell random scalar - the random scalar is what makes
// chips break away in irregular, differently-sized pieces instead of a uniform honeycomb.
vec2 voronoiChip(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	float minDist = 1e10;
	float cellRand = 0.0;

	for(int y = -1; y <= 1; y++) {
		for(int x = -1; x <= 1; x++) {
			vec2 n = vec2(x, y);
			vec2 pt = hash2(i + n);
			float dd = dot(n + pt - f, n + pt - f);

			if(dd < minDist) {
				minDist = dd;
				cellRand = hash2(pt + vec2(3.7, 8.1)).x;
			}
		}
	}

	return vec2(sqrt(minDist), cellRand);
}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {
	// Falls back to a flat fill with no baked tile (canvas.set_sdf() in build_decal_atlas()
	// below), AND at effectParam <= 0 - the ported algorithm's "uneven paint survival" term
	// (broad/mid/fine fbm noise further down) has a threshold baseline that is NOT scaled by
	// effectParam, only nudged by a small +0.08, so without this explicit bailout --erosion 0
	// would still show real patchiness instead of pristine paint (worn.cpp's original never
	// wired 0 to mean "off" for this effectId - it treated 0 as "minimum wear", not "no wear").
	if(data.sd < 0.0 || data.effectParam <= 0.0) {
		return vec4(data.layerColor.rgb, data.fill * data.layerColor.a);
	}

	// distFromEdge grows from 0 at the boundary inward, so chips concentrate at the edge and
	// vanish toward the center - like paint eroding from the outside in.
	float distFromEdge = max(0.0, data.sd - 0.5);

	// Procedural wear map, global across the whole word (not per-glyph): low-frequency smooth
	// noise for organic base variation (some letters naturally more protected than others) plus
	// sparse Voronoi "epicenters" (concentrated damage zones, like a water stain or impact).
	float baseNoise = smoothNoise2(data.emCoord * 0.08).x;

	vec2 epiCell = floor(data.emCoord * 0.1);
	vec2 epiCenter = (epiCell + hash2(epiCell)) * 10.0;
	float epiDist = distance(data.emCoord, epiCenter);
	float epicenter = 1.0 - smoothstep(0.0, 5.0, epiDist);

	float wearMap = clamp(baseNoise * 0.35 + epicenter * 0.3, 0.0, 1.2);
	float damage = data.effectParam * (0.5 + wearMap);

	// Large edge chunks - damage-weighted threshold, so eroded areas lose much bigger bites and
	// protected areas stay nearly clean.
	const float CHUNK_FREQ = 6.0;
	const float FALLOFF = 1.2;

	float chunkThreshold = damage - distFromEdge * FALLOFF;
	vec2 vc = voronoiChip(data.emCoord * CHUNK_FREQ);
	float cr = max(0.0, chunkThreshold * (0.8 + 0.4 * vc.y));
	float chunks = smoothstep(cr + 0.02, cr - 0.02, vc.x);

	// Interior holes - no edge gating, scaled by the same wear map so they concentrate where the
	// paint is already most degraded.
	const float HOLE_FREQ = 3.5;
	float holeThreshold = damage * 0.5;
	vec2 vh = voronoiChip(data.emCoord * HOLE_FREQ + vec2(7.3, 2.1));
	float hr = holeThreshold * (0.5 + 0.9 * vh.y);
	float holes = smoothstep(hr + 0.02, hr - 0.02, vh.x);

	float chipMask = max(chunks, holes);

	// Uneven paint survival, thresholded (not a direct alpha ramp) so the paint breaks into
	// islands instead of fading linearly across each glyph.
	float broad = fbm(data.emCoord * 0.35 + vec2(19.7, 4.2));
	float mid = fbm(data.emCoord * 1.20 + vec2(3.1, 44.8));
	float fine = fbm(data.emCoord * 7.50 + vec2(91.0, 12.0));

	float survivalField = broad * 0.60 + mid * 0.30 + fine * 0.10;

	float dir = dot(data.emCoord, normalize(vec2(0.75, 0.35)));
	float dirBias = smoothstep(-1.0, 7.0, dir);
	float survivalThreshold = mix(0.56, 0.30, dirBias);

	survivalThreshold += data.effectParam * 0.08;
	survivalThreshold += chipMask * 0.04;

	float survival = smoothstep(
		survivalThreshold - 0.16,
		survivalThreshold + 0.16,
		survivalField
	);

	float opacityNoise = mix(0.65, 1.05, fbm(data.emCoord * 2.0 + vec2(8.0, 31.0)));

	float protectedPaint = 0.18 * (1.0 - chipMask);
	float finalFill = data.fill * max(protectedPaint, (1.0 - chipMask) * survival * opacityNoise);

	return vec4(data.layerColor.rgb, finalFill * data.layerColor.a);
}
"""

def apply_light_uniforms(state_set):
	state_set.uniforms.extend((
		osg.Uniform(osg.Uniform.Type.FLOAT_VEC3, "lightPos", LIGHT_POS),
		osg.Uniform(osg.Uniform.Type.FLOAT_VEC3, "lightColor", LIGHT_COLOR),
		osg.Uniform(osg.Uniform.Type.FLOAT, "lightRadius", LIGHT_RADIUS),
		osg.Uniform(osg.Uniform.Type.FLOAT, "lightSourceRadius", LIGHT_SOURCE_RADIUS),
		osg.Uniform("skyColor", SKY_COLOR),
		osg.Uniform("groundColor", GROUND_COLOR),
	))

def build_wall_geode(gltf_path):
	textures = polyhaven.resolve_pbr_textures(gltf_path)

	def load_texture(path):
		image = osgDB.readImageFile(path)

		if not image:
			raise RuntimeError(f"Couldn't load texture: {path}")

		return osg.Texture2D(
			image=image,
			wrap=(osg.Texture.CLAMP_TO_EDGE, osg.Texture.CLAMP_TO_EDGE),
			filter=(osg.Texture.LINEAR_MIPMAP_LINEAR, osg.Texture.LINEAR),
		)

	corner = WALL_ORIGIN - WALL_TANGENT_U * 0.5 - WALL_TANGENT_V * 0.5
	geom = osg.createTexturedQuadGeometry(corner, WALL_TANGENT_U, WALL_TANGENT_V)

	geode = osg.Geode(name="wall")

	geode.drawables.append(geom)

	program = osg.Program(name="pyosgslug-text-decal-wall", shaders=(
		osg.Shader(osg.Shader.VERTEX, WALL_VERTEX_SHADER),
		osg.Shader(osg.Shader.FRAGMENT, WALL_FRAGMENT_SHADER),
	))

	ss = geode.stateSet

	ss.attributes.append(program)
	ss.textureAttributes[0] = load_texture(textures["baseColor"])
	ss.textureAttributes[1] = load_texture(textures["normal"])
	ss.textureAttributes[2] = load_texture(textures["orm"])
	ss.uniforms.extend((
		osg.Uniform("baseColorTex", 0),
		osg.Uniform("normalTex", 1),
		osg.Uniform("ormTex", 2),
	))
	apply_light_uniforms(ss)

	return geode

def build_decal_atlas(args):
	source_atlas = slughorn.Atlas()
	config = slughorn.freetype.LoadConfig()

	# First real test of decaling a CompositeShape with more than one DISTINCT shape/color per
	# layer (canvas.text() already produces one Layer per glyph, but they all share args.ink -
	# a COLR emoji's layers are separate outlines with their own authored palette colors). Nothing
	# here is emoji-specific in DecalDrawable itself - font.getColorGlyph()/load_emoji_font()'s
	# CompositeShape is the exact same type canvas.finalize() returns (see osgSlug::Font::ColorGlyph
	# = slughorn::CompositeShape), and pyosgslug-template.py's create_scene_emoji() renders one via
	# plain ShapeDrawable.addCompositeShape() with layer.transform/scale left at their defaults
	# ((0,0)/1) - so the same defaults are left untouched here rather than guessed at.
	if args.emoji:
		codepoint = slughorn.emoji.slack_name_to_codepoint(args.emoji)

		if codepoint is None:
			raise RuntimeError(f"Unknown emoji name: {args.emoji!r}")

		glyphs = slughorn.freetype.load_emoji_font(args.font, [codepoint], source_atlas, config)
		comp = glyphs.get(codepoint)

		if comp is None:
			raise RuntimeError(
				f"{args.font} has no COLR data for U+{codepoint:04X} ({args.emoji}) - "
				"needs a COLRv0/v1 emoji font, e.g. NotoColorEmoji-Regular.ttf or "
				"Twemoji.Mozilla.ttf, not a plain glyph-outline font"
			)

		# canvas.set_sdf()/canvas.text() do this per glyph automatically (Canvas._applySDF());
		# load_emoji_font() bypasses Canvas entirely, so it has to be requested by hand here.
		source_atlas.request_sdf([layer.key for layer in comp.layers], MSDF_RANGE)

	else:
		if not slughorn.freetype.load_ascii_font(args.font, source_atlas, config):
			raise RuntimeError(f"Couldn't load font: {args.font}")

		canvas = slughorn.canvas.Canvas(source_atlas)

		canvas.set_sdf(True, MSDF_RANGE)
		canvas.text(
			args.text, args.font_size, 0.5, 0.5, args.ink, config.metrics,
			anchor_y=slughorn.canvas.TextAnchorY.CAP_CENTER,
			align_x=slughorn.canvas.TextAlignX.CENTER,
		)

		comp = canvas.finalize()

	for layer in comp.layers:
		layer.bleed = MSDF_RANGE
		# TEMPORARY short-circuit: DECAL_FRAGMENT_HOOK's survival term has a threshold baseline
		# that's barely coupled to effectParam (see ai/context-todo-decal.md) - ANY positive value,
		# even 0.05, engages ~50% patchy transparency, which reads as "wall/sibling bleeding
		# through" and is easy to mistake for the (separate, already-fixed-by-blend-mode=normal)
		# sibling-layer BlendMode bug. Forcing 0 here so --emoji renders can be judged on layering
		# alone. Remove once survivalThreshold actually ramps smoothly from effectParam=0.
		layer.effectParam = 0.0 if args.emoji else args.erosion
		layer.blendMode = BLEND_MODES[args.blend_mode]

	# Captured before any panel-driven alpha edits: each layer's own AUTHORED rgb (identical
	# across every layer for --text, since they all share args.ink, but genuinely distinct per
	# layer for --emoji's own palette colors) - the alpha slider in make_panel_fn() below
	# re-applies a new alpha on top of each layer's own captured rgb, never overwriting it with
	# args.ink.rgb, so it stays correct for both cases.
	base_colors = [(layer.color.r, layer.color.g, layer.color.b) for layer in comp.layers]

	source_atlas.set_sdf(slughorn.SDF.Config(tile_size=64))
	source_atlas.build()

	atlas = osgSlug.Atlas.fromAtlas(source_atlas)

	atlas.packTextures()

	decal = osgSlug.DecalDrawable()

	decal.setStepsU(24)
	decal.setStepsV(24)
	decal.setHooks({osgSlug.FragmentHook: DECAL_FRAGMENT_HOOK})

	normal = DECAL_TANGENT_U.cross(DECAL_TANGENT_V)

	normal.normalize()

	pushed_origin = WALL_ORIGIN + normal * SURFACE_PUSH

	for layer in comp.layers:
		decal.addPlanarDecal(layer, pushed_origin, DECAL_TANGENT_U, DECAL_TANGENT_V)

	atlas.children.append(decal)

	decal.stateSet.modes[GL_DEPTH_TEST] = osg.StateAttribute.ON

	return atlas, decal, base_colors

def parse_args():
	parser = argparse.ArgumentParser(description=__doc__)

	parser.add_argument("text", nargs="*", default=["slughorn"],
		help="text to decal onto the wall (ignored if --emoji is given); all remaining positional "
		"words are joined with spaces, so quoting isn't required (default: %(default)s)")
	parser.add_argument("--emoji", default=None,
		help="emoji name to decal instead of --text (CLDR short name, colons optional, e.g. "
		"'fire' or ':fire:') - requires --font to point at a COLRv0/v1 emoji font, e.g. "
		"font/COLRv1/NotoColorEmoji-Regular.ttf, NOT the default Silkscreen text font")
	parser.add_argument("--font", default=FONT_PATH, help="TTF/OTF font path")
	# Unlike a normal ShapeDrawable, DecalDrawable's quad is a hard boundary at canvas [0,1] -
	# oversized text gets clipped at the edges instead of spilling into more screen space (this
	# bit the first render: 0.4 clipped "slughorn" down to about 4 visible letters). 0.18 leaves
	# real margin for an 8-character word; pyosgslug-text-msdf.py's own 0.25 (that script CAN
	# overflow harmlessly) is not a safe reference here.
	parser.add_argument("--font-size", type=float, default=0.18,
		help="canvas em-size, ignored for --emoji - a COLR glyph's own layers are left at their "
		"natural transform/scale, same as pyosgslug-template.py's create_scene_emoji() "
		"(default: %(default)s)")
	parser.add_argument("--texture", default="worn_brick_wall",
		help="Polyhaven slug, URL, or local .gltf for the wall material (default: %(default)s)")
	parser.add_argument("--res", default="2k", help="Polyhaven download resolution (default: %(default)s)")
	# "overlay" is the user-confirmed winner (2026-09-09) - the wall's own brick highlights/mortar
	# shading visibly show through the ink, which is the actual "physically part of the wall" cue
	# every earlier from-scratch relighting attempt was chasing. See ai/context-todo-decal.md.
	parser.add_argument("--blend-mode", choices=sorted(BLEND_MODES), default="overlay",
		help="how the paint composites against the wall's already-rendered pixels (default: %(default)s)")
	parser.add_argument("--erosion", type=float, default=0.12,
		help="0=pristine paint, 1=heavily eroded, worn.cpp's own effectParam convention (default: %(default)s)")
	parser.add_argument("--ink", default="0.30,0.15,0.65,1.0", help="R,G,B,A paint color (default: %(default)s)")

	args = parser.parse_args()
	args.text = " ".join(args.text)
	args.ink = slughorn.Color(*(float(c) for c in args.ink.split(",")))

	return args

def build_scene(w, h):
	args = parse_args()

	if args.emoji and args.erosion:
		print(
			f"pyosgslug-text-decal: --erosion is temporarily disabled for --emoji "
			f"(ignoring --erosion {args.erosion}) - see build_decal_atlas()"
		)

	if args.texture.endswith(".gltf") and os.path.exists(args.texture):
		gltf_path = args.texture

	else:
		gltf_path = polyhaven.download_polyhaven_texture(polyhaven.slug_from_arg(args.texture), args.res)

	root = osg.Group(name="text-decal")
	atlas, decal, base_colors = build_decal_atlas(args)

	root.children.append(build_wall_geode(gltf_path))
	root.children.append(atlas)

	return root, args, decal, base_colors

def make_panel_fn(args, decal, base_colors):
	# Every field here is a genuine LIVE edit - no rebuild, unlike pyosgslug-blend.py's per-circle
	# BlendMode (which really does need one, since its 3 circles can each pick a DIFFERENT mode,
	# potentially re-splitting/merging RenderGroups). Every layer here always shares ONE blend
	# mode, so DecalDrawable.setBlendMode() never changes group membership, only what each
	# existing group renders with - see osgSlug's DecalDrawable.hpp for the reasoning. Erosion/
	# alpha were already ordinary live per-layer setters (effectParam/color), nothing new needed
	# for those.
	state = {
		"blend_mode": args.blend_mode,
		"erosion": 0.0 if args.emoji else args.erosion,
		"alpha": args.ink.a,
	}

	def draw_panel(render_info):
		changed, idx = osgx.imgui.combo(
			"Blend Mode",
			BLEND_MODE_NAMES.index(state["blend_mode"]),
			BLEND_MODE_NAMES
		)

		if changed:
			state["blend_mode"] = BLEND_MODE_NAMES[idx]
			decal.setBlendMode(BLEND_MODES[state["blend_mode"]])

		osgx.imgui.separator()

		# Erosion's survivalThreshold has a baseline barely coupled to effectParam (see
		# ai/context-todo-decal.md) - expect a hard jump from pristine at exactly 0 to ~50%
		# patchy just above it, not a smooth ramp. Not a slider bug, a known shader-tuning gap.
		if args.emoji:
			osgx.imgui.text("Erosion disabled for --emoji (see build_decal_atlas())")

		else:
			changed, erosion = osgx.imgui.slider_float("Erosion", state["erosion"], 0.0, 1.0)

			if changed:
				state["erosion"] = erosion

				for i in range(len(decal.layers)):
					decal.layers[i].effectParam = erosion

		changed, alpha = osgx.imgui.slider_float("Alpha", state["alpha"], 0.0, 1.0)

		if changed:
			state["alpha"] = alpha

			for i, (r, g, b) in enumerate(base_colors):
				decal.layers[i].color = slughorn.Color(r, g, b, alpha)

	return draw_panel

if __name__ == "__main__":
	W, H = window_size()
	viewer = osgViewer.Viewer()
	root, args, decal, base_colors = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.05, 0.05, 0.05, 1.0)

	gui = osgx.imgui.Widget(viewer)

	gui.addSection(
		"Text Decal",
		make_panel_fn(args, decal, base_colors),
		osgx.imgui.SectionOptions(default_open=True)
	)

	while not viewer.done:
		viewer.frame()
