extends Node3D
## Lunar GPU upload only. C++ owns the complete cover and every terrain vertex.
var bridge: Variant
var error := ""
var nodes: Dictionary = {}
var anchors: Dictionary = {}
var resident_ids: Array = []
var staged: Array = []
var wanted: Array = []
var next_upload := 0
var timer := 0.0
var is_ready := false
var owner_id := ""
var expected_body_id := ""
var material: StandardMaterial3D

func _init() -> void:
	material = StandardMaterial3D.new()
	material.vertex_color_use_as_albedo = true
	material.roughness = 1.0
	material.metallic = 0.0

func reset_world() -> void:
	for node: MeshInstance3D in nodes.values(): node.queue_free()
	nodes.clear()
	anchors.clear()
	resident_ids.clear()
	staged.clear()
	wanted.clear()
	next_upload = 0
	timer = 0.0
	is_ready = false
	owner_id = ""
	error = ""

func accept_cover(packet: Dictionary) -> bool:
	# Validate the entire packet before allocating meshes or changing the cover.
	if packet.get("body_kind") != "moon" or not packet.get("body_id") is String or not packet.get("tiles") is Array: return false
	if expected_body_id.is_empty() or packet.body_id != expected_body_id or (not owner_id.is_empty() and owner_id != packet.body_id): return false
	var entries: Array = packet.tiles
	if entries.size() < 6 or entries.size() > 384: return false
	var ids: Dictionary = {}
	for entry in entries:
		if not entry is Dictionary or not entry.get("id") is String or ids.has(entry.id): return false
		ids[entry.id] = true
		if nodes.has(entry.id): continue
		if not entry.get("anchor") is PackedFloat64Array or entry.anchor.size() != 3: return false
		for component in entry.anchor:
			if not is_finite(component): return false
		if not entry.get("vertices") is PackedVector3Array or not entry.get("normals") is PackedVector3Array or not entry.get("colors") is PackedColorArray or not entry.get("indices") is PackedInt32Array: return false
		if entry.vertices.size() != 1221 or entry.normals.size() != entry.vertices.size() or entry.colors.size() != entry.vertices.size() or entry.indices.size() != 7680: return false
		for vertex in entry.vertices:
			if not vertex.is_finite(): return false
		for normal in entry.normals:
			if not normal.is_finite(): return false
		for color in entry.colors:
			for component in [color.r, color.g, color.b, color.a]:
				if not is_finite(component) or component < 0 or component > 1: return false
		for index in entry.indices:
			if index < 0 or index >= entry.vertices.size(): return false
	owner_id = packet.body_id
	staged = entries
	wanted = ids.keys()
	next_upload = 0
	return true

func tick(delta: float, _eye: Vector3, paused: bool) -> void:
	if not error.is_empty() or bridge == null: return
	timer -= maxf(0.0, delta)
	# Paused views still finish a staged cover and prepare their first terrain.
	if staged.is_empty() and timer <= 0.0:
		if not bridge.request_freedom_moon_stream():
			error = str(bridge.get_last_error())
			return
		timer = 0.5 if not paused else 1.0
	if staged.is_empty():
		var packet: Dictionary = bridge.poll_freedom_moon_stream()
		if not packet.is_empty() and not accept_cover(packet):
			error = "Invalid C++ lunar terrain cover"
			return
	# One bounded tile upload each frame; the old complete cover remains visible.
	if next_upload < staged.size():
		var entry: Dictionary = staged[next_upload]
		next_upload += 1
		if not nodes.has(entry.id):
			var arrays: Array = []
			arrays.resize(Mesh.ARRAY_MAX)
			arrays[Mesh.ARRAY_VERTEX] = entry.vertices
			arrays[Mesh.ARRAY_NORMAL] = entry.normals
			arrays[Mesh.ARRAY_COLOR] = entry.colors
			arrays[Mesh.ARRAY_INDEX] = entry.indices
			var mesh := ArrayMesh.new()
			mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
			var node := MeshInstance3D.new()
			node.mesh = mesh
			node.material_override = material
			node.visible = false
			add_child(node)
			nodes[entry.id] = node
			anchors[entry.id] = entry.anchor
	if not staged.is_empty() and next_upload == staged.size():
		for id in nodes.keys():
			if not wanted.has(id):
				nodes[id].queue_free()
				nodes.erase(id)
				anchors.erase(id)
		resident_ids = wanted.duplicate()
		staged.clear()
		wanted.clear()
		next_upload = 0
		is_ready = true
		for id in resident_ids: nodes[id].visible = true
	if nodes.is_empty(): return
	var ordered: Array = nodes.keys()
	var points := PackedFloat64Array()
	for id in ordered: points.append_array(anchors[id])
	var transforms: Array = bridge.freedom_moon_stream_transforms(points)
	if transforms.size() != ordered.size():
		error = "C++ lunar mesh placement is unavailable"
		return
	for index in ordered.size():
		if not transforms[index] is Transform3D or not transforms[index].is_finite():
			error = "Nonfinite lunar mesh placement"
			return
	for index in ordered.size(): nodes[ordered[index]].transform = transforms[index]
