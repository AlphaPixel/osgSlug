#!/usr/bin/env python3
#vimrun! ./pyosgslug-tessellate.py

# First-ever render of slughorn.tessellate()'s output: builds a single five-pointed-star Shape
# via slughorn.canvas.Canvas (concave, no holes - exercises earcut on a non-trivial outline),
# triangulates it with slughorn.tessellate.tessellate(), and draws the resulting Mesh2D as a
# real indexed osg.Geometry (osg.Vec3Array + osg.DrawElementsUInt, both fed straight from numpy -
# no per-vertex Python loop). Raw data out, frontend decides (see tessellate.hpp's own Mesh2D
# comment) - no osgSlug.Atlas/ShapeDrawable involved, since get_shape_contours()+tessellate()
# bypass the SDF rendering pipeline entirely.
#
# Also extrudes the same contours via slughorn.tessellate.extrude() into a closed 3D solid
# (Mesh3D - top/bottom caps plus walls) and renders that alongside the flat star, offset to the
# side. Pass --trackball (or press F12 once running) to orbit around and actually see the depth -
# the default 2D ortho camera looks straight down the Z axis, where it's indistinguishable from
# the flat version.
#
# Requires slughorn built with SLUGHORN_TESSELLATE=ON (vendors ext/earcut.hpp), and an
# OpenSceneGraph.py checkout with osg.DrawElementsUInt bound (pyosg/osg/Geometry.cpp) - added
# alongside this example, since until now only osg.DrawArrays was exposed to Python.

from pyosgslug_example import configure_viewer, parse_args, window_size

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import slughorn

import math
import numpy as np
import sys

if not hasattr(slughorn, "tessellate"):
	sys.exit("slughorn.tessellate is unavailable - rebuild with SLUGHORN_TESSELLATE=ON")

VERTEX_SHADER = """
#version 460 core

in vec4 osg_Vertex;
in vec4 osg_Color;

uniform mat4 osg_ModelViewProjectionMatrix;

out vec4 v_color;

void main() {
	v_color = osg_Color;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
"""

FRAGMENT_SHADER = """
#version 460 core

in vec4 v_color;
out vec4 fragColor;

void main() {
	fragColor = v_color;
}
"""

# Builds a single five-pointed star Shape (em-space, [0, 1] square) via slughorn.canvas.Canvas -
# the same begin_path/move_to/line_to/close_path/fill/finalize sequence test_tessellate.py's own
# _triangle_atlas() uses, just with 10 points instead of 3.
def build_star_atlas():
	atlas = slughorn.Atlas()
	canvas = slughorn.canvas.Canvas(atlas)

	outer, inner = 0.45, 0.18
	cx, cy = 0.5, 0.5

	canvas.begin_path()

	for i in range(10):
		r = outer if i % 2 == 0 else inner
		angle = math.pi / 2.0 + i * math.pi / 5.0
		x, y = cx + r * math.cos(angle), cy + r * math.sin(angle)

		if i == 0:
			canvas.move_to(x, y)
		else:
			canvas.line_to(x, y)

	canvas.close_path()
	canvas.fill(slughorn.Color(1, 1, 1, 1), 1.0, slughorn.Key("star"))
	canvas.finalize(slughorn.Key("star_comp"))

	atlas.build()

	return atlas

# Pads `mesh.positions` (N, 2) out to (N, 3) with a z=0 column - osg.Vec3Array's buffer
# constructor needs that exact shape, and this is the only reshaping numpy can't do with a
# view alone (x/y stay contiguous, z doesn't exist yet). Mesh3D.positions is already (N, 3), so
# extrude()'s result skips this and goes straight into osg.Vec3Array.
def vec3_positions(mesh):
	positions = np.asarray(mesh.positions)
	padded = np.zeros((positions.shape[0], 3), dtype=np.float32)

	padded[:, :2] = positions

	return osg.Vec3Array(padded)

# One osg.Geode, two Geometries sharing the same vertex array: a flat-colored TRIANGLES fill
# straight from `indices`, and a black LINES wireframe over each triangle's 3 edges (built by
# stacking the 3 edge-column-pairs - still pure numpy, no per-triangle Python loop) so the earcut
# triangulation is visible instead of disappearing into one solid fill. Shared by both the flat
# star (tessellate()) and the extruded solid (extrude()) - same shape of data either way, just
# 2D-padded-to-3D vs. already-3D positions.
def build_mesh_geode(verts, indices, fillColor):
	edges = np.concatenate((indices[:, (0, 1)], indices[:, (1, 2)], indices[:, (2, 0)]))

	fillColorArray = osg.Vec4Array((osg.Vec4(*fillColor),))
	edgeColorArray = osg.Vec4Array((osg.Vec4(0.0, 0.0, 0.0, 1.0),))

	fillColorArray.binding = osg.Array.BIND_OVERALL
	edgeColorArray.binding = osg.Array.BIND_OVERALL

	fill = osg.Geometry(
		vertexArray=verts,
		colorArray=fillColorArray,
		primitiveSets=(osg.DrawElementsUInt(osg.PrimitiveSet.TRIANGLES, indices.ravel()),),
	)
	wire = osg.Geometry(
		vertexArray=verts,
		colorArray=edgeColorArray,
		primitiveSets=(osg.DrawElementsUInt(osg.PrimitiveSet.LINES, edges.ravel()),),
	)

	fill.useVertexBufferObjects = True
	wire.useVertexBufferObjects = True

	geode = osg.Geode()

	geode.drawables.extend((fill, wire))

	return geode

def build_scene(w, h):
	atlas = build_star_atlas()
	contours = atlas.get_shape_contours(slughorn.Key("star"))

	mesh = slughorn.tessellate.tessellate(contours)
	solid = slughorn.tessellate.extrude(contours, 0.15)

	print(f"slughorn.tessellate.tessellate(): {mesh}")
	print(f"slughorn.tessellate.extrude(): {solid}")

	flat = build_mesh_geode(vec3_positions(mesh), np.asarray(mesh.indices), (0.9, 0.55, 0.15, 1.0))
	extruded = build_mesh_geode(
		osg.Vec3Array(np.asarray(solid.positions, dtype=np.float32)),
		np.asarray(solid.indices),
		(0.2, 0.6, 0.85, 1.0),
	)

	# Offset to the side (star spans roughly [0.05, 0.95]) rather than sitting on top of the flat
	# version - both are visible at once, orbit with --trackball to see the extrusion's depth.
	extrudedXform = osg.MatrixTransform()

	extrudedXform.matrix = osg.Matrix.translate(1.3, 0.0, 0.0)
	extrudedXform.children.append(extruded)

	program = osg.Program(name="pyosgslug-tessellate", shaders=(
		osg.Shader(osg.Shader.VERTEX, VERTEX_SHADER),
		osg.Shader(osg.Shader.FRAGMENT, FRAGMENT_SHADER),
	))

	root = osg.Group()

	root.children.extend((flat, extrudedXform))
	root.stateSet.attributes.append(program)

	return root

if __name__ == "__main__":
	args = parse_args(description=__doc__)
	W, H = window_size()

	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.camera.clearColor = osg.Vec4(0.2, 0.2, 0.2, 1.0)
	configure_viewer(viewer, root, args)

	while not viewer.done:
		viewer.frame()
