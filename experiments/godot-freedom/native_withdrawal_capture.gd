extends SceneTree
## Opt-in close render inspection at one committed C++ withdrawal state.
const FlightView = preload("res://native_flight_view.gd")


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or DisplayServer.get_name() == "headless" or not DirAccess.dir_exists_absolute(args[2]):
		push_error("Expected approach save, prepared assets, existing output and a rendering display")
		quit(1)
		return
	for arg in args:
		if not arg.is_absolute_path():
			quit(1)
			return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not owner.initialize_freedom_continue(args[0]) or not owner.capture_freedom_port() or not owner.release_freedom_port():
		push_error(str(owner.get_last_error()))
		quit(1)
		return
	var commands := PackedFloat64Array()
	commands.resize(12)
	commands[4] = 0.25
	for tick in 120:
		if not owner.advance_freedom_flight(1.0 / 120.0, commands, false):
			push_error(str(owner.get_last_error()))
			quit(1)
			return
	root.size = Vector2i(1280, 720)
	var view := FlightView.new()
	root.add_child(view)
	view.set_process(false)
	if not view.initialize(owner, args[1]):
		push_error(view.error)
		quit(1)
		return
	var committed: Dictionary = owner.get_freedom_flight_state()
	for child in view.get_children():
		if child is VBoxContainer:
			child.visible = false
	var caption := Label.new()
	caption.position = Vector2(16, 16)
	view.add_child(caption)
	# This is an inspection camera, explicitly separate from gameplay input.
	view.camera.position = committed.body_basis * Vector3(2.15, 2.8, -2.6)
	view.camera.look_at(committed.body_basis * Vector3(1.31, 2.35, -1.45), committed.body_basis.y)
	view.terrain_camera.transform = view.camera.transform
	var captures := []
	for phase in ["paused", "firing_01", "firing_02", "firing_03"]:
		caption.text = "EXHAUST INSPECTION · committed tick %s\n%s · visual phase only; flight state frozen" % [committed.tick, phase]
		if not view.exhaust.update_applied(committed, 0.1, phase == "paused"):
			quit(1)
			return
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var picture := root.get_texture().get_image()
		var filename: String = phase + ".png"
		if picture == null or picture.is_empty() or picture.save_png(args[2].path_join(filename)) != OK:
			quit(1)
			return
		captures.append({"file": filename, "sha256": FileAccess.get_sha256(args[2].path_join(filename)), "withdrawal_intensity": view.exhaust.withdrawal_intensity, "phase": view.exhaust.phase})
	if owner.get_freedom_flight_state() != committed:
		push_error("Render inspection changed the committed flight owner")
		quit(1)
		return
	var report := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	var sources := {}
	for name in ["native_withdrawal_capture.gd", "native_flight_view.gd", "hopper_presentation.gd", "native_main_exhaust.gd", "native_main_exhaust.gdshader", "native_withdrawal_aperture.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[name] = FileAccess.get_sha256("res://" + name)
	report.store_string(JSON.stringify({"scope": "Close inspection at one unchanged committed C++ withdrawal tick; visual phase advances only; no terrain/boarding/collision/performance claim", "tick": committed.tick, "checksum": committed.checksum, "negative_force_body": Array(committed.negative_force_body), "negative_force_ratings": Array(committed.negative_force_ratings), "model_sha256": FlightView.WAYFARER_HASH, "sources_sha256": sources, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "camera_position": [view.camera.position.x, view.camera.position.y, view.camera.position.z], "captures": captures}, "\t") + "\n")
	view.free()
	print("Native withdrawal detail captures: %d" % captures.size())
	quit(0)
