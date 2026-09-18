extends RefCounted
## Shared with the Blender authoring tools; no independent camera scale fudge.
static var cached: Dictionary = {}

static func valid(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema_version") != 1 or value.get("geometry_revision") != 5 or value.get("units") != "metres":
		return false
	for key in ["pilot_eye_blender", "cabin_to_ship"]:
		var vector: Variant = value.get(key)
		if not vector is Array or vector.size() != 3:
			return false
		for component in vector:
			if not (component is float or component is int) or not is_finite(float(component)) or absf(float(component)) > 100:
				return false
	return true

static func spec() -> Dictionary:
	if cached.is_empty():
		var value: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://cockpit-layout.json"))
		if not valid(value):
			push_error("Invalid shared cockpit geometry/camera contract")
			return {}
		cached = value
	return cached

static func converted(values: Array) -> Vector3:
	return Vector3(float(values[0]), float(values[2]), -float(values[1]))

static func eye() -> Vector3:
	var data := spec()
	return converted(data.pilot_eye_blender) if not data.is_empty() else Vector3(INF, INF, INF)

static func mount() -> Transform3D:
	var data := spec()
	return Transform3D(Basis.IDENTITY, converted(data.cabin_to_ship)) if not data.is_empty() else Transform3D.IDENTITY

static func hide_duplicate_envelope(node: Node) -> void:
	if node is MeshInstance3D and (str(node.name).begins_with("Cell_glazing") or str(node.name).begins_with("Cell_pressure_skin") or str(node.name).begins_with("Cell_structural_frame") or str(node.name).begins_with("Cell glazing") or str(node.name).begins_with("Cell pressure skin") or str(node.name).begins_with("Cell structural frame")):
		node.hide()
	for child in node.get_children():
		hide_duplicate_envelope(child)
