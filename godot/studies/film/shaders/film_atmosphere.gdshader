shader_type spatial;
render_mode unshaded, cull_back, depth_draw_never, shadows_disabled;

// Optical review shell only. NOT authoritative weather or flight conditions.
// Phase is supplied by the film frame clock, never wall-clock shader TIME.
uniform bool cloud_layer = false;
uniform float phase = 0.0;
uniform float coverage = 0.5;
uniform vec3 seed_offset = vec3(4.2, 1.7, 9.3);
uniform vec3 sun_direction = vec3(-0.8, 0.3, 0.5);
uniform vec4 tint : source_color = vec4(0.42, 0.56, 0.75, 1.0);
varying vec3 sphere_direction;
varying vec3 world_normal;
varying vec3 world_point;

float hash3(vec3 p) {
	p = fract(p * vec3(0.1031, 0.11369, 0.13787));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}
float noise3(vec3 p) {
	vec3 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash3(i), hash3(i + vec3(1,0,0)), f.x),
		mix(hash3(i + vec3(0,1,0)), hash3(i + vec3(1,1,0)), f.x), f.y),
		mix(mix(hash3(i + vec3(0,0,1)), hash3(i + vec3(1,0,1)), f.x),
		mix(hash3(i + vec3(0,1,1)), hash3(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
	return noise3(p) * 0.53 + noise3(p * 2.03) * 0.27 +
		noise3(p * 4.11) * 0.13 + noise3(p * 8.17) * 0.07;
}
void vertex() {
	sphere_direction = normalize(VERTEX);
	world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
	world_point = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}
void fragment() {
	vec3 n = normalize(world_normal);
	float light = smoothstep(-0.16, 0.48, dot(n, normalize(sun_direction)));
	vec3 view_dir = normalize(CAMERA_POSITION_WORLD - world_point);
	float limb = pow(1.0 - max(0.0, dot(n, view_dir)), 3.5);
	if (cloud_layer) {
		vec3 p = sphere_direction * 13.0 + seed_offset;
		p.x += phase * 0.0015;
		// Warped weather-like bands, rather than latitude-longitude texture seams.
		p += vec3(fbm(p * 0.42), fbm(p * 0.42 + 17.0), fbm(p * 0.42 - 11.0)) * 2.5;
		float body = fbm(p * 0.75) * 0.62 + fbm(p * 3.2) * 0.27 + fbm(p * 12.7) * 0.11;
		float density = smoothstep(0.60 - coverage * 0.22, 0.84 - coverage * 0.22, body);
		float shadow = fbm(p * 3.2 + normalize(sun_direction) * 0.22);
		ALBEDO = tint.rgb * (0.015 + light * 0.85) * (0.68 + shadow * 0.45);
		ALPHA = 1.0 - exp(-density * 4.5);
	} else {
		ALBEDO = tint.rgb * (0.035 + light * 1.4);
		ALPHA = limb * 0.58;
	}
}
