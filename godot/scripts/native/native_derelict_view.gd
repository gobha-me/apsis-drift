extends Node3D
## An earned C++ contact, rendered without a pilot, propulsion or local clock.
const Hopper = preload("res://scripts/characters/hopper_presentation.gd")
const MAX_COMPONENT := 2.0e15
var model: Node3D
var contact_id := ""
var error := ""

static func valid_identity(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20 or value.begins_with("0"): return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 14: return false
	if typeof(value.get("version")) != TYPE_INT or value.version != 1: return false
	for field in ["selected", "known", "local", "inspected", "can_scan", "can_inspect"]:
		if not value.get(field) is bool: return false
	for field in ["note", "history"]:
		if not value.get(field) is String or value[field].length() > 512: return false
	if not value.get("id") is String: return false
	if not value.get("position") is Vector3 or not value.position.is_finite(): return false
	for component in [value.position.x, value.position.y, value.position.z]:
		if absf(component) > MAX_COMPONENT: return false
	if not value.get("basis") is Basis or not value.basis.is_finite() or absf(value.basis.determinant() - 1.0) > 0.00001 or not value.basis.is_equal_approx(value.basis.orthonormalized()): return false
	for field in ["distance_metres", "relative_speed_metres_per_second"]:
		if not value.get(field) is float or not is_finite(value[field]) or value[field] < 0.0 or value[field] > 4.0e15: return false
	if not value.selected and (value.known or value.local or value.inspected or value.can_scan or value.can_inspect): return false
	if value.known:
		if not value.selected or not valid_identity(value.id): return false
	elif not value.id.is_empty() or value.local or value.inspected or value.can_inspect: return false
	if not value.inspected and not value.history.is_empty(): return false
	if not value.local:
		if value.position != Vector3.ZERO or value.basis != Basis.IDENTITY or value.distance_metres != 0.0 or value.relative_speed_metres_per_second != 0.0 or value.can_inspect: return false
	elif not value.known: return false
	if value.can_inspect and (value.distance_metres < 100.0 or value.distance_metres > 500.0 or value.relative_speed_metres_per_second > 20.0): return false
	return true

static func text(value: Variant) -> String:
	if not valid_packet(value) or not value.selected or not value.known: return ""
	if not value.local:
		return "Derelict contact retained · another location" + (" · inspected" if value.inspected else "")
	return "DERELICT · %.1f %s · relative speed %.1f m/s%s" % [value.distance_metres / 1000.0 if value.distance_metres >= 1000.0 else value.distance_metres, "km" if value.distance_metres >= 1000.0 else "m", value.relative_speed_metres_per_second, " · exterior inspected" if value.inspected else ""]

func initialize(assets: String, binding: Dictionary, identity: String) -> bool:
	visible = false
	if model != null or not valid_identity(identity):
		error = "Derelict contact identity is unavailable"
		return false
	model = Hopper.load_selected_asset(assets, binding)
	if model == null or not model.valid_installed() or not model.set_gear_preview(0.0):
		if model != null: model.free()
		model = null
		error = "Admitted derelict Wayfarer asset is unavailable"
		return false
	add_child(model)
	contact_id = identity
	return true

func render_layer(node: Node, mask: int) -> void:
	if node is GeometryInstance3D: node.layers = mask
	for child in node.get_children(): render_layer(child, mask)

func present(packet: Dictionary, _paused: bool, near_distance: float, far_distance: float) -> bool:
	# Pausing adds no interpolation or animation: only the supplied committed
	# pose is used, exactly as when the world is running.
	visible = false
	if model == null or not error.is_empty() or not valid_packet(packet) or not is_finite(near_distance) or not is_finite(far_distance) or near_distance <= 0.0 or far_distance < near_distance: return false
	if packet.known and packet.id != contact_id: return false
	if not packet.local or packet.distance_metres > far_distance: return true
	transform = Transform3D(packet.basis, packet.position)
	render_layer(self, 2 if packet.distance_metres <= near_distance else 1)
	visible = true
	return true
