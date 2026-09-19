extends "res://guidance_visual_review.gd"
## Display review uses unsaved relocations, not a flown orbit or re-entry proof.
func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	await frames(20)
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study.set_player_paused(false)
	study.live_paused = true
	study.player_input.enabled = false
	var deadline := Time.get_ticks_msec() + 45000
	while not study.planet_stream.is_ready:
		if Time.get_ticks_msec() > deadline:
			push_error("Orbit review terrain timeout")
			quit(1)
			return
		study.pause_menu.hide_menu()
		await frames(1)
	study.live_bridge.start_practice(false)
	study.live_bridge.advance_thrust(1.0 / 120, PackedFloat64Array([0, 0, 0, 0, 0, 0, 0]), true)
	study.select_flight_plan(1)
	study.head_angles = Vector2(-0.38, 0.48)
	study.pause_menu.hide_menu()
	await capture("orbit-established-assisted-cockpit")
	study.toggle_pilot()
	await capture("orbit-established-assisted-chase")
	study.live_bridge.advance_thrust(1.0 / 120, PackedFloat64Array([0, 0, 0, 0, 0, 0, 0]), false)
	await capture("orbit-established-manual-chase")
	# Preserve observation history solely to inspect the risk message.
	study.live_bridge.start_practice(true)
	await capture("orbit-at-risk-inspection")
	study.reset_orbit_status()
	await capture("not-in-orbit-inspection")
	print("Orbit-assist visual review complete: %d failures" % failures)
	quit(1 if failures else 0)
