extends Node3D
## Measured asset adapter only. Flight, landing and damage remain C++ owned.
const CONTRACT := "hopper-wayfarer-01.json"
var specification: Dictionary = {}
var gear_nodes: Dictionary = {}

static func finite_vector(value: Variant, limit: float = 100.0) -> bool:
	if not value is Array or value.size() != 3:
		return false
	for component in value:
		if not (component is float or component is int) or not is_finite(float(component)) or absf(float(component)) > limit:
			return false
	return true

static func vector(value: Array) -> Vector3:
	return Vector3(value[0], value[1], value[2])

static func pose_from_columns(value: Array) -> Transform3D:
	return Transform3D(Basis(vector(value[0]), vector(value[1]), vector(value[2])), vector(value[3]))

static func valid_transform(value: Variant, rigid: bool = true) -> bool:
	if not value is Array or value.size() != 4:
		return false
	for column in value:
		if not finite_vector(column):
			return false
	var pose := pose_from_columns(value)
	if not rigid:
		# Authored telescoping rods change length; forbid collapse/reflection.
		return pose.basis.determinant() > 0.1 and pose.basis.determinant() < 10.0 and pose.basis.x.length() < 10 and pose.basis.y.length() < 10 and pose.basis.z.length() < 10
	return pose.basis.is_equal_approx(pose.basis.orthonormalized()) and absf(pose.basis.determinant() - 1.0) < 0.001

static func valid(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema_version") != 1 or value.get("id") != "wayfarer" or value.get("units") != "metres":
		return false
	if not finite_vector(value.get("pilot_eye"), 10.0) or value.get("vertical_fov_degrees") != 75:
		return false
	if value.get("model") != "hopper-wayfarer-01.glb" or not value.get("model_sha256") is String or value.model_sha256.length() != 64:
		return false
	var screens: Variant = value.get("screens")
	if not screens is Array or screens.size() != 3:
		return false
	for i in 3:
		var screen: Variant = screens[i]
		if not screen is Dictionary or screen.get("page") != i or not valid_transform(screen.get("transform")):
			return false
		if not screen.get("size") is Array or screen["size"].size() != 2:
			return false
		for size_component in screen["size"]:
			if not (size_component is float or size_component is int) or not is_finite(float(size_component)) or size_component < 0.1 or size_component > 1.0:
				return false
	var count: Variant = value.get("gear_preview_samples")
	if not (count is int or count is float) or not is_finite(float(count)) or count != int(count) or count < 2 or count > 64:
		return false
	if not value.get("gear_preview") is Dictionary or value.gear_preview.size() > 128:
		return false
	for key in value.gear_preview:
		if not key is String or not key.begins_with("HopperGear") or not value.gear_preview[key] is Array or value.gear_preview[key].size() != int(count):
			return false
		for pose in value.gear_preview[key]:
			if not valid_transform(pose, false):
				return false
	return true

static func load_asset(directory: String) -> Node3D:
	var input := FileAccess.open(directory.path_join(CONTRACT), FileAccess.READ)
	if input == null or input.get_length() > 2 * 1024 * 1024:
		return null
	var spec: Variant = JSON.parse_string(input.get_as_text())
	if not valid(spec):
		return null
	var path: String = directory.path_join(spec.model)
	if FileAccess.get_sha256(path) != spec.model_sha256:
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	for mesh in state.get_meshes():
		mesh.get_mesh().generate_lods(60.0, 25.0, [])
	var model := document.generate_scene(state) as Node3D
	if model == null:
		return null
	var instance = load("res://hopper_presentation.gd").new()
	instance.name = "Wayfarer"
	instance.specification = spec
	instance.add_child(model)
	var glass: Node3D = model.find_child("HopperGlass", true, false)
	if not glass is MeshInstance3D:
		instance.free()
		return null
	# glTF transmission does not provide the required native cockpit visibility.
	# Deliberate clear glass presentation; no shader pretending opaque hull is glass.
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = Color(0.75, 0.90, 0.96, 0.055)
	material.roughness = 0.08
	material.metallic_specular = 0.18
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	glass.material_override = material
	glass.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	for key in spec.gear_preview:
		var node := model.find_child(key, true, false) as Node3D
		if node == null:
			instance.free()
			return null
		instance.gear_nodes[key] = node
	return instance

func set_gear_preview(deployed: float) -> bool:
	# Explicit asset inspection, never inferred from altitude or used as landing state.
	if not is_finite(deployed) or deployed < 0 or deployed > 1:
		return false
	var sample := (1.0 - deployed) * (int(specification.gear_preview_samples) - 1)
	var low := int(floor(sample))
	var high := mini(low + 1, int(specification.gear_preview_samples) - 1)
	for key in gear_nodes:
		var poses: Array = specification.gear_preview[key]
		var a := pose_from_columns(poses[low])
		var b := pose_from_columns(poses[high])
		var weight := sample - low
		gear_nodes[key].transform = Transform3D(Basis(a.basis.x.lerp(b.basis.x, weight), a.basis.y.lerp(b.basis.y, weight), a.basis.z.lerp(b.basis.z, weight)), a.origin.lerp(b.origin, weight))
	return true
