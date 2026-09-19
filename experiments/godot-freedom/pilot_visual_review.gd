extends "res://guidance_visual_review.gd"
## Native study inspection, not character animation or collision qualification.

func capture(name: String) -> void:
	var intended: Transform3D = study.camera.global_transform
	var state: Dictionary = study.live_bridge.get_state()
	# Main processing is stopped for inspection; explicitly synchronize BOTH
	# composited render cameras or the PNG would retain the old seated view.
	study.world_view.refresh(study, state.altitude, state.planet_radius)
	await super.capture(name)
	if not study.camera.global_transform.is_equal_approx(intended) or not study.world_view.near_camera.global_transform.is_equal_approx(intended) or not study.world_view.camera.global_transform.is_equal_approx(intended):
		failures += 1
		push_error("Inspection cameras disagreed during capture")
	var path: String = study.options["--review-dir"].path_join(name + ".json")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		failures += 1
		push_error("Pilot capture metadata unavailable")
		return
	var metadata: Dictionary = parsed
	metadata["pilot_asset"] = study.options["--pilot-asset"].get_file()
	metadata["pilot_sha256"] = FileAccess.get_sha256(study.options["--pilot-asset"])
	metadata["occupied_cabin"] = study.options["--pilot-cabin"].get_file()
	metadata["occupied_cabin_sha256"] = FileAccess.get_sha256(study.options["--pilot-cabin"])
	metadata["camera_transform"] = str(intended)
	metadata["first_person"] = study.pilot_view
	metadata["note"] = "Static seated pilot study inspection; not flown orbit, animation, collision or final character acceptance"
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		failures += 1
		push_error("Could not write pilot capture metadata")
		return
	file.store_string(JSON.stringify(metadata, "\t"))
	file.close()

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	await frames(20)
	if not is_instance_valid(study.seated_pilot):
		push_error("Pilot review requires --pilot-asset=/absolute/path/to/study.glb")
		quit(1)
		return
	var directory: String = study.options.get("--review-dir", "")
	if directory.is_empty():
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(directory)
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study.set_player_paused(false)
	study.live_paused = true
	study.player_input.enabled = false
	study.pause_menu.hide_menu()
	var deadline := Time.get_ticks_msec() + 45000
	while not study.planet_stream.is_ready:
		if Time.get_ticks_msec() > deadline:
			push_error("Pilot review terrain timeout")
			quit(1)
			return
		await frames(1)
	study.set_process(false)
	study.pause_menu.hide() # Inspection only: a focus notification must not cover the PNG.
	study.label.hide()
	study.caption.hide()
	study.debug_backdrop.hide()
	var before: Dictionary = study.live_bridge.get_state()
	var cabin: Transform3D = study.pilot_cockpit.global_transform
	if study.pilot_view:
		study.toggle_pilot()
	study.camera.fov = 48
	for side in [-1, 1]:
		study.camera.global_position = cabin * Vector3(side * 2.3, 2.15, -2.4)
		study.camera.look_at(cabin * Vector3(0, 0.95, 0.0), cabin.basis.y)
		await capture("pilot-exterior-" + ("left" if side < 0 else "right"))
	study.toggle_pilot()
	study.camera.fov = 75
	study.head_angles = Vector2.ZERO
	study.update_head_camera()
	await capture("pilot-cockpit-forward")
	study.head_angles = Vector2(-0.85, 0)
	study.update_head_camera()
	await capture("pilot-cockpit-body")
	# Low-light inspection without changing planet/state or pretending to fly.
	study.sun_light.light_energy = 0.03
	study.scene_environment.ambient_light_energy = 0.12
	study.toggle_pilot()
	study.camera.fov = 48
	study.camera.global_position = cabin * Vector3(2.3, 2.15, -2.4)
	study.camera.look_at(cabin * Vector3(0, 0.95, 0.0), cabin.basis.y)
	await capture("pilot-exterior-low-light")
	if study.live_bridge.get_state() != before:
		failures += 1
		push_error("Pilot inspection changed authoritative flight")
	print("Pilot native visual review: %d failures; static fit inspection, not final character acceptance" % failures)
	quit(1 if failures else 0)
