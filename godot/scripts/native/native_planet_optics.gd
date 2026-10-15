extends RefCounted
## One current C++ optical atlas shared by sky and terrain; no generated field.
const EnvironmentState = preload("res://scripts/native/native_environment.gd")
var atlas_owner := ""
var atlas_bytes := PackedByteArray()
var atlas_texture: ImageTexture

static func number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func valid(state: Dictionary) -> bool:
	if not state.has("optics"): return true # Historical, explicitly unselected.
	var data: Variant = state.optics
	if not data is Dictionary or not EnvironmentState.valid(state): return false
	for field in ["version", "disclosure"]:
		if not data.get(field) is int or data[field] != 1: return false
	for field in ["system_id", "planet_id"]:
		if not EnvironmentState.decimal(data.get(field)) or data[field] != state.get(field): return false
	if not data.get("width") is int or data.width != 64 or not data.get("height") is int or data.height != 32: return false
	if not data.get("density") is PackedByteArray or data.density.size() != 2048: return false
	for column in 64:
		if data.density[column] != data.density[0] or data.density[1984 + column] != data.density[1984]: return false
	for field in ["reference_radius", "lower_height_metres", "upper_height_metres", "opacity_cap"]:
		if not number(data.get(field)): return false
	if data.reference_radius != state.planet_radius or data.opacity_cap < 0.0 or data.opacity_cap > 0.75: return false
	if data.lower_height_metres < 0.0 or data.upper_height_metres < data.lower_height_metres or data.upper_height_metres > state.atmosphere_edge: return false
	if data.upper_height_metres == data.lower_height_metres and (data.opacity_cap != 0.0 or data.lower_height_metres != 0.0): return false
	if not EnvironmentState.color(data.get("tint")): return false
	var center: Variant = data.get("planet_center")
	if not center is Vector3 or not center.is_finite() or center != Vector3(0.0, -(state.planet_radius + state.altitude), 0.0): return false
	var frame: Variant = data.get("fixed_to_tangent")
	if not frame is Basis or not frame.is_finite() or absf(frame.determinant() - 1.0) > 0.0001: return false
	for axis in [frame.x, frame.y, frame.z]:
		if absf(axis.length_squared() - 1.0) > 0.0001: return false
	return absf(frame.x.dot(frame.y)) <= 0.0001 and absf(frame.x.dot(frame.z)) <= 0.0001 and absf(frame.y.dot(frame.z)) <= 0.0001

func apply(state: Dictionary, materials: Array[ShaderMaterial]) -> bool:
	if not valid(state): return false
	for material in materials:
		if material == null or material.shader == null: return false
	if not state.has("optics"):
		for material in materials:
			material.set_shader_parameter("optical_enabled", false)
			material.set_shader_parameter("optical_atlas", null)
		atlas_owner = ""
		atlas_bytes = PackedByteArray()
		atlas_texture = null
		return true
	var data: Dictionary = state.optics
	var owner := "%s:%s:%d" % [data.system_id, data.planet_id, data.version]
	if atlas_texture == null or owner != atlas_owner or data.density != atlas_bytes:
		var image := Image.create_from_data(64, 32, false, Image.FORMAT_R8, data.density)
		if image == null or image.is_empty(): return false
		var texture := ImageTexture.create_from_image(image)
		if texture == null: return false
		atlas_texture = texture
		atlas_owner = owner
		atlas_bytes = data.density.duplicate()
	# Every tile consumes the same field and transform. Prepare them once per
	# update, including the inverse, rather than once per live terrain material.
	var parameters := [["optical_atlas", atlas_texture], ["optical_fixed_from_tangent", data.fixed_to_tangent.inverse()],
		["optical_planet_center", data.planet_center], ["optical_reference_radius", data.reference_radius],
		["optical_lower_height", data.lower_height_metres], ["optical_upper_height", data.upper_height_metres],
		["optical_opacity_cap", data.opacity_cap], ["optical_tint", data.tint],
		["optical_sun_direction", state.lighting.direction], ["optical_sun_color", state.lighting.star_color]]
	for material in materials:
		for pair in parameters:
			material.set_shader_parameter(pair[0], pair[1])
		material.set_shader_parameter("optical_enabled", data.opacity_cap > 0.0)
	return true
