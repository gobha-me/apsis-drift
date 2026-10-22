extends RefCounted
## Admit the C++ personal point; presentation never supplies its position.
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")

static func body_name(marker: Dictionary) -> String:
	return str(marker.get("body_name", marker.get("planet_name", "Unresolved body")))

static func valid(value: Dictionary) -> bool:
	if not value.has("surface_marker"): return true
	var marker: Variant = value.surface_marker
	if not marker is Dictionary or typeof(marker.get("version")) != TYPE_INT or marker.version not in [1, 2, 3]: return false
	var lunar: bool = marker.version in [2, 3]
	for field in ["selected", "present", "current_body", "can_mark", "can_clear"]:
		if typeof(marker.get(field)) != TYPE_BOOL: return false
	for field in ["system_name", "body_name" if lunar else "planet_name"]:
		if not marker.get(field) is String or marker[field].length() > 256: return false
	if (marker.current_body and not marker.present) or (marker.present and not marker.selected) or (marker.can_clear and not marker.present): return false
	if lunar:
		# Explicit lunar versions are closed: no PlanetId alias or stale local bearing.
		var keys := ["version", "selected", "present", "current_body", "can_mark", "can_clear", "system_name", "body_name"]
		if not marker.selected: return false
		if marker.present: keys.append_array(["system_id", "body_kind", "body_version", "body_id", "tick"])
		if marker.current_body: keys.append_array(["position", "distance_metres"])
		if marker.size() != keys.size(): return false
		for field in keys:
			if not marker.has(field): return false
	if not marker.present:
		return not marker.has("position") and not marker.has("distance_metres")
	for field in ["system_id", "body_id" if lunar else "planet_id", "tick"]:
		if not NativeEnvironment.decimal(marker.get(field)): return false
	if lunar and (marker.get("body_kind") != "moon" or typeof(marker.get("body_version")) != TYPE_INT or marker.body_version != 1): return false
	if not NativeEnvironment.decimal(value.get("tick")) or marker.tick.length() > value.tick.length() or (marker.tick.length() == value.tick.length() and marker.tick > value.tick): return false
	if not marker.current_body: return not marker.has("position") and not marker.has("distance_metres")
	var same_body: bool = marker.body_id == value.get("body_id") and marker.body_version == value.get("body_version") and value.get("body_kind") == "moon" if lunar else marker.planet_id == value.get("planet_id") and value.get("body_kind", "planet") == "planet"
	return marker.system_id == value.get("system_id") and same_body and marker.get("position") is Vector3 and marker.position.is_finite() and marker.get("distance_metres") is float and is_finite(marker.distance_metres) and marker.distance_metres >= 0.0
