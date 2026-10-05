shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

// Presentation-only mineral/sediment shading. NO vertex displacement, collision
// surface, random stream or saved-world data is introduced here. Metres.
uniform float altitude_origin = 0.0;
uniform bool planet_tile = false;
uniform vec3 tile_origin = vec3(0.0);
varying vec3 surface_position;
varying vec3 surface_normal;
varying vec3 mineral_position;
varying vec3 radial_normal;
varying float elevation;

float hash_cell(vec3 p) {
	p = fract(p * vec3(0.1031, 0.11369, 0.13787));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

float mineral_noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = fract(p);
	vec3 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash_cell(i), hash_cell(i + vec3(1,0,0)), u.x),
		mix(hash_cell(i + vec3(0,1,0)), hash_cell(i + vec3(1,1,0)), u.x), u.y),
		mix(mix(hash_cell(i + vec3(0,0,1)), hash_cell(i + vec3(1,0,1)), u.x),
		mix(hash_cell(i + vec3(0,1,1)), hash_cell(i + vec3(1,1,1)), u.x), u.y), u.z);
}

void vertex() {
	surface_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	surface_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
	mineral_position = planet_tile ? VERTEX + tile_origin : surface_position + vec3(0.0, altitude_origin, 0.0);
	radial_normal = planet_tile ? normalize(MODEL_NORMAL_MATRIX * normalize(VERTEX + tile_origin)) : vec3(0.0, 1.0, 0.0);
	elevation = planet_tile ? UV.x : surface_position.y + altitude_origin;
}

void fragment() {
	vec3 p = mineral_position;
	// Fine surface slopes must fade spatially, not switch at a mesh LOD edge.
	// VERTEX is view-space here, so this remains stable across floating-origin
	// shifts and tile replacements. Geometry/physical heights are unchanged.
	float normal_detail = planet_tile ? 1.0 - smoothstep(120000.0, 500000.0, length(VERTEX)) : 1.0;
	vec3 terrain_normal = normalize(mix(normalize(radial_normal), normalize(surface_normal), normal_detail));
	float footprint = max(length(dFdx(p)), length(dFdy(p)));
	float broad = mineral_noise(p * 0.0018);
	broad = mix(broad, 0.5, smoothstep(150.0, 800.0, footprint));
	float seams = mineral_noise(p * 0.014 + broad * 3.0);
	seams = mix(seams, 0.5, smoothstep(15.0, 100.0, footprint));
	float chips = mineral_noise(p * 0.16);
	float grit = mineral_noise(p * 2.2);
	// Derivative filtering suppresses subpixel detail in the distance.
	chips = mix(chips, 0.5, smoothstep(2.0, 12.0, footprint));
	grit = mix(grit, 0.5, smoothstep(0.15, 1.5, footprint));
	float strata = sin(elevation * 0.085 + seams * 6.0 + broad * 14.0);
	strata *= 1.0 - smoothstep(6.0, 55.0, footprint);
	float slope = 1.0 - clamp(dot(terrain_normal, normalize(radial_normal)), 0.0, 1.0);
	float exposed = smoothstep(0.018, 0.15, slope + (seams - 0.5) * 0.075);
	// Low-frequency mineral staining is cosmetic only, continuous across tiles
	// and shared by ground/orbit views; it is not a second elevation generator.
	float regional = mineral_noise(p * 0.000006);
	vec3 bedrock = mix(vec3(0.12, 0.135, 0.145), vec3(0.34, 0.235, 0.14), regional);
	vec3 dust = mix(vec3(0.19, 0.145, 0.095), vec3(0.38, 0.30, 0.20), broad);
	vec3 rock = mix(vec3(0.095, 0.11, 0.12), vec3(0.26, 0.25, 0.22), seams);
	rock *= 0.90 + strata * 0.12 + chips * 0.22;
	vec3 color = mix(dust, rock, exposed);
	color *= mix(vec3(0.9), bedrock * 4.0, 0.35);
	// Retain a contribution from the C++ terrain's authored surface palette.
	color *= mix(vec3(0.85), COLOR.rgb * 1.7, 0.20);
	ALBEDO = color * (0.40 + chips * 0.10 + grit * 0.04);
	// At orbital distances retain broad mineral staining and a muted contribution
	// from the C++ palette, not undersampled metre-scale noise or noisy RGB flecks.
	float orbital = planet_tile ? smoothstep(80000.0, 700000.0, length(VERTEX)) : 0.0;
	vec3 muted_palette = mix(vec3(dot(COLOR.rgb, vec3(0.2126, 0.7152, 0.0722))), COLOR.rgb, 0.2);
	ALBEDO = mix(ALBEDO, mix(bedrock, muted_palette, 0.10) * 0.45, orbital);
	ROUGHNESS = clamp(0.92 - exposed * 0.20 + grit * 0.05, 0.6, 1.0);
	// Screen derivatives give a small normal-only relief, not invented hills.
	float detail_height = chips * 0.65 + grit * 0.045 + strata * 0.035;
	vec3 dpdx = dFdx(surface_position);
	vec3 dpdy = dFdy(surface_position);
	vec3 n = terrain_normal;
	vec3 r1 = cross(dpdy, n);
	vec3 r2 = cross(n, dpdx);
	float det = dot(dpdx, r1);
	vec3 gradient = (r1 * dFdx(detail_height) + r2 * dFdy(detail_height)) / max(abs(det), 0.000001);
	n = normalize(n - sign(det) * gradient * 0.65);
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);
}
