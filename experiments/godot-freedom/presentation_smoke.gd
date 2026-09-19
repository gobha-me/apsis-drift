extends SceneTree
## GPU readability/camera/sky checks. Survey relocation is inspection, not flight proof.
var study: Variant
var failures := 0
const DEVICE := 77

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func frames(count: int) -> void:
	for i in count:
		await process_frame

func axis(code: int, value: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = DEVICE
	event.axis = code
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func button(code: int, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = DEVICE
	event.button_index = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func capture(name: String, inspection := false) -> void:
	await frames(10)
	await RenderingServer.frame_post_draw
	var path: String = study.options.get("--review-dir", "")
	if path.is_empty():
		return
	check(root.get_texture().get_image().save_png(path + "/" + name + ".png") == OK, "PNG capture failed")
	var meta := FileAccess.open(path + "/" + name + ".json", FileAccess.WRITE)
	meta.store_string(JSON.stringify({"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Godot native capture of original code-authored assets", "script": "presentation_smoke.gd", "inspection_relocation": inspection, "flight_state": study.live_bridge.get_state(), "note": "Pose inspections are not proof of continuous flight; see flight_journey_test.cpp"}, "\t"))

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	await frames(20)
	var zoom_before: float = study.chase_distance
	var angles_before: Vector2 = study.chase_angles
	study.set_chase_distance(NAN)
	study.orbit_camera(Vector2(INF, NAN))
	check(study.chase_distance == zoom_before and study.chase_angles == angles_before, "Invalid camera input mutated pose")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study.set_player_paused(false)
	study.player_input.device = DEVICE
	study.player_input.install()
	var deadline := Time.get_ticks_msec() + 30000
	while not study.planet_stream.is_ready:
		if study.live_paused:
			study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
			study.set_player_paused(false)
		if Time.get_ticks_msec() > deadline:
			check(false, "Stream setup timeout")
			quit(1)
			return
		await frames(1)
	await frames(20)
	check(study.flight_displays.screens.size() == 3, "Three live panels missing")
	check(not study.debug_backdrop.visible and not study.label.visible, "Cockpit debug not hidden by default")
	for panel in study.flight_displays.screens:
		check(panel.state.tick > 0 and panel.state.has("periapsis"), "Panel lacks authoritative telemetry")
	await capture("cockpit-live")
	study.head_angles = Vector2(-0.38, 0.48)
	await capture("cockpit-nav")
	study.head_angles = Vector2(-0.38, -0.48)
	await capture("cockpit-systems")
	if study.live_bridge.get_state().flight_model == "thrust-lab-2":
		study.player_input.assist = false
		await frames(20)
		check(not study.live_bridge.get_state().attitude_stabilized, "Coasting instruments retain stabilized state")
		await capture("cockpit-systems-coast")
		study.player_input.assist = true
		await frames(20)
	study.head_angles = Vector2.ZERO
	study.toggle_pilot()
	await frames(3)
	var initial_camera: Vector3 = study.camera.position
	button(JOY_BUTTON_LEFT_STICK, true)
	axis(JOY_AXIS_RIGHT_X, 0.8)
	axis(JOY_AXIS_RIGHT_Y, -0.45)
	await frames(30)
	check(study.camera.position.distance_to(initial_camera) > 1, "External look did not orbit camera")
	check((int(study.live_bridge.get_state().controls) & 204) == 0, "Camera orbit also commanded yaw/heave")
	await capture("chase-orbit")
	button(JOY_BUTTON_LEFT_STICK, false)
	axis(JOY_AXIS_RIGHT_X, 0)
	axis(JOY_AXIS_RIGHT_Y, 0)
	await frames(90)
	check(study.chase_angles.length() < 0.04, "Camera did not return to chase")
	study.pause_menu.camera_distance_changed.emit(65)
	await frames(3)
	check(absf(study.camera.position.distance_to(study.ship.position) - 65 * sqrt(1.04)) < 0.1, "Chase zoom ignored")
	study.pause_menu.debug_changed.emit(true)
	await frames(3)
	check(study.debug_backdrop.visible and "TICK" in study.label.text, "Diagnostic toggle failed")
	study.pause_menu.debug_changed.emit(false)
	# Explicit high-altitude inspection; continuous physics proven separately.
	study.live_paused = true
	check(study.live_bridge.set_survey_pose(0.25, 0.4, 650000), "Space inspection pose rejected")
	study.chase_distance = 36
	study.chase_angles = Vector2(0.08, -0.8)
	button(JOY_BUTTON_LEFT_STICK, true)
	await frames(90)
	await capture("space-stars", true)
	check(study.scene_environment.background_mode == Environment.BG_SKY, "Space switched to empty background")
	check(study.live_bridge.get_sky_catalog().size() == 1536, "Star catalog missing")
	check(study.world_view.camera.far / study.world_view.camera.near <= 100001 and study.world_view.near_camera.far == 300, "Depth ranges not separated")
	button(JOY_BUTTON_LEFT_STICK, false)
	study.set_player_paused(true)
	study.pause_menu.practice_requested.emit(false)
	check(study.live_paused and study.live_bridge.get_state().clear_orbit and not study.player_input.assist, "Practice UI did not load safe coasting start")
	study.set_player_paused(false)
	await frames(40)
	check(study.live_bridge.get_state().clear_orbit, "Practice coast immediately lost orbit")
	await capture("orbit-practice", true)
	study.set_player_paused(true)
	study.pause_menu.practice_requested.emit(true)
	study.set_player_paused(false)
	var altitude_before: float = study.live_bridge.get_state().altitude
	await frames(120)
	check(study.live_bridge.get_state().altitude < altitude_before - 100 and study.live_bridge.get_state().dynamic_pressure > 0, "Re-entry practice did not descend into air")
	await capture("reentry-practice", true)
	print("Presentation frame rate (final sample): %.1f FPS" % Performance.get_monitor(Performance.TIME_FPS))
	print("Presentation smoke: %d failures; live panels, clean/debug, orbit camera, zoom, stars" % failures)
	quit(0 if failures == 0 else 1)
