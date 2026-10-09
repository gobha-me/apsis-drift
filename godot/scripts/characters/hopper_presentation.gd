extends Node3D
## Measured asset adapter only. Flight, landing and damage remain C++ owned.
const CONTRACT := "hopper-wayfarer-01.json"
var specification: Dictionary = {}
var gear_nodes: Dictionary = {}
var exterior_surface := 18
var selected_binding: Dictionary = {}
var replacement_nodes: Array[Node3D] = []
var operating_nodes: Dictionary = {}
var runtime_pose: Dictionary = {}
var atlas_material: StandardMaterial3D
var glass_material: StandardMaterial3D
var atlas_state: Array = []
const Operating = preload("res://scripts/ships/wayfarer_operating_view.gd")

const STOWED_MODEL := "d286dfd5174940ccd7e3db0cb023d3571ad460c6b22609c6f81208e16f06e2fc"
const STOWED_FRAME := "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6"
const STOWED_ROOTS := ["WFStowed0", "WFStowed1", "WFStowed2", "WFStowed3", "WFStowed4", "WFStowed5", "WFStowed6", "WFStowedBridge_crotch", "WFStowedBridge_lap_port", "WFStowedBridge_lap_starboard", "WFStowedBridge_shoulder_port", "WFStowedBridge_shoulder_starboard", "WFStowedManifold", "WFStowedResidual"]

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
	var model := import_model(path, 18)
	if model == null:
		return null
	var instance = load("res://scripts/characters/hopper_presentation.gd").new()
	instance.name = "Wayfarer"
	instance.specification = spec
	instance.add_child(model)
	if not instance.bind_calibration(model, spec):
		instance.free()
		return null
	return instance

# One selected atlas index drives both LOD/packing and exhaust attachment.
static func import_model(path: String, atlas: int = -1) -> Node3D:
	if not Operating.valid_glb_container(path):
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var chart_count := 0
	for mesh in state.get_meshes():
		var imported: ImporterMesh = mesh.get_mesh()
		var fixed_skin := false
		if atlas >= 0 and imported.get_surface_count() > atlas:
			var coating := imported.get_surface_material(atlas)
			fixed_skin = coating != null and coating.resource_name == "WF08 | factory-new exterior atlas"
		if fixed_skin:
			chart_count += 1
			if imported.get_blend_shape_count() != 0:
				return null
			var source_skin := imported.get_surface_arrays(atlas)
			imported.generate_lods(60.0, 25.0, [])
			var retained := ImporterMesh.new()
			for surface in imported.get_surface_count():
				var lods := {}
				var arrays := source_skin if surface == atlas else imported.get_surface_arrays(surface)
				var flags := imported.get_surface_format(surface)
				if surface == atlas:
					flags &= ~Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES
				else:
					for level in imported.get_surface_lod_count(surface):
						lods[imported.get_surface_lod_size(surface, level)] = imported.get_surface_lod_indices(surface, level)
				retained.add_surface(imported.get_surface_primitive_type(surface), arrays, [], lods, imported.get_surface_material(surface), imported.get_surface_name(surface), flags)
			mesh.set_mesh(retained)
		else:
			imported.generate_lods(60.0, 25.0, [])
	if atlas >= 0 and chart_count != 1:
		return null
	return document.generate_scene(state) as Node3D

func bind_calibration(model: Node3D, spec: Dictionary) -> bool:
	var nodes := Operating.unique_nodes(model)
	var glass: Variant = nodes.get("HopperGlass")
	if not glass is MeshInstance3D:
		return false
	var gears := {}
	for key in spec.gear_preview:
		var node: Variant = nodes.get(key)
		if not node is Node3D:
			return false
		gears[key] = node
	var skin: Variant = nodes.get("HopperStructure")
	if not skin is MeshInstance3D or skin.mesh == null or skin.mesh.get_surface_count() <= exterior_surface:
		return false
	var coating := skin.mesh.surface_get_material(exterior_surface) as StandardMaterial3D
	if coating == null or coating.resource_name != "WF08 | factory-new exterior atlas":
		return false
	# All late nodes qualify before any material assignment.
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.albedo_color = Color(0.75, 0.90, 0.96, 0.055)
	material.roughness = 0.08
	material.metallic_specular = 0.18
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	glass.material_override = material
	glass.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	atlas_material = coating
	atlas_state = [coating.albedo_color, coating.albedo_texture, coating.normal_texture, coating.normal_scale, coating.metallic, coating.roughness, coating.cull_mode, coating.texture_filter]
	glass_material = material
	gear_nodes = gears
	specification = spec.duplicate(true)
	return true

static func valid_replacement(model: Node3D) -> bool:
	if model == null or model.is_inside_tree() or model.transform != Transform3D.IDENTITY or not Operating.valid_mesh_buffers(model):
		return false
	var nodes := Operating.unique_nodes(model)
	if nodes.is_empty() or model.get_child_count() != STOWED_ROOTS.size():
		return false
	var members := 0
	for name in STOWED_ROOTS:
		var node: Variant = nodes.get(name)
		if not node is MeshInstance3D or node.get_parent() != model or node.get_child_count() != 0 or node.transform != Transform3D.IDENTITY:
			return false
		var extras: Variant = node.get_meta("extras", {})
		var count := 56 if name == "WFStowedResidual" else 1
		if not Operating.metadata_matches(extras, {"source_member_count": count, "static_stow_prototype_not_admitted": true, "source_sha256": Operating.SOURCE_HASH}):
			return false
		members += count
	return members == 69

# Factory works only on detached, authenticated candidates. Tests exercise this
# narrow last-node/pose boundary directly without inventing a WorldAuthority.
func install_replacement(model: Node3D, replacement: Node3D, spec: Dictionary, deltas: Array) -> bool:
	if model.is_inside_tree() or not replacement_nodes.is_empty() or not valid_replacement(replacement) or deltas.size() != Operating.GROUP_NODES.size():
		return false
	var inspector := Operating.new()
	if not inspector.bind_operating_model(model, spec):
		inspector.free()
		return false
	var candidate := {}
	for index in Operating.GROUP_NODES.size():
		var id: String = Operating.GROUP_NODES.keys()[index]
		if not valid_rigid_pose(deltas[index]):
			inspector.free()
			return false
		candidate[id] = deltas[index]
	var lift: Node3D = inspector.moving_nodes.seat_lift
	var nodes := Operating.unique_nodes(model)
	for name in STOWED_ROOTS:
		if nodes.has(name):
			inspector.free()
			return false
	# The whole original lift is replaced once; all new roots remain siblings.
	operating_nodes = inspector.moving_nodes.duplicate()
	operating_nodes.erase("seat_lift")
	model.remove_child(lift)
	lift.free()
	for name in STOWED_ROOTS:
		var node: Node3D = replacement.find_child(name, false, false)
		node.owner = null
		replacement.remove_child(node)
		model.add_child(node)
		node.owner = model
		node.transform = candidate.seat_lift
		replacement_nodes.append(node)
	for id in operating_nodes:
		operating_nodes[id].transform = candidate[id]
	inspector.free()
	return true

static func load_selected_asset(directory: String, binding: Dictionary) -> Node3D:
	# Exact closed C++ binding validation is supplied below; no directory fallback.
	if not valid_binding(binding) or not directory.is_absolute_path() or not sources_unchanged(directory, binding):
		return null
	if binding.profile == "legacy-original":
		var legacy := load_asset(directory)
		if legacy != null:
			legacy.selected_binding = binding.duplicate(true)
		return legacy
	var operating := directory.path_join("operating")
	var stowed := directory.path_join("stowed")
	if not Operating.valid_prepared(operating) or FileAccess.get_sha256(stowed.path_join("model.glb")) != STOWED_MODEL or FileAccess.get_sha256(stowed.path_join("frame.json")) != STOWED_FRAME:
		return null
	var spec: Variant = Operating.bounded_json(operating.path_join(Operating.MANIFEST))
	if not Operating.valid_spec(spec) or spec.model.sha256 != binding.operating_model_sha256:
		return null
	var model := import_model(operating.path_join(spec.model.file), 14)
	var replacement := import_model(stowed.path_join("model.glb"))
	if model == null or replacement == null:
		if model != null: model.free()
		if replacement != null: replacement.free()
		return null
	var instance = load("res://scripts/characters/hopper_presentation.gd").new()
	instance.name = "Wayfarer"
	instance.exterior_surface = 14
	var calibration := {"schema_version": 1, "id": "wayfarer", "units": "metres", "model": "hopper-wayfarer-01.glb", "model_sha256": spec.model.sha256, "pilot_eye": spec.anchors.pilot_eye, "vertical_fov_degrees": 75, "screens": spec.screens, "gear_preview": spec.gear_preview, "gear_preview_samples": spec.gear_preview_samples}
	if not valid(calibration) or not instance.bind_calibration(model, calibration) or not instance.install_replacement(model, replacement, spec, binding.craft_world_deltas):
		model.free()
		replacement.free()
		instance.free()
		return null
	replacement.free()
	instance.add_child(model)
	instance.selected_binding = binding.duplicate(true)
	return instance

static func valid_rigid_pose(value: Variant) -> bool:
	if not value is Transform3D or not value.is_finite() or value.origin.length() > 100.0:
		return false
	return value.basis.is_equal_approx(value.basis.orthonormalized()) and absf(value.basis.determinant() - 1.0) <= 0.00001

static func valid_binding(binding: Variant) -> bool:
	if not Operating.exact_fields(binding, ["profile", "hardware_known", "operating_model_sha256", "stowed_model_sha256", "frame_sha256", "contact_sha256", "craft_world_deltas", "replacement_world_delta", "operating_atlas_surface"]):
		return false
	if not binding.hardware_known is bool or not binding.operating_atlas_surface is int or not binding.craft_world_deltas is Array or not binding.replacement_world_delta is Transform3D:
		return false
	if binding.profile == "legacy-original":
		return not binding.hardware_known and binding.operating_atlas_surface == 18 and binding.craft_world_deltas.is_empty() and binding.replacement_world_delta == Transform3D.IDENTITY and binding.operating_model_sha256 == "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8" and binding.stowed_model_sha256 == "" and binding.frame_sha256 == "" and binding.contact_sha256 == ""
	if binding.profile != "wayfarer-stowed-01" or not binding.hardware_known or binding.operating_atlas_surface != 14 or binding.operating_model_sha256 != "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c" or binding.stowed_model_sha256 != STOWED_MODEL or binding.frame_sha256 != STOWED_FRAME or binding.contact_sha256 != "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a" or binding.craft_world_deltas.size() != 13:
		return false
	for pose in binding.craft_world_deltas:
		if not valid_rigid_pose(pose):
			return false
	return binding.replacement_world_delta == binding.craft_world_deltas[10]

func valid_installed() -> bool:
	if not valid_binding(selected_binding) or get_child_count() != 1 or not calibration_unchanged():
		return false
	if selected_binding.profile == "legacy-original":
		return replacement_nodes.is_empty()
	var model := get_child(0)
	var nodes := Operating.unique_nodes(model)
	if nodes.is_empty() or nodes.has("WFOpSeatLift") or replacement_nodes.size() != 14 or operating_nodes.size() != 12:
		return false
	for index in STOWED_ROOTS.size():
		var node: Variant = nodes.get(STOWED_ROOTS[index])
		if not is_instance_valid(node) or node != replacement_nodes[index] or node.get_parent() != model or node.transform != selected_binding.replacement_world_delta:
			return false
	for index in Operating.GROUP_NODES.size():
		var id: String = Operating.GROUP_NODES.keys()[index]
		if id == "seat_lift": continue
		var node: Variant = operating_nodes.get(id)
		if not is_instance_valid(node) or nodes.get(Operating.GROUP_NODES[id]) != node or node.get_parent() != model or node.transform != selected_binding.craft_world_deltas[index]:
			return false
	return true

# Complete application-derived transforms; these are visual poses only.
static func projected_pose(value: Variant) -> Dictionary:
	if not value is Dictionary or not value.get("craft_world_deltas") is Dictionary:
		return {}
	var columns: Dictionary = value.craft_world_deltas
	if columns.size() != Operating.GROUP_NODES.size(): return {}
	var poses := {}
	for id in Operating.GROUP_NODES:
		var v: Variant = columns.get(id)
		if not v is PackedFloat64Array or v.size() != 12: return {}
		for component in v:
			if not is_finite(component): return {}
		var pose := Transform3D(Basis(Vector3(v[0], v[1], v[2]), Vector3(v[3], v[4], v[5]), Vector3(v[6], v[7], v[8])), Vector3(v[9], v[10], v[11]))
		if not valid_rigid_pose(pose): return {}
		poses[id] = pose
	return poses

func valid_current_pose() -> bool:
	if runtime_pose.is_empty(): return valid_installed()
	if not valid_binding(selected_binding) or selected_binding.profile != "wayfarer-stowed-01" or get_child_count() != 1 or not calibration_unchanged(): return false
	var model := get_child(0)
	var nodes := Operating.unique_nodes(model)
	if nodes.is_empty() or nodes.has("WFOpSeatLift") or replacement_nodes.size() != 14 or operating_nodes.size() != 12: return false
	for index in STOWED_ROOTS.size():
		var node: Variant = nodes.get(STOWED_ROOTS[index])
		if not is_instance_valid(node) or node != replacement_nodes[index] or node.get_parent() != model or node.transform != runtime_pose.seat_lift: return false
	for id in operating_nodes:
		var node: Variant = operating_nodes[id]
		if not is_instance_valid(node) or nodes.get(Operating.GROUP_NODES[id]) != node or node.get_parent() != model or node.transform != runtime_pose[id]: return false
	return true

func set_pose(value: Dictionary) -> bool:
	var candidate := projected_pose(value)
	if candidate.is_empty() or not valid_current_pose(): return false
	# Every known root receives its complete delta once, with no new hierarchy.
	for node in replacement_nodes: node.transform = candidate.seat_lift
	for id in operating_nodes: operating_nodes[id].transform = candidate[id]
	runtime_pose = candidate
	return valid_current_pose()

func set_gear_preview(deployed: float) -> bool:
	# Visual calibration pose, selected by inspection or explicit C++ deployment.
	# This method never infers or commits physical landing state.
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

func calibration_unchanged() -> bool:
	if get_child_count() != 1: return false
	var nodes := Operating.unique_nodes(get_child(0))
	var glass: Variant = nodes.get("HopperGlass")
	var skin: Variant = nodes.get("HopperStructure")
	if not glass is MeshInstance3D or glass.material_override != glass_material or not skin is MeshInstance3D or skin.mesh == null or skin.mesh.get_surface_count() <= exterior_surface or skin.mesh.surface_get_material(exterior_surface) != atlas_material:
		return false
	if atlas_material.next_pass != null or atlas_state != [atlas_material.albedo_color, atlas_material.albedo_texture, atlas_material.normal_texture, atlas_material.normal_scale, atlas_material.metallic, atlas_material.roughness, atlas_material.cull_mode, atlas_material.texture_filter]:
		return false
	for name in gear_nodes:
		if not is_instance_valid(gear_nodes[name]) or nodes.get(name) != gear_nodes[name]: return false
	return true

static func sources_unchanged(directory: String, binding: Dictionary) -> bool:
	if not valid_binding(binding): return false
	if binding.profile == "legacy-original":
		return FileAccess.get_sha256(directory.path_join("hopper-wayfarer-01.glb")) == binding.operating_model_sha256 and FileAccess.get_sha256(directory.path_join(CONTRACT)) == "17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7"
	return Operating.valid_prepared(directory.path_join("operating")) and FileAccess.get_sha256(directory.path_join("stowed/model.glb")) == binding.stowed_model_sha256 and FileAccess.get_sha256(directory.path_join("stowed/frame.json")) == binding.frame_sha256
