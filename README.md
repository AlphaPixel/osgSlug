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

# Demos

<table>
<tr>
<th align="center">Preview</th>
<th>Description</th>
</tr>

<tr>
<td align="center">

![Emoji](https://slughorn.io/github/emoji.png)

</td>
<td>

**Emoji**

Demonstrates loading **COLRv0** and **COLRv1** emojis using the `slughorn/freetype.hpp`
backend. Each layer of the emoji is composited into its own quad and positioned relative
to the base.

slughorn supports all of the advanced **COLRv1** features, including gradients
and transforms.

</td>
</tr>

<tr>
<td align="center">

[![PBR/IBL](https://slughorn.io/github/pbr-ibl.webp)](https://slughorn.io/github/pbr-ibl.webm)

</td>
<td>

**PBR/IBL**

Full PBR (Physics Based Rendering) and IBL (Image Based Lighting) can be applied
to any shape. This video shows the standard `chromatic.hdr` file (converted to KTX2)
applied to a text scene using high metallic/low roughness settings.

</td>
</tr>

<tr>
<td align="center">

[![Animated Glyphs](https://slughorn.io/github/glyph-animate.webp)](https://slughorn.io/github/glyph-animate.webm)

</td>
<td>

**Animated Glyphs**

Simple, shader-driven animation applied to Slug-rendered glyph geometry,
accomplished by adjusting the output positions in the vertex shader.

</td>
</tr>

<tr>
<td align="center">

[![Layer Effects](https://slughorn.io/github/logo.webp)](https://slughorn.io/github/logo.webm)

</td>
<td>

**Layer Effects**

Shows how individual layers in a `CompositeShape` can not only be distinguished in the
shader pipeline, but also how each layer can have its own unique fragment output. From
left to right: basic fill, two GLSL algorithmic fills, traditional texture lookup, and
finally an animated GLSL algorithmic fill.

</td>
</tr>

<tr>
<td align="center">

[![Morphing](https://slughorn.io/github/morph.webp)](https://slughorn.io/github/morph.webm)

</td>
<td>

**Morphing**

Similar to the animated glyphs demo, this example shows both the shape, a simple
triangle, and the debugging bounding box using the `OSGSLUG_DEBUG=3` environment
variable.

</td>
</tr>

<tr>
<td align="center">

![2D](https://slughorn.io/github/project2d.png)

</td>
<td>

**2D Projection**

The standard orthographic-style 2D placement/rendering.

</td>
</tr>

<tr>
<td align="center">

![3D](https://slughorn.io/github/project3d.png)

</td>
<td>

**3D Projection**

The same scene as the 2D Projection above, but with a traditional perspective-style view.

</td>
</tr>

<tr>
<td align="center">

![Gradients](https://slughorn.io/github/gradients.png)

</td>
<td>

**Gradients**

The FreeType, NanoSVG and Canvas backends all fully support linear, radial and
sweep gradient fills/strokes. Gradients can also be dynamically transformed by
the GPU during rendering (though their "color stop" values are static).

</td>
</tr>

<tr>
<td align="center">

![HUD](https://slughorn.io/github/hud.png)

</td>
<td>

**HUD**

slughorn is perfect for HUD elements of any kind in 2D, 3D, or somewhere
dynamically in between.

</td>
</tr>

<tr>
<td align="center">

[![Animated HUD](https://slughorn.io/github/animated-hud.webp)](https://slughorn.io/github/animated-hud.webm)

</td>
<td>

**Animated HUD**

Every `Layer` instance within a `CompositeShape` can be individually accessed
and dynamically modified. When using the *GL4/SSBO* path, updates only require
changing a small subset of the total GPU memory.

</td>
</tr>

<tr>
<td align="center">

![Shapes/CompositeShapes](https://slughorn.io/github/shapes-compositeshapes.png)

</td>
<td>

**Shapes/CompositeShapes**

Each backend has its own examples of how to create both simple `Shape` and
`CompositeShape` objects, groups of `Shape` instances layered together. The last
screenshot demonstrates the `slughorn/canvas.hpp` backend, in addition to showing how
the traditional punch-out effect can be achieved by manually using opposite winding
directions in order to cut out one closed path from another.

</td>
</tr>

<tr>
<td align="center">

[![Mixed Scenes](https://slughorn.io/github/shapes-compositeshapes-mixed.webp)](https://slughorn.io/github/shapes-compositeshapes-mixed.webm)

</td>
<td>

**Mixed Scenes**

`Shape` instances from different backends can be mixed together in the same
`CompositeShape`; for example, fonts from the FreeType backend mixed with
hand-authored content from the native Canvas (to create an animated "card"
mockup, seen here).

</td>
</tr>

<tr>
<td align="center">

[![Animated Scenes](https://slughorn.io/github/shapes-compositeshapes-animation.webp)](https://slughorn.io/github/shapes-compositeshapes-animation.webm)

</td>
<td>

**Animated Scenes**

A `CompositeShape` scene can reference and animate any of the participating
`Shape` instances within it. In this example, a single "pill" shape is repeated
12 times, each being animated using a different time offset.

</td>
</tr>

<tr>
<td align="center">

[![3D Objects](https://slughorn.io/github/sphere3d.webp)](https://slughorn.io/github/sphere3d.webm)

</td>
<td>

**3D Objects**

Slug is not restricted to simple quads; any 3D object or mesh can be assigned compatible
`slughorn` coordinate mappings, such as spheres, cubes, curved surfaces, etc.

</td>
</tr>

<tr>
<td align="center">

![SVG](https://slughorn.io/github/svgs.png)

</td>
<td>

**SVG**

SVG content fits easily within the `slughorn` ecosystem via the `slughorn/nanosvg.hpp`
backend.

*NOTE*: Some SVG features (strokes, text) are **possible**, but have not yet
been implemented.

</td>
</tr>

<tr>
<td align="center">

[![Text](https://slughorn.io/github/text.webp)](https://slughorn.io/github/text.webm)

</td>
<td>

**Text**

Text was the original inspiration for Slug, and will always be incredibly well-supported.
As mentioned above, each glyph in a text layout is nothing more than an instance of
`Shape`, and can be manipulated in any way you can imagine. :)

</td>
</tr>

<tr>
<td align="center">

[![Mixed Text](https://slughorn.io/github/text-mix.webp)](https://slughorn.io/github/text-mix.webm)

</td>
<td>

**Mixed Text**

Text glyphs are simply an instance of `Shape`, and can be freely mixed with any
**other** `Shape` or `CompositeShape` object. This example demonstrates replacing the
character `F` with a simple triangle, which fits seamlessly into the layout process.

</td>
</tr>

<tr>
<td align="center">

![Text Effects](https://slughorn.io/github/text-effects.png?v=2)

</td>
<td>

**Text Effects**

Emphasizing that glyphs truly **are** "just another `Shape`", this example
demonstrates using "inside" *and* "outside" coverage effects (using MSDF sidecar
data), stroking instead of filling, as well a using procedural GLSL to actively
remove sections of a normal glyph fills!

</td>
</tr>

<tr>
<td align="center">

![Text Along Path](https://slughorn.io/github/textpath.png)

</td>
<td>

**Text Along Path**

Individual glyphs can be easily positioned along existing `Path` instances. The
`slughorn::canvas::Path` object can `sample` at **any position** within a
supported `Shape` instance.

</td>
</tr>

<tr>
<td align="center">

[![Compute Shaders](https://slughorn.io/github/compute.webp)](https://slughorn.io/github/compute.webm)

</td>
<td>

**Compute Shaders**

Not only can compute shaders animate/manipulate `slughorn` state, they can also
act as a `fill()` source for other content. This demonstrates two stacked,
blended shapes each using different compute shader output.

</td>
</tr>

</table>

# Blending/Compositing Limitations

There are currently some limitations/artifacts in how complete the osgSlug
blending operations are; in particular, some very noticable antialiasing
artifacts that **look like** premultiplied alpha issues, but aren't. They are a
limitation of using fixed-function OpenGL blending for coverage-aware
Porter-Duff compositing.

Slug renders analytic shapes through conservative bounding geometry. At
antialiased edges, a fragment may have only partial shape coverage. A vector
compositor should apply the Porter-Duff operation only by that coverage amount
and preserve the remaining destination contribution:

`out = mix(dst, porterDuff(src, dst), coverage)`

Fixed-function blending cannot express this for all Porter-Duff operators,
especially modes whose destination factor is zero. Those modes can partially
overwrite the framebuffer in pixels where the Slug shape only barely covers the
pixel, producing small dark fringes near shape boundaries.

The correct path is shader-side compositing with access to the previous
destination pixel, typically via RTT/ping-pong render targets or
framebuffer-fetch-style functionality where available.
