extends RefCounted
## Validate the explicit lunar presentation owner. No planetary identity aliases.
const Hopper = preload("res://scripts/characters/hopper_presentation.gd")

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for byte in value.to_ascii_buffer():
		if byte < 48 or byte > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func rigid(value: Variant) -> bool:
	return value is Basis and value.is_finite() and absf(value.determinant() - 1.0) < 0.0001 and value.is_equal_approx(value.orthonormalized())

static func valid(value: Dictionary) -> bool:
	if value.get("mode") != "freedom_flight" or value.get("body_kind") != "moon" or value.get("body_version") != 1 or value.has("planet_id"): return false
	for key in ["universe_seed", "system_id", "body_id", "craft_id", "frame_id", "tick", "checksum"]:
		if not decimal(value.get(key)): return false
	if value.get("frame_id") != "2" or typeof(value.get("frame_version")) != TYPE_INT or value.frame_version != 1 or not value.get("assistance") is bool: return false
	for key in ["body_radius", "latitude", "longitude", "altitude", "surface_speed", "radial_rate"]:
		if not value.get(key) is float or not is_finite(value[key]): return false
	if value.body_radius <= 0 or value.altitude <= -value.body_radius: return false
	if not rigid(value.get("body_basis")) or not rigid(value.get("station_basis")): return false
	var star: Variant = value.get("star_direction")
	if not star is Vector3 or not star.is_finite() or absf(star.length_squared() - 1.0) > 0.0001: return false
	for key in ["surface", "surface_walk", "resources", "jump", "boarding"]:
		if not value.get(key) is Dictionary: return false
	var boarding: Dictionary = value.boarding
	if boarding.get("state") != "seated" or boarding.get("seated") != true or boarding.get("complete") != true or Hopper.projected_pose(boarding.get("pose")).is_empty(): return false
	var eye: Variant = boarding.get("eye_craft")
	if not eye is PackedFloat64Array or eye.size() != 3: return false
	for component in eye:
		if not is_finite(component) or absf(component) > 100.0: return false
	var resource: Dictionary = value.resources
	if typeof(resource.get("selected")) != TYPE_BOOL or not resource.selected: return false
	if not resource.get("fraction") is float or not is_finite(resource.fraction) or resource.fraction < 0 or resource.fraction > 1: return false
	if typeof(resource.get("jump_charges")) != TYPE_INT or resource.jump_charges < 0 or resource.jump_charges > 3: return false
	var jump: Dictionary = value.jump
	if typeof(jump.get("available")) != TYPE_BOOL or jump.get("phase") not in ["idle", "spool", "transit"] or not jump.get("remaining_seconds") is float or not is_finite(jump.remaining_seconds) or jump.remaining_seconds < 0: return false
	for sign_name in ["positive", "negative"]:
		var force: Variant = value.get(sign_name + "_force_body")
		var ratings: Variant = value.get(sign_name + "_force_ratings")
		if not force is PackedFloat64Array or force.size() != 3 or not ratings is PackedFloat64Array or ratings.size() != 3: return false
		for axis in 3:
			if not is_finite(force[axis]) or force[axis] < 0 or not is_finite(ratings[axis]) or ratings[axis] <= 0: return false
	var color: Variant = value.get("star_color")
	if not color is Color: return false
	for component in [color.r, color.g, color.b, color.a]:
		if not is_finite(component) or component < 0 or component > 1: return false
	if color.a != 1 or not value.get("star_angular_radius") is float or not is_finite(value.star_angular_radius) or value.star_angular_radius <= 0 or value.star_angular_radius > PI / 2: return false
	var surface: Dictionary = value.surface
	if not surface.get("landed") is bool or not surface.get("gear_deployed") is bool: return false
	var walk: Dictionary = value.surface_walk
	if not walk.is_empty():
		if walk.get("version") != 1 or not walk.get("eye_position") is Vector3 or not walk.eye_position.is_finite() or not rigid(walk.get("basis")): return false
		if not walk.get("heading") is float or not is_finite(walk.heading) or absf(walk.heading) > PI: return false
		var access: Variant = walk.get("return_access")
		if not access is Dictionary or not access.get("available") is bool: return false
		if access.available and (not access.get("within_reach") is bool or not access.get("distance_metres") is float or not is_finite(access.distance_metres) or access.distance_metres < 0 or not access.get("position") is Vector3 or not access.position.is_finite()): return false
	return true
