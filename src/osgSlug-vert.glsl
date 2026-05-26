#version 430 core

layout(location = 0) in vec4 a_position; // xyz = world pos, w = layer index (1-based)
layout(location = 1) in vec4 a_emCoord;  // xy = em-coord, zw = UV [0,1]

// Atlas-level shape data: static after packTextures(), never mutated at runtime.
// One entry per unique shape in the atlas, indexed via LayerData.effectData.y.
struct AtlasShapeData {
	vec4 bandXform;    // xy = bandScaleX/Y, zw = bandOffsetX/Y
	vec4 shapeData;    // xy = glyphLoc (ivec2), zw = bandMax (ivec2)
	vec4 originData;   // xy = originX/Y, zw = unused
};

// Per-layer data: one entry per layer in each drawable, indexed by (a_position.w - 1).
// Runtime-mutable — color, effectId, gradient assignment can be changed via dirty().
// Slot order: slughorn contract first (color, gradient), osgSlug machinery last (effectData).
struct LayerData {
	vec4 color;         // slot 0 — RGBA flat color
	vec4 gradientMeta;  // slot 1 — x = gradientId (1-based), yz = gradient center, w = r0_norm
	vec4 gradientXform; // slot 2 — gradient transform (B matrix / direction / sweep)
	vec4 effectData;    // slot 3 — x = effectId, y = shapeIndex (float→int), z = unused, w = worldWidth
};

layout(std430, binding = 0) readonly buffer AtlasShapeBuffer {
	AtlasShapeData atlasShapes[];
};

layout(std430, binding = 1) buffer LayerBuffer {
	LayerData layers[];
};

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

void main() {
	int layerIdx = int(a_position.w + 0.5) - 1;
	LayerData ld = layers[layerIdx];
	AtlasShapeData sd = atlasShapes[int(ld.effectData.y + 0.5)];

	vec3 pos = a_position.xyz;
	float t = osg_SimulationTime;
	int eid = int(ld.effectData.x + 0.5);
	int mode = eid / 100;
	int index = eid % 100;

	if(eid == 5) {
		pos.x += sin(a_emCoord.y * 6.0 + t * 2.0) * 0.2;
		pos.y += sin(a_emCoord.x * 4.0 + t * 1.5) * 0.1;
	}

	if(eid == 6) {
		float u = a_emCoord.x;
		float v = a_emCoord.y;
		float wave = sin(u * 6.28318 * 2.0 - t * 3.0);
		float center = 1.0 - abs(v - 0.5) * 2.0;

		center = clamp(center, 0.0, 1.0);
		center = pow(center, 2.0);

		pos.y += wave * center * 0.3;

		float cx = a_emCoord.x - 0.5;

		pos.x += cx * sin(osg_SimulationTime) * 0.3;
	}

	if(mode == 7) {
		float i = float(index);
		float amp = 0.5 + 0.5 * sin(t * 4.0 + i * 0.7);
		float sx = 0.15 + amp * 0.85;

		float uv_x = a_emCoord.z;
		float W = ld.effectData.w;
		float leftX = pos.x - uv_x * W;

		const float capFrac = 0.122;
		float capW = capFrac * W;
		float bodyW = (1.0 - 2.0 * capFrac) * W;

		if(uv_x > (1.0 - capFrac)) {
			float localT = (uv_x - (1.0 - capFrac)) / capFrac;
			pos.x = leftX + capW + sx * bodyW + localT * capW;
		}

		else if(uv_x > capFrac) {
			float bodyT = (uv_x - capFrac) / (1.0 - 2.0 * capFrac);
			pos.x = leftX + capW + bodyT * sx * bodyW;
		}
	}

	if(eid == 8) {
		float c = cos(t), s = sin(t);
		mat2 R = mat2(c, s, -s, c);
		vec2 origin = pos.xy - a_emCoord.xy + sd.originData.xy;

		pos.xy = R * (pos.xy - origin) + origin;
	}

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_color = ld.color;
	v_bandXform = sd.bandXform;
	v_shapeData = sd.shapeData;
	v_effectId = eid;
	v_gradientId = int(ld.gradientMeta.x + 0.5);
	v_gradientMeta = ld.gradientMeta;
	v_gradientXform = ld.gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
