extends Node3D
## Read-only rendering of one independently owned C++ service Wayfarer.
## Position is supplied relative to the player's current render origin.
const Hopper = preload("res://scripts/characters/hopper_presentation.gd")
const Exhaust = preload("res://scripts/native/native_main_exhaust.gd")
var model: Node3D
var exhaust: Node3D
var craft_id := ""
var error := ""

static func valid_identity(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20 or value.begins_with("0"): return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func valid_packet(value: Variant, elapsed: float) -> bool:
	if not value is Dictionary or value.size() != 6: return false
	if typeof(value.get("version")) != TYPE_INT or value.version != 1 or not valid_identity(value.get("craft_id")) or not value.get("visible") is bool: return false
	if not value.get("position") is Vector3 or not value.position.is_finite(): return false
	for component in [value.position.x, value.position.y, value.position.z]:
		if absf(component) > 2.0e15: return false
	if not value.get("basis") is Basis or not value.basis.is_finite() or not value.basis.is_equal_approx(value.basis.orthonormalized()) or absf(value.basis.determinant() - 1.0) > 0.00001: return false
	if not value.get("propulsion") is Dictionary or not Exhaust.valid_applied(value.propulsion, elapsed): return false
	for sign_name in ["positive", "negative"]:
		var force: PackedFloat64Array = value.propulsion[sign_name + "_force_body"]
		var ratings: PackedFloat64Array = value.propulsion[sign_name + "_force_ratings"]
		for axis in 3:
			if force[axis] > ratings[axis]: return false
	return true

func initialize(assets: String, binding: Dictionary, identity: String) -> bool:
	if model != null or not valid_identity(identity):
		error = "Service craft identity is unavailable"
		return false
	model = Hopper.load_selected_asset(assets, binding)
	if model == null or not model.valid_installed() or not model.set_gear_preview(0.0):
		if model != null: model.free()
		model = null
		error = "Selected service Wayfarer asset is unavailable"
		return false
	add_child(model)
	exhaust = Exhaust.new()
	add_child(exhaust)
	if not exhaust.bind_skin(model, model.exterior_surface):
		error = "Service Wayfarer exhaust interface is unavailable"
		return false
	craft_id = identity
	visible = false
	return true

func render_layer(node: Node, mask: int) -> void:
	if node is GeometryInstance3D: node.layers = mask
	for child in node.get_children(): render_layer(child, mask)

func present(packet: Dictionary, elapsed: float, paused: bool, near_distance: float) -> bool:
	if model == null or not error.is_empty() or not valid_packet(packet, elapsed) or packet.craft_id != craft_id or not is_finite(near_distance) or near_distance <= 0.0:
		visible = false
		return false
	# In-transit or another-frame craft is hidden by C++ visibility, never moved
	# to a made-up nearby position by the presentation layer.
	visible = packet.visible
	if visible: transform = Transform3D(packet.basis, packet.position)
	# Reuse the current native close/distant pass and lights. No second camera,
	# world, interpolation clock, movement controller or local position cache.
	render_layer(self, 2 if packet.position.length() <= near_distance else 1)
	return exhaust.update_applied(packet.propulsion, elapsed, paused or not visible)
