extends SceneTree
## Paused same-seed before/after startup fixture; not a landing/collision proof.
var study: Variant
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func settle() -> bool:
	var deadline := Time.get_ticks_msec() + 60000
	var stable := 0
	while stable < 90:
		await process_frame
		if study.planet_stream.is_ready and study.planet_stream.pending.is_empty() and not study.live_bridge.is_stream_busy():
			stable += 1
		else:
			stable = 0
		if Time.get_ticks_msec() > deadline:
			check(false, "Surface review terrain settling timed out")
			return false
	return true

func capture(name: String) -> void:
	await RenderingServer.frame_post_draw
	var directory: String = study.options.get("--review-dir", "")
	check(root.get_texture().get_image().save_png(directory + "/" + name + ".png") == OK, "Surface PNG failed")
	var metadata := FileAccess.open(directory + "/" + name + ".json", FileAccess.WRITE)
	metadata.store_string(JSON.stringify({
		"license": "BSD-3-Clause; Apsis Drift contributors",
		"source": "Godot native capture of original code-authored assets",
		"script": "surface_start_review.gd",
		"flight_state": study.live_bridge.get_state(),
		"note": "Paused startup comparison, same seed/reference location. Discrete neighborhood survey is not proof of continuous collision safety."
	}, "\t"))

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	await process_frame
	var directory: String = study.options.get("--review-dir", "")
	if directory.is_empty() or study.live_bridge == null or study.planet_stream == null:
		check(false, "Supply native thrust streaming arguments and --review-dir")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(directory)
	study.live_paused = true
	study.player_input.set_enabled(false)
	study.pause_menu.hide_menu()
	check(study.live_bridge.initialize(study.snapshot_text) and study.live_bridge.enable_thrust_flight(), "Original unsurveyed fixture failed")
	study.start_stream()
	if study.pilot_view:
		study.toggle_pilot()
	if not await settle():
		quit(1)
		return
	var before: Dictionary = study.live_bridge.get_state()
	await capture("surface-before")
	study.reset_live_flight()
	study.live_paused = true
	study.pause_menu.hide_menu()
	if study.pilot_view:
		study.toggle_pilot()
	if not await settle():
		quit(1)
		return
	var after: Dictionary = study.live_bridge.get_state()
	check(after.tick == 0 and after.speed == 0, "Surface review accidentally advanced flight")
	check(after.latitude == before.latitude and after.longitude == before.longitude, "Review changed reference area")
	check(after.altitude > before.altitude and after.clearance >= 300, "Survey did not add neighborhood clearance")
	await capture("surface-after")
	study.toggle_pilot()
	await process_frame
	await capture("surface-cockpit")
	print("Surface review: %d failures; old altitude %.3f, new altitude %.3f, clearance %.3f" % [failures, before.altitude, after.altitude, after.clearance])
	quit(0 if failures == 0 else 1)
