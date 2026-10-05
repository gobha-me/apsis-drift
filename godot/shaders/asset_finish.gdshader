shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

// Subtle code-authored finish for the existing GLB surfaces. No displacement.
uniform vec4 base_color : source_color = vec4(0.4, 0.4, 0.4, 1.0);
uniform float metal = 0.0;
uniform float rough = 0.5;
varying vec3 object_position;

float grain(vec3 p) {
	p = fract(p * vec3(0.1031, 0.11369, 0.13787));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

void vertex() { object_position = VERTEX; }

void fragment() {
	float footprint = max(length(dFdx(object_position)), length(dFdy(object_position)));
	float fleck = grain(floor(object_position * 260.0));
	fleck = mix(fleck, 0.5, smoothstep(0.001, 0.008, footprint));
	ALBEDO = base_color.rgb * (0.97 + 0.06 * fleck);
	METALLIC = metal;
	ROUGHNESS = clamp(rough + (fleck - 0.5) * 0.08, 0.16, 1.0);
}
