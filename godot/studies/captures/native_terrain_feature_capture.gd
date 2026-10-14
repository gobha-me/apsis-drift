extends SceneTree
## One initialized measurement viewpoint in the production native presenter.
## No arrival/continuous-flight or hardware-performance claim.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
var failures := 0

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failures += 1
	return value

func vector(value: Array) -> Vector3:
	return Vector3(value[0], value[1], value[2])

func coordinates(value: Vector3) -> Array:
	return [value.x, value.y, value.z]

func finite_three(value: Variant) -> bool:
	if not value is Array or value.size() != 3: return false
	for component in value:
		if not (component is float or component is int) or not is_finite(float(component)): return false
	return true

func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for character in value.to_utf8_buffer():
		if character < 48 or character > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

func valid_manifest(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema") != "apsis.native-terrain-feature/1" or value.get("save_file") != "terrain-feature.json" or value.get("terrain_policy") != 2: return false
	for key in ["universe_seed", "tick", "checksum", "system_id", "planet_id"]:
		if not decimal(value.get(key)): return false
	if not finite_three(value.get("star_direction_local")): return false
	var visibility: Variant = value.get("expected_native_sun_visibility")
	if not (visibility is float or visibility is int) or not is_finite(float(visibility)) or visibility < 0.0 or visibility > 1.0: return false
	if not value.get("points") is Array or value.points.size() != 3: return false
	var labels := ["left_trough", "crest", "right_trough"]
	for index in 3:
		var point: Variant = value.points[index]
		if not point is Dictionary or point.get("label") != labels[index] or not finite_three(point.get("local_metres")): return false
	return true

func nearest_stream_vertex(terrain: Node3D, target: Vector3) -> Dictionary:
	var best := INF
	var closest: MeshInstance3D
	var closest_index := -1
	var closest_tile := ""
	var closest_point := Vector3.ZERO
	var vertex_count := 0
	for tile_id in terrain.nodes:
		var node: MeshInstance3D = terrain.nodes[tile_id]
		if not node.visible: continue
		var vertices: PackedVector3Array = node.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		# The owned stream contract uses a33x33 top grid followed by skirts.
		if not check(vertices.size() >= 1089, "Short streamed top-grid buffer"): return {}
		vertex_count += 1089
		for index in 1089:
			var position: Vector3 = node.transform * vertices[index]
			var distance := position.distance_squared_to(target)
			if distance < best:
				best = distance
				closest = node
				closest_index = index
				closest_tile = str(tile_id)
				closest_point = position
	if closest == null: return {}
	var arrays: Array = closest.mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	if not check(indices.size() >= 6144, "Short streamed top-index buffer"): return {}
	# Only the first32x32x6 indices belong to physical top triangles.
	for index in 6144:
		if not check(indices[index] >= 0 and indices[index] < 1089, "Streamed top index outside top-grid buffer"): return {}
	var neighbors := {}
	for offset in range(0, 6144, 3):
		if closest_index not in [indices[offset], indices[offset + 1], indices[offset + 2]]: continue
		for corner in 3:
			var other: int = indices[offset + corner]
			if other != closest_index: neighbors[other] = true
	var edges: Array = []
	for other in neighbors:
		edges.append(closest_point.distance_to(closest.transform * vertices[other]))
	return {"tile": closest_tile, "vertex": closest_index, "local_metres": coordinates(closest_point), "distance_to_physical_point_metres": sqrt(best), "incident_edge_lengths_metres": edges, "resident_top_vertices_examined": vertex_count, "measurement_scope": "Owned33x33 top grid and first6144top indices; skirts excluded"}

func ready_terrain(view: Control, owner: Variant) -> bool:
	var near_mode: int = view.near_viewport.render_target_update_mode
	var far_mode: int = view.far_viewport.render_target_update_mode
	view.near_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	view.far_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	var started := Time.get_ticks_msec()
	var requested := false
	var ready := false
	while Time.get_ticks_msec() - started < 60000 and view.terrain.error.is_empty() and view.error.is_empty():
		for service in 4: view.terrain.tick(0.0, view.camera.position)
		if not requested and not owner.is_stream_busy() and view.terrain.pending.is_empty():
			if not check(owner.request_stream(view.camera.position), "Feature camera terrain request refused"): break
			requested = true
		if requested and not owner.is_stream_busy() and view.terrain.pending.is_empty() and view.terrain.is_ready:
			ready = true
			break
		await process_frame
	view.near_viewport.render_target_update_mode = near_mode
	view.far_viewport.render_target_update_mode = far_mode
	return check(ready, "Feature terrain failed bounded60s readiness")

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	for arg in args:
		if not arg.is_absolute_path(): quit(1); return
	var manifest_path: String = args[0].path_join("terrain-feature-manifest.json")
	var manifest: Variant = JSON.parse_string(FileAccess.get_file_as_string(manifest_path))
	# Shape/nonfinite refusal happens before bridge/model construction.
	if not check(valid_manifest(manifest), "Invalid measured-terrain manifest"): quit(1); return
	var malformed: Dictionary = manifest.duplicate(true)
	malformed.points[0].local_metres[0] = NAN
	if not check(not valid_manifest(malformed), "Nonfinite feature accepted"): quit(1); return
	if not check(not DirAccess.dir_exists_absolute(args[2]) and DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Feature output unavailable/already used"): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Feature bridge unavailable"): quit(1); return
	var path: String = args[0].path_join(manifest.save_file)
	var source_hash := FileAccess.get_sha256(path)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": args[1]}), "Feature staging refused: " + shell.error): shell.free(); quit(1); return
	var view: Control = shell.current_view
	if not check(view is FlightView and view.paused, "Feature did not open paused flight"): shell.free(); quit(1); return
	view.set_process(false)
	view.set_process_input(false)
	view.set_process_unhandled_input(false)
	view.controls_menu.hide_menu()
	view.cockpit = true
	view.update_view(0.0)
	var before: Dictionary = owner.get_freedom_flight_state()
	if not check(before.tick == manifest.tick and before.checksum == manifest.checksum and before.system_id == manifest.system_id and before.planet_id == manifest.planet_id and before.terrain_policy == 2 and NativeEnvironment.valid(before), "Feature owner/tick/environment changed"): shell.free(); quit(1); return
	if not check(before.star_direction.distance_to(vector(manifest.star_direction_local)) < 0.000002 and NativeEnvironment.sun_visibility(before) == manifest.expected_native_sun_visibility, "Feature daylight differs from actual native environment"): shell.free(); quit(1); return
	if not await ready_terrain(view, owner): shell.free(); quit(1); return
	var points: Array = []
	var camera_view_size: Vector2 = view.camera.get_viewport().get_visible_rect().size
	for point in manifest.points:
		var position := vector(point.local_metres)
		var pixel: Vector2 = view.camera.unproject_position(position)
		var mesh := nearest_stream_vertex(view.terrain, position)
		check(not mesh.is_empty(), "No streamed feature vertices")
		points.append({"label": point.label, "physical_local_metres": coordinates(position), "pixel": [pixel.x, pixel.y], "behind": view.camera.is_position_behind(position), "nearest_stream_vertex": mesh})
	for frame in 3:
		await process_frame
		if DisplayServer.get_name() != "headless": await RenderingServer.frame_post_draw
	check(owner.get_freedom_flight_state() == before, "Feature observation advanced authority")
	var copy: String = args[2].path_join("terrain-feature-resaved.json")
	check(owner.save_freedom_as(copy) and FileAccess.get_sha256(copy) == source_hash and FileAccess.get_sha256(path) == source_hash, "Feature capture changed canonical save")
	var image_hash := ""
	if DisplayServer.get_name() != "headless":
		var picture := root.get_texture().get_image()
		check(picture != null and picture.get_width() == 1280 and picture.get_height() == 720 and picture.save_png(args[2].path_join("feature.png")) == OK, "Feature image unavailable")
		image_hash = FileAccess.get_sha256(args[2].path_join("feature.png"))
	var report := {"scope": "Initialized fixed feature observation in actual native FlightView, not flown arrival or continuous/hardware acceptance", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "source_save_sha256": source_hash, "manifest_sha256": FileAccess.get_sha256(manifest_path), "image_sha256": image_hash, "display": DisplayServer.get_name(), "camera_position_metres": coordinates(view.camera.position), "camera_viewport_size": [camera_view_size.x, camera_view_size.y], "sun_visibility": NativeEnvironment.sun_visibility(before), "camera_basis": [coordinates(view.camera.basis.x), coordinates(view.camera.basis.y), coordinates(view.camera.basis.z)], "camera_near": view.camera.near, "camera_far": view.camera.far, "camera_fov": view.camera.fov, "terrain": view.terrain.report(), "points": points, "failures": failures}
	var hashes := {}
	for source in ["scripts/native/native_flight_view.gd", "scripts/native/native_environment.gd", "scripts/world/planet_stream.gd", "shaders/terrain.gdshader", "bin/libapsis_freedom_bridge.so"]:
		hashes[source] = FileAccess.get_sha256("res://" + source)
	report["sources_sha256"] = hashes
	report["engine"] = Engine.get_version_info()
	var output := FileAccess.open(args[2].path_join("feature-capture.json"), FileAccess.WRITE)
	if not check(output != null, "Feature report unavailable"): shell.free(); quit(1); return
	output.store_string(JSON.stringify(report, "\t") + "\n")
	output.close()
	shell.free()
	print("Native measured terrain: %d failures" % failures)
	quit(0 if failures == 0 else 1)
