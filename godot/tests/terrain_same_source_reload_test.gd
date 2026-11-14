extends "res://studies/captures/native_frame_import_capture.gd"
## Independent presenter: repeated genuine Continue, not the capture source-role
## guard. Inherit actual settle/camera helpers; never override terrain or bridge.
const StreamView = preload("res://scripts/world/planet_stream.gd")
var records: Array = []
var reference_pixels := PackedByteArray()
var reference_state: Dictionary = {}
var reference_camera: Dictionary = {}
var reference_tiles: Dictionary = {}
var reference_ids: Dictionary = {}
var reference_view_id := 0


func bytes_hash(value: Variant) -> String:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(var_to_bytes(value))
	return hash.finish().hex_encode()


func snapshot(view: Control, owner: Variant, output: String, canonical: String, stage: int, frame: int) -> bool:
	var label := "%d-f%02d" % [stage, frame]
	var before: Dictionary = owner.get_freedom_flight_state()
	var camera := {"near": camera_record(view.camera), "terrain": camera_record(view.terrain_camera)}
	if not require(before == reference_state and view.paused, "Frozen authoritative state changed: " + label): return false
	var expected := [float(root.size.x), float(root.size.y)]
	if not require(camera.near.viewport == expected and camera.terrain.viewport == expected, "Actual camera viewport differs from output: " + label): return false
	if reference_camera.is_empty(): reference_camera = camera.duplicate(true)
	if not require(camera == reference_camera, "Repeated Continue changed camera: " + label): return false
	var tiles := {}
	var ids := {}
	for key in view.terrain.nodes:
		var node: MeshInstance3D = view.terrain.nodes[key]
		if not require(node.mesh is ArrayMesh and node.material_override is ShaderMaterial, "Missing actual terrain upload: " + key): return false
		var material: ShaderMaterial = node.material_override
		var uniforms := {}
		for uniform in material.shader.get_shader_uniform_list():
			uniforms[uniform.name] = material.get_shader_parameter(uniform.name)
		if not require(material.render_priority == StreamView.tile_render_priority(key), "Actual material lost tile priority: " + key): return false
		tiles[key] = {"arrays": bytes_hash(node.mesh.surface_get_arrays(0)),
			"transform": bytes_hash(node.global_transform), "uniforms": bytes_hash(uniforms),
			"priority": material.render_priority, "visible": node.visible, "layers": node.layers,
			"sorting_offset": node.sorting_offset}
		ids[key] = [node.get_instance_id(), material.get_instance_id(), node.mesh.get_rid().get_id()]
	if not require(tiles.size() >= 6 and tiles.size() <= 384 and tiles.size() == view.terrain.resident_ids.size(), "Readback has no complete real terrain cover"): return false
	if reference_tiles.is_empty():
		reference_tiles = tiles.duplicate(true)
		reference_ids = ids.duplicate(true)
		reference_view_id = view.get_instance_id()
	if not require(tiles == reference_tiles, "Repeated Continue changed actual buffers/transforms/uniforms: " + label): return false
	if stage == 1:
		if not require(view.get_instance_id() != reference_view_id, "Reload reused first FlightView"): return false
		for key in ids:
			for index in 3:
				if not require(ids[key][index] != reference_ids[key][index], "Reload did not create a fresh instance/material/mesh: " + key): return false
	var save_name := label + "-resaved.json"
	if not require(owner.save_freedom_as(output.path_join(save_name)) and FileAccess.get_file_as_bytes(output.path_join(save_name)) == FileAccess.get_file_as_bytes(canonical), "Complete Save As differs from independent canonical bytes: " + label): return false
	var image := root.get_texture().get_image()
	if not require(valid_image(image, root.size), "Empty/uniform/wrong-size actual readback: " + label): return false
	image.convert(Image.FORMAT_RGBA8)
	var png := label + ".png"
	if not require(image.save_png(output.path_join(png)) == OK, "Cannot retain regression image"): return false
	if reference_pixels.is_empty(): reference_pixels = image.get_data()
	# Exact equality is unconditional; no tolerances, golden updates or hardware
	# exceptions. Saving first preserves the mismatching image when this fails.
	if not require(image.get_data() == reference_pixels, "Same-source reload exact pixel buffers differ: " + label): return false
	if not require(owner.get_freedom_flight_state() == before and camera == {"near": camera_record(view.camera), "terrain": camera_record(view.terrain_camera)}, "Readback changed authoritative state/cameras"): return false
	if not require(view.error.is_empty() and view.terrain.error.is_empty(), "Presentation error at readback: " + view.error + view.terrain.error): return false
	records.append({"stage": stage, "frame": frame, "source": "system-import.json", "image": png,
		"png_sha256": FileAccess.get_sha256(output.path_join(png)), "pixel_sha256": bytes_hash(image.get_data()),
		"canonical_save": save_name, "camera": camera, "tiles": tiles,
		"projection_sha256": bytes_hash(before), "identity": view.terrain.identity})
	return true


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if not require(args.size() == 5, "Expected real fixtures/assets/new output/width/height"):
		quit(1); return
	if not require(DisplayServer.get_name() != "headless" and args[3] in ["800", "1920", "3840"] and args[4] in ["450", "1080", "2160"] and valid_dimensions(args[3].to_int(), args[4].to_int()), "Expected rendering display and supported dimensions"):
		quit(1); return
	if not require(not DirAccess.dir_exists_absolute(args[2]) and not FileAccess.file_exists(args[2]) and DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Output must be new"):
		quit(1); return
	var inputs := {}
	for name in ["system-import.json", "canonical-before.json", "canonical-after.json", "malformed-import.json"]:
		inputs[name] = FileAccess.get_sha256(args[0].path_join(name))
	if not require(inputs.values().all(func(value: String) -> bool: return value.length() == 64), "Authentic fixture set unavailable"):
		quit(1); return
	root.size = Vector2i(args[3].to_int(), args[4].to_int())
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	root.content_scale_size = Vector2i.ZERO
	root.content_scale_factor = 1.0
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not require(ClassDB.class_exists("FreedomBridge"), "Actual C++ bridge unavailable"):
		quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for stage in 2:
		if not require(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("system-import.json"), "assets": args[1]}), "Real same-source Continue refused: " + shell.error): break
		var view: Control = shell.current_view
		if not require(view is FlightView and view.paused and NativeEnvironment.valid(owner.get_freedom_flight_state()), "Expected genuine paused FlightView"): break
		view.set_process(false)
		view.set_process_input(false)
		view.set_process_unhandled_input(false)
		view.controls_menu.hide_menu()
		view.cockpit = false
		for frame in 2:
			await process_frame
			view.layout_hud()
		view.update_view(0.0)
		var state: Dictionary = owner.get_freedom_flight_state()
		if reference_state.is_empty(): reference_state = state.duplicate(true)
		if not require(state == reference_state, "Same-source Continue changed complete flight projection"): break
		if not await settle(view, owner): break
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var last_frame := 0
		for offset in [0, 1, 8, 32]:
			for frame in range(offset - last_frame):
				await process_frame
				await RenderingServer.frame_post_draw
			last_frame = offset
			if not snapshot(view, owner, args[2], args[0].path_join("canonical-before.json"), stage, offset): break
		if not error.is_empty(): break
	for name in inputs:
		require(FileAccess.get_sha256(args[0].path_join(name)) == inputs[name], "Regression modified authentic fixture: " + name)
	require(records.size() == 8, "Both real views must complete all four frozen readbacks")
	if records.size() == 8:
		require(records[0].identity.generation != records[4].identity.generation, "Repeated Continue did not replace actual stream generation")
	var sources := {}
	for path in ["tests/terrain_same_source_reload_test.gd", "studies/captures/native_frame_import_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_flight_view.gd", "scripts/world/planet_stream.gd", "shaders/terrain.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[path] = FileAccess.get_sha256("res://" + path)
	var report := {"schema": "apsis.terrain-same-source-reload-test/1", "passed": error.is_empty(), "error": error,
		"viewport": [root.size.x, root.size.y], "fixture_sha256": inputs, "sources_sha256": sources,
		"prepared_assets_sha256": FileAccess.get_sha256(args[1].path_join("prepared.json")),
		"engine": Engine.get_version_info(), "engine_sha256": FileAccess.get_sha256(OS.get_executable_path()),
		"display": DisplayServer.get_name(), "rendering_method": RenderingServer.get_current_rendering_method(), "captures": records}
	var file := FileAccess.open(args[2].path_join("reload.json"), FileAccess.WRITE)
	if require(file != null, "Cannot retain reload regression report"):
		file.store_string(JSON.stringify(report, "\t") + "\n")
		file.close()
	shell.free()
	owner = null
	await process_frame
	print("Terrain same-source reload: " + ("PASS" if error.is_empty() else "FAIL"))
	quit(0 if error.is_empty() else 1)
