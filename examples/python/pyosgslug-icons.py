#!/usr/bin/env python3
#vimrun! ./pyosgslug-icons.py

# Python port of examples/tmp/osgslug-tmp.icon.cpp - a gallery of 2D "icon" renderings for
# every osgx::Polyhedron, drawn with slughorn's Canvas (move_to/line_to/fill/stroke), no 3D
# geometry or GL shading involved. Three rendering styles side by side, one row each:
#
#   - wireframe:  every front-facing face outline, stroked (backface-cull hidden-line removal
#                 only - a convex solid's front faces never overlap in projection).
#   - shaded:     front-facing faces filled with a flat lambertian shade of the shape's color.
#   - silhouette: convex hull of every projected vertex, single flat fill, no facets.
#
# All three share one fixed 3/4-view projection (project_polyhedron() below), built straight
# from each Polyhedron's own .vertices/.faces/.faceNormal() - no per-shape tuning.
#
# Motivation: mixing these flat slughorn icons in with the real 3D osgx.Polyhedron dice pieces
# already used by ~/dev/mm/prototype.py's match4 board (built on pyosg-match4.py) - a cheap
# "token" representation alongside the full 3D die meshes.
#
# build_scene(w, h) is the same runner contract every other pyosgslug-*.py example uses.

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import osgx
import slughorn

RADIUS = 1.2
STEP = 3.2
WIREFRAME_STROKE_WIDTH = 0.035

# Shared by every icon so the whole gallery reads as one consistent "lighting rig" - a 3/4
# angle so no face of any shape here projects edge-on.
VIEW_DIR = osg.Vec3(1.0, 1.0, 1.0)

VIEW_DIR.normalize()

SHAPES = (
	("Tetrahedron", osg.Vec3(0.12, 0.78, 0.34), osgx.Tetrahedron),
	("Cube", osg.Vec3(0.52, 0.55, 0.61), osgx.Cube),
	("Octahedron", osg.Vec3(0.04, 0.72, 0.90), osgx.Octahedron),
	("PentagonalTrapezohedron", osg.Vec3(0.88, 0.16, 0.72), osgx.PentagonalTrapezohedron),
	("Dodecahedron", osg.Vec3(0.96, 0.78, 0.08), osgx.Dodecahedron),
	("Icosahedron", osg.Vec3(0.90, 0.10, 0.10), osgx.Icosahedron),
)

def project_polyhedron(poly):
	"""Returns (verts, faces): verts is a list of osg.Vec2, every Polyhedron vertex projected
	onto the plane perpendicular to VIEW_DIR. faces is a list of dicts - indices (into verts),
	depth (average dot(vertex, VIEW_DIR), larger = nearer the camera), light (clamped diffuse
	term), front_facing."""
	right = osg.Vec3(0.0, 1.0, 0.0).cross(VIEW_DIR)

	right.normalize()

	up = VIEW_DIR.cross(right)
	verts = [osg.Vec2(v.dot(right), v.dot(up)) for v in poly.vertices]
	faces = []

	for f in range(len(poly.faces)):
		face = poly.faces[f]
		normal = poly.faceNormal(f)
		facing = normal.dot(VIEW_DIR)
		depth = sum(poly.vertices[idx].dot(VIEW_DIR) for idx in face.vertices) / len(face.vertices)

		faces.append({
			"indices": face.vertices,
			"depth": depth,
			"light": max(0.0, facing),
			"front_facing": facing > 0.0,
		})

	return verts, faces

# Andrew's monotone chain. `pts` is always a real Polyhedron's vertex count (>= 4), so no
# empty/degenerate-input handling is needed here.
def convex_hull(pts):
	pts = sorted(pts, key=lambda p: (p.x, p.y))

	def cross(o, a, b):
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x)

	hull = []

	for p in pts:
		while len(hull) >= 2 and cross(hull[-2], hull[-1], p) <= 0.0:
			hull.pop()

		hull.append(p)

	lower_len = len(hull) + 1

	for p in reversed(pts[:-1]):
		while len(hull) >= lower_len and cross(hull[-2], hull[-1], p) <= 0.0:
			hull.pop()

		hull.append(p)

	hull.pop()

	return hull

def shade(color, light):
	l = 0.25 + 0.75 * light

	return slughorn.Color(color.x * l, color.y * l, color.z * l, 1.0)

def outline_face(canvas, verts, face):
	v0 = verts[face["indices"][0]]

	canvas.move_to(v0.x, v0.y)

	for idx in face["indices"][1:]:
		v = verts[idx]

		canvas.line_to(v.x, v.y)

	canvas.close_path()

# Draws each UNIQUE edge exactly once, as its own open 2-point subpath - not one closed loop
# per face (that's build_shaded()'s job). A closed per-face loop here would stroke every
# interior edge twice with independently-computed miter joins that disagree at the shared
# vertex - see osgslug-tmp.icon.cpp's own comment for the full story (a real artifact hit
# there on the tetrahedron/octahedron/icosahedron's shared pole vertices).
def build_wireframe(canvas, verts, faces, color):
	edges = set()

	for face in faces:
		if not face["front_facing"]:
			continue

		idx = face["indices"]

		for i in range(len(idx)):
			a, b = idx[i], idx[(i + 1) % len(idx)]

			if a > b:
				a, b = b, a

			edges.add((a, b))

	canvas.begin_path()

	for a, b in edges:
		canvas.move_to(verts[a].x, verts[a].y)
		canvas.line_to(verts[b].x, verts[b].y)

	canvas.stroke(WIREFRAME_STROKE_WIDTH, shade(color, 1.0))

	return canvas.finalize()

def build_shaded(canvas, verts, faces, color):
	# Back-to-front by depth: not strictly required (a convex solid's front faces never
	# overlap in projection), but cheap insurance against AA fringes at shared edges picking
	# the wrong face's color.
	order = sorted((f for f in faces if f["front_facing"]), key=lambda f: f["depth"])

	for face in order:
		canvas.begin_path()
		outline_face(canvas, verts, face)
		canvas.fill(shade(color, face["light"]))

	return canvas.finalize()

def build_silhouette(canvas, verts, faces, color):
	hull = convex_hull(verts)

	canvas.begin_path()
	canvas.move_to(hull[0].x, hull[0].y)

	for p in hull[1:]:
		canvas.line_to(p.x, p.y)

	canvas.close_path()
	canvas.fill(shade(color, 1.0))

	return canvas.finalize()

STYLES = (
	("wireframe", build_wireframe),
	("shaded", build_shaded),
	("silhouette", build_silhouette),
)

def build_scene(w, h):
	# The authoring-side atlas: slughorn.Atlas owns the Canvas + build(), same split
	# pyosgslug-simple.py uses - osgSlug.Atlas's Python binding only exposes osg.Group.
	a = slughorn.Atlas()
	canvas = slughorn.canvas.Canvas(a)

	cells = []

	for row, (style_name, build) in enumerate(STYLES):
		for col, (shape_name, color, ctor) in enumerate(SHAPES):
			poly = ctor(osg.Vec3(), RADIUS)
			verts, faces = project_polyhedron(poly)
			composite = build(canvas, verts, faces, color)

			cells.append((row, col, composite))

			osg.notice(f"{style_name} / {shape_name}")

	a.build()

	# fromAtlas() copies `a` into a new osgSlug.Atlas and packs it in one step.
	atlas = osgSlug.Atlas.fromAtlas(a)

	ox = -STEP * (len(SHAPES) - 1) / 2.0
	oy = STEP * (len(STYLES) - 1) / 2.0

	for row, col, composite in cells:
		sd = osgSlug.ShapeDrawable()

		sd.addCompositeShape(composite)

		mt = osg.MatrixTransform()

		mt.matrix = osg.Matrix.translate(ox + col * STEP, oy - row * STEP, 0.0)
		mt.children.append(sd)

		# No Geode wrapper needed - osgSlug's Drawable IS an osg.Node in this OSG fork - and
		# no manual sd.compile() call either: atlas is already packed, so Atlas.addChild's
		# override walks the newly-added subtree and compiles any osgSlug.Drawable it finds.
		atlas.children.append(mt)

	return atlas

if __name__ == "__main__":
	from pyosgslug_example import window_size, make_trackball

	W, H = window_size()

	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = osg.Vec4(0.2, 0.2, 0.2, 1.0)

	while not viewer.done:
		viewer.frame()
