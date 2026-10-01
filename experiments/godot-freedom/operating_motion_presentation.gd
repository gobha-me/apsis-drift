extends Node3D
## Read-only source-bound hardware view. Every mechanism transform comes from C++.
const Operating = preload("res://wayfarer_operating_view.gd")
const RECIPE_HASH := "afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298"
const IDENTITIES := {
	"craft_source_sha256": "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677",
	"station_source_sha256": "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6",
	"closure_source_sha256": "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4",
	"craft_model_sha256": "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c",
	"station_model_sha256": "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
	"contact_sha256": "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a",
	"station_closure_sha256": "662287856666bc9f3bffdd6ccda3fce2a2c2b55fd393998f4cf65597c3ffeada"
}
const SCOPE := "Source-local craft/D1 transform evaluation; no geometry, actor, clock, pressure, save or collision admission"
var bridge: Variant
var hardware: Variant
var error := ""
var last_pose: Dictionary = {}
var last_progress := PackedFloat64Array()
var craft_parent: Node3D
var station_parents: Dictionary = {}

static func station_ids() -> Array[String]:
	var result: Array[String] = []
	for index in 17:
		result.append("station_d1_%02d" % index)
	return result

static func valid_progress(value: Variant) -> bool:
	if not (value is Array or value is PackedFloat64Array) or value.size() != 4:
		return false
	for component in value:
		if not (component is int or component is float) or not is_finite(float(component)) or component < 0.0 or component > 1.0:
			return false
	return true

static func valid_matrix(value: Variant) -> bool:
	if not value is PackedFloat64Array or value.size() != 12:
		return false
	for number in value:
		if not is_finite(number) or absf(number) > 100.0:
			return false
	# Qualify binary64 components before converting to Godot's local float vectors.
	for first in 3:
		for second in 3:
			var dot := 0.0
			for axis in 3:
				dot += value[first * 3 + axis] * value[second * 3 + axis]
			if absf(dot - (1.0 if first == second else 0.0)) > 0.000005:
				return false
	var determinant: float = value[0] * (value[4] * value[8] - value[5] * value[7]) - value[3] * (value[1] * value[8] - value[2] * value[7]) + value[6] * (value[1] * value[5] - value[2] * value[4])
	return absf(determinant - 1.0) <= 0.000005

static func native_transform(value: PackedFloat64Array) -> Transform3D:
	return Transform3D(Basis(Vector3(value[0], value[1], value[2]), Vector3(value[3], value[4], value[5]), Vector3(value[6], value[7], value[8])), Vector3(value[9], value[10], value[11]))

static func valid_native_pose(value: Variant) -> bool:
	if not Operating.exact_fields(value, ["craft_world_deltas", "station_node_local", "station_contact_deltas"]):
		return false
	for map_name in value:
		var ids: Array = Operating.GROUP_NODES.keys() if map_name == "craft_world_deltas" else station_ids()
		if not Operating.exact_fields(value[map_name], ids):
			return false
		for id in ids:
			if not valid_matrix(value[map_name][id]):
				return false
	return true

static func verified_recipe(directory: String) -> String:
	if not directory.is_absolute_path():
		return ""
	var receipt: Variant = Operating.bounded_json(directory.path_join("prepared.json"))
	if not Operating.exact_fields(receipt, ["schema", "package_id", "package_sha256", "runtime", "recipe_sha256", "identities", "scope"]) or receipt.schema != "apsis.prepared-operating-motion/1" or receipt.package_id != "operating-motion-01" or receipt.runtime != "operating-motion-01.json" or receipt.recipe_sha256 != RECIPE_HASH or receipt.identities != IDENTITIES or receipt.scope != SCOPE or not Operating.digest(receipt.package_sha256):
		return ""
	var file := FileAccess.open(directory.path_join(receipt.runtime), FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > 128 * 1024:
		return ""
	var bytes := file.get_buffer(128 * 1024 + 1)
	if bytes.is_empty() or bytes.size() > 128 * 1024:
		return ""
	var hash := HashingContext.new()
	if hash.start(HashingContext.HASH_SHA256) != OK or hash.update(bytes) != OK or hash.finish().hex_encode() != RECIPE_HASH:
		return ""
	return bytes.get_string_from_utf8()

func initialize_motion(owner: Variant, native_assets: String, operating_assets: String, motion_assets: String) -> bool:
	if hardware != null or not native_assets.is_absolute_path() or not operating_assets.is_absolute_path():
		error = "Fresh view and absolute prepared asset directories required"
		return false
	var recipe := verified_recipe(motion_assets)
	if recipe.is_empty() or owner == null or not owner.initialize_operating_motion(recipe):
		error = "Selected C++ motion recipe refused"
		return false
	var candidate := Operating.new()
	if not candidate.initialize(owner, native_assets, operating_assets):
		error = candidate.error
		candidate.free()
		return false
	hardware = candidate
	bridge = owner
	add_child(hardware)
	craft_parent = hardware.ship.get_child(0)
	for id in hardware.closure_nodes:
		station_parents[id] = hardware.closure_nodes[id].get_parent()
	if not set_progress(PackedFloat64Array([0.0, 0.0, 0.0, 0.0])):
		hardware.free()
		hardware = null
		bridge = null
		craft_parent = null
		station_parents.clear()
		return false
	error = ""
	return true

func apply_native_pose(value: Variant) -> bool:
	if hardware == null or not valid_native_pose(value) or not is_instance_valid(craft_parent):
		error = "Complete finite native pose required"
		return false
	var craft_candidates := {}
	var station_candidates := {}
	for id in Operating.GROUP_NODES:
		var node: Variant = hardware.moving_nodes.get(id)
		if not is_instance_valid(node) or not node is MeshInstance3D or node.get_parent() != craft_parent or str(node.name) != Operating.GROUP_NODES[id] or not Operating.metadata_matches(node.get_meta("extras", {}), {"operating_group": id, "source_rig": Operating.GROUP_RIGS[id], "source_sha256": Operating.SOURCE_HASH}):
			error = "Craft source binding changed"
			return false
		craft_candidates[id] = native_transform(value.craft_world_deltas[id])
	for binding in hardware.closure_spec.bindings:
		var node: Variant = hardware.closure_nodes.get(binding.id)
		if not is_instance_valid(node) or not node is Node3D or node.get_parent() != station_parents.get(binding.id) or str(node.name) != binding.runtime_node or not Operating.metadata_subset(node.get_meta("extras", {}), binding.expected_extras):
			error = "D1 source binding changed"
			return false
		station_candidates[binding.id] = native_transform(value.station_node_local[binding.id])
	# No mutation occurs until every matrix and both complete node rosters qualify.
	for id in craft_candidates:
		hardware.moving_nodes[id].transform = craft_candidates[id]
	for id in station_candidates:
		hardware.closure_nodes[id].transform = station_candidates[id]
	last_pose = value.duplicate(true)
	error = ""
	return true

func set_progress(progress: Variant) -> bool:
	if bridge == null or not valid_progress(progress):
		error = "Four finite bounded mechanism progresses required"
		return false
	var result: Dictionary = bridge.get_operating_motion_pose(progress[0], progress[1], progress[2], progress[3])
	if not apply_native_pose(result):
		return false
	last_progress = PackedFloat64Array(progress)
	return true
