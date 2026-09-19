extends SceneTree
## Paused GPU inspection; survey/practice relocation is not flight proof.
var study: Variant
var failures := 0

func _initialize() -> void:
	call_deferred("run")

func frames(count: int) -> void:
	for i in count:
		await process_frame

func capture(name: String) -> void:
	await frames(12)
	await RenderingServer.frame_post_draw
	var path: String = study.options.get("--review-dir", "")
	if path.is_empty() or root.get_texture().get_image().save_png(path + "/" + name + ".png") != OK:
		failures += 1
		push_error("Guidance capture failed")
		return
	var file := FileAccess.open(path + "/" + name + ".json", FileAccess.WRITE)
	file.store_string(JSON.stringify({"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Original project assets rendered natively in Godot", "inspection_relocation": true, "guidance": study.flight_guidance, "orbit_status": study.current_orbit_status, "flight_state": study.live_bridge.get_state(), "note": "Paused inspection, not achieved orbit or controller qualification"}, "\t"))

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	await frames(20)
	# The hidden review window intentionally never owns OS keyboard focus.
	# Suppress only its own focus pause callbacks while manually paused.
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study.set_player_paused(false)
	study.live_paused = true
	study.player_input.enabled = false
	var deadline := Time.get_ticks_msec() + 45000
	while not study.planet_stream.is_ready:
		if Time.get_ticks_msec() > deadline:
			push_error("Guidance review terrain timeout")
			quit(1)
			return
		study.pause_menu.hide_menu()
		await frames(1)
	study.live_bridge.start_practice(false)
	study.select_flight_plan(1)
	study.head_angles = Vector2(-0.38, 0.48)
	study.pause_menu.hide_menu()
	await capture("guidance-orbit-cockpit")
	study.toggle_pilot()
	await capture("guidance-orbit-chase")
	study.select_flight_plan(2)
	await capture("guidance-return-chase")
	study.flight_plan_menu.open(2)
	await capture("guidance-menu")
	print("Guidance visual review complete: %d failures" % failures)
	quit(1 if failures else 0)
