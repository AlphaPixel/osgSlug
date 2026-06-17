#version 430 core

// Tangent-plane sphere decal vertex shader.
//
// Each decal layer is placed at a specific lat/lon on a sphere via a tangent frame baked into
// the per-layer SSBO. Moving a decal at runtime costs only 3 Vec4 writes + dirtyLayers(i) --
// no CPU geometry work.
//
// Vertex layout (set by SSBODecalDrawable::compile()):
//
// a_position: xy = (lu, lv) normalized grid in [0,1], z = 0, w = layer index (1-based)
// a_emCoord: xy = shape em-space coordinate, zw = (lu, lv) UV
//
// Per-layer SSBO (DecalLayerData, 7 Vec4s at binding 1):
//
// [0] color -- RGBA flat color
// [1] gradientMeta
// [2] gradientXform
// [3] effectData -- x=effectId, y=shapeIndex, z=0, w=effectParam
// [4] center -- xyz = unit-sphere point at (lat,lon), w = sphere radius
// [5] tangentEast -- xyz = T_east * fullWidth_worldUnits, w = unused
// [6] tangentNorth-- xyz = T_north * fullHeight_worldUnits, w = unused
//
// AtlasShapeData and atlasShapes[] are prepended by Atlas::createDecalProgram()
// (SHADER_ATLAS_TYPES -- binding 0 only, no LayerData / binding 1 conflict).

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_emCoord;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

out vec2 v_emCoord;
out vec2 v_uv;
out vec4 v_color;
out float v_layerIndex;

flat out vec4 v_bandXform;
flat out vec4 v_shapeData;
flat out int v_effectId;
flat out int v_gradientId;
out vec4 v_gradientMeta;
out vec4 v_gradientXform;

vec3 osgSlug_Vertex(
	vec3 pos,
	vec2 emCoord,
	vec2 uv,
	int effectId,
	vec2 origin,
	float effectParam,
	float time
);

struct DecalLayerData {
	vec4 color;
	vec4 gradientMeta;
	vec4 gradientXform;
	vec4 effectData;
	vec4 center;
	vec4 tangentEast;
	vec4 tangentNorth;
};

layout(std430, binding = 1) buffer DecalLayerBuffer {
	DecalLayerData decalLayers[];
};

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	DecalLayerData ld = decalLayers[layerIdx];
	AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

	int effectId = int(ld.effectData.x + 0.5);

	// Map normalized grid [0,1] to centered [-0.5, 0.5], then displace along tangent frame.
	float lu = a_position.x - 0.5;
	float lv = a_position.y - 0.5;

	vec3 P = ld.center.xyz;
	float r = ld.center.w;
	vec3 Te = ld.tangentEast.xyz;
	vec3 Tn = ld.tangentNorth.xyz;

	// Gnomonic (central) projection: project tangent-plane point back onto sphere surface.
	vec3 world = normalize(P + lu * Te + lv * Tn) * r;

	vec3 pos = osgSlug_Vertex(
		world,
		a_emCoord.xy,
		a_emCoord.zw,
		effectId,
		sd.originData.xy,
		ld.effectData.w,
		osg_SimulationTime
	);

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_color = ld.color;
	v_bandXform = sd.bandXform;
	v_shapeData = sd.shapeData;
	v_effectId = effectId;
	v_gradientId = int(ld.gradientMeta.x + 0.5);
	v_gradientMeta = ld.gradientMeta;
	v_gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
