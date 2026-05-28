#version 330 core

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec4 a_emCoord;
layout(location = 3) in vec4 a_bandXform;
layout(location = 4) in vec4 a_shapeData;

// .x = effectId (integer packed as float)
// .yz = origin (shape.originX, shape.originY)
// .w = effectParam (user-settable via setLayerEffectParam)
layout(location = 5) in vec4 a_effectData;

layout(location = 6) in vec4 a_gradientMeta;
layout(location = 7) in vec4 a_gradientXform;

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
	int effectId = int(a_effectData.x + 0.5);

	vec3 pos = osgSlug_Vertex(
		a_position.xyz,
		a_emCoord.xy,
		a_emCoord.zw,
		effectId,
		a_effectData.yz,
		a_effectData.w,
		osg_SimulationTime
	);

	v_emCoord       = a_emCoord.xy;
	v_uv            = a_emCoord.zw;
	v_layerIndex    = a_position.w;
	v_color         = a_color;
	v_bandXform     = a_bandXform;
	v_shapeData     = a_shapeData;
	v_effectId      = effectId;
	v_gradientId    = int(a_gradientMeta.x + 0.5);
	v_gradientMeta  = a_gradientMeta;
	v_gradientXform = a_gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
