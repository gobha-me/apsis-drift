extends SceneTree
## Actual prepared GLB imports and buffer checks. No game or GPU visual claim.
var failures := 0


func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)


func valid_buffers(arrays: Array) -> bool:
	if arrays.size() != Mesh.ARRAY_MAX or not arrays[Mesh.ARRAY_VERTEX] is PackedVector3Array:
		return false
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	if vertices.is_empty() or vertices.size() > 30000000:
		return false
	for vertex in vertices:
		if not vertex.is_finite():
			return false
	var indices: Variant = arrays[Mesh.ARRAY_INDEX]
	if indices != null:
		if not indices is PackedInt32Array or indices.size() > 100000000 or indices.size() % 3 != 0:
			return false
		for index in indices:
			if index < 0 or index >= vertices.size():
				return false
	if indices == null or indices.is_empty():
		if vertices.size() % 3 != 0:
			return false
	for key in [Mesh.ARRAY_NORMAL, Mesh.ARRAY_TEX_UV, Mesh.ARRAY_TEX_UV2]:
		var values: Variant = arrays[key]
		if values == null:
			continue
		if not (values is PackedVector3Array or values is PackedVector2Array):
			return false
		if not values.is_empty() and values.size() != vertices.size():
			return false
		for value in values:
			if not value.is_finite():
				return false
	var tangents: Variant = arrays[Mesh.ARRAY_TANGENT]
	if tangents != null:
		if not tangents is PackedFloat32Array or (not tangents.is_empty() and tangents.size() != vertices.size() * 4):
			return false
		for value in tangents:
			if not is_finite(value):
				return false
	return true


func malformed_buffers() -> void:
	var arrays: Array = []
	check(not valid_buffers(arrays), "Short mesh array accepted")
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
	check(valid_buffers(arrays), "Synthetic triangle refused")
	for indices in [PackedInt32Array([-1, 1, 2]), PackedInt32Array([0, 1, 3]), PackedInt32Array([0, 1])]:
		arrays[Mesh.ARRAY_INDEX] = indices
		check(not valid_buffers(arrays), "Index/count boundary accepted")
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
	for value in [NAN, INF, -INF]:
		arrays[Mesh.ARRAY_VERTEX][0] = Vector3(value, 0, 0)
		check(not valid_buffers(arrays), "Nonfinite vertex accepted")
	arrays[Mesh.ARRAY_VERTEX][0] = Vector3.ZERO
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.UP])
	check(not valid_buffers(arrays), "Short normal buffer accepted")
	arrays[Mesh.ARRAY_NORMAL] = null
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2(NAN, 0), Vector2.ZERO, Vector2.ZERO])
	check(not valid_buffers(arrays), "Nonfinite UV accepted")


func _initialize() -> void:
	call_deferred("run")


func inspect_model(directory: String, name: String, expected_hash: String, expected: Dictionary) -> Dictionary:
	var path := directory.path_join(name)
	if FileAccess.get_sha256(path) != expected_hash:
		check(false, "Prepared GLB hash differs: " + name)
		return {}
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		check(false, "GLB import refused: " + name)
		return {}
	var scene: Node = document.generate_scene(state)
	if scene == null:
		check(false, "GLB generated no scene: " + name)
		return {}
	root.add_child(scene)
	var stats := {"meshes": 0, "surfaces": 0, "triangles": 0, "vertices": 0}
	for child: Node in scene.find_children("*", "MeshInstance3D", true, false):
		var node := child as MeshInstance3D
		check(node.mesh != null and node.global_transform.is_finite(), "Invalid mesh/transform")
		if node.mesh == null:
			continue
		var box: AABB = node.global_transform * node.mesh.get_aabb()
		check(box.position.is_finite() and box.size.is_finite(), "Nonfinite geometry bounds")
		stats.meshes += 1
		for surface in node.mesh.get_surface_count():
			check(node.mesh.surface_get_primitive_type(surface) == Mesh.PRIMITIVE_TRIANGLES, "Unexpected primitive")
			var arrays: Array = node.mesh.surface_get_arrays(surface)
			check(valid_buffers(arrays), "Invalid geometry buffers")
			if arrays.size() != Mesh.ARRAY_MAX:
				continue
			var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var indices: Variant = arrays[Mesh.ARRAY_INDEX]
			stats.surfaces += 1
			stats.vertices += vertices.size()
			stats.triangles += int(indices.size() / 3) if indices != null and not indices.is_empty() else int(vertices.size() / 3)
	for key in expected:
		check(stats[key] == expected[key], "Selected geometry changed: " + name + " / " + str(key))
	scene.free()
	return stats


func run() -> void:
	malformed_buffers()
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		check(false, "Expected prepared assets and report path")
		quit(1)
		return
	var result := {}
	result.wayfarer = inspect_model(args[0], "hopper-wayfarer-01.glb", "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8", {"meshes": 34, "surfaces": 119, "triangles": 728738})
	await process_frame
	result.station = inspect_model(args[0], "station-reference.glb", "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80", {"meshes": 5009, "surfaces": 5056, "triangles": 13687924, "vertices": 9510065})
	await process_frame
	var report := FileAccess.open(args[1], FileAccess.WRITE)
	if report == null:
		check(false, "Report could not be written")
	else:
		report.store_string(JSON.stringify({"pass": failures == 0, "failures": failures, "models": result, "scope": "Headless prepared-byte import, buffer and selected geometry qualification; no GPU or gameplay claim"}, "\t") + "\n")
	print("Native starter asset import: %d failures" % failures)
	quit(0 if failures == 0 else 1)
