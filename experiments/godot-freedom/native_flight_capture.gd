extends SceneTree
## Opt-in GPU review of a real selected flight; not owner journey/performance proof.
const FlightView = preload("res://native_flight_view.gd")


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not args[2].is_absolute_path() or not DirAccess.dir_exists_absolute(args[2]):
		push_error("Expected selected flight save, prepared assets, existing capture directory")
		quit(1)
		return
	if DisplayServer.get_name() == "headless":
		push_error("Flight capture requires a rendering display driver")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	if not bridge.initialize_freedom_continue(args[0]):
		push_error(str(bridge.get_last_error()))
		quit(1)
		return
	root.size = Vector2i(1280, 720)
	var view := FlightView.new()
	root.add_child(view)
	view.set_process(false)
	if not view.initialize(bridge, args[1]) or view.exhaust == null:
		push_error("Flight capture requires the selected Wayfarer: " + view.error)
		view.free()
		quit(1)
		return
	# Poll the actual selected terrain worker; no fabricated cover/surface.
	for frame in 6000:
		view.terrain.tick(0.02, view.camera.position)
		if not view.terrain.error.is_empty():
			push_error(str(view.terrain.error))
			quit(1)
			return
		await process_frame
		await RenderingServer.frame_post_draw
		await create_timer(0.02).timeout
		if view.terrain.is_ready:
			break
	if not view.terrain.is_ready:
		push_error("Selected terrain did not become ready for capture")
		quit(1)
		return
	var captures := []
	var initial: Dictionary = bridge.get_freedom_flight_state()
	for item in [["neutral", false, true, 0.0], ["main_phase_a", false, false, 0.05], ["main_phase_b", false, false, 0.11], ["cockpit", true, false, 0.02], ["paused", false, true, 0.0]]:
		if item[0] == "main_phase_a":
			var controls := PackedFloat64Array()
			controls.resize(12)
			controls[5] = 0.5
			if not bridge.advance_freedom_flight(1.0 / 120.0, controls, false):
				push_error(str(bridge.get_last_error()))
				quit(1)
				return
		view.state = bridge.get_freedom_flight_state()
		view.cockpit = item[1]
		if view.paused != item[2]:
			view.toggle_pause()
		view.update_view(item[3])
		view.terrain.tick(0.0, view.camera.position)
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var spatial: SubViewport = view.camera.get_viewport()
		if spatial.size != Vector2i(view.size) or view.terrain_camera.get_viewport().size != spatial.size:
			push_error("Flight capture viewport did not resize with the root")
			quit(1)
			return
		var image := root.get_texture().get_image()
		var close_image := spatial.get_texture().get_image()
		var filename: String = item[0] + ".png"
		if image == null or image.is_empty() or image.save_png(args[2].path_join(filename)) != OK:
			push_error("Flight capture failed")
			quit(1)
			return
		var close_filename: String = item[0] + "-close.png"
		if close_image == null or close_image.is_empty() or close_image.get_used_rect().get_area() < 1000 or close_image.save_png(args[2].path_join(close_filename)) != OK:
			push_error("Close pass contains no visible ship geometry")
			quit(1)
			return
		captures.append({"file": filename, "sha256": FileAccess.get_sha256(args[2].path_join(filename)), "close_file": close_filename, "close_sha256": FileAccess.get_sha256(args[2].path_join(close_filename)), "width": image.get_width(), "height": image.get_height(), "logical_viewport": [spatial.size.x, spatial.size.y], "tick": view.state.tick, "checksum": view.state.checksum, "cockpit": view.cockpit, "paused": view.paused, "main_intensity": view.exhaust.intensity, "visual_phase": view.exhaust.phase, "near_camera": [view.camera.near, view.camera.far], "terrain_camera": [view.terrain_camera.near, view.terrain_camera.far]})
	var sources := {}
	for path in ["native_flight_capture.gd", "native_flight_view.gd", "hopper_presentation.gd", "native_main_exhaust.gd", "native_main_exhaust.gdshader", "planet_stream.gd", "terrain.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[path] = FileAccess.get_sha256("res://" + path)
	var report := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Selected saved flight, native terrain/cameras/main exhaust review; no station journey or hardware-performance acceptance", "selected_save_sha256": FileAccess.get_sha256(args[0]), "model_sha256": FlightView.WAYFARER_HASH, "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "initial_tick": initial.tick, "initial_checksum": initial.checksum, "renderer": RenderingServer.get_video_adapter_name(), "engine": Engine.get_version_info(), "sources_sha256": sources, "terrain": view.terrain.report(), "captures": captures}, "\t") + "\n")
	view.free()
	print("Saved flight rendered captures: %d" % captures.size())
	quit(0)
