extends SceneTree
## Actual voyage checkpoints: headless presentation/input contracts or opt-in GPU
## review. Rendering waits freeze authoritative time; no relocation or new world.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const LABELS = ["station-start", "seated", "departed", "atmosphere-entry", "cruise-start", "cruise-end", "space-exit", "home-intercept", "near-port", "captured", "station-return"]
var failed := false

func _initialize() -> void:
	call_deferred("run")

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failed = true
	return value

static func finite_number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	for byte in value.to_utf8_buffer():
		if byte < 48 or byte > 57: return false
	return true

static func valid_trace(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema_version") != 1 or value.get("seed") != "42" or value.get("physical_catalog") != 2 or value.get("physical_ephemeris") != 2 or value.get("terrain_source_lod") != 8 or value.get("terrain_relief_version") != 0: return false
	var rows: Variant = value.get("checkpoints")
	if not rows is Array or rows.size() != LABELS.size(): return false
	for i in rows.size():
		var row: Variant = rows[i]
		if not row is Dictionary or row.get("label") != LABELS[i] or row.get("file") != LABELS[i] + ".json" or row.get("next_file") != LABELS[i] + "-next.json": return false
		for field in ["tick", "checksum", "next_tick", "next_checksum"]:
			if not decimal(row.get(field)): return false
		for field in ["attached", "walking"]:
			if not row.get(field) is bool: return false
		for field in ["altitude_metres", "air_speed_metres_per_second", "air_density", "atmosphere_edge_metres", "terrain_elevation_metres"]:
			if not finite_number(row.get(field)): return false
		var position: Variant = row.get("position_metres")
		if not position is Array or position.size() != 3: return false
		for component in position:
			if not finite_number(component): return false
	return true

func freeze(view: Control) -> void:
	view.set_process(false)
	view.set_process_input(false)
	view.set_process_unhandled_input(false)
	if view is FlightView:
		view.player_input.set_process_input(false)
		view.player_input.set_process_unhandled_input(false)
		view.controls_menu.set_process_input(false)

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not args[2].is_absolute_path(): quit(1); return
	var source := FileAccess.open(args[0].path_join("trace.json"), FileAccess.READ)
	if source == null or source.get_length() > 32768: quit(1); return
	var trace: Variant = JSON.parse_string(source.get_as_text())
	source.close()
	if not check(valid_trace(trace), "Malformed planetary checkpoint manifest"): quit(1); return
	# Shape/nonfinite/path refusal precedes model construction and visual review.
	for damage in ["shape", "nonfinite", "path", "version", "terrain_recipe"]:
		var bad: Dictionary = trace.duplicate(true)
		if damage == "shape": bad.checkpoints[0].position_metres.append(0.0)
		elif damage == "nonfinite": bad.checkpoints[0].altitude_metres = NAN
		elif damage == "path": bad.checkpoints[0].file = "../unrelated.json"
		elif damage == "version": bad.physical_ephemeris = 3
		else: bad.terrain_relief_version = 1
		check(not valid_trace(bad), "Invalid checkpoint accepted: " + damage)
	if failed: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native C++ bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var rendered := DisplayServer.get_name() != "headless"
	var captures: Array = []
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Checkpoint output directory unavailable"): shell.free(); quit(1); return
	for row in trace.checkpoints:
		var path: String = args[0].path_join(row.file)
		if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": args[1]}), "Checkpoint staging refused: " + shell.error): break
		var view: Control = shell.current_view
		freeze(view)
		# Drain resources retired by the previous view even in headless mode.
		# The newly installed view is frozen before yielding.
		for frame in 2: await process_frame
		var state: Dictionary = owner.get_freedom_flight_state()
		if not check(state.tick == row.tick and state.checksum == row.checksum and state.attached == row.attached and (view is WalkView) == row.walking and view.paused, "Checkpoint changed while entering native view: " + row.label): break
		check(not state.port_approach.active, "Continue restored approach command")
		view._process(1.0)
		check(owner.get_freedom_flight_state() == state, "Paused checkpoint advanced time")
		if rendered:
			if view is FlightView:
				# Keep the real Continue pause, but uncover its frozen scene.
				view.controls_menu.hide_menu()
				view.cockpit = false
				view.update_view(0.0)
				for frame in 3000:
					view.terrain.tick(0.02, view.camera.position)
					await process_frame
					await create_timer(0.02).timeout
					if view.terrain.is_ready or not view.terrain.error.is_empty(): break
				if not check(view.terrain.is_ready and view.terrain.error.is_empty(), "Checkpoint terrain did not become ready"): break
			for frame in 3:
				await process_frame
				await RenderingServer.frame_post_draw
			if not check(owner.get_freedom_flight_state() == state, "Rendering advanced authoritative flight"): break
			var picture := root.get_texture().get_image()
			var name: String = row.label + ".png"
			if not check(picture != null and not picture.is_empty() and picture.save_png(args[2].path_join(name)) == OK, "Could not save checkpoint image"): break
			captures.append({"label": row.label, "file": name, "sha256": FileAccess.get_sha256(args[2].path_join(name)), "save_sha256": FileAccess.get_sha256(path), "tick": state.tick, "checksum": state.checksum, "width": picture.get_width(), "height": picture.get_height(), "terrain": view.terrain.report() if view is FlightView else {}})
		# Resume and advance one real neutral tick through the public bridge;
		# independently exported C++ bytes must match the complete native save.
		if view is WalkView: view.resume_requested()
		else: view.toggle_pause()
		if not check(not view.paused, "Neutral native resume refused: " + row.label): break
		var neutral := PackedFloat64Array()
		neutral.resize(3 if view is WalkView else 12)
		var advanced: bool = owner.advance_freedom_walk(1.0 / 120.0, neutral) if view is WalkView else owner.advance_freedom_flight(1.0 / 120.0, neutral, false)
		check(advanced, "Native neutral step refused: " + str(owner.get_last_error()))
		state = owner.get_freedom_flight_state()
		check(state.tick == row.next_tick and state.checksum == row.next_checksum, "Native checkpoint step differs from C++: " + row.label)
		var saved: String = args[2].path_join(row.label + "-native.json")
		check(owner.save_freedom_as(saved) and FileAccess.get_sha256(saved) == FileAccess.get_sha256(args[0].path_join(row.next_file)), "Complete native phase save differs: " + row.label)
		if failed: break
	if rendered and not failed:
		var hashes := {}
		for name in ["studies/captures/native_planetary_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_flight_view.gd", "scripts/world/planet_stream.gd", "shaders/terrain.gdshader", "shaders/native_close_composite.gdshader", "bin/libapsis_freedom_bridge.so"]:
			hashes[name] = FileAccess.get_sha256("res://" + name)
		var report := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
		if report == null: shell.free(); quit(1); return
		report.store_string(JSON.stringify({"scope": "Native rendering of real public-command voyage checkpoints; frozen rendering time, not a continuous native/manual/performance trace", "trace_sha256": FileAccess.get_sha256(args[0].path_join("trace.json")), "sources_sha256": hashes, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "captures": captures}, "\t") + "\n")
		report.close()
	shell.free()
	print("Native planetary checkpoints: %d failures" % int(failed))
	quit(1 if failed else 0)
