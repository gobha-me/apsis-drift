extends SceneTree
## Opt-in visible port lifecycle review; no pose rewrites or owner-handoff claim.
const FlightView = preload("res://native_flight_view.gd")


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not args[2].is_absolute_path() or not DirAccess.dir_exists_absolute(args[2]) or DisplayServer.get_name() == "headless":
		push_error("Expected approach save, prepared assets, existing output and a rendering display")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not owner.initialize_freedom_continue(args[0]):
		push_error(str(owner.get_last_error()))
		quit(1)
		return
	root.size = Vector2i(1280, 720)
	var view := FlightView.new()
	root.add_child(view)
	view.set_process(false)
	if not view.initialize(owner, args[1]) or view.station == null or view.state.attached or not view.state.docking.ready:
		push_error("Capture requires a real ready Wayfarer approach: " + view.error)
		quit(1)
		return
	for frame in 6000:
		view.terrain.tick(0.02, view.camera.position)
		await process_frame
		await RenderingServer.frame_post_draw
		await create_timer(0.02).timeout
		if view.terrain.is_ready or not view.terrain.error.is_empty():
			break
	if not view.terrain.is_ready or not view.terrain.error.is_empty():
		push_error("Selected terrain refused during port capture")
		quit(1)
		return
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	var captures := []
	var before_release: Dictionary = {}
	view.toggle_pause()
	for phase in ["approach", "captured", "cockpit", "constrained", "released", "departure"]:
		view.cockpit = phase == "cockpit"
		if phase == "captured":
			view.port_command("capture_freedom_port")
			if not owner.get_freedom_flight_state().attached:
				push_error(str(owner.get_last_error()))
				quit(1)
				return
		elif phase == "constrained":
			for tick in 120:
				if not owner.advance_freedom_flight(1.0 / 120.0, neutral, false):
					push_error(str(owner.get_last_error()))
					quit(1)
					return
			before_release = owner.get_freedom_flight_state()
		elif phase == "released":
			view.port_command("release_freedom_port")
			if owner.get_freedom_flight_state().attached or owner.get_freedom_flight_state().checksum != before_release.checksum:
				push_error("Release refused or changed same-tick canonical pose")
				quit(1)
				return
		elif phase == "departure":
			var commands := neutral.duplicate()
			commands[4] = 0.25
			# Withdraw down the reserved column before applying forward thrust.
			for tick in 720:
				commands[5] = 0.1 if tick >= 360 else 0.0
				if not owner.advance_freedom_flight(1.0 / 120.0, commands, false):
					push_error(str(owner.get_last_error()))
					quit(1)
					return
		view.state = owner.get_freedom_flight_state()
		view.update_view(0.05)
		view.terrain.tick(0.0, view.camera.position)
		if view.station.transform != Transform3D(view.state.station_basis, view.state.station_position):
			push_error("Station registration changed independently of C++")
			quit(1)
			return
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var picture := root.get_texture().get_image()
		var filename: String = phase + ".png"
		if picture == null or picture.is_empty() or picture.save_png(args[2].path_join(filename)) != OK:
			push_error("Port lifecycle image could not be saved")
			quit(1)
			return
		captures.append({"phase": phase, "file": filename, "sha256": FileAccess.get_sha256(args[2].path_join(filename)), "width": picture.get_width(), "height": picture.get_height(), "tick": view.state.tick, "checksum": view.state.checksum, "attached": view.state.attached, "target_port": view.state.target_port, "collar_separation_metres": view.state.docking.separation, "capture_ready": view.state.docking.ready, "main_intensity": view.exhaust.intensity, "camera_position": [view.camera.position.x, view.camera.position.y, view.camera.position.z], "station_position": [view.state.station_position.x, view.state.station_position.y, view.state.station_position.z]})
	var sources := {}
	for name in ["native_port_capture.gd", "native_flight_view.gd", "native_station_view.gd", "hopper_presentation.gd", "native_main_exhaust.gd", "native_main_exhaust.gdshader", "planet_stream.gd", "terrain.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[name] = FileAccess.get_sha256("res://" + name)
	var report := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Selected physical port lifecycle, actual station/ship registration; no walking/boarding/surface or hardware-performance acceptance", "selected_save_sha256": FileAccess.get_sha256(args[0]), "station_sha256": view.station.STATION_HASH, "wayfarer_sha256": FlightView.WAYFARER_HASH, "sources_sha256": sources, "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "terrain": view.terrain.report(), "captures": captures}, "\t") + "\n")
	view.free()
	print("Native port rendered captures: %d" % captures.size())
	quit(0)
