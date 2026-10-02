shader_type spatial;
render_mode unshaded, blend_mix, depth_draw_never;

// Original procedural mark, BSD-3-Clause; source binding: WITHDRAWAL_EXHAUST.md.
// This pass shares the actual source vertices. Height limits are outward-rounded
// float32 chart bounds, not clearance tolerances or a physical hull opening.
varying vec3 body_position;

void vertex() {
	body_position = VERTEX;
}

void fragment() {
	bool port = body_position.x < 0.0;
	vec2 centre = vec2(port ? -1.309999942779541 : 1.309999942779541, -1.4500000476837158);
	vec2 heights = port ? vec2(1.9833636283874512, 2.045159101486206) : vec2(1.9857518672943115, 2.0489799976348877);
	if (body_position.y < heights.x || body_position.y > heights.y || length(body_position.xz - centre) > 0.018) {
		discard;
	}
	ALBEDO = vec3(0.008, 0.012, 0.016);
	ALPHA = 0.92;
}
