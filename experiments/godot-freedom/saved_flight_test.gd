extends SceneTree
## Real selected flight/control/Save As trace against independently written C++.
const FlightView = preload("res://native_flight_view.gd")
const MainExhaust = preload("res://native_main_exhaust.gd")
var failures := 0


func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func refused(owner: Variant, elapsed: float, fractions: PackedFloat64Array, paused: bool) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	check(not owner.advance_freedom_flight(elapsed, fractions, paused), "Malformed saved-flight command accepted")
	check(not str(owner.get_last_error()).is_empty(), "Refusal lost diagnostic")
	var diagnostic: String = owner.get_last_error()
	check(owner.get_freedom_flight_state() == before, "Rejected batch changed flight or time")
	check(owner.get_last_error() == diagnostic, "Pure view discarded command diagnostic")


func check_exhaust_inputs(state: Dictionary) -> void:
	# Validate source ownership and every gross buffer before importing a model.
	check(MainExhaust.valid_applied(state, 0.0), "Actual selected force state refused")
	for key in ["positive_force_body", "negative_force_body", "positive_force_ratings", "negative_force_ratings"]:
		for size in [0, 2, 4, 1000]:
			var malformed := state.duplicate(true)
			var buffer := PackedFloat64Array()
			buffer.resize(size)
			malformed[key] = buffer
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong buffer dimensions: " + key)
		for bad in [NAN, INF, -INF, -1.0]:
			for axis in 3:
				var malformed := state.duplicate(true)
				malformed[key][axis] = bad
				check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted invalid component: " + key)
		for bad_type in [null, [], Vector3.ZERO, PackedFloat32Array([1.0, 1.0, 1.0])]:
			var malformed := state.duplicate(true)
			malformed[key] = bad_type
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong buffer type: " + key)
	for key in ["positive_force_ratings", "negative_force_ratings"]:
		for axis in 3:
			var malformed := state.duplicate(true)
			malformed[key][axis] = 0.0
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted zero force rating")
	for key in ["mode", "frame_id", "attached"]:
		var malformed := state.duplicate(true)
		malformed.erase(key)
		check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted missing owner identity: " + key)
	for bad_elapsed in [NAN, INF, -INF, -0.01, 60.01]:
		check(not MainExhaust.valid_applied(state, bad_elapsed), "Exhaust accepted invalid elapsed time")
	for wrong_owner in [{"mode": "freedom_station"}, {"frame_id": "1"}, {"frame_id": 2}, {"attached": 0}]:
		var malformed := state.duplicate(true)
		malformed.merge(wrong_owner, true)
		check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong physical owner")


func _initialize() -> void:
	call_deferred("run")


func physical_key(key: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = key
	event.keycode = key
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	check(Input.is_physical_key_pressed(key) == pressed, "Real physical key event did not update input")


func joy_axis(axis: int, value: float, device := 0) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = device
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func joy_button(button: int, pressed: bool, device := 0) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = button
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func check_orbit_fields(start: Dictionary) -> void:
	for field in ["orbit_classification", "orbit_bound", "orbit_near_parabolic", "periapsis_radius", "apoapsis_radius"]:
		var malformed := start.duplicate(true)
		malformed.erase(field)
		check(not FlightView.valid_state(malformed), "Missing orbital field accepted: " + field)
	for field in ["periapsis_radius", "apoapsis_radius"]:
		for value in [NAN, INF, -INF, -1.0, 2.1e15, "0", false, [], 1]:
			var malformed := start.duplicate(true)
			malformed[field] = value
			check(not FlightView.valid_state(malformed), "Malformed orbital radius accepted: " + field)
	for field in ["orbit_bound", "orbit_near_parabolic"]:
		for value in [null, 0, 1, "true"]:
			var malformed := start.duplicate(true)
			malformed[field] = value
			check(not FlightView.valid_state(malformed), "Nonboolean orbital flag accepted")
	for value in [null, "unknown", "STABLE", 0, false]:
		var malformed := start.duplicate(true)
		malformed.orbit_classification = value
		check(not FlightView.valid_state(malformed), "Unknown orbital classification accepted")
	var malformed := start.duplicate(true)
	malformed.apoapsis_radius = start.periapsis_radius - 1.0
	check(not FlightView.valid_state(malformed), "Apoapsis below periapsis accepted")
	malformed = start.duplicate(true)
	malformed.orbit_near_parabolic = true
	check(not FlightView.valid_state(malformed), "Bound near-parabolic orbit accepted")
	malformed = start.duplicate(true)
	malformed.orbit_classification = "escape"
	check(not FlightView.valid_state(malformed), "Bound escape classification accepted")


func check_orbit_fixtures(directory: String) -> void:
	var output: Array = []
	var oracle_path := directory.path_join("orbital-readout.json")
	var code := OS.execute(directory.path_join("apsis-drift-freedom-start-fixture"), PackedStringArray([oracle_path, "42", "25", "orbital-readout"]), output, true)
	check(code == 0, "Independent C++ orbital fixtures failed: " + str(output))
	if code != 0:
		return
	var rows: Variant = JSON.parse_string(FileAccess.get_file_as_string(oracle_path))
	check(rows is Array and rows.size() == 7, "Orbital fixture manifest malformed")
	if not rows is Array:
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	for row in rows:
		var path := directory.path_join(row.file)
		var original := FileAccess.get_file_as_bytes(path)
		check(owner.initialize_freedom_continue(path), "Analytic orbital saved state refused: " + row.file)
		var state: Dictionary = owner.get_freedom_flight_state()
		check(FlightView.valid_state(state), "Real orbital bridge state malformed: " + row.file)
		check(state.orbit_classification == row.classification and state.orbit_bound == row.bound and state.orbit_near_parabolic == row.near_parabolic and state.periapsis_radius == row.periapsis_radius and state.apoapsis_radius == row.apoapsis_radius and state.planet_radius == row.planet_radius, "Bridge orbital prediction differs from C++ oracle: " + row.file)
		var text := FlightView.orbit_text(state)
		check("%.1f km" % ((row.periapsis_radius - row.planet_radius) / 1000.0) in text and "Central-body" in text and "Not terrain" in text, "Orbit readout used radius as altitude or granted terrain safety")
		if row.apoapsis_radius == null:
			check("Apo unavailable" in text, "Missing apoapsis fabricated a number")
		if row.file == "orbit-outbound.json":
			check(state.periapsis_radius < state.planet_radius and "Escape" in text and "Intersects" not in text, "Past outbound periapsis became future impact")
		if row.file == "orbit-inbound.json":
			check("Intersects reference surface" in text, "Inbound hyperbola lost impact assessment")
		if row.file == "orbit-bound-no-apo.json":
			check(state.orbit_bound and "Stable orbit" in text, "Missing apoapsis became unbound")
		check(owner.get_freedom_flight_state() == state and FileAccess.get_file_as_bytes(path) == original, "Readout query mutated fixture/flight")
	owner = null


func check_hud_layout(view: Control, owner: Variant, directory: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	for style in [view.hud_scroll.get_theme_stylebox("panel"), view.orbital_forecast.get_theme_stylebox("normal")]:
		check(style is StyleBoxFlat and style.bg_color.a >= 0.9 and maxf(style.bg_color.r, maxf(style.bg_color.g, style.bg_color.b)) <= 0.05, "HUD lost its dark text backing")
		for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
			check(style.get_content_margin(side) == 0.0, "HUD backing shifted content margins")
	var save := directory.path_join("hud-before.json")
	check(owner.save_freedom_as(save), "HUD baseline save failed")
	var original := FileAccess.get_file_as_bytes(save)
	view.controls_menu.hide_menu()
	for pixels in [Vector2i(1280, 720), Vector2i(960, 540), Vector2i(800, 450), Vector2i(640, 450)]:
		root.size = pixels
		for frame in 3:
			await process_frame
		view.update_view(0.0)
		view.home_cue.text += " · " + "Long station identity and off-screen cue ".repeat(8)
		view.save_status.text = "Save failed: " + "long-save-directory-name/".repeat(12) + "flight.json"
		for frame in 3:
			await process_frame
		var bounds := Rect2(Vector2.ZERO, view.size)
		check(bounds.encloses(view.hud_scroll.get_rect()), "HUD scroll bounds escaped " + str(pixels))
		check(view.hud_column.size.x <= view.hud_scroll.size.x and view.hud_scroll.horizontal_scroll_mode == ScrollContainer.SCROLL_MODE_DISABLED, "HUD has horizontal overflow")
		var font_pixels: float = view.telemetry.get_theme_font_size("normal_font_size") * pixels.x / view.size.x
		check(font_pixels >= 17.5 and font_pixels <= 20.5 and view.pause_button.size.y * pixels.y / view.size.y >= 39.5, "HUD type/action targets shrank")
		check(view.hud_stacked == (pixels.x < 800), "Narrow HUD did not use local stacked fallback")
		if not view.hud_stacked:
			check(bounds.encloses(view.orbital_forecast.get_rect()) and not view.hud_scroll.get_rect().intersects(view.orbital_forecast.get_rect()), "HUD overlaps forecast")
		check(view.hud_scroll.get_v_scroll_bar().visible, "HUD scroll affordance hidden")
		for label in [view.telemetry, view.home_cue, view.hint, view.save_status]:
			check(label.get_content_height() > 0 and label.size.y >= label.get_content_height(), "HUD text collapsed or clipped")
			check(label.get_global_rect().end.x <= view.hud_scroll.get_v_scroll_bar().get_global_rect().position.x + 1.0, "HUD text extended under scrollbar")
		view.hud_scroll.scroll_vertical = 0
		await process_frame
		var position: Vector2 = view.hud_scroll.get_global_rect().get_center()
		var motion := InputEventMouseMotion.new()
		motion.position = position
		motion.global_position = position
		root.push_input(motion, true)
		for step in 8:
			var wheel := InputEventMouseButton.new()
			wheel.position = position
			wheel.global_position = position
			wheel.button_index = MOUSE_BUTTON_WHEEL_DOWN
			wheel.pressed = true
			wheel.factor = 5.0
			root.push_input(wheel, true)
		await process_frame
		check(view.hud_scroll.scroll_vertical > 0, "Actual GUI wheel did not scroll HUD")
		view.hud_scroll.ensure_control_visible(view.save_status)
		await process_frame
		check(view.hud_scroll.get_global_rect().intersects(view.save_status.get_global_rect()), "Save status unreachable by scroll")
		if view.hud_stacked:
			view.hud_scroll.ensure_control_visible(view.orbital_forecast)
			await process_frame
			check(view.orbital_forecast.get_parent() == view.hud_column and view.hud_scroll.get_global_rect().intersects(view.orbital_forecast.get_global_rect()), "Stacked forecast unreachable")
		check(view.orbital_forecast.get_content_height() <= view.orbital_forecast.size.y, "Orbital HUD text overflowed")
		check(view.orbital_forecast.text == FlightView.orbit_text(before) and owner.get_freedom_flight_state() == before and view.paused, "HUD layout/scroll changed forecast/state or resumed")
	check(owner.save_freedom_as(save) and FileAccess.get_file_as_bytes(save) == original, "HUD resize/scroll changed saved bytes")
	root.size = Vector2i(1280, 720)
	await process_frame
	view.hud_scroll.scroll_vertical = 0
	view.update_view(0.0)
	view.controls_menu.show_menu()


func check_hud_profiles(directory: String, assets: String) -> void:
	root.size = Vector2i(800, 450)
	for fixture in ["flight-18.json", "port-far.json", "port-approach.json", "port-docked.json"]:
		var owner: Variant = ClassDB.instantiate("FreedomBridge")
		check(owner.initialize_freedom_continue(directory.path_join(fixture)), "HUD profile fixture refused")
		var before: Dictionary = owner.get_freedom_flight_state()
		var view := FlightView.new()
		view.persist_controls = false
		root.add_child(view)
		view.set_process(false)
		check(view.initialize(owner, assets), "HUD profile view refused: " + view.error)
		for frame in 3:
			await process_frame
		if before.frame_id == "2":
			check(view.port_buttons.size() == 2 and view.capture_button.disabled == (before.attached or not before.docking.ready) and view.release_button.disabled == not before.attached, "Responsive HUD changed real port assessment")
			check(view.port_buttons[0].disabled == before.attached and (before.attached and "Attached to D" in view.dock_status.text or not before.attached and str(before.docking.reason) in view.dock_status.text), "Responsive HUD lost port target/refusal")
		else:
			check(view.port_buttons.is_empty() and view.capture_button == null and view.dock_status == null, "Legacy HUD invented docking controls")
		check(owner.get_freedom_flight_state() == before and view.paused, "HUD profile changed owner")
		view.free()
		owner = null
		await process_frame
	root.size = Vector2i(1280, 720)
	await process_frame


func check_saved_reference(view: Node, owner: Variant, directory: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var before_path := directory.path_join("reference-before.json")
	var after_path := directory.path_join("reference-after.json")
	check(owner.save_freedom_as(before_path), "Reference oracle Save As failed")
	var before_bytes := FileAccess.get_file_as_bytes(before_path)
	var menu: CanvasLayer = view.controls_menu
	view.player_input.device = 0
	view.player_input.install()
	menu.basics_button.grab_focus()
	physical_key(KEY_ENTER, true)
	await process_frame
	physical_key(KEY_ENTER, false)
	check(menu.basics.visible and view.paused and not view.player_input.enabled, "Real saved reference entry resumed flight")
	joy_button(JOY_BUTTON_A, true)
	await process_frame
	joy_button(JOY_BUTTON_A, false)
	check(menu.basics.page == 1, "Saved reference controller page navigation failed")
	view._process(1.0 / 60.0)
	check(owner.get_freedom_flight_state() == before, "Reading reference advanced C++ clock/state")
	view._notification(NOTIFICATION_APPLICATION_FOCUS_OUT)
	view._notification(NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and not menu.basics.visible and menu.menu_margin.visible and owner.get_freedom_flight_state() == before, "Reference focus return resumed or changed flight")
	menu.show_basics()
	check(menu.basics.visible, "Reference could not reopen after focus return")
	# Current held inputs still block resume from the help panel.
	physical_key(KEY_W, true)
	physical_key(KEY_ESCAPE, true)
	await process_frame
	physical_key(KEY_ESCAPE, false)
	check(view.paused and not view.controls_armed and owner.get_freedom_flight_state() == before, "Reference Resume bypassed held-flight gate")
	physical_key(KEY_W, false)
	menu.show_basics()
	menu.basics.back_button.grab_focus()
	physical_key(KEY_ENTER, true)
	await process_frame
	physical_key(KEY_ENTER, false)
	check(not menu.basics.visible and menu.menu_margin.visible and root.gui_get_focus_owner() == menu.basics_button and view.paused, "Saved reference Back lost pause/focus")
	check(owner.save_freedom_as(after_path) and FileAccess.get_file_as_bytes(after_path) == before_bytes, "Reference changed committed C++ save bytes")
	check(view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0, "Reference displayed firing exhaust")
	menu.show_menu()


func check_controller_domains() -> void:
	for size in [0, 6, 8, 1000]:
		var bad := PackedFloat64Array()
		bad.resize(size)
		check(FlightView.actuator_fractions(bad).is_empty(), "Saved adapter accepted malformed dimensions")
	for value in [NAN, INF, -INF, -1.01, 1.01]:
		for i in 7:
			var bad := PackedFloat64Array([0, 0, 0, 0, 0, 0, 0])
			bad[i] = value
			check(FlightView.actuator_fractions(bad).is_empty(), "Saved adapter accepted nonfinite/out-of-range demand")
	for i in 2:
		var bad := PackedFloat64Array([0, 0, 0, 0, 0, 0, 0])
		bad[i] = -0.1
		check(FlightView.actuator_fractions(bad).is_empty(), "Saved adapter accepted negative engine demand")
	check(FlightView.actuator_fractions(PackedFloat64Array([0.3, 0.2, 0.4, -0.5, 0.6, -0.7, 0.8])) == PackedFloat64Array([0, 0.8, 0.2, 0.7, 0, 0.3, 0.4, 0.5, 0, 0, 0, 0.6]), "Saved adapter changed complete semantic signs or binary64 fractions")


func check_saved_controller(view: Control, owner: Variant, save: String) -> void:
	check(owner.enable_streaming(), "Controller fixture could not enable authoritative terrain")
	var controls: Node = view.player_input
	controls.device = 0
	controls.install()
	view.toggle_pause()
	check(not view.paused and controls.enabled, "Neutral production adapter did not resume")
	var reference: Variant = ClassDB.instantiate("FreedomBridge")
	check(reference.initialize_freedom_continue(save), "Independent controller owner refused Continue")
	# Independent explicit channel expectations; physical key events reach the view.
	var keys := [KEY_E, KEY_SPACE, KEY_S, KEY_Q, KEY_CTRL, KEY_W, KEY_I, KEY_A, KEY_Z, KEY_K, KEY_D, KEY_X]
	for channel in 12:
		physical_key(keys[channel], true)
		var expected := PackedFloat64Array()
		expected.resize(12)
		expected[channel] = 1.0
		check(FlightView.actuator_fractions(controls.sample().thrust_axes) == expected, "Approved keyboard actuator sign missing: " + str(channel))
		view._process(1.0 / 120.0)
		check(reference.advance_freedom_flight(1.0 / 120.0, expected, false) and owner.get_freedom_flight_state() == reference.get_freedom_flight_state(), "Production keyboard command differs from independent saved owner")
		physical_key(keys[channel], false)
		controls.sample()
	# All controller directions, including fractional demands and gross triggers.
	for item in [[JOY_AXIS_TRIGGER_RIGHT, 0.6, 5], [JOY_AXIS_TRIGGER_LEFT, 0.7, 2], [JOY_AXIS_LEFT_Y, 0.7, 6], [JOY_AXIS_LEFT_Y, -0.7, 9], [JOY_AXIS_LEFT_X, -0.7, 8], [JOY_AXIS_LEFT_X, 0.7, 11], [JOY_AXIS_RIGHT_X, -0.7, 7], [JOY_AXIS_RIGHT_X, 0.7, 10], [JOY_AXIS_RIGHT_Y, -0.7, 1], [JOY_AXIS_RIGHT_Y, 0.7, 4]]:
		joy_axis(item[0], item[1])
		var expected := PackedFloat64Array()
		expected.resize(12)
		expected[item[2]] = controls.shape(absf(PackedFloat32Array([item[1]])[0]), controls.settings.deadzone, 1.0 if item[0] >= JOY_AXIS_TRIGGER_LEFT else controls.settings.curve)
		check(FlightView.actuator_fractions(controls.sample().thrust_axes) == expected, "Approved controller channel/sign/fraction differs")
		view._process(1.0 / 120.0)
		check(reference.advance_freedom_flight(1.0 / 120.0, expected, false) and owner.get_freedom_flight_state() == reference.get_freedom_flight_state(), "Controller fixed-tick command differs from independent saved owner")
		joy_axis(item[0], 0.0)
		controls.sample()
	for item in [[JOY_BUTTON_LEFT_SHOULDER, 3], [JOY_BUTTON_RIGHT_SHOULDER, 0]]:
		joy_button(item[0], true)
		var expected := PackedFloat64Array()
		expected.resize(12)
		expected[item[1]] = 1.0
		view._process(1.0 / 120.0)
		check(reference.advance_freedom_flight(1.0 / 120.0, expected, false) and owner.get_freedom_flight_state() == reference.get_freedom_flight_state(), "Bumper lateral thrust differs from authoritative commands")
		joy_button(item[0], false)
		controls.sample()
	var directory := save.get_base_dir()
	check(owner.save_freedom_as(directory.path_join("controller-owner.json")) and reference.save_freedom_as(directory.path_join("controller-reference.json")), "Controller parity Save As failed")
	check(FileAccess.get_file_as_bytes(directory.path_join("controller-owner.json")) == FileAccess.get_file_as_bytes(directory.path_join("controller-reference.json")), "Controller fixed schedule changed exact saved bytes")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1.0, 7)
	check(FlightView.actuator_fractions(controls.sample().thrust_axes) == PackedFloat64Array([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]), "Foreign pad acquired saved flight")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0, 7)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.6)
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0.4)
	var gross := FlightView.actuator_fractions(controls.sample().thrust_axes)
	check(gross[5] > gross[2] and gross[2] > 0, "Saved input erased independent opposed engines")
	view._process(1.0 / 120.0)
	check(view.exhaust.intensity > 0.0, "Real controller main command did not light saved exhaust")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0)
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0.0)
	controls.sample()
	# View and look are presentation only; both registered cameras remain mirrored.
	var before: Dictionary = owner.get_freedom_flight_state()
	joy_button(JOY_BUTTON_X, true)
	joy_button(JOY_BUTTON_X, false)
	check(view.cockpit, "Controller view button did not reach production callback")
	joy_button(JOY_BUTTON_LEFT_STICK, true)
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	joy_axis(JOY_AXIS_RIGHT_Y, -0.6)
	var looking: Dictionary = controls.sample()
	check(looking.thrust_axes[3] == 0 and looking.thrust_axes[6] == 0, "Saved head-look leaked yaw/heave")
	view._process(1.0 / 120.0)
	check(view.look_offset.length() > 0 and view.camera.transform == view.terrain_camera.transform and view.camera.basis != view.state.body_basis, "Saved head-look missing or split registered cameras")
	joy_button(JOY_BUTTON_LEFT_STICK, false)
	view._process(1.0 / 120.0)
	check(view.look_offset == Vector2.ZERO and view.camera.basis == view.state.body_basis and controls.sample().thrust_axes[3] == 0, "Look release did not recenter/suppress held yaw")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0)
	controls.sample()
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	check(controls.sample().thrust_axes[3] > 0, "Centered look channels did not rearm")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	controls.sample()
	physical_key(KEY_F3, true)
	physical_key(KEY_F3, false)
	check(not view.cockpit, "Explicit legacy F3 camera alias failed")
	joy_button(JOY_BUTTON_Y, true)
	joy_button(JOY_BUTTON_Y, false)
	check(not owner.get_freedom_flight_state().assistance and not controls.assist and not view.assist_button.button_pressed, "Controller assistance did not reflect accepted C++ state")
	joy_button(JOY_BUTTON_Y, true)
	joy_button(JOY_BUTTON_Y, false)
	check(owner.get_freedom_flight_state().assistance and controls.assist, "Controller assistance did not restore accepted state")
	# A real remap while the old physical key stays held must retain its barrier.
	physical_key(KEY_W, true)
	view._process(1.0 / 120.0)
	before = owner.get_freedom_flight_state()
	var phase: float = view.exhaust.phase
	controls.persist = true
	controls.settings_path = "user://saved-controls-%d.json" % OS.get_process_id()
	check(not FileAccess.file_exists(controls.settings_path), "Owned preference fixture already exists")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_T}), "Saved remap rejected")
	check(view.paused and not controls.enabled and view.exhaust.intensity == 0.0 and view.exhaust.phase == phase and owner.get_freedom_flight_state() == before, "Remap did not immediately pause/darken without a C++ step")
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Removed held key escaped remap-neutral barrier")
	physical_key(KEY_W, false)
	view.observe_neutral_controls()
	check(view.controls_armed, "Released old key retained stale barrier")
	controls.defaults()
	controls.load_settings()
	check(controls.bindings.forward.key.code == KEY_T, "Saved remap did not reload from isolated preferences")
	check(owner.get_freedom_flight_state() == before, "External preference changes mutated saved flight")
	check(DirAccess.remove_absolute(controls.settings_path) == OK, "Owned preference fixture could not be removed")
	controls.persist = false
	controls.defaults()
	controls.install()
	# Controller navigation reaches the real Save As callback while paused.
	await process_frame
	await process_frame
	# A prior paused neutral observation cannot authorize a later held press.
	view.observe_neutral_controls()
	physical_key(KEY_I, true)
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Delayed held input bypassed current-neutral resume check")
	physical_key(KEY_I, false)
	view.controls_menu.save_button.grab_focus()
	joy_button(JOY_BUTTON_A, true)
	await process_frame
	joy_button(JOY_BUTTON_A, false)
	check(view.save_dialog.visible and view.paused and not controls.enabled and owner.get_freedom_flight_state() == before, "Pad UI accept did not open real paused Save As")
	view.save_dialog.hide()
	view.save_dialog.canceled.emit()
	check(view.paused and view.controls_menu.panel.visible, "Save cancellation resumed flight or lost controls menu")
	view.controls_menu.saved_port_buttons["Target D2"].pressed.emit()
	check(owner.get_freedom_flight_state().target_port == 2, "Saved menu did not reach existing port selection owner")
	var selected: Dictionary = owner.get_freedom_flight_state()
	check(view.controls_menu.saved_port_buttons["Capture selected port"].disabled and view.controls_menu.saved_port_buttons["Capture selected port"].tooltip_text == selected.docking.reason, "Saved menu ignored physical capture assessment/refusal reason")
	# Deliberate callback-owner refusal test; a player cannot activate this disabled button.
	view.controls_menu.saved_port_buttons["Capture selected port"].pressed.emit()
	check(owner.get_freedom_flight_state() == selected and not str(owner.get_last_error()).is_empty(), "Saved menu bypassed physical capture refusal")
	for button in view.controls_menu.panel.find_children("*", "Button", true, false):
		check(not "practice" in button.text.to_lower() and not "experimental" in button.text.to_lower() and not "guidance" in button.text.to_lower(), "Saved menu exposed an unavailable study action")
	controls.settings.prompts = 2
	controls.last_device = "pad"
	view.controls_menu.refresh_bindings()
	check("Square" in view.controls_menu.saved_note.text and "Triangle" in view.controls_menu.saved_note.text, "Saved help did not derive physical prompt override")
	view.toggle_pause()
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1)
	joy_axis(JOY_AXIS_RIGHT_Y, 1)
	view._process(1.0 / 120.0)
	before = owner.get_freedom_flight_state()
	phase = view.exhaust.phase
	controls.connection_changed(0, false)
	check(view.paused and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase and owner.get_freedom_flight_state() == before, "Selected device loss advanced state or retained lit effects")
	controls.connection_changed(0, true)
	check(view.paused, "Device reconnect automatically resumed saved flight")
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Held reconnect axes bypassed explicit neutral resume")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0)
	controls.sample()
	check(owner.get_freedom_flight_state() == before and view.exhaust.phase == phase, "Reconnect-neutral callback changed owner/phase")
	view.pause_controls("Controller regression complete")


func check_focus_loss(view: Control, owner: Variant, directory: String) -> void:
	# Earlier direct Continue checks reset the real stream owned by this bridge.
	check(owner.enable_streaming(), "Focus regression could not reenable authoritative terrain")
	view.state = owner.get_freedom_flight_state()
	view.toggle_pause()
	physical_key(KEY_W, true)
	physical_key(KEY_CTRL, true)
	var resolved := FlightView.actuator_fractions(view.player_input.sample().thrust_axes)
	check(resolved[5] == 1.0 and resolved[4] == 1.0, "Physical flight keys did not reach production gross channels")
	view._process(1.0 / 120.0)
	check(view.error.is_empty() and view.exhaust.intensity == 1.0 and view.exhaust.withdrawal_intensity == 1.0, "Focus regression did not begin with real main and withdrawal firing: " + str([view.error, view.paused, view.focused, view.controls_armed, view.exhaust.intensity, view.exhaust.withdrawal_intensity]))
	var before: Dictionary = owner.get_freedom_flight_state()
	var phase: float = view.exhaust.phase
	var checkpoint := directory.path_join("focus-before.json")
	var after := directory.path_join("focus-after.json")
	check(owner.save_freedom_as(checkpoint), "Focus baseline save failed")
	var saved := FileAccess.get_file_as_bytes(checkpoint)
	view._notification(NOTIFICATION_APPLICATION_FOCUS_OUT)
	check(not view.focused and view.paused and not view.controls_armed and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.phase == phase, "Focus-out did not immediately pause and darken both effects")
	check(owner.get_freedom_flight_state() == before, "Focus notification changed committed C++ flight")
	view._process(60.0)
	view.toggle_pause()
	check(view.paused and owner.get_freedom_flight_state() == before and view.exhaust.phase == phase, "Unfocused callbacks scheduled flight or resumed it")
	view._notification(NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.focused and view.paused and not view.controls_armed, "Focus-in automatically resumed or rearmed controls")
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Held main/withdrawal input resumed after focus return")
	physical_key(KEY_W, false)
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Releasing only one held channel rearmed flight")
	physical_key(KEY_CTRL, false)
	view._process(60.0)
	check(view.controls_armed and view.paused and owner.get_freedom_flight_state() == before and view.exhaust.phase == phase, "Neutral rearming advanced paused time or resumed flight")
	check(owner.save_freedom_as(after) and FileAccess.get_file_as_bytes(after) == saved, "Focus/neutral callbacks changed exact save bytes")
	view.toggle_pause()
	check(not view.paused, "Neutral controls did not permit explicit resume")
	physical_key(KEY_W, true)
	physical_key(KEY_CTRL, true)
	view._process(1.0 / 120.0)
	check(view.error.is_empty() and int(owner.get_freedom_flight_state().tick) == int(before.tick) + 1 and view.exhaust.intensity == 1.0 and view.exhaust.withdrawal_intensity == 1.0, "Resume did not restore actual controls or accumulated unfocused ticks")
	physical_key(KEY_W, false)
	physical_key(KEY_CTRL, false)
	var independent: Variant = ClassDB.instantiate("FreedomBridge")
	check(independent.initialize_freedom_continue(checkpoint), "Focus comparison could not Continue")
	var commands := PackedFloat64Array()
	commands.resize(12)
	commands[4] = 1.0
	commands[5] = 1.0
	check(independent.advance_freedom_flight(1.0 / 120.0, commands, false) and independent.get_freedom_flight_state() == owner.get_freedom_flight_state(), "Resumed input differs from uninterrupted C++ commands")
	check(owner.save_freedom_as(after) and independent.save_freedom_as(checkpoint) and FileAccess.get_file_as_bytes(after) == FileAccess.get_file_as_bytes(checkpoint), "Resumed real controls differ in exact C++ save bytes")
	# Each physical flight key independently blocks rearming; no omitted axis.
	before = owner.get_freedom_flight_state()
	phase = view.exhaust.phase
	for key in [KEY_E, KEY_SPACE, KEY_S, KEY_Q, KEY_CTRL, KEY_W, KEY_I, KEY_A, KEY_Z, KEY_K, KEY_D, KEY_X]:
		view._notification(NOTIFICATION_APPLICATION_FOCUS_OUT)
		physical_key(key, true)
		view._notification(NOTIFICATION_APPLICATION_FOCUS_IN)
		view.toggle_pause()
		check(view.paused and not view.controls_armed, "Held physical channel escaped focus gate: " + str(key))
		physical_key(key, false)
		view._process(1.0 / 120.0)
		check(view.paused and view.controls_armed and owner.get_freedom_flight_state() == before and view.exhaust.phase == phase, "Neutral key release changed flight or resumed: " + str(key))


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 6:
		push_error("Expected flight, C++ trace, corrupt, station saves and prepared assets")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the C++ bridge first")
		quit(1)
		return
	var original: Array[PackedByteArray] = []
	for path in args.slice(0, 4):
		original.append(FileAccess.get_file_as_bytes(path))
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	refused(owner, 0.0, neutral, false)
	check(not owner.set_freedom_assistance(false), "Uninitialized owner accepted assistance")
	check(owner.initialize_freedom_continue(args[0]), "Selected flight refused: " + str(owner.get_last_error()))
	var start: Dictionary = owner.get_freedom_flight_state()
	check(FlightView.valid_state(start) and start.tick == "25" and start.frame_id == "2" and start.assistance, "Selected state was not the saved Wayfarer")
	check(absf(start.body_basis.determinant() - 1.0) < 0.00001 and absf(start.station_basis.determinant() - 1.0) < 0.00001, "Projection changed orientation handedness")
	check(absf(start.altitude - 500000.0) < 0.001, "Rotation projection changed saved radial altitude")
	check_controller_domains()
	check_orbit_fields(start)
	check_orbit_fixtures(args[0].get_base_dir())
	check_exhaust_inputs(start)
	await check_hud_profiles(args[0].get_base_dir(), args[4])
	check(owner.get_freedom_start().is_empty() and owner.get_state().is_empty(), "Flight created a docked or study owner")
	for size in [0, 1, 11, 13, 1000]:
		var truncated := PackedFloat64Array()
		truncated.resize(size)
		refused(owner, 1.0 / 120.0, truncated, false)
	for bad in [NAN, INF, -INF, -0.01, 1.01]:
		for index in 12:
			var invalid := neutral.duplicate()
			invalid[index] = bad
			refused(owner, 1.0 / 120.0, invalid, false)
			refused(owner, 0.0, invalid, true)
	for bad_elapsed in [NAN, INF, -1.0, 60.01]:
		refused(owner, bad_elapsed, neutral, false)
	check(owner.initialize_freedom_continue(args[5]), "Valid terminal clock fixture refused")
	refused(owner, 0.1, neutral, false)
	check(owner.initialize_freedom_continue(args[0]), "Clock refusal damaged normal Continue")
	check(owner.advance_freedom_flight(60.0, neutral, true) and owner.get_freedom_flight_state() == start, "Paused time changed clock/body")
	check(not owner.initialize_freedom_continue(args[2]) and owner.get_freedom_flight_state() == start, "Corrupt load replaced selected flight")
	var view := FlightView.new()
	view.persist_controls = false
	root.add_child(view)
	check(view.initialize(owner, args[4]), "Production flight view failed: " + view.error)
	view.set_process(false)
	check(view.paused and not view.cockpit and view.ship.get_child_count() == 2 and view.exhaust != null, "Continue did not start safely paused with the actual model")
	check(view.terrain.bridge == owner and owner.get_freedom_flight_state() == start, "Terrain/view invented or advanced saved state")
	check(is_equal_approx(view.camera.near, 0.05) and view.camera.far == 200.0 and view.terrain_camera.far / view.terrain_camera.near <= 500001.0, "Native camera depth ranges are unsafe")
	check(view.camera.transform == view.terrain_camera.transform and view.camera.fov == view.terrain_camera.fov and view.camera.cull_mask == 3 and view.terrain_camera.cull_mask == 1, "Close terrain occlusion/camera registration was lost")
	check(view.camera.get_world_3d() == view.terrain_camera.get_world_3d() and view.camera.get_viewport() != view.terrain_camera.get_viewport(), "Depth passes lost the shared world or viewport isolation")
	check(view.exhaust.plumes.size() == 2 and view.exhaust.withdrawal_plumes.size() == 2 and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0, "Neutral Continue displayed thrust or lost source outlets")
	var structural_skin: MeshInstance3D = view.ship.find_child("HopperStructure", true, false)
	check(structural_skin != null and structural_skin.transform == Transform3D.IDENTITY, "Qualified skin lost its body-local identity")
	var skin_arrays: Array = structural_skin.mesh.surface_get_arrays(MainExhaust.SKIN_SURFACE)
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(skin_arrays[Mesh.ARRAY_VERTEX].to_byte_array())
	check(hash.finish().hex_encode() == "19a2e4799f007e26bf0de87b2e0472b3416e3ce622d40e1c2a0e468e3f1f0ca2", "Imported skin positions differ from the qualified source")
	check(skin_arrays[Mesh.ARRAY_INDEX].size() == 631083, "Imported skin lost retained source triangles")
	hash.start(HashingContext.HASH_SHA256)
	hash.update(skin_arrays[Mesh.ARRAY_INDEX].to_byte_array())
	# Godot reverses each source triangle's winding for its clockwise convention.
	check(hash.finish().hex_encode() == "53d2c00fb625e9de091092a75f0470ffdf068782eb8af9f9f6bdec4511599465", "Imported skin topology differs from the qualified source")
	var coating: StandardMaterial3D = structural_skin.get_surface_override_material(MainExhaust.SKIN_SURFACE)
	check(coating != null and coating.next_pass is ShaderMaterial, "Source-conformed aperture pass is unavailable")
	var emitted_meshes := []
	for i in 2:
		var plume: MeshInstance3D = view.exhaust.withdrawal_plumes[i]
		check(plume.basis == Basis(Vector3.RIGHT, Vector3.DOWN, Vector3.FORWARD), "Withdrawal plume acquired a trigonometric axis tilt")
		var vertices: PackedVector3Array = plume.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		var recorded_vertices := []
		for vertex in vertices:
			check(vertex.is_finite() and float(vertex.x) * float(vertex.x) + float(vertex.z) * float(vertex.z) <= MainExhaust.WITHDRAWAL_RADIUS * MainExhaust.WITHDRAWAL_RADIUS, "Generated plume vertex exceeded the qualified radial envelope")
			check(float(plume.position.y) - float(vertex.y) >= MainExhaust.WITHDRAWAL_ANCHORS[i].y, "Generated plume base rounded inward")
			recorded_vertices.append([vertex.x, vertex.y, vertex.z])
		emitted_meshes.append({"position": [plume.position.x, plume.position.y, plume.position.z], "vertices": recorded_vertices})
	var mesh_record := FileAccess.open(args[0].get_base_dir().path_join("native-withdrawal-mesh.json"), FileAccess.WRITE)
	check(mesh_record != null, "Generated plume numeric record could not be saved")
	if mesh_record != null:
		mesh_record.store_string(JSON.stringify(emitted_meshes, "\t", true, true))
		mesh_record.close()
	var glass: MeshInstance3D = view.ship.find_child("HopperGlass", true, false)
	check(glass != null and glass.material_override != null and glass.material_override.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA and glass.material_override.albedo_color.a < 0.1, "Selected cockpit glass blocks native flight visibility")
	await check_hud_layout(view, owner, args[0].get_base_dir())
	await check_saved_reference(view, owner, args[0].get_base_dir())
	await check_saved_controller(view, owner, args[0])
	check(owner.initialize_freedom_continue(args[0]), "Controller regression damaged original Continue")
	view.state = owner.get_freedom_flight_state()
	check_focus_loss(view, owner, args[0].get_base_dir())
	check(owner.initialize_freedom_continue(args[0]) and owner.get_freedom_flight_state() == start, "Focus control damaged the original Continue")
	view.state = owner.get_freedom_flight_state()
	var commands := neutral.duplicate()
	commands[4] = 0.1
	commands[5] = 0.25
	commands[7] = 0.05
	var output := args[0].get_base_dir().path_join("godot-flight-trace.json")
	for n in 120:
		if n == 60:
			check(owner.save_freedom_as(output) and owner.initialize_freedom_continue(output), "Mid-trace Save As/Continue failed")
			check(owner.set_freedom_assistance(false), "Explicit Advanced piloting refused")
		check(owner.advance_freedom_flight(1.0 / 120.0, commands, false), "C++ flight rejected a real control step")
		view.state = owner.get_freedom_flight_state()
		view.update_view(0.0)
		check(view.orbital_forecast.text == FlightView.orbit_text(view.state), "Committed flight step left stale orbital forecast")
	var final: Dictionary = owner.get_freedom_flight_state()
	check(view.orbital_forecast.text == FlightView.orbit_text(final) and final.periapsis_radius != start.periapsis_radius, "Live actuation did not refresh orbital readout")
	check(final.tick == "145" and not final.assistance and final.checksum != start.checksum, "Input did not advance/persist selected state")
	check(owner.save_freedom_as(output), "Flight Save As refused")
	check(FileAccess.get_file_as_bytes(output) == original[1], "Godot input/save trace differs from independent C++ trace bytes")
	check(view.exhaust.update_applied(final, 0.1, false) and is_equal_approx(view.exhaust.intensity, 0.25), "Exhaust ignored actual C++ gross main force")
	check(is_equal_approx(view.exhaust.withdrawal_intensity, 0.1) and view.exhaust.withdrawal_plumes[0].visible, "Withdrawal exhaust ignored independent C++ gross negative-Y force")
	var phase: float = view.exhaust.phase
	check(view.exhaust.update_applied(final, 0.1, false) and view.exhaust.phase > phase and view.exhaust.plumes[0].visible, "Applied plume did not animate")
	phase = view.exhaust.phase
	check(view.exhaust.update_applied(final, 60.0, true) and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.phase == phase, "Paused exhaust changed phase or kept firing")
	var invalid_force := final.duplicate(true)
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false) and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible, "Truncated force buffer lit a plume")
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0, NAN])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false), "Nonfinite force lit a plume")
	phase = view.exhaust.phase
	var attached_force := final.duplicate(true)
	attached_force.attached = true
	check(view.exhaust.update_applied(attached_force, 0.1, false) and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Attached craft displayed firing or advanced exhaust phase")
	check(not view.exhaust.update_applied({}, 0.1, false) and not view.exhaust.plumes[0].visible and view.exhaust.phase == phase, "Missing owner retained firing or changed exhaust phase")
	# A different presentation cadence consumes exactly the same commands/ticks.
	var other: Variant = ClassDB.instantiate("FreedomBridge")
	check(other.initialize_freedom_continue(args[0]), "Cadence comparison could not Continue")
	for n in 60:
		if n == 30:
			check(other.set_freedom_assistance(false), "Cadence assistance refused")
		check(other.advance_freedom_flight(1.0 / 60.0, commands, false), "Two-tick presentation batch refused")
	check(other.get_freedom_flight_state() == final, "Presentation cadence changed authoritative state or applied propulsion")
	check(other.initialize_freedom_continue(args[0]), "Opposing-channel fixture could not Continue")
	var opposed := neutral.duplicate()
	opposed[2] = 1.0
	opposed[5] = start.positive_force_ratings[2] / start.negative_force_ratings[2]
	check(other.advance_freedom_flight(1.0 / 120.0, opposed, false), "Physical opposing firings refused")
	var gross: Dictionary = other.get_freedom_flight_state()
	check(absf(gross.applied_force_body[2]) < 0.00001 and gross.negative_force_body[2] > 0.0, "Gross opposition was erased or became net thrust")
	check(view.exhaust.update_applied(gross, 0.1, false) and view.exhaust.intensity > 0.0, "Zero net propulsion hid an actual firing main nozzle")
	check(view.exhaust.withdrawal_intensity == 0.0 and not view.exhaust.withdrawal_plumes[0].visible, "Main firing lit the vertical outlets")
	check(other.initialize_freedom_continue(args[0]), "Vertical opposition fixture could not Continue")
	opposed.fill(0.0)
	opposed[1] = 0.25
	opposed[4] = 0.25 * start.positive_force_ratings[1] / start.negative_force_ratings[1]
	check(other.advance_freedom_flight(1.0 / 120.0, opposed, false), "Physical opposing vertical firings refused")
	gross = other.get_freedom_flight_state()
	check(absf(gross.applied_force_body[1]) < 0.00001 and gross.negative_force_body[1] > 0.0, "Gross vertical opposition was erased")
	check(view.exhaust.update_applied(gross, 0.1, false) and view.exhaust.withdrawal_intensity > 0.0 and view.exhaust.withdrawal_plumes[0].visible and view.exhaust.intensity == 0.0, "Zero net vertical force hid the firing outlet or lit main thrust")
	check(other.advance_freedom_flight(1.0 / 120.0, neutral, false), "Neutral coast after opposition refused")
	check(view.exhaust.update_applied(other.get_freedom_flight_state(), 0.1, false) and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0, "Coasting velocity or gravity was presented as exhaust")
	check(other.initialize_freedom_continue(output) and other.get_freedom_flight_state().checksum == final.checksum, "Flight Save As did not resume exact state")
	for path in ["relative.json", "", args[0].get_base_dir(), args[0].get_base_dir() + "/missing/save.json"]:
		check(not owner.save_freedom_as(path) and owner.get_freedom_flight_state() == final, "Refused Save As changed selected flight")
	check(other.initialize_freedom_continue(args[3]) and other.get_freedom_flight_state().is_empty(), "Station selection retained stale saved flight")
	# Exercise the production consumer after a lit frame, including its early exit.
	view.paused = false
	check(view.exhaust.update_applied(final, 0.1, false) and view.exhaust.plumes[0].visible and view.exhaust.withdrawal_plumes[0].visible, "Consumer failure control did not start firing")
	phase = view.exhaust.phase
	view.toggle_pause()
	check(view.paused and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Pause callback left exhaust firing until the next frame")
	view.toggle_pause()
	check(view.exhaust.update_applied(final, 0.1, false), "Refused-batch control did not resume firing")
	phase = view.exhaust.phase
	view._process(NAN)
	check(not view.error.is_empty() and view.paused and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.phase == phase, "Rejected flight batch left stale exhaust firing")
	check(owner.get_freedom_flight_state() == final, "Consumer refusal advanced C++ flight")
	view._process(0.1)
	check(owner.get_freedom_flight_state() == final and view.exhaust.phase == phase, "Failed view advanced state or visual phase")
	view.toggle_pause()
	view.port_command("select_freedom_port", 2)
	view.assist_button.toggled.emit(not final.assistance)
	check(view.paused and owner.get_freedom_flight_state() == final, "Terminal error UI commands changed C++ state or resumed flight")
	view.update_view(0.1)
	check(view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Terminal error refresh relit stale exhaust")
	view.error = ""
	view.paused = false
	view.terrain.error = "Unavailable terrain control"
	check(view.exhaust.update_applied(final, 0.1, false), "Terrain failure control did not start firing")
	phase = view.exhaust.phase
	view._process(1.0 / 120.0)
	check(not view.error.is_empty() and view.paused and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Terrain failure refreshed stale firing or advanced visual phase")
	check(int(owner.get_freedom_flight_state().tick) == int(final.tick) + 1, "Terrain failure rolled back the valid C++ flight tick")
	view.terrain.error = ""
	view.error = ""
	view.paused = false
	view.bridge = other
	check(view.exhaust.update_applied(final, 0.1, false), "Lost-owner consumer control did not start firing")
	phase = view.exhaust.phase
	view._process(0.1)
	check(not view.error.is_empty() and view.paused and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Lost flight owner retained stale firing")
	view.error = ""
	view.paused = false
	check(view.exhaust.update_applied(final, 0.1, false), "Assistance failure control did not start firing")
	phase = view.exhaust.phase
	view.assist_button.toggled.emit(true)
	check(not view.error.is_empty() and view.paused and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Assistance failure callback retained stale firing")
	view.free()
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == original[i], "Native flight modified a source fixture")
	print("Saved native flight: %d failures; actual C++ controls and save trace, no GPU qualification" % failures)
	quit(0 if failures == 0 else 1)
