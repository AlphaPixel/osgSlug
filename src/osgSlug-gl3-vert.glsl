#version 330 core

layout(location = 0) in vec4 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec4 a_emCoord;
layout(location = 3) in vec4 a_bandXform;
layout(location = 4) in vec4 a_shapeData;

// .x = effectId (integer packed as float)
// .yz = origin (shape.originX, shape.originY); the shape's origin/anchor/pivot/etc
// .w = spare animation parameter (speed multiplier, phase offset, etc.)
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

void main() {
	vec3 pos = a_position.xyz;
	float t = osg_SimulationTime;
	int eid = int(a_effectData.x + 0.5);
	int gid = int(a_gradientMeta.x + 0.5);
	int mode = eid / 100;
	int index = eid % 100;

	// A subtle "wobble" effect; currently unused.
	if(eid == 5) {
		pos.x += sin(a_emCoord.y * 6.0 + t * 2.0) * 0.2;
		pos.y += sin(a_emCoord.x * 4.0 + t * 1.5) * 0.1;
	}

	// A wobble AND morph, combined; used by `osgslug-simple-animation` and
	// `osgslug-font-animation`.
	if(eid == 6) {
		float u = a_emCoord.x;
		float v = a_emCoord.y;
		float wave = sin(u * 6.28318 * 2.0 - t * 3.0);
		float center = 1.0 - abs(v - 0.5) * 2.0;

		center = clamp(center, 0.0, 1.0);
		center = pow(center, 2.0);

		pos.y += wave * center * 0.3;

		// float stretch = 1.0 + wave * 0.1;
		// pos.x += sin(osg_SimulationTime) * 0.2;

		float cx = a_emCoord.x - 0.5;

		pos.x += cx * sin(osg_SimulationTime) * 0.3;
	}

	// 9-slice pill animation: caps stay round, only the body stretches.
	// Requires SubdividedDrawable (denser vertex grid so each vertex knows its UV position).
	// a_effectData.w = total world width of this layer's quad, baked at compile time.
	// leftX = pos.x - uv.x * W recovers the left edge at any vertex without cross-vertex comm.
	if(mode == 7) {
		float i = float(index);
		float amp = 0.5 + 0.5 * sin(t * 4.0 + i * 0.7);
		float sx = 0.15 + amp * 0.85;

		float uv_x = a_emCoord.z;
		float W = a_effectData.w;
		float leftX = pos.x - uv_x * W;

		// capFrac: cap width as a fraction of the total quad UV range.
		// For roundedRect(0.1, 0.1, 0.8, 0.1, 0.1) with expand=0.01:
		// emRange = 0.82, radius = 0.1 => capFrac = 0.1 / 0.82 ~= 0.122
		const float capFrac = 0.122;
		float capW = capFrac * W;
		float bodyW = (1.0 - 2.0 * capFrac) * W;

		if(uv_x > (1.0 - capFrac)) {
			// Right cap: translate to the animated right end.
			float localT = (uv_x - (1.0 - capFrac)) / capFrac;

			pos.x = leftX + capW + sx * bodyW + localT * capW;
		}

		else if(uv_x > capFrac) {
			// Body: stretch between the two fixed-size caps.
			float bodyT = (uv_x - capFrac) / (1.0 - 2.0 * capFrac);

			pos.x = leftX + capW + bodyT * sx * bodyW;
		}

		// Left cap (uv_x <= capFrac): pos.x unchanged; anchored at the left edge.
	}

	// Clock hand rotation: forward-rotates the quad around the world pivot, inverse-rotates.
	if(eid == 8) {
		float c = cos(t), s = sin(t);
		mat2 R = mat2(c, s, -s, c);
		vec2 origin = pos.xy - a_emCoord.xy + a_effectData.yz;

		pos.xy = R * (pos.xy - origin) + origin;
	}

	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_layerIndex = a_position.w;
	v_bandXform = a_bandXform;
	v_shapeData = a_shapeData;
	v_color = a_color;
	v_effectId = eid;
	v_gradientId = gid;
	v_gradientMeta = a_gradientMeta;
	v_gradientXform = a_gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
