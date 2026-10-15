extends Node3D
## Main-thread GPU upload/scene ownership only. C++ supplies all mesh buffers.
## At most two complete covers resident during an atomic replacement.

var bridge: Variant
var nodes: Dictionary = {}
var anchors: Dictionary = {}
var pending: Array = []
var wanted: Dictionary = {}
var resident_ids: Array = []
var upload_index := 0
var timer := 0.0
var is_ready := false
var swaps := 0
var generated_total := 0
var retired_total := 0
var last_report: Dictionary = {}
var pending_report: Dictionary = {}
var error := ""
var render_layer := 1
var identity: Dictionary = {}


static func valid_decimal(value: Variant) -> bool:
	if typeof(value) != TYPE_STRING or value.is_empty() or value.length() > 20:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for index in range(value.length()):
		var code: int = value.unicode_at(index)
		if code < 48 or code > 57:
			return false
	return value.length() < 20 or value <= "18446744073709551615"


static func valid_identity(value: Dictionary) -> bool:
	if typeof(value.get("schema_version")) != TYPE_INT or value.schema_version != 1:
		return false
	if not valid_decimal(value.get("generation")) or value.generation == "0":
		return false
	if typeof(value.get("terrain_policy")) != TYPE_INT:
		return false
	if value.get("kind") == "owned":
		return value.size() == 9 and value.terrain_policy in [1, 2] and valid_decimal(value.get("system_id")) and valid_decimal(value.get("body_id")) and typeof(value.get("body_version")) == TYPE_INT and value.body_version == 1 and value.get("body_kind") == "planet" and typeof(value.get("physical_catalog")) == TYPE_INT and value.physical_catalog in [1, 2]
	if value.get("kind") == "study":
		return value.size() == 6 and value.terrain_policy == 0 and valid_decimal(value.get("planet_id")) and typeof(value.get("experimental_relief_version")) == TYPE_INT and value.experimental_relief_version in [0, 1]
	return false


func reset_world() -> void:
	for node: MeshInstance3D in nodes.values():
		node.visible = false
		node.queue_free()
	nodes.clear()
	anchors.clear()
	pending.clear()
	wanted.clear()
	resident_ids.clear()
	upload_index = 0
	timer = 0.0
	is_ready = false
	last_report.clear()
	pending_report.clear()
	identity.clear()
	error = ""


func _create_mesh(arrays: Array) -> ArrayMesh:
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _refuse_cover(reason: String) -> void:
	# The accepted cover keeps its last complete placement. Incoming nodes
	# remain owned until explicit reset, but cannot replace or obscure it.
	for id in nodes:
		if not resident_ids.has(id): nodes[id].visible = false
	error = reason


func tick(delta: float, observer: Vector3) -> void:
	var current: Dictionary = bridge.get_stream_identity()
	if not valid_identity(current):
		error = "C++ terrain stream identity is unavailable or invalid"
		return
	if current != identity:
		# Retire both covers and any partially uploaded payload before keys
		# from another owner, saved policy or stream generation can alias them.
		# An implicit retirement must not acknowledge a latched fault. The
		# owning view still needs to pause and silence stale presentation.
		var latched_error := error
		reset_world()
		error = latched_error
		identity = current.duplicate(true)
	if not error.is_empty():
		return
	timer += delta
	if pending.is_empty():
		var batch: Dictionary = bridge.poll_stream()
		if batch.has("error"):
			error = str(batch.error)
			return
		if batch.has("tiles"):
			if typeof(batch.get("identity")) != TYPE_DICTIONARY or not valid_identity(batch.identity) or batch.identity != identity:
				error = "C++ terrain batch differs from the selected stream"
				return
			pending = batch.tiles
			wanted.clear()
			for entry in pending:
				wanted[entry.id] = true
			upload_index = 0
			pending_report = batch.duplicate(false)
			pending_report.erase("tiles")
		elif timer >= 0.35:
			timer = 0
			if not bridge.request_stream(observer):
				error = str(bridge.get_last_error())
				return
	if not pending.is_empty():
		var uploaded := 0
		var begin := Time.get_ticks_usec()
		while upload_index < pending.size():
			var entry: Dictionary = pending[upload_index]
			upload_index += 1
			if nodes.has(entry.id):
				continue
			if not entry.has("vertices"):
				_refuse_cover("C++ stream omitted an uncached tile payload")
				return
			var arrays: Array = []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = entry.vertices
			arrays[Mesh.ARRAY_NORMAL] = entry.normals
			arrays[Mesh.ARRAY_COLOR] = entry.colors
			arrays[Mesh.ARRAY_TEX_UV] = entry.uv
			arrays[Mesh.ARRAY_INDEX] = entry.indices
			var mesh := _create_mesh(arrays)
			if mesh == null or mesh.get_surface_count() != 1:
				_refuse_cover("Terrain mesh upload did not produce one surface")
				return
			var instance := MeshInstance3D.new()
			instance.layers = render_layer
			instance.mesh = mesh
			var material := ShaderMaterial.new()
			material.shader = load("res://shaders/terrain.gdshader")
			material.set_shader_parameter("planet_tile", true)
			material.set_shader_parameter("tile_origin", Vector3(entry.anchor[0], entry.anchor[1], entry.anchor[2]))
			instance.material_override = material
			instance.visible = false
			add_child(instance)
			nodes[entry.id] = instance
			anchors[entry.id] = entry.anchor
			uploaded += 1
			# A count limit plus a soft CPU budget. One GPU upload may still stall.
			if uploaded >= 4 or Time.get_ticks_usec() - begin >= 2000:
				break
	# Transform both covers while staging. Subtraction happens in C++ doubles,
	# before Godot ever receives a float; mesh coordinates are tile-local.
	var ids := nodes.keys()
	var positions := PackedFloat64Array()
	for id in ids:
		positions.append_array(anchors[id])
	var transforms: Array = bridge.stream_transforms(positions)
	if transforms.size() != ids.size():
		_refuse_cover("Invalid stream transforms: " + str(bridge.get_last_error()))
		return
	# Validate the whole batch before moving even the first accepted tile.
	for transform in transforms:
		if not transform is Transform3D or not transform.is_finite():
			_refuse_cover("Invalid terrain placement transform")
			return
	for i in range(ids.size()):
		nodes[ids[i]].transform = transforms[i]
	if not pending.is_empty() and upload_index == pending.size():
		for id in nodes.keys():
			if not wanted.has(id):
				var retired: MeshInstance3D = nodes[id]
				retired.visible = false
				retired.queue_free()
				nodes.erase(id)
				anchors.erase(id)
				retired_total += 1
			else:
				nodes[id].visible = true
		resident_ids = nodes.keys()
		last_report = pending_report
		generated_total += int(pending_report.generated)
		pending_report = {}
		pending.clear()
		wanted.clear()
		is_ready = true
		swaps += 1


# Clip only presentation geometry already supplied by C++. Include both covers:
# a staged tile can become visible during the same main-thread placement tick.
# AABBs include actual relief, coarse chords and skirts; no terrain recipe is
# duplicated here. The cap/ratio remain the existing distant-depth policy.
func depth_range(relation: Transform3D, minimum_far: float) -> Vector2:
	if not relation.is_finite() or absf(relation.basis.determinant()) < 0.000001 or not is_finite(minimum_far) or minimum_far < 100000.0 or minimum_far > 1000000000.0 or nodes.size() > 768:
		return Vector2.ZERO
	var inverse := relation.affine_inverse()
	if not inverse.is_finite(): return Vector2.ZERO
	var far_clip := minimum_far
	for node in nodes.values():
		if not is_instance_valid(node) or not node is MeshInstance3D or node.mesh == null or not node.transform.is_finite():
			return Vector2.ZERO
		var bounds: AABB = node.mesh.get_aabb()
		if not bounds.position.is_finite() or not bounds.size.is_finite() or not bounds.end.is_finite() or bounds.size.x < 0.0 or bounds.size.y < 0.0 or bounds.size.z < 0.0:
			return Vector2.ZERO
		# Transform the whole box into camera space; its minimum Z bounds
		# forward depth even when the camera is translated or rolled.
		var camera_bounds: AABB = (inverse * node.transform) * bounds
		if not camera_bounds.position.is_finite() or not camera_bounds.size.is_finite() or not camera_bounds.end.is_finite(): return Vector2.ZERO
		# One-metre/one-ppm clip slack covers float AABB/placement rounding.
		var depth := -float(camera_bounds.position.z)
		far_clip = maxf(far_clip, depth + maxf(1.0, absf(depth) * 0.000001))
	far_clip = minf(1000000000.0, far_clip)
	return Vector2(maxf(0.2, far_clip / 500000.0), far_clip)


func report() -> Dictionary:
	var result := last_report.duplicate()
	result["resident_tiles"] = resident_ids.size()
	result["staged_and_resident_tiles"] = nodes.size()
	result["swaps"] = swaps
	result["generated_total"] = generated_total
	result["retired_total"] = retired_total
	result["ready"] = is_ready
	result["identity"] = identity.duplicate(true)
	return result
