extends RefCounted
## Read-only inspection packet, with no PlanetId or Moon identity alias.
const Companion = preload("res://scripts/native/native_moon_state.gd")

static func valid(value: Dictionary) -> bool:
	if not Companion.valid_companion(value, "minor"): return false
	if value.get("coordinate_frame") != "minor_relative_inertial" or value.get("attached") != false: return false
	if value.surface.landed or value.surface.gear_deployed or not value.surface_walk.is_empty() or value.surface.gear_clearance.available: return false
	if value.surface_marker.current_body or value.surface_marker.can_mark: return false
	if not value.get("minor_center") is Vector3 or not value.minor_center.is_finite() or not Companion.rigid(value.get("minor_fixed_basis")): return false
	if not value.get("minor_tint") is Color: return false
	for component in [value.minor_tint.r, value.minor_tint.g, value.minor_tint.b, value.minor_tint.a]:
		if not is_finite(component) or component < 0.0 or component > 1.0: return false
	if value.minor_tint.a != 1.0: return false
	for key in ["exclusion_radius", "stand_off_gap"]:
		if not value.get(key) is float or not is_finite(value[key]): return false
	if value.exclusion_radius <= value.body_radius or value.stand_off_gap <= 0.0: return false
	return value.get("inspection_limited") is bool and value.inspection_limited
