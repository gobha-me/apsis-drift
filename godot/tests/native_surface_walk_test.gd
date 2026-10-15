extends "res://studies/captures/native_voyage_capture.gd"
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const Navigation = preload("res://scripts/native/home_navigation.gd")
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


func check_depth_inputs() -> void:
	for eye in [Vector3(NAN, 0, 0), Vector3(0, INF, 0), Vector3(0, 0, -INF), Vector3(3e38, 3e38, 3e38)]:
		check(FlightView.surface_depth_range(eye, 100000.0) == Vector2.ZERO, "Nonfinite surface camera eye accepted")
	for limit in [NAN, INF, -INF, -1.0, 0.0, 199.0, 1000000001.0]:
		check(FlightView.surface_depth_range(Vector3.ZERO, limit) == Vector2.ZERO, "Invalid surface depth envelope accepted")
	check(FlightView.surface_depth_range(Vector3.ZERO, 100000.0) == Vector2(0.05, 200.0), "Near surface depth changed ordinary close framing")
	for distance in [250.0, 1000.0, 100000.0, 1000000.0]:
		var depth: Vector2 = FlightView.surface_depth_range(Vector3(0, 0, distance), 100000.0)
		check(depth.x >= 0.05 and depth.y <= 100000.0 and depth.y / depth.x <= 500001.0 and depth.y >= minf(distance + 16.0, 100000.0), "Surface depth escaped its distance/ratio/distant envelope")


func saved_anchor(path: String) -> Dictionary:
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	# Existing format30->29/28/27/25->23 wrappers own this unchanged anchor.
	for level in 12:
		if not value is Dictionary: return {}
		if value.get("surface") is Dictionary and value.surface.get("landed") is Dictionary:
			return value.surface.landed.duplicate(true)
		value = value.get("voyage", value.get("base"))
	return {}


func check_return_bearing(view: Control) -> void:
	var access: Dictionary = view.state.surface_walk.return_access
	check(access.available and view.ship_marker.visible and view.ship_marker.label_text == "RETURN" and not view.home_marker.visible, "Ground return access marker unavailable")
	var expected := Navigation.project(view.camera, access.position, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.ship_marker.cue == expected, "RETURN bearing diverged from actual projected entry")
	check("RETURN" in view.primary_status.text and "%.1f m" % access.distance_metres in view.primary_status.text, "Compact ground HUD omitted access range")


func check_return_fallback(view: Control) -> void:
	var original: Dictionary = view.state.duplicate(true)
	# Simulate only an unavailable presentation observation, never mutate C++.
	view.state.surface_walk.return_access = {"available": false}
	view.update_view(0.0)
	var expected := Navigation.project(view.camera, Vector3.ZERO, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.error.is_empty() and view.ship_marker.visible and view.ship_marker.label_text == "SHIP" and view.ship_marker.cue == expected, "Unavailable return access lost the explicit craft-centre fallback")
	check("Return access unavailable" in view.primary_status.text and not view.home_marker.visible, "Unavailable return access showed a guessed distance or changed HOME")
	view.state = original
	view.update_view(0.0)
	check_return_bearing(view)

func check_suit_lamp(view: Control, owner: Variant, output: String) -> void:
	var state: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join("lamp-before.json")
	check(owner.save_freedom_as(path), "Suit lamp save oracle refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	var sun := [view.light.light_energy, view.local_light.light_energy, view.planet_environment.ambient_light_energy]
	check(view.paused and view.suit_lamp.visible and view.suit_lamp_enabled and view.suit_lamp_button.visible and view.suit_lamp_button.focus_mode == Control.FOCUS_ALL, "Outside lamp lost default light or accessible paused control")
	check(view.suit_lamp.get_parent() == view.camera and view.suit_lamp.get_world_3d() == view.terrain_camera.get_world_3d() and view.suit_lamp.light_cull_mask == 3 and view.suit_lamp.spot_range <= 25.0 and view.suit_lamp.spot_range >= 18.0 and view.suit_lamp.shadow_enabled, "Suit lamp lost bounded shared-world illumination/occlusion")
	# Real GUI activation changes only the presentation; it cannot Resume.
	# Playback disables the shell's processing. Enable only this GUI control,
	# keeping the view and authoritative simulation disabled during frame waits.
	var button_mode: int = view.suit_lamp_button.process_mode
	view.suit_lamp_button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	view.suit_lamp_button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(view.suit_lamp_button)
	check(view.suit_lamp_button.has_focus() and view.suit_lamp_button.can_process(), "Lamp GUI activation lacks focus or processing")
	key(KEY_ENTER, true)
	await process_frame
	key(KEY_ENTER, false)
	view.suit_lamp_button.process_mode = button_mode
	check(not view.suit_lamp.visible and not view.suit_lamp_enabled and view.suit_lamp_button.text == "Suit lamp: Off" and view.paused, "Actual paused lamp activation failed or resumed walking")
	view.focused = false
	view.suit_lamp_button.pressed.emit()
	view.focused = true
	check(not view.suit_lamp_enabled, "Unfocused lamp callback changed its choice")
	view.suit_lamp_button.pressed.emit()
	check(view.suit_lamp.visible and view.suit_lamp_button.text == "Suit lamp: On" and view.paused, "Lamp could not turn on while retaining pause")
	var offset: Transform3D = view.suit_lamp.transform
	var look: Vector2 = view.look_offset
	view.look_offset += Vector2(0.2, -0.1)
	view.update_view(0.0)
	check(view.suit_lamp.global_transform.is_equal_approx(view.camera.global_transform * offset) and view.suit_lamp.transform == offset, "Lamp did not follow the real ground eye/look")
	view.look_offset = look
	view.update_view(0.0)
	view.suit_lamp_button.pressed.emit() # Leave off to check fresh-view reset below.
	check(view.paused and owner.get_freedom_flight_state() == state and owner.save_freedom_as(output.path_join("lamp-after.json")) and FileAccess.get_file_as_bytes(output.path_join("lamp-after.json")) == bytes, "Lamp inspection/toggle changed complete voyage or simulation clock")
	check(sun == [view.light.light_energy, view.local_light.light_energy, view.planet_environment.ambient_light_energy], "Lamp altered actual sunlight or ambient policy")
	view.suit_lamp_button.release_focus()


func check_paused_return_feedback(view: Control, owner: Variant, output: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join("paused-return-before.json")
	check(owner.save_freedom_as(path), "Paused return feedback Save oracle refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	var menu: CanvasLayer = view.controls_menu
	var button: Button = view.surface_walk_menu_button
	check(view.paused and view.focused and view.surface_walking() and menu.panel.visible and button.visible and not button.disabled, "Distant paused return action is unavailable")
	# Enable only the actual button; the replay shell/world remain disabled.
	var button_mode: int = button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	button.grab_focus()
	menu.left_scroll.ensure_control_visible(button)
	check(button.has_focus() and button.can_process(), "Paused return GUI lacks focus or processing")
	key(KEY_ENTER, true)
	await process_frame
	key(KEY_ENTER, false)
	var refusal := str(owner.get_last_error())
	check(not refusal.is_empty(), "Actual distant return did not produce a provider refusal")
	for frame in 3:
		menu._process(0.0)
		await process_frame
	check(menu.panel.visible and menu.message.is_visible_in_tree() and refusal in menu.message.text and menu.controls.status == refusal and view.save_status.text == refusal, "Paused return refusal disappeared behind the menu or on its next frame")
	check(root.get_visible_rect().encloses(menu.message.get_global_rect()), "Paused return refusal is outside the visible viewport")
	check(view.paused and view.focused and button.has_focus() and owner.get_freedom_flight_state() == before and owner.save_freedom_as(output.path_join("paused-return-after.json")) and FileAccess.get_file_as_bytes(output.path_join("paused-return-after.json")) == bytes, "Paused refusal changed focus, clock, actor, craft or complete save")
	button.process_mode = button_mode
	button.release_focus()


func check_visibility_refresh(view: Control, owner: Variant, output: String, label: String, anchor: Dictionary) -> void:
	var path := output.path_join(label + ".json")
	var after := output.path_join(label + "-refresh.json")
	var state: Dictionary = owner.get_freedom_flight_state()
	var model: int = view.staged_model.get_instance_id()
	check(owner.save_freedom_as(path), "Visibility baseline Save failed")
	var bytes := FileAccess.get_file_as_bytes(path)
	for frame in 3: view.update_view(0.0)
	check(view.error.is_empty() and view.paused and owner.get_freedom_flight_state() == state and view.staged_model.get_instance_id() == model and view.staged_model.valid_current_pose(), "Camera refresh changed the paused actor/craft/model")
	check(owner.save_freedom_as(after) and FileAccess.get_file_as_bytes(after) == bytes and saved_anchor(path) == anchor, "Visibility refresh changed complete save or landed anchor")
	check(view.camera.position.is_equal_approx(state.surface_walk.eye_position) and view.camera.transform == view.terrain_camera.transform and view.camera.fov == view.terrain_camera.fov, "Surface cameras lost authoritative eye/basis/FOV")
	check_return_bearing(view)
	check(view.camera.far >= view.camera.position.length() + 16.0 and view.camera.far <= view.terrain_camera.far and view.camera.far / view.camera.near <= 500001.0, "Landed craft remains beyond the close-pass far plane")
	check(view.camera.cull_mask == 3 and view.terrain_camera.cull_mask == 1 and view.camera.get_world_3d() == view.terrain_camera.get_world_3d() and view.light.light_cull_mask == 1 and view.local_light.light_cull_mask == 2, "Visibility repair lost shared terrain occlusion or light/layer ownership")
	check(-view.camera.basis.z.dot((-view.camera.position).normalized()) > 0.9, "Actual backward walking no longer looks toward the retained craft")


func check_far_visibility(source: String, assets: String, output: String, rendered: bool, captures: Array) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Far visibility source refused: " + shell.error)
	if failed: shell.free(); return
	var view: Control = shell.current_view
	var model: int = view.staged_model.get_instance_id()
	var original_near: float = view.camera.near
	var original_far: float = view.camera.far
	var anchor := saved_anchor(source.path_join("exited.json"))
	check(not anchor.is_empty() and view.paused and view.surface_walking(), "Far visibility lost the actual landed surface actor")
	# Real supported commands, not a shifted camera or fabricated actor save.
	walk(view, KEY_S, 15000)
	check(view.error.is_empty() and view.camera.position.length() > 250.0 and view.staged_model.get_instance_id() == model, "Actual supported walk failed to reach beyond250m; do not relocate/flatten the fixture")
	if failed: shell.free(); return
	view.pause_controls("Inspecting the actual distant landed craft")
	check_visibility_refresh(view, owner, output, "visibility-far", anchor)
	if failed: shell.free(); return
	if rendered:
		var prior_cockpit: bool = view.cockpit
		await capture_phase(view, owner, output, {"label": "visibility-far"}, captures)
		view.cockpit = prior_cockpit
		view.update_view(0.0)
	var far_path := output.path_join("visibility-far.json")
	var far_bytes := FileAccess.get_file_as_bytes(far_path)
	var far_state: Dictionary = owner.get_freedom_flight_state()
	check(shell.select_start(owner, {"mode": "continue", "value": far_path, "assets": assets}), "Distant actor paused Continue refused: " + shell.error)
	if failed: shell.free(); return
	view = shell.current_view
	model = view.staged_model.get_instance_id()
	check(view.paused and view.surface_walking() and owner.get_freedom_flight_state() == far_state and FileAccess.get_file_as_bytes(far_path) == far_bytes, "Far Continue changed the actor, craft or source bytes")
	check_visibility_refresh(view, owner, output, "visibility-continued", anchor)
	walk(view, KEY_W, 6000)
	check(view.camera.position.length() < 180.0 and view.staged_model.get_instance_id() == model, "Actual return did not cross the old200m clip boundary")
	view.pause_controls("Inspecting the returned near view")
	check_visibility_refresh(view, owner, output, "visibility-near", anchor)
	check(view.camera.near == original_near and view.camera.far == original_far, "Near on-foot return retained unnecessarily distant depth")
	if failed: shell.free(); return
	if rendered:
		var prior_cockpit: bool = view.cockpit
		await capture_phase(view, owner, output, {"label": "visibility-near"}, captures)
		view.cockpit = prior_cockpit
		view.update_view(0.0)
	walk(view, KEY_W, 9000)
	view.surface_walk_button.pressed.emit()
	check(view.error.is_empty() and view.paused and not view.surface_walking() and view.staged_model.get_instance_id() == model and view.camera.near == original_near and view.camera.far == original_far, "Far walk/reboard lost the retained craft or exact seated depth")
	shell.free()


func check_ground_pause_heading(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	for trigger in ["pause", "focus", "menu", "save"]:
		if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Heading source Continue refused"): return
		var view: Control = shell.current_view
		if not check(view.surface_walking() and resume(view), "Heading source did not resume on foot"): return
		if trigger == "pause":
			var original := source.path_join("exited.json")
			var original_bytes := FileAccess.get_file_as_bytes(original)
			var keyboard_reference: Variant = ClassDB.instantiate("FreedomBridge")
			if not check(keyboard_reference.stage_freedom_continue(original), "Keyboard ground reference Continue refused"): return
			var keyboard_pending: Dictionary = keyboard_reference.get_pending_freedom_start()
			if not check(keyboard_reference.commit_pending_freedom_start(keyboard_pending.candidate_id), "Keyboard ground reference commit refused"): return
			# The ground view retains look in Godot's single-precision Vector2;
			# construct the expected rightward command with that same public type.
			var expected_look := Vector2(-float(owner.get_freedom_flight_state().surface_walk.heading), 0)
			expected_look += Vector2(float(view.player_input.settings.look_speed), 0) * (1.0 / 120.0)
			var keyboard_heading := wrapf(-float(expected_look.x), -PI, PI)
			if not check(keyboard_reference.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, keyboard_heading]), false) and keyboard_reference.save_freedom_as(output.path_join("keyboard-ground-reference.json")), "Keyboard ground reference tick refused"): return
			key(KEY_RIGHT, true)
			view._process(1.0 / 120.0)
			key(KEY_RIGHT, false)
			if not check(view.error.is_empty() and owner.save_freedom_as(output.path_join("keyboard-ground-actual.json")) and FileAccess.get_file_as_bytes(output.path_join("keyboard-ground-actual.json")) == FileAccess.get_file_as_bytes(output.path_join("keyboard-ground-reference.json")), "Actual ground keyboard look diverged from the independent C++ walking tick"): return
			if not check(FileAccess.get_file_as_bytes(original) == original_bytes, "Keyboard ground look changed its source Save"): return
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTON_RIGHT
		button.pressed = true
		button.button_mask = MOUSE_BUTTON_MASK_RIGHT
		Input.parse_input_event(button)
		Input.flush_buffered_events()
		var motion := InputEventMouseMotion.new()
		motion.relative = Vector2(150, 0)
		motion.button_mask = MOUSE_BUTTON_MASK_RIGHT
		# The deterministic shell disables automatic event delivery/processing;
		# feed the real mouse event to the public handler, then a normal frame.
		view._input(motion)
		view._process(1.0 / 120.0)
		var release: InputEventMouseButton = button.duplicate()
		release.pressed = false
		release.button_mask = 0
		Input.parse_input_event(release)
		Input.flush_buffered_events()
		var turned: Dictionary = owner.get_freedom_flight_state()
		if not check(view.error.is_empty() and absf(turned.surface_walk.heading) > 0.1, "Actual mouse turn did not earn a nonzero heading"): return
		var prefix := output.path_join("heading-" + trigger)
		if not check(owner.save_freedom_as(prefix + "-turned.json"), "Earned heading Save refused"): return
		var before_bytes := FileAccess.get_file_as_bytes(prefix + "-turned.json")
		var reference: Variant = ClassDB.instantiate("FreedomBridge")
		if not check(reference.stage_freedom_continue(prefix + "-turned.json"), "Neutral reference Continue refused: " + str(reference.get_last_error())): return
		var pending: Dictionary = reference.get_pending_freedom_start()
		if not check(not pending.is_empty() and reference.commit_pending_freedom_start(pending.candidate_id), "Neutral reference commit refused: " + str(reference.get_last_error())): return
		if not check(reference.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, turned.surface_walk.heading]), false) and reference.save_freedom_as(prefix + "-reference.json"), "Neutral same-heading reference refused: " + str(reference.get_last_error())): return
		if trigger == "pause": view.toggle_pause()
		elif trigger == "focus":
			view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
			view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
		elif trigger == "menu": view.instruments_button.pressed.emit()
		else:
			view.controls_menu.export_save_button.pressed.emit()
			check(view.save_dialog.visible, "Surface Save As did not open")
			view.save_dialog.hide()
			view.save_dialog.file_selected.emit(prefix + "-selected.json")
			check(FileAccess.get_file_as_bytes(prefix + "-selected.json") == before_bytes, "Actual Save As changed paused heading/actor/craft")
		view._process(0.125)
		check(view.paused and view.error.is_empty() and owner.get_freedom_flight_state() == turned and owner.save_freedom_as(prefix + "-paused.json") and FileAccess.get_file_as_bytes(prefix + "-paused.json") == before_bytes, "Pause/focus/menu/save changed the committed voyage: " + trigger)
		if not resume(view): return
		view._process(1.0 / 120.0)
		var after: Dictionary = owner.get_freedom_flight_state()
		check(after.surface_walk.heading == turned.surface_walk.heading and after.tick.to_int() == turned.tick.to_int() + 1, "First neutral resumed tick reset the earned heading or clock: " + trigger)
		if not check(owner.save_freedom_as(prefix + "-resumed.json"), "Resumed heading Save refused"): return
		check(after == reference.get_freedom_flight_state() and FileAccess.get_file_as_bytes(prefix + "-resumed.json") == FileAccess.get_file_as_bytes(prefix + "-reference.json"), "Neutral resume changed actor/body/resources beyond its genuine reference tick: " + trigger)
		var saved_before: Dictionary = JSON.parse_string(before_bytes.get_string_from_utf8())
		var saved_after: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(prefix + "-resumed.json"))
		check(saved_after.actor.foot_position_metres == saved_before.actor.foot_position_metres and saved_anchor(prefix + "-resumed.json") == saved_anchor(prefix + "-turned.json"), "Stationary resume moved foot or landed anchor: " + trigger)
		check(view.camera.position.is_equal_approx(after.surface_walk.eye_position) and view.camera.transform == view.terrain_camera.transform, "Resumed ground eye/registered cameras disagreed: " + trigger)
	# Reinstall the original genuine seated fixture before the old canonical
	# input route; none of these four short transactions enters its goldens.
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("source.json"), "assets": assets}), "Heading check did not reset the canonical route")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	check_depth_inputs()
	if failed: quit(1); return
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
	check_ground_pause_heading(shell, owner, args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	var view: Control = shell.current_view
	var model_id: int = view.staged_model.get_instance_id()
	var audio_id: int = shell.audio_session.get_instance_id()
	var captures: Array = []
	var rendered := DisplayServer.get_name() != "headless"
	check(view.paused and not view.surface_walking() and view.state.surface.landed, "Continue lost landed seated readiness")
	check(not view.ship_marker.visible and view.home_marker.label_text == "HOME", "Seated landing showed a ground ship bearing or changed HOME")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Seated view exposed the on-foot lamp")
	check(not view.hud_scroll.visible and "Landed" in view.primary_status.text and view.context_action.disabled, "Landed compact HUD lost actual mode or allowed a paused maneuver")
	view.surface_walk_button.pressed.emit()
	check(view.paused and view.surface_walking() and view.liftoff_button.disabled and view.assist_button.disabled, "Actual exit button did not transfer input and pause")
	check_return_bearing(view)
	check(view.state.surface_walk.return_access.within_reach and view.state.surface_walk.return_access.distance_metres == 0.0, "Exit failed to identify its exact reachable ground access")
	check_return_fallback(view)
	var malformed: Dictionary = view.state.duplicate(true)
	malformed.surface_walk.return_access.position = Vector3(NAN, 0, 0)
	check(not FlightView.valid_state(malformed), "Nonfinite return point accepted")
	malformed = view.state.duplicate(true)
	malformed.surface_walk.return_access.distance_metres = -1.0
	check(not FlightView.valid_state(malformed), "Negative return range accepted")
	check(view.light.light_cull_mask == 1 and view.local_light.light_cull_mask == 2 and view.light.light_energy == 1.0 and is_equal_approx(view.local_light.light_energy, NativeEnvironment.sun_visibility(view.state)), "Suited view lost separate terrain/local sunlight")
	check(view.surface_walk_menu_button.focus_mode == Control.FOCUS_ALL and not view.surface_walk_menu_button.disabled and view.controls_menu.saved_assist_button.disabled, "Surface menu lost controller focus or assistance guard")
	await check_suit_lamp(view, owner, args[2])
	var hold_before: Dictionary = owner.get_freedom_flight_state()
	check(view.controls_menu.saved_hold_button.disabled and not hold_before.orbit_hold.available and not owner.set_freedom_hold(true) and owner.get_freedom_flight_state() == hold_before, "Outside pilot gained ship orbit hold")
	check(not view.orbital_forecast.visible and not view.jump_button.visible and not view.assist_button.visible, "Outside HUD retained unrelated flight actions")
	check("On foot" in view.primary_status.text and "Suit equipped" in view.primary_status.text and not "Reference altitude" in view.primary_status.text, "Suited compact HUD retained ship motion")
	var help_before: Dictionary = owner.get_freedom_flight_state()
	view.controls_menu.show_basics()
	view.controls_menu.basics.page = 9
	view.controls_menu.basics.refresh()
	check(view.controls_menu.basics.visible and "On foot" in view.controls_menu.basics._paused.text and "Return through hatch" in view.controls_menu.basics.body.text and "oxygen countdown" in view.controls_menu.basics.body.text, "Actual suited help lost actor, return or endurance boundary")
	view._process(0.125)
	check(view.paused and owner.get_freedom_flight_state() == help_before, "Suited reference advanced or replaced the C++ actor")
	view.controls_menu.close_basics()
	for button in view.controls_menu.saved_port_buttons.values():
		check(button.disabled, "Outside controls menu retained a flight action")
	checkpoint(owner, args[0], args[2], "exited")
	var title_save := args[2].path_join("title-paused.json")
	check(owner.save_freedom_as(title_save), "Outside pre-Title Save refused")
	var title_hash := FileAccess.get_sha256(title_save)
	view.controls_menu.title_button.pressed.emit()
	view.toggle_pause()
	view._process(0.125)
	check(shell.title_origin == view and view.paused and view.surface_walking() and owner.save_freedom_as(title_save) and FileAccess.get_sha256(title_save) == title_hash, "Outside Title advanced/changed actor or craft")
	shell.title_confirmation.get_cancel_button().pressed.emit()
	check(shell.current_view == view and view.paused and view.surface_walking() and root.gui_get_focus_owner() == view.controls_menu.title_button, "Outside Title Cancel lost paused selection")
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
	check(not view.state.surface_walk.return_access.within_reach and view.state.surface_walk.return_access.distance_metres > 3.5, "Distant RETURN cue incorrectly claims reach")
	check_return_bearing(view)
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
	await check_paused_return_feedback(view, owner, args[2])
	if failed: shell.free(); quit(1); return
	# Continue re-stages the actual actor, craft and terrain in the same root.
	view.controls_menu.load_button.pressed.emit()
	shell.load_dialog.file_selected.emit(args[2].path_join("halfway.json"))
	shell.confirm_load()
	check(shell.load_origin == null and shell.current_view != view and shell.error.is_empty(), "Outside in-game Load did not stage")
	view = shell.current_view
	check(view.paused and view.surface_walking() and shell.audio_session.get_instance_id() == audio_id, "Outside Continue lost paused actor or duplicated audio owner")
	check(view.suit_lamp_enabled and view.suit_lamp.visible and view.suit_lamp_button.text == "Suit lamp: On", "Outside Continue persisted the presentation-only lamp choice or lost default illumination")
	check_return_bearing(view)
	walk(view, KEY_S, 600)
	checkpoint(owner, args[0], args[2], "far")
	check(view.camera.position.is_equal_approx(view.state.surface_walk.eye_position) and view.camera.position.length() > 20.0, "Walking did not move the rendered eye away from the craft")
	check_return_bearing(view)
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
	check(not view.ship_marker.visible, "Ground ship bearing remained visible after reboarding")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Suit lamp remained active after reboarding")
	checkpoint(owner, args[0], args[2], "seated")
	check(shell.select_start(owner, {"mode": "continue", "value": args[2].path_join("seated.json"), "assets": args[1]}), "Returned seated Continue did not stage")
	view = shell.current_view
	if resume(view):
		view.liftoff_button.pressed.emit()
		for batch in 8: view._process(0.125)
	checkpoint(owner, args[0], args[2], "lifted")
	check(not view.state.surface.landed and view.error.is_empty(), "Returned pilot cannot lift off with real thrust")
	check(not view.ship_marker.visible, "Ground ship bearing remained visible in flight")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Suit lamp remained active during flight")
	# Declared destruction is an explicit fixture, not an invented exposure event.
	check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("pending-loss.json"), "assets": args[1]}), "Pending outside loss failed to stage")
	check(shell.recovery_overlay != null and shell.current_view.surface_walking() and shell.current_view.paused, "Pending loss lost outside actor or explicit recovery choice")
	frozen = owner.get_freedom_flight_state()
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([1, 0, 0]), false) and owner.get_freedom_flight_state() == frozen, "Recorded loss allowed outside movement")
	check(shell.select_start(owner, {"mode": "recovery", "assets": args[1]}), "Outside replacement failed to stage")
	check(shell.current_view is WalkView and shell.current_view.paused and shell.recovery_overlay == null, "Recovery failed to restore one paused station actor")
	checkpoint(owner, args[0], args[2], "recovered")
	if not failed: await check_far_visibility(args[0], args[1], args[2], rendered, captures)
	var report := FileAccess.open(args[2].path_join("surface-walk-report.json"), FileAccess.WRITE)
	if report != null:
		report.store_string(JSON.stringify({"pass": not failed, "scope": "near-ground native suited walking, complete save parity; instant hatch transfer; no oxygen/environment damage policy", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if rendered else "headless", "asset_license_records": "native-assets/stowed/licenses", "operating_asset_provenance": "native-assets/operating/provenance.json", "captures": captures}, "\t") + "\n")
		report.close()
	shell.free()
	DirAccess.remove_absolute(wave)
	print("Native surface walk: %d failures" % int(failed))
	quit(1 if failed else 0)
