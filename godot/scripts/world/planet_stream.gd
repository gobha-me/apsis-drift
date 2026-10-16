extends Node3D
signal terrain_material_created(material: ShaderMaterial)
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
const MORPH_SECONDS := 0.25
const MAX_TILE_VERTICES := 33 * 33 + 4 * 33
var morph_active := false
var morph_progress := 0.0
var morph_entries: Dictionary = {}
var morph_draw_ids: Dictionary = {}
var morph_upload_index := 0
# Extra attributes may remain on accepted fine meshes, inactive, until retirement.
var morph_attribute_bytes: Dictionary = {}


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
		return value.size() == 9 and value.terrain_policy in [1, 2, 3] and valid_decimal(value.get("system_id")) and valid_decimal(value.get("body_id")) and typeof(value.get("body_version")) == TYPE_INT and value.body_version == 1 and value.get("body_kind") == "planet" and typeof(value.get("physical_catalog")) == TYPE_INT and value.physical_catalog in [1, 2]
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
	_clear_morph_state()
	morph_attribute_bytes.clear()


func _create_mesh(arrays: Array) -> ArrayMesh:
	var mesh := ArrayMesh.new()
	var flags := 0
	if arrays[Mesh.ARRAY_CUSTOM0] is PackedFloat32Array:
		flags = (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT) | (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM1_SHIFT) | (Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM2_SHIFT)
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [], {}, flags)
	return mesh


func _clear_morph_state() -> void:
	morph_active = false
	morph_progress = 0.0
	morph_entries.clear()
	morph_draw_ids.clear()
	morph_upload_index = 0


func _base_material(node: MeshInstance3D) -> void:
	if node.material_override is ShaderMaterial:
		node.material_override.set_shader_parameter("morph_active", false)
		node.material_override.set_shader_parameter("morph_fine_weight", 1.0)
	if node.mesh is ArrayMesh:
		node.mesh.custom_aabb = AABB()


func _refuse_cover(reason: String) -> void:
	# Roll back even an active common-refinement draw to the accepted base cover.
	for id in nodes:
		_base_material(nodes[id])
		nodes[id].visible = resident_ids.has(id)
	_clear_morph_state()
	error = reason


static func _morph_key(value: Variant) -> PackedInt32Array:
	if not value is String: return PackedInt32Array()
	var parts: PackedStringArray = value.split(":")
	if parts.size() != 4: return PackedInt32Array()
	var key := PackedInt32Array()
	for part in parts:
		if not valid_decimal(part) or part.length() > 4: return PackedInt32Array()
		key.append(part.to_int())
	if key[0] > 5 or key[1] > 13 or key[2] >= (1 << key[1]) or key[3] >= (1 << key[1]): return PackedInt32Array()
	return key


func _begin_batch(batch: Dictionary) -> bool:
	var entries: Variant = batch.get("tiles")
	if not entries is Array or entries.is_empty() or entries.size() > 384:
		_refuse_cover("Invalid terrain cover buffer")
		return false
	var next_ids: Dictionary = {}
	for entry in entries:
		if not entry is Dictionary or not entry.get("id") is String or entry.id.is_empty() or entry.id.length() > 64 or next_ids.has(entry.id):
			_refuse_cover("Invalid terrain tile roster")
			return false
		next_ids[entry.id] = true
	var packet: Variant = batch.get("morph", {})
	if not packet is Dictionary:
		_refuse_cover("Invalid terrain transition packet")
		return false
	var endpoints: Dictionary = {}
	var draw: Dictionary = {}
	if not packet.is_empty():
		if not packet.get("version") is int or packet.version != 1 or not packet.get("entries") is Array or packet.entries.is_empty() or packet.entries.size() > 768 or not packet.get("draw_ids") is Array or packet.draw_ids.is_empty() or packet.draw_ids.size() > 768 or resident_ids.is_empty():
			_refuse_cover("Unsupported terrain transition header")
			return false
		var expected := next_ids.duplicate()
		for entry in packet.entries:
			if not entry is Dictionary or not entry.get("refining") is bool:
				_refuse_cover("Invalid terrain transition direction")
				return false
			var fine := _morph_key(entry.get("id"))
			var coarse := _morph_key(entry.get("coarse_id"))
			if fine.is_empty() or coarse.is_empty() or fine[0] != coarse[0] or fine[1] <= coarse[1] or endpoints.has(entry.id):
				_refuse_cover("Invalid terrain transition ancestry")
				return false
			var difference := fine[1] - coarse[1]
			if (fine[2] >> difference) != coarse[2] or (fine[3] >> difference) != coarse[3]:
				_refuse_cover("Unrelated terrain transition keys")
				return false
			if entry.refining:
				if not next_ids.has(entry.id) or resident_ids.has(entry.id) or not resident_ids.has(entry.coarse_id):
					_refuse_cover("Refining terrain differs from the accepted cover")
					return false
			elif not resident_ids.has(entry.id) or next_ids.has(entry.id) or not next_ids.has(entry.coarse_id):
				_refuse_cover("Coarsening terrain differs from the accepted cover")
				return false
			else:
				expected.erase(entry.coarse_id)
				expected[entry.id] = true
			endpoints[entry.id] = entry
		for id in packet.draw_ids:
			if not id is String or not expected.has(id) or draw.has(id):
				_refuse_cover("Invalid terrain transition draw roster")
				return false
			draw[id] = true
		if draw.size() != expected.size():
			_refuse_cover("Incomplete terrain transition draw roster")
			return false
		for id in draw:
			if not resident_ids.has(id) and not endpoints.has(id):
				_refuse_cover("Missing terrain transition endpoint")
				return false
	pending = entries
	wanted = next_ids
	morph_entries = endpoints
	morph_draw_ids = draw
	upload_index = 0
	morph_upload_index = 0
	pending_report = batch.duplicate(false)
	pending_report.erase("tiles")
	pending_report.erase("morph")
	return true


static func _valid_bounds(bounds: AABB) -> bool:
	return bounds.position.is_finite() and bounds.size.is_finite() and bounds.end.is_finite() and bounds.size.x >= 0.0 and bounds.size.y >= 0.0 and bounds.size.z >= 0.0


func _upload_morph(entry: Dictionary) -> bool:
	if not nodes.has(entry.id): return false
	var node: MeshInstance3D = nodes[entry.id]
	if not node.mesh is ArrayMesh or node.mesh.get_surface_count() != 1 or not node.material_override is ShaderMaterial: return false
	var arrays: Array = node.mesh.surface_get_arrays(0)
	if not arrays[Mesh.ARRAY_VERTEX] is PackedVector3Array: return false
	var count: int = arrays[Mesh.ARRAY_VERTEX].size()
	if count <= 0 or count > MAX_TILE_VERTICES: return false
	for field in ["position_elevation", "normals", "colors"]:
		var values: Variant = entry.get(field)
		if not values is PackedFloat32Array or values.size() != 4 * count: return false
		for value in values:
			if not is_finite(value): return false
	var bounds: AABB = node.mesh.get_aabb()
	if not _valid_bounds(bounds): return false
	for i in count:
		var offset := 4 * i
		var normal := Vector3(entry.normals[offset], entry.normals[offset + 1], entry.normals[offset + 2])
		var wetness: float = entry.normals[offset + 3]
		if wetness < 0.0 or wetness > 1.0 or (identity.get("terrain_policy") != 3 and wetness != 0.0): return false
		var magnitude := normal.length_squared()
		if not is_finite(magnitude) or magnitude <= 0.000000000001 or magnitude > 1.00001: return false
		for channel in 4:
			if entry.colors[offset + channel] < 0.0 or entry.colors[offset + channel] > 1.0: return false
		bounds = bounds.expand(Vector3(entry.position_elevation[offset], entry.position_elevation[offset + 1], entry.position_elevation[offset + 2]))
	if not _valid_bounds(bounds): return false
	arrays[Mesh.ARRAY_CUSTOM0] = entry.position_elevation
	arrays[Mesh.ARRAY_CUSTOM1] = entry.normals
	arrays[Mesh.ARRAY_CUSTOM2] = entry.colors
	var mesh := _create_mesh(arrays)
	if mesh == null or mesh.get_surface_count() != 1: return false
	mesh.custom_aabb = bounds
	node.mesh = mesh
	morph_attribute_bytes[entry.id] = count * 4 * 4 * 3
	return true


func _set_morph_progress() -> void:
	for id in morph_entries:
		var material: ShaderMaterial = nodes[id].material_override
		material.set_shader_parameter("morph_active", true)
		material.set_shader_parameter("morph_fine_weight", morph_progress if morph_entries[id].refining else 1.0 - morph_progress)


func _finish_cover() -> void:
	for id in nodes.keys():
		if not wanted.has(id):
			var retired: MeshInstance3D = nodes[id]
			retired.visible = false
			retired.queue_free()
			nodes.erase(id)
			anchors.erase(id)
			morph_attribute_bytes.erase(id)
			retired_total += 1
		else:
			nodes[id].visible = true
			_base_material(nodes[id])
	resident_ids = nodes.keys()
	last_report = pending_report
	generated_total += int(pending_report.generated)
	pending_report = {}
	pending.clear()
	wanted.clear()
	_clear_morph_state()
	is_ready = true
	swaps += 1


func tick(delta: float, observer: Vector3, animate := true) -> void:
	if not is_finite(delta) or delta < 0.0 or not observer.is_finite():
		_refuse_cover("Invalid terrain presentation clock or observer")
		return
	var current: Dictionary = bridge.get_stream_identity()
	if not valid_identity(current):
		_refuse_cover("C++ terrain stream identity is unavailable or invalid")
		return
	if current != identity:
		var latched_error := error
		reset_world()
		error = latched_error
		identity = current.duplicate(true)
		# Queued old-world children leave at frame end. Do not add a third
		# cover's upload tail while the previous two covers still own nodes.
		if get_child_count() > 0: return
	if not error.is_empty(): return
	if nodes.is_empty() and get_child_count() > 0: return
	# Scene deletion happens at frame end. Repeated ticks within one frame
	# must not accumulate another upload cover behind queued retired nodes.
	var can_stage := get_child_count() == nodes.size()
	timer += delta
	if pending.is_empty() and not morph_active and can_stage:
		var batch: Dictionary = bridge.poll_stream()
		if batch.has("error"):
			_refuse_cover(str(batch.error))
			return
		if batch.has("tiles"):
			if typeof(batch.get("identity")) != TYPE_DICTIONARY or not valid_identity(batch.identity) or batch.identity != identity:
				_refuse_cover("C++ terrain batch differs from the selected stream")
				return
			if not _begin_batch(batch): return
		elif timer >= 0.35:
			timer = 0
			if not bridge.request_stream(observer):
				_refuse_cover(str(bridge.get_last_error()))
				return
	if not pending.is_empty() and not morph_active and can_stage:
		var uploaded := 0
		var begin := Time.get_ticks_usec()
		var morph_ids := morph_entries.keys()
		while upload_index < pending.size() or morph_upload_index < morph_ids.size():
			if upload_index < pending.size():
				var entry: Dictionary = pending[upload_index]
				upload_index += 1
				if nodes.has(entry.id): continue
				if not entry.has("vertices"):
					_refuse_cover("C++ stream omitted an uncached tile payload")
					return
				var uv: Variant = entry.get("uv")
				if not entry.get("vertices") is PackedVector3Array or not uv is PackedVector2Array or uv.size() != entry.vertices.size() or uv.size() <= 0 or uv.size() > MAX_TILE_VERTICES:
					_refuse_cover("Invalid terrain surface attributes")
					return
				for coordinate: Vector2 in uv:
					if not coordinate.is_finite() or coordinate.y not in [0.0, 1.0] or (identity.get("terrain_policy") != 3 and coordinate.y != 0.0):
						_refuse_cover("Invalid terrain datum attributes")
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
				terrain_material_created.emit(material)
				instance.visible = false
				add_child(instance)
				nodes[entry.id] = instance
				anchors[entry.id] = entry.anchor
			else:
				if not _upload_morph(morph_entries[morph_ids[morph_upload_index]]):
					_refuse_cover("Invalid terrain morph attribute buffer or mesh")
					return
				morph_upload_index += 1
			uploaded += 1
			if uploaded >= 4 or Time.get_ticks_usec() - begin >= 2000: break
	# Both covers move together; subtraction remains in C++ doubles.
	var ids := nodes.keys()
	var positions := PackedFloat64Array()
	for id in ids: positions.append_array(anchors[id])
	var transforms: Array = bridge.stream_transforms(positions)
	if transforms.size() != ids.size():
		_refuse_cover("Invalid stream transforms: " + str(bridge.get_last_error()))
		return
	for transform in transforms:
		if not transform is Transform3D or not transform.is_finite():
			_refuse_cover("Invalid terrain placement transform")
			return
	for i in range(ids.size()): nodes[ids[i]].transform = transforms[i]
	if morph_active:
		if animate:
			morph_progress = minf(1.0, morph_progress + delta / MORPH_SECONDS)
			_set_morph_progress()
		if morph_progress >= 1.0: _finish_cover()
	elif not pending.is_empty() and upload_index == pending.size() and morph_upload_index == morph_entries.size():
		if morph_entries.is_empty():
			_finish_cover()
		else:
			morph_active = true
			morph_progress = 0.0
			for id in nodes: nodes[id].visible = morph_draw_ids.has(id)
			_set_morph_progress()


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
		if node.mesh is ArrayMesh and node.mesh.custom_aabb != AABB():
			if not _valid_bounds(node.mesh.custom_aabb): return Vector2.ZERO
			bounds = bounds.merge(node.mesh.custom_aabb)
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
	result["morph_active"] = morph_active
	result["morph_progress"] = morph_progress
	var attribute_bytes := 0
	for bytes in morph_attribute_bytes.values(): attribute_bytes += int(bytes)
	result["morph_attribute_payload_bytes"] = attribute_bytes
	result["ready"] = is_ready
	result["identity"] = identity.duplicate(true)
	return result
