extends Control
## Code-native fractured-view demonstration from saved C++ craft condition.
## This is an optical cue, not a glass-fracture, pressure or oxygen model.
const Identifiers = preload("res://scripts/native/native_environment.gd")
var forebody_severity := 0
var reduced_view := false
var craft_id := ""


static func valid(value: Dictionary, craft: String, frame: String, frame_version: int) -> bool:
	if not value.get("selected") is bool:
		return false
	if not value.selected:
		return value.size() == 1
	if value.get("policy_version") != 1 or typeof(value.get("policy_version")) != TYPE_INT or value.get("version") not in [1, 2] or typeof(value.get("version")) != TYPE_INT:
		return false
	if value.get("craft_id") != craft or value.get("frame_id") != frame or value.get("frame_version") != frame_version or typeof(value.get("frame_version")) != TYPE_INT:
		return false
	for field in ["forebody", "landing_structure"]:
		if typeof(value.get(field)) != TYPE_INT or value[field] < 0 or value[field] > 3:
			return false
	for field in ["cracked", "visibility_degraded", "landing_available", "pending_loss"]:
		if not value.get(field) is bool:
			return false
	if value.cracked != (value.forebody >= 1) or value.visibility_degraded != (value.forebody >= 2) or value.landing_available != (value.landing_structure < 2):
		return false
	if not Identifiers.decimal(value.get("tick")) or not value.get("latest") is Dictionary:
		return false
	var event: Dictionary = value.latest
	if event.is_empty():
		return value.version == 1
	for field in ["serial", "tick", "source_condition_digest", "source_checksum", "candidate_checksum", "system_id", "planet_id"]:
		if not Identifiers.decimal(event.get(field)):
			return false
	if event.serial == "0" or event.get("region") not in ["forebody", "landing_structure"]:
		return false
	if typeof(event.get("damage_class")) != TYPE_INT or event.damage_class < 0 or event.damage_class > 3 or not event.get("catastrophic") is bool:
		return false
	if typeof(event.get("closing_speed")) not in [TYPE_FLOAT, TYPE_INT] or not is_finite(float(event.closing_speed)) or event.closing_speed < 0.0:
		return false
	if typeof(event.get("fraction")) not in [TYPE_FLOAT, TYPE_INT] or not is_finite(float(event.fraction)) or event.fraction < 0.0 or event.fraction > 1.0:
		return false
	var point: Variant = event.get("contact_point_craft")
	if not point is PackedFloat64Array or point.size() != 3:
		return false
	for coordinate in point:
		if not is_finite(coordinate):
			return false
	if value.version == 2:
		if value.size() != 15: return false
		var repair: Variant = value.get("repair")
		if not repair is Dictionary or repair.size() != 4 or not repair.get("paid") is bool: return false
		for field in ["tick", "boundary_serial", "credits"]:
			if not Identifiers.decimal(repair.get(field)): return false
		if repair.boundary_serial == "0" or decimal_before(event.serial, repair.boundary_serial) or decimal_before(value.tick, repair.tick) or (decimal_before(repair.tick, event.tick) and event.serial == repair.boundary_serial): return false
		if repair.paid:
			if decimal_before("60", repair.credits) or repair.credits == "0" or int(repair.credits) % 10 != 0: return false
		elif repair.credits != "0": return false
		if event.serial == repair.boundary_serial:
			if value.forebody != 0 or value.landing_structure != 0: return false
		elif not decimal_before(repair.tick, event.tick): return false
	return true


static func decimal_before(left: String, right: String) -> bool:
	return left.length() < right.length() or (left.length() == right.length() and left < right)


static func condition_text(value: Dictionary) -> String:
	if not value.get("selected", false):
		return "Terrain impact consequences unselected"
	var forebody := "Forebody intact" if value.forebody == 0 else "Forebody damaged · view cracks" if not value.visibility_degraded else "Forebody damaged · reduced cockpit visibility"
	var landing := "Landing structure intact" if value.landing_structure == 0 else "Landing structure damaged" if value.landing_available else "Landing structure damaged · landing unavailable"
	return forebody + "\n" + landing


static func feedback(value: Dictionary) -> String:
	if not value.get("selected", false):
		return ""
	if value.pending_loss:
		return "Craft lost · recovery pending"
	if value.latest.is_empty():
		return ""
	if value.get("version", 1) == 2 and value.get("repair", {}).get("boundary_serial") == value.latest.serial:
		return "Craft repaired at Origin · previous terrain impact retained as history"
	if value.latest.damage_class == 0:
		return "Terrain contact · no new damage"
	if value.latest.region == "landing_structure":
		return "Landing structure damaged" + (" · landing unavailable" if not value.landing_available else "")
	return "Forebody damaged · " + ("reduced cockpit visibility" if value.visibility_degraded else "view cracks")


func apply(value: Dictionary, craft: String, frame: String, frame_version: int, cockpit: bool, outside: bool) -> bool:
	if not valid(value, craft, frame, frame_version):
		visible = false
		forebody_severity = 0
		reduced_view = false
		craft_id = ""
		queue_redraw()
		return false
	var severity: int = value.forebody if value.selected else 0
	var degraded: bool = value.visibility_degraded if value.selected else false
	if severity != forebody_severity or degraded != reduced_view or craft != craft_id:
		forebody_severity = severity
		reduced_view = degraded
		craft_id = craft
		queue_redraw()
	visible = cockpit and not outside and forebody_severity >= 1
	return true


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE
	resized.connect(queue_redraw)
	visible = false


func _draw() -> void:
	if forebody_severity < 1 or not size.is_finite() or size.x <= 0.0 or size.y <= 0.0:
		return
	# Fixed normalized view lines keep the retained cue stable across viewport
	# sizes, look direction and frame cadence. They consume no simulation time.
	var points := PackedVector2Array([
		Vector2(0.78, 0.03), Vector2(0.73, 0.22),
		Vector2(0.73, 0.22), Vector2(0.84, 0.39),
		Vector2(0.73, 0.22), Vector2(0.61, 0.29),
		Vector2(0.84, 0.39), Vector2(0.94, 0.42),
		Vector2(0.84, 0.39), Vector2(0.79, 0.55),
		Vector2(0.61, 0.29), Vector2(0.57, 0.18),
		Vector2(0.61, 0.29), Vector2(0.53, 0.34)])
	for index in points.size():
		points[index] *= size
	draw_multiline(points, Color(0.01, 0.025, 0.04, 0.9), 3.0, true)
	draw_multiline(points, Color(0.72, 0.84, 0.9, 0.9), 1.0, true)
	if reduced_view:
		# Declared optical obstruction, leaving the central flight view and
		# controls readable. Never changes the camera, hull or atmosphere.
		var mask := PackedVector2Array([Vector2(0.68, 0), Vector2(1, 0), Vector2(1, 0.67), Vector2(0.86, 0.42), Vector2(0.73, 0.22)])
		for index in mask.size():
			mask[index] *= size
		draw_colored_polygon(mask, Color(0.04, 0.065, 0.075, 0.96))
