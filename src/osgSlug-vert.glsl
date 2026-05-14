#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_emCoord;
layout(location = 3) in vec4 a_bandXform;
layout(location = 4) in vec4 a_shapeData;
layout(location = 5) in float a_effectId;
layout(location = 6) in float a_gradientId;
layout(location = 7) in vec4 a_gradientXform;

uniform mat4 osg_ModelViewProjectionMatrix;
uniform float osg_SimulationTime;

out vec2 v_emCoord;
out vec4 v_color;

flat out vec4 v_bandXform;
flat out vec4 v_shapeData;
flat out int v_effectId;
flat out int v_gradientId;
out vec4 v_gradientXform;

void main() {
	vec3 pos = a_position;
	float t = osg_SimulationTime;
	int eid = int(a_effectId + 0.5);
	int gid = int(a_gradientId + 0.5);
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

	// Stretches the quad towards the right; used by 'osgslug-compositeshape-canvas`, but stretches
	// the shape uniformly (which is the easy/naive approach). Will be fixed later.
	if(mode == 7) {
		float i = float(index);
		float t = osg_SimulationTime;
		float amp = 0.5 + 0.5 * sin(t * 4.0 + i * 0.7);
		float sx = 0.3 + amp * 1.4;

		pos.x = (pos.x - 0.0) * sx + 0.0;
	}

	v_emCoord = a_emCoord;
	v_bandXform = a_bandXform;
	v_shapeData = a_shapeData;
	v_color = a_color;
	v_effectId = eid;
	v_gradientId = gid;
	v_gradientXform = a_gradientXform;

	gl_Position = osg_ModelViewProjectionMatrix * vec4(pos, 1.0);
}
