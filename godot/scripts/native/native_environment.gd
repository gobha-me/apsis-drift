extends RefCounted
## Read-only current-body lighting; C++ owns the body, atmosphere and time.
const SkyShader = preload("res://shaders/native_navigation_sky.gdshader")

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value[0] == "0": return false
	for byte in value.to_ascii_buffer():
		if byte < 48 or byte > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func color(value: Variant) -> bool:
	return value is Color and is_finite(value.r) and is_finite(value.g) and is_finite(value.b) and value.r >= 0 and value.r <= 1 and value.g >= 0 and value.g <= 1 and value.b >= 0 and value.b <= 1 and value.a == 1

static func valid(state: Dictionary) -> bool:
	var data: Variant = state.get("lighting")
	if not data is Dictionary or not data.get("version") is int or data.version != 1 or data.get("probe_frame") != "native_tangent_metres" or data.get("body_kind") != "planet" or not data.get("body_version") is int or data.body_version != 1: return false
	if not data.get("catalog_generator") is int or data.catalog_generator not in [1, 2]: return false
	for pair in [["system_id", "system_id"], ["body_id", "planet_id"], ["tick", "tick"]]:
		if not decimal(data.get(pair[0])) or data[pair[0]] != state.get(pair[1]): return false
	if not decimal(data.get("star_id")): return false
	for value in [state.get("planet_radius"), state.get("altitude"), data.get("scale_height"), data.get("sea_density"), data.get("atmosphere_edge"), data.get("star_angular_radius")]:
		if not (value is float or value is int) or not is_finite(float(value)): return false
	if state.planet_radius <= 0 or state.planet_radius > 1.0e12 or absf(state.altitude) > 1.0e12 or state.altitude <= -state.planet_radius or data.scale_height > 1.0e9 or data.sea_density > 1.0e8 or data.atmosphere_edge > 1.0e12 or data.star_angular_radius <= 0 or data.star_angular_radius > PI / 2: return false
	if data.atmosphere_edge != state.get("atmosphere_edge"): return false
	if data.scale_height == 0:
		if data.sea_density != 0 or data.atmosphere_edge != 0: return false
	elif data.scale_height < 0 or data.sea_density <= 0 or data.atmosphere_edge <= 0: return false
	if not color(data.get("star_color")) or data.star_color.r + data.star_color.g + data.star_color.b <= 0 or not color(data.get("atmosphere_tint")): return false
	var direction: Variant = data.get("direction")
	return direction is Vector3 and direction.is_finite() and absf(direction.length_squared() - 1.0) < 0.0001 and direction == state.get("star_direction")

static func install(environment: Environment) -> ShaderMaterial:
	var material := ShaderMaterial.new()
	material.shader = SkyShader
	var sky := Sky.new()
	sky.sky_material = material
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	environment.sky = sky
	environment.background_mode = Environment.BG_SKY
	return material

static func apply(state: Dictionary, environment: Environment, light: DirectionalLight3D, material: ShaderMaterial) -> bool:
	if not valid(state): return false
	var data: Dictionary = state.lighting
	for pair in [["radius", state.planet_radius], ["altitude", state.altitude], ["scale_height", data.scale_height], ["sea_density", data.sea_density], ["atmosphere_edge", data.atmosphere_edge], ["atmosphere_tint", data.atmosphere_tint], ["sun_direction", data.direction], ["sun_color", data.star_color], ["sun_angular_radius", data.star_angular_radius]]:
		material.set_shader_parameter(pair[0], pair[1])
	var horizon := -acos(clampf(float(state.planet_radius) / (float(state.planet_radius) + maxf(0.0, float(state.altitude))), 0.0, 1.0))
	var elevation := asin(clampf(data.direction.y, -1.0, 1.0))
	var daylight := smoothstep(horizon - data.star_angular_radius, horizon + data.star_angular_radius + 0.08, elevation)
	environment.ambient_light_energy = 0.06 + 0.54 * daylight
	environment.ambient_light_color = data.star_color.lerp(data.atmosphere_tint, 0.35)
	light.light_color = data.star_color
	return true
