#!/usr/bin/env python3
#vimrun! ./pyosgslug-pbr-ibl-icons.py --env /path/to/manifest.gltf

# Python counterpart to examples/tmp/osgslug-tmp.liticons.cpp. The top row is real osgx
# Polyhedron geometry; the bottom row is made of flat slughorn faces. Both sample one PBR/IBL
# environment. The icon row's key experiment is its per-LAYER normal table: no extra geometry,
# just iconFaceNormals[int(geom.layerIndex) - 1] in an osgSlug FragmentHook.
#
# By default the icons are drawn as seen from VIEW_DIR but lit with the unrotated face normals, so
# the rows are not a like-for-like lighting comparison. --compare rotates the meshes and the icon
# normals by the same VIEW_DIR -> +Z rotation: from the home camera (looking down -Z), both rows
# then show one orientation under one world-fixed environment, and differ only by perspective.

import argparse

from OpenSceneGraph import *
from OpenSceneGraph.GL import *

import osgSlug
import osgx
import slughorn

RADIUS = 1.2
STEP = 3.2
MAX_ICON_LAYERS = 16
CLEAR_COLOR = osg.Vec4(0.2, 0.2, 0.2, 1.0)

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

MESH_VERTEX_SHADER = """
#version 430 core

in vec4 osg_Vertex;
in vec3 osg_Normal;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform mat4 osg_ModelViewMatrix;
uniform mat4 osg_ViewMatrixInverse;

out vec3 vNormal;
out vec3 vWorldPos;

// World space, matching the environment and the icon row's normals.
void main() {
	mat4 modelMatrix = osg_ViewMatrixInverse * osg_ModelViewMatrix;

	vNormal = normalize(mat3(modelMatrix) * osg_Normal);
	vWorldPos = (modelMatrix * osg_Vertex).xyz;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
"""

MESH_FRAGMENT_SHADER = """
#version 430 core

const float PI = 3.14159265359;
#pragma osgx::pbr *
#pragma osgx::environment ENVIRONMENT_INPUTS, ENVIRONMENT_SAMPLE

in vec3 vNormal;
in vec3 vWorldPos;

uniform mat4 osg_ViewMatrixInverse;
uniform float roughness;
uniform float metallic;
uniform vec3 bodyColor;

out vec4 fragColor;

void main() {
	vec3 N = normalize(vNormal);
	vec3 V = normalize(osg_ViewMatrixInverse[3].xyz - vWorldPos);
	vec3 diffuseIrradiance = osgx_EnvironmentIrradiance(N);
	vec3 prefiltered = osgx_EnvironmentSpecular(reflect(-V, N), roughness);
	vec3 Fd = osgx_F_MultiScatter(N, V, roughness, vec3(0.04), osgx_environmentBRDFLUT);
	vec3 Fm = osgx_F_MultiScatter(N, V, roughness, bodyColor, osgx_environmentBRDFLUT);
	vec3 color = diffuseIrradiance * bodyColor * (1.0 - Fd) * (1.0 - metallic) +
		prefiltered * mix(Fd, Fm, metallic);

	color = osgx_TonemapPBRNeutral(color);
	fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
"""

def make_lit_icon_fragment_shader():
	return f"""
#version 430 core

const float PI = 3.14159265359;
#pragma osgx::pbr *
#pragma osgx::environment ENVIRONMENT_INPUTS, ENVIRONMENT_SAMPLE
#pragma osgSlug fragment

uniform mat4 osg_ViewMatrixInverse;
uniform mat4 osg_ModelViewMatrix;
uniform float roughness;
uniform float metallic;
uniform vec3 iconFaceNormals[{MAX_ICON_LAYERS}];

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {{
	int layerIdx = int(geom.layerIndex + 0.5) - 1;
	vec3 localN = iconFaceNormals[clamp(layerIdx, 0, {MAX_ICON_LAYERS - 1})];
	mat4 modelMatrix = osg_ViewMatrixInverse * osg_ModelViewMatrix;
	vec3 N = normalize(mat3(modelMatrix) * localN);
	vec3 worldPos = (modelMatrix * vec4(data.emCoord, 0.0, 1.0)).xyz;
	vec3 V = normalize(osg_ViewMatrixInverse[3].xyz - worldPos);
	vec3 diffuseIrradiance = osgx_EnvironmentIrradiance(N);
	vec3 prefiltered = osgx_EnvironmentSpecular(reflect(-V, N), roughness);
	vec3 Fd = osgx_F_MultiScatter(N, V, roughness, vec3(0.04), osgx_environmentBRDFLUT);
	vec3 Fm = osgx_F_MultiScatter(N, V, roughness, data.layerColor.rgb, osgx_environmentBRDFLUT);
	vec3 color = diffuseIrradiance * data.layerColor.rgb * (1.0 - Fd) * (1.0 - metallic) +
		prefiltered * mix(Fd, Fm, metallic);

	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	return vec4(color, data.fill * data.layerColor.a);
}}
"""

def view_basis():
	"""Return (right, up, VIEW_DIR): the icon projection's orthonormal, right-handed basis."""
	right = osg.Vec3(0.0, 1.0, 0.0).cross(VIEW_DIR)

	right.normalize()

	return right, VIEW_DIR.cross(right), VIEW_DIR

def view_rotation():
	"""Rotation taking VIEW_DIR to +Z (toward the home camera), right to +X and up to +Y."""
	right, up, view = view_basis()

	return osg.Matrix(
		right.x, up.x, view.x, 0.0,
		right.y, up.y, view.y, 0.0,
		right.z, up.z, view.z, 0.0,
		0.0, 0.0, 0.0, 1.0,
	)

def rotate_to_view(v):
	right, up, view = view_basis()

	return osg.Vec3(v.dot(right), v.dot(up), v.dot(view))

def project_polyhedron(poly):
	"""Project a Polyhedron into the VIEW_DIR-orthogonal plane, retaining each face normal."""
	right, up, _ = view_basis()
	verts = [osg.Vec2(v.dot(right), v.dot(up)) for v in poly.vertices]
	faces = []

	for index, face in enumerate(poly.faces):
		normal = poly.faceNormal(index)
		facing = normal.dot(VIEW_DIR)
		depth = sum(poly.vertices[i].dot(VIEW_DIR) for i in face.vertices) / len(face.vertices)

		faces.append({
			"indices": face.vertices,
			"normal": normal,
			"depth": depth,
			"front_facing": facing > 0.0,
		})

	return verts, faces

def build_lit_icon(canvas, verts, faces, color):
	"""Return (CompositeShape, normals), where normal i belongs to composite layer i."""
	normals = []
	visible = sorted((f for f in faces if f["front_facing"]), key=lambda f: f["depth"])

	for face in visible:
		v0 = verts[face["indices"][0]]

		canvas.begin_path()
		canvas.move_to(v0.x, v0.y)

		for index in face["indices"][1:]:
			v = verts[index]

			canvas.line_to(v.x, v.y)

		canvas.close_path()
		canvas.fill(slughorn.Color(color.x, color.y, color.z, 1.0))
		normals.append(face["normal"])

	return canvas.finalize(), normals

def apply_environment(state_set, environment, roughness, metallic):
	state_set.attributes.append(environment)
	state_set.uniforms.extend((
		osg.Uniform("roughness", roughness),
		osg.Uniform("metallic", metallic),
	))

def parse_args():
	parser = argparse.ArgumentParser(description=__doc__)
	environment_group = parser.add_mutually_exclusive_group(required=True)

	environment_group.add_argument("--env", metavar="MANIFEST", help="pre-baked osgx_pbribl environment manifest")
	environment_group.add_argument("--hdr", metavar="PATH", help="HDR environment to bake at startup")
	parser.add_argument("--roughness", type=float, default=0.35, help="mesh-row roughness (default: %(default)s)")
	parser.add_argument("--metallic", type=float, default=0.15, help="mesh-row metallic (default: %(default)s)")
	parser.add_argument("--icon-roughness", type=float, default=0.55, help="icon-row roughness (default: %(default)s)")
	parser.add_argument("--icon-metallic", type=float, default=0.0, help="icon-row metallic (default: %(default)s)")
	parser.add_argument(
		"--compare", action="store_true",
		help="rotate the mesh row and the icon normals into the icons' view, so both rows show one "
		"orientation under the same world-fixed environment"
	)
	parser.add_argument("--ibl-intensity", type=float, default=1.0, help="shared IBL intensity (default: %(default)s)")

	return parser.parse_args()

def build_scene(w, h):
	args = parse_args()
	if args.env:
		environment = osgx.gltf.pbribl.loadEnvironment(args.env)

	else:
		environment = osgx.Environment(osgDB.readImageFile(args.hdr))
		environment.rotation = osgx.gltf.pbribl.KHRONOS_ENVIRONMENT_ROTATION

	if environment is None:
		raise RuntimeError("Failed to prepare PBR/IBL environment")

	environment.diffuseIntensity = args.ibl_intensity
	environment.specularIntensity = args.ibl_intensity

	root = osg.Group(name="pbr-ibl-icons")

	if environment.bakeRoot is not None:
		root.children.append(environment.bakeRoot)

	mesh_program = osg.Program(name="pbr-ibl-icon-meshes", shaders=(
		osg.Shader(osg.Shader.VERTEX, MESH_VERTEX_SHADER),
		osg.Shader(osg.Shader.FRAGMENT, osgx.resolveShaderLibs(MESH_FRAGMENT_SHADER)),
	))
	icon_atlas_source = slughorn.Atlas()
	canvas = slughorn.canvas.Canvas(icon_atlas_source)
	icon_cells = []
	mesh_x = -STEP * (len(SHAPES) - 1) / 2.0

	for column, (name, color, ctor) in enumerate(SHAPES):
		poly = ctor(osg.Vec3(), RADIUS)
		geode = osg.Geode(name=name)

		geode.drawables.append(poly)
		geode.stateSet.attributes.append(mesh_program)
		geode.stateSet.uniforms.append(osg.Uniform("bodyColor", color))
		apply_environment(geode.stateSet, environment, args.roughness, args.metallic)

		mesh_matrix = osg.Matrix.translate(mesh_x + column * STEP, STEP * 0.5, 0.0)

		if args.compare:
			mesh_matrix = view_rotation() * mesh_matrix

		mesh_transform = osg.MatrixTransform(mesh_matrix)

		mesh_transform.children.append(geode)
		root.children.append(mesh_transform)

		verts, faces = project_polyhedron(poly)
		composite, normals = build_lit_icon(canvas, verts, faces, color)

		if args.compare:
			normals = [rotate_to_view(n) for n in normals]

		if len(normals) > MAX_ICON_LAYERS:
			raise RuntimeError(f"{name} needs {len(normals)} icon layers; MAX_ICON_LAYERS is {MAX_ICON_LAYERS}")

		icon_cells.append((column, composite, normals))

	icon_atlas_source.build()
	icon_atlas = osgSlug.Atlas.fromAtlas(icon_atlas_source)
	icon_fragment_hook = make_lit_icon_fragment_shader()

	for column, composite, normals in icon_cells:
		sd = osgSlug.ShapeDrawable()

		sd.addCompositeShape(composite)
		sd.setHooks({osgSlug.FragmentHook: icon_fragment_hook})

		transform = osg.MatrixTransform(osg.Matrix.translate(mesh_x + column * STEP, -STEP * 0.5, 0.0))

		transform.children.append(sd)
		icon_atlas.children.append(transform)

		# Atlas.addChild compiles ShapeDrawable when it is attached. Its state set now owns the
		# hook program, so add the per-drawable normal table and shared PBR resources there.
		padded_normals = tuple(normals + [osg.Vec3()] * (MAX_ICON_LAYERS - len(normals)))

		sd.stateSet.uniforms.append(
			osg.Uniform(osg.Uniform.Type.FLOAT_VEC3, "iconFaceNormals", padded_normals)
		)
		apply_environment(sd.stateSet, environment, args.icon_roughness, args.icon_metallic)

		# Atlas's default StateSet disables GL_DEPTH_TEST (HUD/overlay default). This icon is
		# embedded in a real 3D scene alongside the mesh row, so it opts back into real depth
		# testing itself, same as BoxDrawable/DecalDrawable/PathDrawable/ScanlineDrawable do.
		sd.stateSet.attributes[osg.StateAttribute.DEPTH] = (
			osg.Depth(osg.Depth.LESS, 0.0, 1.0, False), osg.StateAttribute.ON
		)

	root.children.append(icon_atlas)

	return root

if __name__ == "__main__":
	from pyosgslug_example import window_size, make_trackball

	W, H = window_size()
	viewer = osgViewer.Viewer()
	root = build_scene(W, H)

	viewer.sceneData = root
	viewer.cameraManipulator = make_trackball(root)
	viewer.camera.clearColor = CLEAR_COLOR

	while not viewer.done:
		viewer.frame()
