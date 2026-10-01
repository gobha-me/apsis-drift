extends SceneTree
## Private owner-review reel. Cameras are editorial; only C++ advances flight.
## The selected legacy orbit is independent of unfinished station boarding.

const FlightView = preload("res://native_flight_view.gd")
const FPS := 24
const SHOT_SECONDS := 20
const SHOTS := ["powered_exterior", "cockpit", "coast", "attitude", "planet"]
const TRUTH := "Saved flight slice / independent orbit start"
var bridge: Variant
var view: Variant
var output := ""
var selected_save := ""
var assets := ""
var review := false
var pixels := Vector2i(1920, 1080)
var caption: Label
var output_frame := 0
var observations: Array[Dictionary] = []
var shots: Array[Dictionary] = []
var initial: Dictionary
var failed := false


func _initialize() -> void:
	call_deferred("run")


func require_ok(condition: bool, message: String) -> bool:
	if condition:
		return true
	push_error(message)
	failed = true
	quit(1)
	return false


func parse_options() -> bool:
	var options := {}
	for argument in OS.get_cmdline_user_args():
		var pair := argument.split("=", true, 1)
		if not require_ok(pair.size() == 2 and pair[0] in ["--save", "--assets", "--output", "--review", "--render-size"] and not options.has(pair[0]), "Unknown or duplicate reel argument"):
			return false
		options[pair[0]] = pair[1]
	selected_save = options.get("--save", "")
	assets = options.get("--assets", "")
	output = options.get("--output", "")
	if not require_ok(selected_save.is_absolute_path() and assets.is_absolute_path() and output.is_absolute_path(), "Supply absolute --save, --assets and --output paths"):
		return false
	if not require_ok(FileAccess.file_exists(selected_save) and DirAccess.dir_exists_absolute(assets) and DirAccess.dir_exists_absolute(output) and DirAccess.get_files_at(output).is_empty() and DirAccess.get_directories_at(output).is_empty(), "Require selected save, prepared assets and an empty output directory"):
		return false
	if not require_ok(options.get("--review", "false") in ["false", "true"] and options.get("--render-size", "1920x1080") in ["1920x1080", "1280x720"], "Invalid review or render size"):
		return false
	review = options.get("--review", "false") == "true"
	if options.get("--render-size", "1920x1080") == "1280x720":
		pixels = Vector2i(1280, 720)
	return true


func run() -> void:
	if not parse_options():
		return
	if not require_ok(DisplayServer.get_name() != "headless", "Reel requires a rendering display; headless is for contracts"):
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not require_ok(ClassDB.class_exists("FreedomBridge"), "Stage the qualified C++ bridge first"):
		return
	bridge = ClassDB.instantiate("FreedomBridge")
	if not require_ok(bridge.initialize_freedom_continue(selected_save), "Selected flight refused: " + str(bridge.get_last_error())):
		return
	initial = bridge.get_freedom_flight_state()
	if not require_ok(FlightView.valid_state(initial) and initial.frame_id == "2" and not initial.attached and bridge.get_freedom_walk_state().is_empty(), "Reel requires an unattached legacy Wayfarer flight, not a station actor"):
		return
	var source_hash := FileAccess.get_sha256(selected_save)
	root.size = pixels
	root.use_debanding = true
	view = FlightView.new()
	root.add_child(view)
	view.set_process(false)
	view.set_process_unhandled_input(false)
	if not require_ok(view.initialize(bridge, assets) and view.exhaust != null, "Selected flight view refused: " + str(view.error)):
		return
	# Keep both native depth passes and the same C++ world, but remove game menus.
	for child in view.get_children():
		if child is Control and not child is SubViewportContainer:
			child.hide()
	view.paused = false
	for child in view.scene.get_children():
		if child is WorldEnvironment:
			# Soft editorial exposure/fill; source geometry and physical sun stay.
			child.environment.ambient_light_color = Color(0.7, 0.78, 0.9)
			child.environment.ambient_light_energy = 1.2
			view.terrain_camera.environment = child.environment.duplicate()
			view.terrain_camera.environment.tonemap_exposure = 2.4
	view.camera.environment.ambient_light_color = Color(0.7, 0.78, 0.9)
	view.camera.environment.ambient_light_energy = 1.2
	build_caption()
	for shot in SHOTS:
		if shot == "attitude" and not require_ok(bridge.set_freedom_assistance(false), "Assistance change refused"):
			return
		var first := output_frame
		var before: Dictionary = bridge.get_freedom_flight_state()
		view.state = before
		view.paused = shot == "planet"
		view.update_view(0.0)
		pose_camera(shot, 0.0)
		if not await warm_terrain():
			return
		for index in SHOT_SECONDS * FPS:
			var u := float(index) / float(SHOT_SECONDS * FPS - 1)
			var controls := flight_controls(shot, index)
			# Five exact 120Hz ticks per 24fps frame. No dropped catch-up shortcut.
			if not require_ok(bridge.advance_freedom_flight(1.0 / FPS, controls, shot == "planet"), "C++ flight step refused: " + str(bridge.get_last_error())):
				return
			view.state = bridge.get_freedom_flight_state()
			if not require_ok(FlightView.valid_state(view.state), "Flight observation became invalid"):
				return
			view.update_view(1.0 / FPS)
			pose_camera(shot, u)
			update_caption(shot)
			var capture := not review or index in [0, SHOT_SECONDS * FPS / 2, SHOT_SECONDS * FPS - 1]
			if capture or index % FPS == 0:
				view.terrain.tick(1.0 / FPS, view.camera.position)
				if not capture:
					await process_frame
					await RenderingServer.frame_post_draw
				if not require_ok(view.terrain.error.is_empty(), "Selected terrain failed: " + str(view.terrain.error)):
					return
			if index % FPS == 0 or index == SHOT_SECONDS * FPS - 1:
				observations.append(observe(shot, index, controls))
			if capture:
				if not await capture_frame():
					return
			if index % (FPS * 5) == 0:
				print("REEL_FLIGHT %s %d/%d tick=%s" % [shot, index, SHOT_SECONDS * FPS, view.state.tick])
		shots.append({"id": shot, "first_frame": first, "frames": output_frame - first, "seconds": SHOT_SECONDS,
			"paused": shot == "planet", "time_scale": 1.0, "first_tick": before.tick, "last_tick": view.state.tick,
			"first_checksum": before.checksum, "last_checksum": view.state.checksum,
			"terrain": view.terrain.report(), "caption": TRUTH})
		if not require_ok(FileAccess.get_sha256(selected_save) == source_hash, "Source save changed during capture"):
			return
	var saved := output.path_join("final-flight.json")
	if not require_ok(bridge.save_freedom_as(saved), "Final C++ Save As refused: " + str(bridge.get_last_error())):
		return
	var final: Dictionary = bridge.get_freedom_flight_state()
	var reopened: Variant = ClassDB.instantiate("FreedomBridge")
	if not require_ok(reopened.initialize_freedom_continue(saved), "Final C++ save did not reopen"):
		return
	var resumed: Dictionary = reopened.get_freedom_flight_state()
	if not require_ok(resumed.tick == final.tick and resumed.checksum == final.checksum and resumed.position_metres == final.position_metres and resumed.velocity_metres_per_second == final.velocity_metres_per_second and resumed.orientation_wxyz == final.orientation_wxyz, "Final Save As changed physical state"):
		return
	var sources := {}
	for path in ["reel_flight_capture.gd", "native_flight_view.gd", "hopper_presentation.gd", "native_main_exhaust.gd", "native_main_exhaust.gdshader", "planet_stream.gd", "terrain.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[path] = FileAccess.get_sha256("res://" + path)
	var report := {"schema_version": 1, "kind": "Godot offline owner-review saved-flight chapter; editorial cameras, independent legacy orbit",
		"fps": FPS, "seconds": SHOT_SECONDS * SHOTS.size(), "frames": output_frame, "pixels": [pixels.x, pixels.y], "review_only": review,
		"engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(),
		"selected_save_sha256": source_hash, "source_save_unchanged": FileAccess.get_sha256(selected_save) == source_hash,
		"initial_tick": initial.tick, "initial_checksum": initial.checksum, "final_tick": final.tick, "final_checksum": final.checksum,
		"final_save_sha256": FileAccess.get_sha256(saved), "final_save_reopened": true, "sources_sha256": sources,
		"model_sha256": FlightView.WAYFARER_HASH, "descriptor_sha256": FlightView.WAYFARER_DESCRIPTOR_HASH,
		"universe_seed": final.universe_seed, "system_id": final.system_id, "planet_id": final.planet_id, "craft_id": final.craft_id,
		"shots": shots, "observations": observations, "runtime_performance_claim": false,
		"scope": "Actual C++ flight/actuation/coast/selected terrain; final same-world editorial planet inspection pauses physics. No boarding, seated actor, descent, landing or continuous station-to-flight claim.",
		"lighting": "C++ same-tick star plus presentation ambient fill1.2 and distant-terrain exposure2.4; no new terrain or atmosphere geometry",
		"audio": "No live audio captured; root mixes separately licensed score", "licenses": "Prepared freedom-starter-01 license/provenance receipts; original capture code BSD-3-Clause"}
	var file := FileAccess.open(output.path_join("render.json"), FileAccess.WRITE)
	if not require_ok(file != null, "Cannot write reel report"):
		return
	file.store_string(JSON.stringify(report, "\t") + "\n")
	print("REEL_FLIGHT complete frames=%d tick=%s checksum=%s" % [output_frame, final.tick, final.checksum])
	view.free()
	quit(0)


func build_caption() -> void:
	caption = Label.new()
	caption.position = Vector2(24, 24)
	caption.add_theme_font_size_override("font_size", 20)
	caption.add_theme_color_override("font_color", Color(0.76, 0.82, 0.9))
	caption.add_theme_color_override("font_shadow_color", Color.BLACK)
	caption.add_theme_constant_override("shadow_offset_x", 1)
	caption.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(caption)


func update_caption(shot: String) -> void:
	caption.text = TRUTH + ("\nPaused editorial planet view" if shot == "planet" else "")


func flight_controls(shot: String, index: int) -> PackedFloat64Array:
	var result := PackedFloat64Array()
	result.resize(12)
	var seconds := float(index) / FPS
	if shot == "powered_exterior":
		result[5] = 0.24 + 0.63 * smoothstep(0.0, 12.0, seconds)
	elif shot == "cockpit" and seconds < 8.0:
		result[5] = 0.32 * (1.0 - smoothstep(3.0, 8.0, seconds))
	elif shot == "attitude":
		if seconds < 2.0:
			result[8] = 0.008
		elif seconds >= 16.0 and seconds < 18.0:
			result[11] = 0.008
	return result


func pose_camera(shot: String, u: float) -> void:
	var basis: Basis = view.state.body_basis
	view.cockpit = shot == "cockpit"
	if shot == "cockpit":
		var head := Basis.from_euler(Vector3(-0.04, lerpf(-0.08, 0.10, u), 0.0))
		view.camera.transform = Transform3D(basis * head, basis * view.pilot_eye)
		view.camera.fov = 74.0
	elif shot == "planet":
		var radius: float = view.state.planet_radius
		var centre := Vector3(0, -radius - float(view.state.altitude), 0)
		var sun: Vector3 = view.state.star_direction.normalized()
		var side := sun.cross(Vector3.UP).normalized()
		if side.length_squared() < 0.5:
			side = Vector3.RIGHT
		view.camera.position = centre + (sun * 2.6 + side * lerpf(0.6, 0.9, u)) * radius
		view.camera.look_at(centre, Vector3.UP if absf(sun.y) < 0.98 else Vector3.RIGHT)
		view.camera.fov = 48.0
		view.terrain_camera.far = radius * 8.0
		view.terrain_camera.near = view.terrain_camera.far / 400000.0
	else:
		var offset := Vector3(lerpf(25.0, 20.0, u), lerpf(7.0, 10.0, u), lerpf(29.0, 26.0, u))
		if shot == "coast":
			offset = Vector3(lerpf(-22.0, -15.0, u), 8.0, lerpf(20.0, 29.0, u))
		elif shot == "attitude":
			offset = Vector3(23.0, 7.0, 31.0)
		view.camera.position = basis * offset if shot != "attitude" else offset
		view.camera.look_at(Vector3(0, 0.5, 0), Vector3.UP if shot == "attitude" else basis.y)
		view.camera.fov = 53.0
	view.terrain_camera.transform = view.camera.transform
	view.terrain_camera.fov = view.camera.fov


func warm_terrain() -> bool:
	var stable := 0
	var deadline := Time.get_ticks_msec() + 120000
	while Time.get_ticks_msec() < deadline:
		view.terrain.tick(0.02, view.camera.position)
		await process_frame
		await RenderingServer.frame_post_draw
		if not require_ok(view.terrain.error.is_empty(), "Terrain warmup refused: " + str(view.terrain.error)):
			return false
		if view.terrain.is_ready and view.terrain.pending.is_empty() and not bridge.is_stream_busy():
			stable += 1
		else:
			stable = 0
		if stable >= 4:
			return true
	return require_ok(false, "Selected C++ terrain did not become ready")


func observe(shot: String, index: int, controls: PackedFloat64Array) -> Dictionary:
	var state: Dictionary = view.state
	var eye: Vector3 = view.camera.position
	return {"shot": shot, "shot_frame": index, "tick": state.tick, "checksum": state.checksum,
		"position_metres": Array(state.position_metres), "velocity_metres_per_second": Array(state.velocity_metres_per_second),
		"orientation_wxyz": Array(state.orientation_wxyz), "commands": Array(controls), "altitude_metres": state.altitude,
		"air_density": state.air_density, "main_force_newtons": state.negative_force_body[2], "main_exhaust_intensity": view.exhaust.intensity,
		"camera_metres": [eye.x, eye.y, eye.z], "fov_degrees": view.camera.fov,
		"paused": shot == "planet", "time_scale": 1.0, "dropped_seconds": state.dropped_seconds}


func capture_frame() -> bool:
	await process_frame
	await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if not require_ok(image != null and image.get_size() == pixels, "Capture dimensions differ from requested viewport"):
		return false
	if not require_ok(image.save_png(output.path_join("frame-%06d.png" % output_frame)) == OK, "Cannot write reel frame"):
		return false
	output_frame += 1
	return true
