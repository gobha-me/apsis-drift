extends "res://studies/captures/native_voyage_capture.gd"
## Actual native root, input, C++ terrain and complete Save comparisons.
## The source is explicitly a near-ground test setup, not a flight acceptance.

func key(physical: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = physical
	event.physical_keycode = physical
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func checkpoint(owner: Variant, source: String, output: String, name: String) -> bool:
	var path := output.path_join(name + ".json")
	return check(owner.save_freedom_as(path) and FileAccess.get_sha256(path) == FileAccess.get_sha256(source.path_join(name + ".json")), "Complete surface voyage differs at " + name)


func walk(view: Control, physical: int, ticks: int) -> void:
	if not resume(view): return
	key(physical, true)
	var controls: PackedFloat64Array = view.surface_controls(0.0)
	check(controls[0] == (-1.0 if physical == KEY_S else 1.0), "Physical walking key did not reach native input")
	while ticks > 0 and not failed:
		var count := mini(ticks, 15)
		view._process(float(count) / 120.0)
		check(view.error.is_empty(), "Surface input refused: " + view.error)
		ticks -= count
	key(physical, false)


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var wave := ProjectSettings.globalize_path("user://surface-audio.wav")
	var recording := FileAccess.open(wave, FileAccess.WRITE)
	recording.store_buffer(load("res://tests/recorded_ship_audio_test.gd").fixture(2))
	recording.close()
	shell.configure_audio({"audio_hum": wave, "audio_propulsion": wave, "audio_persist": false})
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK and shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("source.json"), "assets": args[1]}), "Landed source did not stage: " + shell.error): shell.free(); quit(1); return
	var view: Control = shell.current_view
	var model_id: int = view.staged_model.get_instance_id()
	var audio_id: int = shell.audio_session.get_instance_id()
	var captures: Array = []
	var rendered := DisplayServer.get_name() != "headless"
	check(view.paused and not view.surface_walking() and view.state.surface.landed, "Continue lost landed seated readiness")
	check(not view.hud_scroll.visible and "Landed" in view.primary_status.text and view.context_action.disabled, "Landed compact HUD lost actual mode or allowed a paused maneuver")
	view.surface_walk_button.pressed.emit()
	check(view.paused and view.surface_walking() and view.liftoff_button.disabled and view.assist_button.disabled, "Actual exit button did not transfer input and pause")
	check(view.surface_walk_menu_button.focus_mode == Control.FOCUS_ALL and not view.surface_walk_menu_button.disabled and view.controls_menu.saved_assist_button.disabled, "Surface menu lost controller focus or assistance guard")
	check(not view.orbital_forecast.visible and not view.jump_button.visible and not view.assist_button.visible, "Outside HUD retained unrelated flight actions")
	check("On foot" in view.primary_status.text and "Suit equipped" in view.primary_status.text and not "Reference altitude" in view.primary_status.text, "Suited compact HUD retained ship motion")
	for button in view.controls_menu.saved_port_buttons.values():
		check(button.disabled, "Outside controls menu retained a flight action")
	checkpoint(owner, args[0], args[2], "exited")
	shell.audio_session.refresh()
	check(shell.audio_session.audio.diagnostics().targets == Vector3.ZERO, "Outside actor retained cockpit or propulsion playback")
	var frozen: Dictionary = owner.get_freedom_flight_state()
	for size in [0, 2, 4]:
		var controls := PackedFloat64Array()
		controls.resize(size)
		check(not owner.advance_freedom_surface_walk(0.125, controls, false), "Wrong walking buffer dimensions accepted")
	for value in [NAN, INF, -INF]:
		check(not owner.advance_freedom_surface_walk(value, PackedFloat64Array([0, 0, 0]), false), "Nonfinite elapsed time accepted")
		for axis in 3:
			var controls := PackedFloat64Array([0, 0, 0])
			controls[axis] = value
			check(not owner.advance_freedom_surface_walk(0.125, controls, false), "Nonfinite walking axis accepted")
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([2, 0, 0]), false) and not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([0, 0, 4]), false), "Out-of-range walking command accepted")
	check(owner.get_freedom_flight_state() == frozen, "Invalid input mutated the live voyage")
	view._process(0.125)
	check(owner.get_freedom_flight_state().tick == frozen.tick, "Paused walking advanced shared time")
	check(view.camera.position.is_equal_approx(view.state.surface_walk.eye_position) and view.terrain_camera.transform == view.camera.transform, "Camera lost C++ ground eye or terrain frame")
	key(KEY_S, true)
	view.toggle_pause()
	check(view.paused, "Held walking key bypassed neutral resume")
	key(KEY_S, false)
	if failed: shell.free(); quit(1); return
	if rendered: await capture_phase(view, owner, args[2], {"label": "exited"}, captures)
	walk(view, KEY_S, 600)
	checkpoint(owner, args[0], args[2], "halfway")
	if failed: shell.free(); quit(1); return
	check(view.context_source == view.surface_walk_button and not view.context_action.disabled, "Actual ground return action is inaccessible")
	view.context_action.pressed.emit()
	check(view.surface_walking() and not view.paused and view.error.is_empty(), "Distant return changed input ownership")
	check(not view.save_status.text.is_empty() and view.save_status.text.left(180) in view.primary_status.text, "Actual return refusal disappeared behind the hidden detail wall")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	var paused_tick: String = view.state.tick
	view._process(0.125)
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and view.state.tick == paused_tick, "Focus loss advanced ground time or resumed silently")
	# Continue re-stages the actual actor, craft and terrain in the same root.
	check(shell.select_start(owner, {"mode": "continue", "value": args[2].path_join("halfway.json"), "assets": args[1]}), "Outside Continue did not stage: " + shell.error)
	view = shell.current_view
	check(view.paused and view.surface_walking() and shell.audio_session.get_instance_id() == audio_id, "Outside Continue lost paused actor or duplicated audio owner")
	walk(view, KEY_S, 600)
	checkpoint(owner, args[0], args[2], "far")
	check(view.camera.position.is_equal_approx(view.state.surface_walk.eye_position) and view.camera.position.length() > 20.0, "Walking did not move the rendered eye away from the craft")
	if rendered: await capture_phase(view, owner, args[2], {"label": "far"}, captures)
	var bad_path := args[2].path_join("corrupt.json")
	var bad: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[2].path_join("far.json")))
	bad.actor.planet = 0
	var file := FileAccess.open(bad_path, FileAccess.WRITE)
	file.store_string(JSON.stringify(bad))
	file.close()
	frozen = owner.get_freedom_flight_state()
	check(not shell.select_start(owner, {"mode": "continue", "value": bad_path, "assets": args[1]}) and owner.get_freedom_flight_state() == frozen and shell.current_view == view, "Corrupt outside Continue replaced the live owner")
	walk(view, KEY_W, 1200)
	checkpoint(owner, args[0], args[2], "returned")
	if rendered: await capture_phase(view, owner, args[2], {"label": "returned"}, captures)
	var retained_model: int = view.staged_model.get_instance_id()
	view.context_action.pressed.emit()
	check(view.paused and not view.surface_walking() and view.staged_model.get_instance_id() == retained_model and retained_model != model_id, "Nearby return failed or replaced the continued ship")
	checkpoint(owner, args[0], args[2], "seated")
	check(shell.select_start(owner, {"mode": "continue", "value": args[2].path_join("seated.json"), "assets": args[1]}), "Returned seated Continue did not stage")
	view = shell.current_view
	if resume(view):
		view.liftoff_button.pressed.emit()
		for batch in 8: view._process(0.125)
	checkpoint(owner, args[0], args[2], "lifted")
	check(not view.state.surface.landed and view.error.is_empty(), "Returned pilot cannot lift off with real thrust")
	# Declared destruction is an explicit fixture, not an invented exposure event.
	check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("pending-loss.json"), "assets": args[1]}), "Pending outside loss failed to stage")
	check(shell.recovery_overlay != null and shell.current_view.surface_walking() and shell.current_view.paused, "Pending loss lost outside actor or explicit recovery choice")
	frozen = owner.get_freedom_flight_state()
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([1, 0, 0]), false) and owner.get_freedom_flight_state() == frozen, "Recorded loss allowed outside movement")
	check(shell.select_start(owner, {"mode": "recovery", "assets": args[1]}), "Outside replacement failed to stage")
	check(shell.current_view is WalkView and shell.current_view.paused and shell.recovery_overlay == null, "Recovery failed to restore one paused station actor")
	checkpoint(owner, args[0], args[2], "recovered")
	var report := FileAccess.open(args[2].path_join("surface-walk-report.json"), FileAccess.WRITE)
	if report != null:
		report.store_string(JSON.stringify({"pass": not failed, "scope": "near-ground native suited walking, complete save parity; instant hatch transfer; no oxygen/environment damage policy", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if rendered else "headless", "asset_license_records": "native-assets/stowed/licenses", "operating_asset_provenance": "native-assets/operating/provenance.json", "captures": captures}, "\t") + "\n")
		report.close()
	shell.free()
	DirAccess.remove_absolute(wave)
	print("Native surface walk: %d failures" % int(failed))
	quit(1 if failed else 0)
