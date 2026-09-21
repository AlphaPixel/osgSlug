#!/usr/bin/env python3
#vimrun! ./pyosgslug-pbr-ibl-icons.py --env /path/to/manifest.gltf

# Python counterpart to examples/tmp/osgslug-tmp.liticons.cpp. The top row is real osgx
# Polyhedron geometry; the bottom row is made of flat slughorn faces. Both sample one PBR/IBL
# environment. The icon row's key experiment is its per-LAYER normal table: no extra geometry,
# just iconFaceNormals[int(geom.layerIndex) - 1] in an osgSlug FragmentHook.

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
uniform mat3 osg_NormalMatrix;

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
	vNormal = normalize(osg_NormalMatrix * osg_Normal);
	vWorldPos = osg_Vertex.xyz;
	gl_Position = osg_ModelViewProjectionMatrix * osg_Vertex;
}
"""

MESH_FRAGMENT_SHADER = """
#version 430 core

const float PI = 3.14159265359;
#pragma osgx::pbr *

in vec3 vNormal;
in vec3 vWorldPos;

uniform samplerCube envMap;
uniform sampler2D brdfLUT;
uniform samplerCube diffuseEnv;
uniform vec3 iblAxis[3];
uniform mat4 osg_ViewMatrixInverse;
uniform float iblIntensity;
uniform float roughness;
uniform float metallic;
uniform vec3 bodyColor;

out vec4 fragColor;

vec3 osgx_OrientIBL(vec3 d) {
	return vec3(dot(d, iblAxis[0]), dot(d, iblAxis[1]), dot(d, iblAxis[2]));
}

void main() {
	vec3 N = normalize(vNormal);
	vec3 V = normalize(osg_ViewMatrixInverse[3].xyz - vWorldPos);
	vec3 diffuseIrradiance = texture(diffuseEnv, osgx_OrientIBL(N)).rgb;
	float maxMip = float(max(textureQueryLevels(envMap) - 2, 0));
	vec3 prefiltered = textureLod(envMap, osgx_OrientIBL(reflect(-V, N)), roughness * maxMip).rgb;
	vec3 Fd = osgx_F_MultiScatter(N, V, roughness, vec3(0.04), brdfLUT);
	vec3 Fm = osgx_F_MultiScatter(N, V, roughness, bodyColor, brdfLUT);
	vec3 color = (diffuseIrradiance * bodyColor * (1.0 - Fd) * (1.0 - metallic) +
		prefiltered * mix(Fd, Fm, metallic)) * iblIntensity;

	color = osgx_TonemapPBRNeutral(color);
	fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
"""

def make_lit_icon_fragment_shader():
	return f"""
#version 430 core

const float PI = 3.14159265359;
#pragma osgx::pbr *
#pragma osgSlug fragment

uniform samplerCube envMap;
uniform sampler2D brdfLUT;
uniform samplerCube diffuseEnv;
uniform vec3 iblAxis[3];
uniform mat4 osg_ViewMatrixInverse;
uniform mat4 osg_ModelViewMatrix;
uniform float iblIntensity;
uniform float roughness;
uniform float metallic;
uniform vec3 iconFaceNormals[{MAX_ICON_LAYERS}];

vec3 osgx_OrientIBL(vec3 d) {{
	return vec3(dot(d, iblAxis[0]), dot(d, iblAxis[1]), dot(d, iblAxis[2]));
}}

vec4 osgSlug_Fragment(osgSlug_FragmentData data) {{
	int layerIdx = int(geom.layerIndex + 0.5) - 1;
	vec3 localN = iconFaceNormals[clamp(layerIdx, 0, {MAX_ICON_LAYERS - 1})];
	mat4 modelMatrix = osg_ViewMatrixInverse * osg_ModelViewMatrix;
	vec3 N = normalize(mat3(modelMatrix) * localN);
	vec3 worldPos = (modelMatrix * vec4(data.emCoord, 0.0, 1.0)).xyz;
	vec3 V = normalize(osg_ViewMatrixInverse[3].xyz - worldPos);
	vec3 diffuseIrradiance = texture(diffuseEnv, osgx_OrientIBL(N)).rgb;
	float maxMip = float(max(textureQueryLevels(envMap) - 2, 0));
	vec3 prefiltered = textureLod(envMap, osgx_OrientIBL(reflect(-V, N)), roughness * maxMip).rgb;
	vec3 Fd = osgx_F_MultiScatter(N, V, roughness, vec3(0.04), brdfLUT);
	vec3 Fm = osgx_F_MultiScatter(N, V, roughness, data.layerColor.rgb, brdfLUT);
	vec3 color = (diffuseIrradiance * data.layerColor.rgb * (1.0 - Fd) * (1.0 - metallic) +
		prefiltered * mix(Fd, Fm, metallic)) * iblIntensity;

	color = osgx_TonemapPBRNeutral(color);
	color = pow(color, vec3(1.0 / 2.2));

	return vec4(color, data.fill * data.layerColor.a);
}}
"""

def project_polyhedron(poly):
	"""Project a Polyhedron into the VIEW_DIR-orthogonal plane, retaining each face normal."""
	right = osg.Vec3(0.0, 1.0, 0.0).cross(VIEW_DIR)

	right.normalize()

	up = VIEW_DIR.cross(right)
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

def apply_environment_uniforms(state_set, environment, ibl_intensity, roughness, metallic):
	state_set.textureAttributes[5] = environment.envMap
	state_set.textureAttributes[6] = environment.brdfLUT
	state_set.textureAttributes[7] = environment.diffuseEnv
	state_set.uniforms.extend((
		osg.Uniform("envMap", 5),
		osg.Uniform("brdfLUT", 6),
		osg.Uniform("diffuseEnv", 7),
		osg.Uniform(osg.Uniform.Type.FLOAT_VEC3, "iblAxis", tuple(environment.iblAxis)),
		osg.Uniform("iblIntensity", ibl_intensity),
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
	parser.add_argument("--ibl-intensity", type=float, default=1.0, help="shared IBL intensity (default: %(default)s)")

	return parser.parse_args()

def build_scene(w, h):
	args = parse_args()
	environment = (
		osgx.gltf.pbribl.PBRIBLEnvironment.load(args.env)
		if args.env else osgx.gltf.pbribl.PBRIBLEnvironment.prepare(args.hdr, lutSize=1024)
	)

	if not environment.valid():
		raise RuntimeError("Failed to prepare PBR/IBL environment")

	root = osg.Group(name="pbr-ibl-icons")

	if environment.root is not None:
		root.children.append(environment.root)

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
		apply_environment_uniforms(geode.stateSet, environment, args.ibl_intensity, args.roughness, args.metallic)

		mesh_transform = osg.MatrixTransform(osg.Matrix.translate(mesh_x + column * STEP, STEP * 0.5, 0.0))

		mesh_transform.children.append(geode)
		root.children.append(mesh_transform)

		verts, faces = project_polyhedron(poly)
		composite, normals = build_lit_icon(canvas, verts, faces, color)

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
		apply_environment_uniforms(sd.stateSet, environment, args.ibl_intensity, args.icon_roughness, args.icon_metallic)

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
