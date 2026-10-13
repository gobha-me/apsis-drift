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
	identity.clear()
	error = ""


func tick(delta: float, observer: Vector3) -> void:
	var current: Dictionary = bridge.get_stream_identity()
	if not valid_identity(current):
		error = "C++ terrain stream identity is unavailable or invalid"
		return
	if current != identity:
		# Retire both covers and any partially uploaded payload before keys
		# from another owner, saved policy or stream generation can alias them.
		reset_world()
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
			last_report = batch.duplicate(false)
			last_report.erase("tiles")
			generated_total += int(batch.generated)
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
				error = "C++ stream omitted an uncached tile payload"
				return
			var arrays: Array = []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = entry.vertices
			arrays[Mesh.ARRAY_NORMAL] = entry.normals
			arrays[Mesh.ARRAY_COLOR] = entry.colors
			arrays[Mesh.ARRAY_TEX_UV] = entry.uv
			arrays[Mesh.ARRAY_INDEX] = entry.indices
			var mesh := ArrayMesh.new()
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
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
		if upload_index == pending.size():
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
			pending.clear()
			wanted.clear()
			is_ready = true
			swaps += 1
	# Transform both covers while staging. Subtraction happens in C++ doubles,
	# before Godot ever receives a float; mesh coordinates are tile-local.
	var ids := nodes.keys()
	var positions := PackedFloat64Array()
	for id in ids:
		positions.append_array(anchors[id])
	var transforms: Array = bridge.stream_transforms(positions)
	if transforms.size() != ids.size():
		error = "Invalid stream transforms: " + str(bridge.get_last_error())
		return
	for i in range(ids.size()):
		nodes[ids[i]].transform = transforms[i]


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
