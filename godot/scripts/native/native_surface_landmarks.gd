extends RefCounted
## Bounded C++ geological sites; presentation owns no placement or inspection.
const EnvironmentState = preload("res://scripts/native/native_environment.gd")
const MAX_SITES := 8

static func valid(state: Dictionary) -> bool:
	if not state.has("surface_landmarks"): return true
	var data: Variant = state.surface_landmarks
	if not data is Dictionary or data.size() != 8 or typeof(data.get("version")) != TYPE_INT or data.version != 1: return false
	for field in ["selected", "can_enable"]:
		if typeof(data.get(field)) != TYPE_BOOL: return false
	if data.selected and data.can_enable: return false
	for field in ["system_id", "planet_id", "tick"]:
		if not EnvironmentState.decimal(data.get(field)) or data[field] != state.get(field): return false
	if typeof(data.get("inspection_count")) != TYPE_INT or data.inspection_count < 0 or data.inspection_count > 64: return false
	if not data.get("sites") is Array or data.sites.size() > MAX_SITES: return false
	if not data.selected and (not data.sites.is_empty() or data.inspection_count != 0): return false
	var outside: Variant = state.get("surface_walk", {})
	var on_foot: bool = outside is Dictionary and not outside.is_empty()
	var ids := {}
	for site in data.sites:
		if not site is Dictionary or site.size() != 11 or not EnvironmentState.decimal(site.get("id")) or ids.has(site.id): return false
		ids[site.id] = true
		if typeof(site.get("kind")) != TYPE_INT or site.kind < 0 or site.kind > 2: return false
		if not site.get("label") is String or site.label.length() > 64 or not site.get("description") is String or site.description.length() > 256: return false
		if not site.get("position") is Vector3 or not site.position.is_finite(): return false
		if not site.get("radius_metres") is float or not is_finite(site.radius_metres) or site.radius_metres < 28.0 or site.radius_metres > 52.0: return false
		if not EnvironmentState.color(site.get("tint")): return false
		if not site.get("distance_metres") is float or not is_finite(site.distance_metres) or site.distance_metres < 0.0: return false
		for field in ["nearby", "can_inspect", "inspected"]:
			if typeof(site.get(field)) != TYPE_BOOL: return false
		if site.nearby and (not on_foot or site.distance_metres > site.radius_metres + 8.0): return false
		if site.can_inspect and (not site.nearby or site.distance_metres > 8.0): return false
		if site.inspected and data.inspection_count == 0: return false
	return true

static func nearest_nearby(state: Dictionary) -> Dictionary:
	if not valid(state): return {}
	var data: Dictionary = state.get("surface_landmarks", {})
	var nearest: Dictionary = {}
	for site: Dictionary in data.get("sites", []):
		if not site.nearby: continue
		if nearest.is_empty() or site.distance_metres < nearest.distance_metres or (site.distance_metres == nearest.distance_metres and (site.id.length() < nearest.id.length() or (site.id.length() == nearest.id.length() and site.id < nearest.id))): nearest = site
	return nearest

func apply(state: Dictionary, materials: Array[ShaderMaterial]) -> bool:
	# Validate the whole owner/packet and destination set before any shader write.
	if not valid(state): return false
	for material in materials:
		if material == null or material.shader == null: return false
	var sites: Array = state.get("surface_landmarks", {}).get("sites", [])
	if sites.is_empty():
		for material in materials:
			material.set_shader_parameter("surface_landmark_count", 0)
			material.set_shader_parameter("surface_landmarks_enabled", false)
		return true
	var centres := PackedVector3Array()
	var radii := PackedFloat32Array()
	var tints := PackedVector4Array()
	centres.resize(MAX_SITES)
	radii.resize(MAX_SITES)
	tints.resize(MAX_SITES)
	for i in sites.size():
		centres[i] = sites[i].position
		radii[i] = sites[i].radius_metres
		var tint: Color = sites[i].tint
		tints[i] = Vector4(tint.r, tint.g, tint.b, tint.a)
	for material in materials:
		material.set_shader_parameter("surface_landmark_centres", centres)
		material.set_shader_parameter("surface_landmark_radii", radii)
		material.set_shader_parameter("surface_landmark_tints", tints)
		material.set_shader_parameter("surface_landmark_count", sites.size())
		material.set_shader_parameter("surface_landmarks_enabled", not sites.is_empty())
	return true
