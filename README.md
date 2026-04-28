# osgSlug

osgSlug is a frontend for the [slughorn](https://github.com/AlphaPixel/slughorn)
library, simplifying the usage of the recent OSS release of the [Slug](https://sluglibrary.com/)
technique by Eric Lengyel in OpenSceneGraph.

# Overview

This project is currently a rapid testing frontend, visualizer, and
"proof-of-concept" demo for **slughorn** (where most of the critical, generic
C++ code lives). As of **mid-April, 2026**, *both projects* are currently under
very active development. Check back regularly to follow our progress, as we have
some really great ideas moving forward!

# Examples

## Demos

| Preview | Description |
|:---:|---|
| [![Animated Glyph](https://github.com/AlphaPixel/osgSlug/releases/download/demo-assets/glyph-animate-preview.webp)](https://github.com/AlphaPixel/osgSlug/releases/download/demo-assets/glyph-animate.mp4) | **Animated glyph effects**<br><br>Time-driven shader effects applied directly to Slug-rendered vector glyphs. |
| ![Emoji Bestiary](https://github.com/AlphaPixel/osgSlug/releases/download/demo-assets/emoji-bestiary.webp) | **Emoji bestiary**<br><br>Single-codepoint emoji glyphs rendered through the osgSlug pipeline: octopus, scorpion, peacock, and T-Rex. |

# TODO

## Critical

- [ ] Demonstrate drawing multiple layers on the SAME QUAD (instead of each
  having its OWN quad)
- [ ] Use `slug_t` (instead of `float`) everywhere it's appropriate! Potentially
  even introduce `using Vec3 = osg::Vec3f` inside `osgSlug` namespace to further
  reinforce! **IMPORTANT** Also need to ensure things like `sin/cos` return
  `slug_t`, any usage of `osg::DegreesToRadians`, etc.
- [ ] Improved `osgSlug::{Font/Text}` support
- [ ] Add an example showing toggle/cycle layers

## Soon

- [ ] Create a reusable "screen grid" widget
- [ ] Audio visualizer effect/demo
- [ ] Progress meter demo
- [ ] Convert to using better `VertexAtribIPointer` where appropriate
  (`effectId` and `band{Tex,Max}{X,Y}`.
- [ ] Change `slughorn_render.py` to dump SVG for the `save_curves_debug`
- [ ] Add CMake helpers for compiling in each backend
- [ ] Slug "mip-mapping"! Starting with osgSlug, introduce the ability to
  "short-circuit" the Slug quad based on some "level of detail" rule; when the
  shape is some threshold of distance AWAY, revert to simple texture lookup
  approximations. NOTE: `slughorn` MIGHT be able to participate in this
  optmization as well ... somehow
- [ ] Fix `numBands` auto-calculation and account for spatial curve
  distribution, not just count
- [x] pybind11 wrapper
- [ ] Change `composites` to `compositeShapes` in serialization, etc
- [x] Change the `autoMetrics` defaul to `true`
- [x] Rename `slughorn-ft2.hpp` to `slughorn-freetype.hpp`
- [ ] Enforce VERSION compatibility in backends
- [ ] UDL types for `slughorn::Key::from{String,Codepoing}`, potentially as
  `_ukey`, `_skey` or similar?
- [ ] Add additional per-vertex metadata (like which "quad corner" we are)
- [ ] Improve `osgSlug::Font` to expose the additional font metrics exposed by
  `slughorn-freetype.hpp`(so that the `Text` object can do better layout)

## Medium Term

- [ ] Add `gl_InstanceID` quad creation
- [ ] Add per-Layer profiling; will requiring profiling per-quad, if that's even
  possible?
- [ ] Convert `osgSlug::Atlas` to be an `osg::StateAttribute` (or behave like
  one, at least)
- [ ] Introduce `slughorn-harfbuzz.hpp` text API, using Harfbuzz to "shape" it
  properly. Note: it will necessarily NEED to be built on top of the FreeType2
  backend (`slughorn-freetype.hpp`)!
- [ ] Qt6 QPainterPath provides moveTo / lineTo / quadTo / cubicTo /
  closeSubpath via elementAt() iteration; maps cleanly to CurveDecomposer.
  Quadratics are native to Qt6 (unlike Cairo which works in cubics). QFont /
  QRawFont provide glyph outline extraction as a potential FreeType2 complement.
  QSvgRenderer provides SVG loading as a potential NanoSVG complement.
  Structure to match slughorn-cairo.hpp: decomposePath(QPainterPath, Atlas&).
- [ ] Allow `serial::writeJSON` for ANY object (not JUST `Atlas`)
- [ ] Helpers for the `9-slice` method of a rounded rectangle
- [x] `Atlas::createDefaultStateSet()` member instead of free function in
  `Drawable.hpp`
- [ ] Sync `TEX_WIDTH` / `kLogBandTextureWidth`; uniform or shader preamble
  injection
- [ ] Premultiplied alpha
- [ ] Proper depth testing and render order
- [x] Remove `slug_color` uniform remnant from shaders (color is pure vertex
  attribute now)
- [ ] Simulate a fragment processing frame in Python and inspect the math
- [ ] Tooling for "optimizing" Atlas::build
- [ ] Generate tight carrier mesh from Slug
  coverage via sampling + contour extraction + triangulation (offline). Preserve
  em-space UVs.
- [ ] Generate SDF/MSDF texture data with Python emulator

## When Ready

- [ ] Add an `effectId/shapeId` UDL and helper like `slug_t/_cv/cv`
- [x] `Atlas::Key` type conversion (`uint32_t` -> `Codepoint | Name`
  discriminated union)
- [x] Conic subdivision for Skia circular geometry (`iter.conicWeight()`)
- [x] Minimal Skia `args.gn` build config (trim from 25GB)
- [ ] Non-square band grids (`bandMaxX != bandMaxY`)
- [ ] Layer::scale - evaluate for removal; currently only meaningful for
  FT2/text. All geometry backends leave it at 1.0. If osgSlug::Font /
  osgSlug::Text take full ownership of font-size-to-world scaling, Layer::scale
  becomes dead weight and computeQuad could take scale as a call-site parameter
  instead. Defer until text pipeline stabilizes.

## Someday / Fun

- [ ] Live `numBands` slider tool with real-time rebuild + heatmap feedback
- [ ] VSG adapter (trivial now given `slughorn` separation)


