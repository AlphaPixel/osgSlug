#version 430 core

// Struct definitions and SSBO bindings are in osgSlug-types.glsl, prepended at load time.

layout(location = 0) in vec4 a_position; // xyz = world pos, w = layer index (1-based)
layout(location = 1) in vec4 a_emCoord;  // xy = em-coord, zw = UV [0,1]

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

// Defined in the linked effects or noop unit.
vec3 osgSlug_Vertex(
	vec3 pos,
	vec2 emCoord,
	vec2 uv,
	int effectId,
	vec2 origin,
	float effectParam,
	float time
);

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	LayerData ld = layers[layerIdx];
	AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

	int effectId = int(ld.effectData.x + 0.5);

	vec3 pos = osgSlug_Vertex(
		a_position.xyz,
		a_emCoord.xy,
		a_emCoord.zw,
		effectId,
		sd.originData.xy,
		ld.effectData.w,
		osg_SimulationTime
	);

	v_emCoord       = a_emCoord.xy;
	v_uv            = a_emCoord.zw;
	v_layerIndex    = a_position.w;
	v_color         = ld.color;
	v_bandXform     = sd.bandXform;
	v_shapeData     = sd.shapeData;
	v_effectId      = effectId;
	v_gradientId    = int(ld.gradientMeta.x + 0.5);
	v_gradientMeta  = ld.gradientMeta;
	v_gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
