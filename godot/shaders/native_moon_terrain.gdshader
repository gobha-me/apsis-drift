shader_type spatial;
// Vertex-colour lunar ground. C++ owns geometry and the optional site packet.
uniform bool surface_landmarks_enabled = false;
uniform int surface_landmark_count = 0;
uniform vec3 surface_landmark_centres[8];
uniform float surface_landmark_radii[8];
uniform vec4 surface_landmark_tints[8];
varying vec3 surface_position;

void vertex() {
	surface_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 1.0;
	METALLIC = 0.0;
	if (surface_landmarks_enabled) {
		vec3 eye = INV_VIEW_MATRIX[3].xyz;
		for (int i = 0; i < 8; i++) {
			if (i >= surface_landmark_count) break;
			float radius = max(surface_landmark_radii[i], 0.001);
			float visible = 1.0 - smoothstep(300.0, 360.0, length(surface_landmark_centres[i] - eye));
			vec3 relative = clamp(surface_position - surface_landmark_centres[i], vec3(-radius * 2.0), vec3(radius * 2.0));
			float coverage = 1.0 - smoothstep(radius * 0.82, radius, length(relative));
			float phase = dot(relative, vec3(0.21, 0.12, 0.16));
			float bands = smoothstep(-0.25, 0.25, sin(phase + sin(phase * 0.37) * 0.7));
			vec3 tint = surface_landmark_tints[i].rgb;
			vec3 exposure = mix(tint * 0.48, min(vec3(1.0), tint * 0.88 + vec3(0.10)), bands);
			ALBEDO = mix(ALBEDO, exposure, coverage * visible * 0.96);
		}
	}
}
