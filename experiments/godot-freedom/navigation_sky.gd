extends RefCounted
## Bounded runtime texture from identified C++ stars, not a second generator.
static func material(catalog: Array, tint: Color, pressure: float) -> ShaderMaterial:
	var image := Image.create(4096, 2048, false, Image.FORMAT_RGB8)
	image.fill(Color.BLACK)
	for star in catalog:
		var direction: Vector3 = star.direction
		var u := atan2(direction.z, direction.x) / TAU + 0.5
		var v := acos(clampf(direction.y, -1, 1)) / PI
		var fx := u * image.get_width()
		var fy := v * image.get_height()
		var x := int(fx) % image.get_width()
		var y := clampi(int(fy), 0, image.get_height() - 1)
		var color: Color = star.color * float(star.brightness)
		# Subpixel energy splat avoids block-shaped, three-texel-wide stars.
		for dx in 2:
			for dy in 2:
				var weight := (1 - fmod(fx, 1) if dx == 0 else fmod(fx, 1)) * (1 - fmod(fy, 1) if dy == 0 else fmod(fy, 1))
				var px := posmod(x + dx, image.get_width())
				var py := clampi(y + dy, 0, image.get_height() - 1)
				image.set_pixel(px, py, image.get_pixel(px, py) + color * weight * 1.5)
	var result := ShaderMaterial.new()
	result.shader = preload("res://navigation_sky.gdshader")
	result.set_shader_parameter("stars", ImageTexture.create_from_image(image))
	result.set_shader_parameter("view_to_inertial", Basis.IDENTITY)
	result.set_shader_parameter("atmosphere_tint", tint)
	result.set_shader_parameter("atmosphere_density", pressure / 1013.25)
	return result
