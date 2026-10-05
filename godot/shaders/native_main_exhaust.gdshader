shader_type spatial;
render_mode unshaded, blend_add, cull_disabled, depth_draw_never, shadows_disabled;

// Original procedural visual, BSD-3-Clause. Anchors/provenance: SAVED_NATIVE_FLIGHT.md.
uniform float intensity = 0.0;
uniform float phase = 0.0;

void fragment() {
	float ribs = 0.78 + 0.22 * sin(UV.y * 65.0 - phase * 35.0);
	float tail = pow(1.0 - UV.y, 1.4);
	float edge = 0.7 + 0.3 * sin(UV.x * 6.2831853);
	ALBEDO = mix(vec3(0.08, 0.3, 1.0), vec3(0.55, 0.85, 1.0), tail);
	EMISSION = ALBEDO * 2.0;
	ALPHA = intensity * tail * ribs * edge * 0.65;
}
