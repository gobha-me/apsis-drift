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


func tick(delta: float, observer: Vector3) -> void:
	if not error.is_empty():
		return
	timer += delta
	if pending.is_empty():
		var batch: Dictionary = bridge.poll_stream()
		if batch.has("error"):
			error = str(batch.error)
			return
		if batch.has("tiles"):
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
			material.shader = load("res://terrain.gdshader")
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
	return result
