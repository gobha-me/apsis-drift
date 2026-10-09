shader_type canvas_item;
render_mode unshaded, blend_premul_alpha;

// BSD-3-Clause. The transparent 3D viewport has already applied surface alpha
// to RGB. Preserve additive exhaust instead of multiplying it by alpha again.
void fragment() {
	COLOR = texture(TEXTURE, UV);
}
