extends RefCounted
## C++ supplied artist coefficients; no terrain, climate or material simulation.
const EnvironmentState = preload("res://scripts/native/native_environment.gd")
const LIMITS = {"albedo_scale_basis_points": 10000, "detail_contrast_basis_points": 20000,
	"roughness_basis_points": 10000, "specular_basis_points": 10000}
var initialized := false
var changed := false
var selected: Dictionary = {}
var parameters: Array = []

static func valid(state: Dictionary) -> bool:
	if not state.has("surface_appearance"): return true
	var data: Variant = state.surface_appearance
	if not data is Dictionary or data.size() != 8: return false
	for field in ["version", "disclosure"]:
		if not data.get(field) is int or data[field] != 1: return false
	for field in ["system_id", "planet_id"]:
		if not EnvironmentState.decimal(data.get(field)) or data[field] != state.get(field): return false
	for field in LIMITS:
		if not data.get(field) is int or data[field] < 0 or data[field] > LIMITS[field]: return false
	return true

func select(state: Dictionary) -> bool:
	if not valid(state): return false
	var candidate: Dictionary = state.get("surface_appearance", {})
	changed = not initialized or candidate != selected
	if not changed: return true
	selected = candidate.duplicate(true)
	parameters = [["surface_profile_enabled", not selected.is_empty()]]
	if not selected.is_empty():
		for pair in [["surface_albedo_scale", "albedo_scale_basis_points"],
			["surface_detail_contrast", "detail_contrast_basis_points"],
			["surface_roughness", "roughness_basis_points"],
			["surface_specular", "specular_basis_points"]]:
			parameters.append([pair[0], float(selected[pair[1]]) / 10000.0])
	initialized = true
	return true

func apply(materials: Array[ShaderMaterial]) -> bool:
	if not initialized: return false
	# Validate the complete destination set before changing any accepted material.
	for material in materials:
		if material == null or material.shader == null: return false
	for material in materials:
		for pair in parameters:
			material.set_shader_parameter(pair[0], pair[1])
	return true
