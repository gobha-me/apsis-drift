extends SceneTree
## Production first-person consumer against independent C++ journey save bytes.
const WalkView = preload("res://scripts/native/native_walk_view.gd")
var failures := 0


func commit_fixture_new_game(owner: Variant, seed: String) -> bool:
	if not owner.stage_freedom_new_game(seed): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

# This test owns C++ source fixtures only; renderer admission is exercised by
# native_start_staging, not by a direct fixture token commit.
func commit_fixture_continue(owner: Variant, path: String) -> bool:
	if not owner.stage_freedom_continue(path): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

func check(ok: bool, message: String) -> void:
	if not ok:
		push_error(message)
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func physical_key(key: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = key
	event.keycode = key
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	check(Input.is_physical_key_pressed(key) == pressed, "Software physical key did not update current input")


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


func right_mouse(pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_RIGHT
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	check(Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) == pressed, "Software right mouse did not update current input")


func paused_checkpoint(view: Control, owner: Variant, path: String, walk: Dictionary, flight: Dictionary, bytes: PackedByteArray, pose: Transform3D, heading: float, pitch: float, label: String) -> void:
	view._process(60.0)
	check(view.advance_requested(60.0, PackedFloat64Array([1.0, 1.0, PI])), label + ": paused request failed")
	view.input_controls(60.0)
	check(view.paused and owner.get_freedom_walk_state() == walk and owner.get_freedom_flight_state() == flight, label + ": actor/craft/clock advanced")
	check(view.camera.transform == pose and view.requested_heading == heading and view.pitch == pitch, label + ": paused inspection changed")
	check(view.light.basis.z.is_equal_approx(flight.star_direction), label + ": station light diverged from frozen C++ direction")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, label + ": exact C++ save bytes changed")


func check_hud_layout(view: Control, owner: Variant, path: String, bytes: PackedByteArray) -> void:
	var walk: Dictionary = owner.get_freedom_walk_state()
	var flight: Dictionary = owner.get_freedom_flight_state()
	var pose: Transform3D = view.camera.transform
	var heading: float = view.requested_heading
	var pitch: float = view.pitch
	var hint: String = view.hud_hint.text
	var status: String = view.save_status.text
	var device: int = view.selected_pad
	var temporary_pad: bool = not view.available_pads.get(device, false)
	if temporary_pad:
		Input.joy_connection_changed.emit(19, true)
		joy_button(JOY_BUTTON_START, true, 19)
		joy_button(JOY_BUTTON_START, false, 19)
		device = view.selected_pad
	check(view.available_pads.get(device, false) and view.paused, "Station layout fixture has no explicitly selected paused pad")
	var backing: StyleBox = view.hud_scroll.get_theme_stylebox("panel")
	check(backing is StyleBoxFlat and backing.bg_color.a >= 0.9 and maxf(backing.bg_color.r, maxf(backing.bg_color.g, backing.bg_color.b)) <= 0.05, "Station HUD lacks adequate dark backing")
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		check(backing.get_content_margin(side) == 0.0, "Station backing shifted content margins")
	check(view.hud_scroll.follow_focus and view.pause_button.focus_mode == Control.FOCUS_ALL and view.save_button.focus_mode == Control.FOCUS_ALL, "Station HUD lost existing focus navigation")
	for pixels in [Vector2i(1280, 720), Vector2i(960, 540), Vector2i(800, 450), Vector2i(640, 450)]:
		root.size = pixels
		for frame in 3:
			await process_frame
		view.hud_hint.text = hint + "\n" + "Existing station movement and selected-controller controls. ".repeat(12)
		view.save_status.text = "Save failed: " + "long-station-save-directory/".repeat(36) + "journey.json"
		for frame in 3:
			await process_frame
		var bounds := Rect2(Vector2.ZERO, view.size)
		check(bounds.encloses(view.hud_scroll.get_rect()), "Station HUD escaped viewport " + str(pixels))
		check(view.hud_column.size.x <= view.hud_scroll.size.x and view.hud_scroll.horizontal_scroll_mode == ScrollContainer.SCROLL_MODE_DISABLED, "Station HUD has horizontal overflow")
		var font_pixels: float = view.telemetry.get_theme_font_size("font_size") * pixels.x / view.size.x
		check(font_pixels >= 17.5 and font_pixels <= 20.5, "Station HUD font shrank in small window")
		for button in [view.pause_button, view.save_button, view.load_button, view.settings_button, view.title_button]:
			check(button.size.y * pixels.y / view.size.y >= 39.5, "Station action height shrank")
		for label in [view.telemetry, view.hud_hint, view.save_status]:
			check(label.autowrap_mode == TextServer.AUTOWRAP_WORD_SMART and label.size.y >= label.get_minimum_size().y, "Station text clipped or lost wrapping")
			check(label.get_global_rect().end.x <= view.hud_scroll.get_v_scroll_bar().get_global_rect().position.x + 1.0, "Station text extends under scroll gutter")
		check(view.save_status.get_line_count() > 1 and view.hud_scroll.get_v_scroll_bar().visible, "Long save status did not wrap or show scroll affordance")
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
		check(view.hud_scroll.scroll_vertical > 0, "Actual mouse wheel did not scroll station HUD")
		view.hud_scroll.ensure_control_visible(view.save_status)
		await process_frame
		check(view.hud_scroll.get_global_rect().intersects(view.save_status.get_global_rect()), "Station save status unreachable")
		# Wheel scrolling can hide the current focus; follow-focus runs on a
		# genuine focus transition, not grabbing an already focused control.
		view.save_button.grab_focus()
		await process_frame
		view.pause_button.grab_focus()
		await process_frame
		check(view.hud_scroll.get_global_rect().encloses(view.pause_button.get_global_rect()), "Follow-focus did not reveal Resume")
		physical_key(KEY_DOWN, true)
		physical_key(KEY_DOWN, false)
		await process_frame
		check(root.gui_get_focus_owner() == view.save_button and view.hud_scroll.get_global_rect().encloses(view.save_button.get_global_rect()), "Keyboard focus did not reveal Save As")
		physical_key(KEY_ENTER, true)
		physical_key(KEY_ENTER, false)
		check(view.save_dialog.visible, "Readable station Save As did not invoke real chooser")
		view.save_dialog.hide()
		view.save_dialog.canceled.emit()
		await process_frame
		joy_button(JOY_BUTTON_DPAD_DOWN, true, device)
		joy_button(JOY_BUTTON_DPAD_DOWN, false, device)
		await process_frame
		check(root.gui_get_focus_owner() == view.save_button and view.hud_scroll.get_global_rect().encloses(view.save_button.get_global_rect()), "Selected D-pad did not reveal Save As")
		joy_button(JOY_BUTTON_DPAD_DOWN, true, device)
		joy_button(JOY_BUTTON_DPAD_DOWN, false, device)
		check(root.gui_get_focus_owner() == view.load_button and view.hud_scroll.get_global_rect().encloses(view.load_button.get_global_rect()), "Selected D-pad did not reveal Load")
		joy_button(JOY_BUTTON_DPAD_DOWN, true, device)
		joy_button(JOY_BUTTON_DPAD_DOWN, false, device)
		check(root.gui_get_focus_owner() == view.settings_button and view.hud_scroll.get_global_rect().encloses(view.settings_button.get_global_rect()), "Selected D-pad did not reveal Settings")
		joy_button(JOY_BUTTON_DPAD_DOWN, true, device)
		joy_button(JOY_BUTTON_DPAD_DOWN, false, device)
		check(root.gui_get_focus_owner() == view.title_button and view.hud_scroll.get_global_rect().encloses(view.title_button.get_global_rect()), "Selected D-pad did not reveal Title")
		joy_button(JOY_BUTTON_DPAD_UP, true, device)
		joy_button(JOY_BUTTON_DPAD_UP, false, device)
		joy_button(JOY_BUTTON_DPAD_UP, true, device)
		joy_button(JOY_BUTTON_DPAD_UP, false, device)
		joy_button(JOY_BUTTON_DPAD_UP, true, device)
		joy_button(JOY_BUTTON_DPAD_UP, false, device)
		joy_button(JOY_BUTTON_A, true, device)
		joy_button(JOY_BUTTON_A, false, device)
		check(view.save_dialog.visible, "Selected pad did not invoke readable station Save As")
		view.save_dialog.hide()
		view.save_dialog.canceled.emit()
		paused_checkpoint(view, owner, path, walk, flight, bytes, pose, heading, pitch, "HUD resize/scroll/navigation")
	if temporary_pad:
		Input.joy_connection_changed.emit(19, false)
	view.hud_hint.text = hint
	view.save_status.text = status
	root.size = Vector2i(1280, 720)
	for frame in 3:
		await process_frame
	view.hud_scroll.scroll_vertical = 0


func check_station_controls(view: Control, owner: Variant, path: String, start: Dictionary, bytes: PackedByteArray) -> void:
	await process_frame
	await process_frame
	var flight: Dictionary = owner.get_freedom_flight_state()
	var pose: Transform3D = view.camera.transform
	var heading: float = view.requested_heading
	var pitch: float = view.pitch
	check(view.paused and not view.controls_armed, "Held W startup ran before neutral input")
	paused_checkpoint(view, owner, path, start, flight, bytes, pose, heading, pitch, "Startup")
	physical_key(KEY_W, false)
	view.observe_neutral_controls()
	check(view.paused and view.controls_armed, "Startup neutral observation implicitly resumed")
	# Individually opposed keys are held controls, even when their sums cancel.
	for pair in [[KEY_W, KEY_S], [KEY_A, KEY_D]]:
		physical_key(pair[0], true)
		physical_key(pair[1], true)
		view.set_paused(false)
		check(view.paused and not view.controls_armed, "Opposed walking keys bypassed Resume")
		physical_key(pair[0], false)
		physical_key(pair[1], false)
	# A neutral paused frame cannot authorize a press deferred until Resume.
	view.observe_neutral_controls()
	physical_key(KEY_D, true)
	physical_key(KEY_ESCAPE, true)
	physical_key(KEY_ESCAPE, false)
	check(view.paused and not view.controls_armed, "Deferred held key bypassed current-neutral Escape")
	physical_key(KEY_D, false)
	right_mouse(true)
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Held right-drag bypassed Resume")
	var motion := InputEventMouseMotion.new()
	motion.relative = Vector2(90, -70)
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	paused_checkpoint(view, owner, path, start, flight, bytes, pose, heading, pitch, "Opposed/deferred/mouse")
	right_mouse(false)
	physical_key(KEY_ESCAPE, true)
	physical_key(KEY_ESCAPE, false)
	check(not view.paused, "Explicit neutral Escape did not resume walking")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.set_paused(false)
	check(view.paused and not view.focused, "Direct Resume bypassed focus loss")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	physical_key(KEY_W, true)
	view.toggle_pause()
	paused_checkpoint(view, owner, path, start, flight, bytes, pose, heading, pitch, "Focus return held")
	physical_key(KEY_W, false)
	view.set_paused(false)
	check(not view.paused, "Explicit neutral focus rearm refused")
	view.set_paused(true)
	# Real menu key events select the actual Save As callback, without a tick.
	view.pause_button.grab_focus()
	physical_key(KEY_DOWN, true)
	physical_key(KEY_DOWN, false)
	physical_key(KEY_ENTER, true)
	physical_key(KEY_ENTER, false)
	check(view.save_dialog.visible and view.paused, "Keyboard navigation did not open real Save As")
	physical_key(KEY_W, true)
	view.set_paused(false)
	view.save_dialog.hide()
	view.save_dialog.canceled.emit()
	view.toggle_pause()
	paused_checkpoint(view, owner, path, start, flight, bytes, pose, heading, pitch, "Save cancellation held")
	physical_key(KEY_W, false)
	view.open_save_dialog()
	view.save_dialog.hide()
	view.save_dialog.file_selected.emit(path)
	check(view.paused and FileAccess.get_file_as_bytes(path) == bytes, "Save selection resumed or changed committed bytes")
	# Software connection signals plus actual pad events exercise selected ownership.
	if view.selected_pad >= 0:
		Input.joy_connection_changed.emit(view.selected_pad, false)
	Input.joy_connection_changed.emit(0, true)
	joy_axis(JOY_AXIS_LEFT_X, 0.7)
	joy_button(JOY_BUTTON_START, true)
	joy_button(JOY_BUTTON_START, false)
	check(view.selected_pad == 0 and view.paused, "Explicit Start selection resumed or failed to select absent pad")
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Selected held left stick bypassed Resume")
	joy_axis(JOY_AXIS_LEFT_X, 0.0)
	for axis in [JOY_AXIS_LEFT_Y, JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
		joy_axis(axis, 0.8)
		view.set_paused(false)
		check(view.paused and not view.controls_armed, "Held movement/look axis bypassed Resume")
		joy_axis(axis, 0.0)
	Input.joy_connection_changed.emit(7, true)
	joy_axis(JOY_AXIS_LEFT_X, 1.0, 7)
	joy_axis(JOY_AXIS_RIGHT_Y, 1.0, 7)
	joy_button(JOY_BUTTON_START, true, 7)
	joy_button(JOY_BUTTON_START, false, 7)
	check(view.selected_pad == 0 and view.current_controls_neutral(), "Foreign pad stole selection or neutral authority")
	view.pause_button.grab_focus()
	joy_button(JOY_BUTTON_DPAD_DOWN, true)
	joy_button(JOY_BUTTON_DPAD_DOWN, false)
	joy_button(JOY_BUTTON_A, true)
	joy_button(JOY_BUTTON_A, false)
	check(view.save_dialog.visible and view.paused, "Selected pad navigation did not open real Save As")
	view.save_dialog.hide()
	view.save_dialog.canceled.emit()
	view.set_paused(false)
	check(not view.paused, "Explicit neutral selected-pad Resume refused")
	joy_axis(JOY_AXIS_RIGHT_Y, 0.8)
	Input.joy_connection_changed.emit(0, false)
	check(view.paused and view.selected_pad == 0, "Disconnect resumed or automatically reassigned pad")
	joy_button(JOY_BUTTON_START, true)
	joy_button(JOY_BUTTON_START, false)
	check(view.paused, "Stale disconnected-pad Start resumed walking")
	view.save_button.grab_focus()
	joy_button(JOY_BUTTON_A, true)
	joy_button(JOY_BUTTON_A, false)
	check(view.paused and not view.save_dialog.visible, "Stale disconnected-pad accept opened Save As")
	Input.joy_connection_changed.emit(0, true)
	view.set_paused(false)
	check(view.paused and not view.controls_armed, "Reconnect reused held look axis")
	joy_axis(JOY_AXIS_RIGHT_Y, 0.0)
	paused_checkpoint(view, owner, path, start, flight, bytes, pose, heading, pitch, "Device/dialog menu")
	joy_axis(JOY_AXIS_LEFT_X, 0.0, 7)
	joy_axis(JOY_AXIS_RIGHT_Y, 0.0, 7)
	view.set_paused(false)
	# Actual station events retain WASD signs, not the saved-flight mapping.
	physical_key(KEY_W, true)
	check(view.input_controls(0.0) == PackedFloat64Array([1.0, 0.0, heading]), "Station W channel changed")
	physical_key(KEY_W, false)
	physical_key(KEY_S, true)
	check(view.input_controls(0.0) == PackedFloat64Array([-1.0, 0.0, heading]), "Station S channel changed")
	physical_key(KEY_S, false)
	physical_key(KEY_A, true)
	check(view.input_controls(0.0) == PackedFloat64Array([0.0, -1.0, heading]), "Station A channel changed")
	physical_key(KEY_A, false)
	joy_axis(JOY_AXIS_LEFT_X, 1.0)
	joy_axis(JOY_AXIS_LEFT_Y, -1.0)
	check(view.input_controls(0.0) == PackedFloat64Array([1.0, 1.0, heading]), "Selected left stick changed station movement channels")
	joy_axis(JOY_AXIS_LEFT_X, 0.0)
	joy_axis(JOY_AXIS_LEFT_Y, 0.0)
	joy_axis(JOY_AXIS_RIGHT_X, 1.0)
	joy_axis(JOY_AXIS_RIGHT_Y, -1.0)
	view.input_controls(0.1)
	check(is_equal_approx(view.requested_heading, heading - 0.18) and is_equal_approx(view.pitch, pitch + 0.18), "Selected right stick changed station look signs/rate")
	joy_axis(JOY_AXIS_RIGHT_X, 0.0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0.0)
	right_mouse(true)
	motion = InputEventMouseMotion.new()
	# parse_input_event takes window coordinates; the headless viewport stretches them.
	motion.relative = root.get_final_transform().basis_xform(Vector2(-100, 50))
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	check(is_equal_approx(view.requested_heading, heading + 0.12) and is_equal_approx(view.pitch, pitch + 0.03) and owner.get_freedom_walk_state() == start, "Actual right-drag changed station look mapping or advanced C++ time")
	right_mouse(false)
	var accepted_heading: float = view.requested_heading
	physical_key(KEY_D, true)
	var expected: Variant = ClassDB.instantiate("FreedomBridge")
	check(commit_fixture_new_game(expected, "42") and expected.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, 1.0, accepted_heading])), "Independent C++ station input oracle refused")
	view._process(1.0 / 120.0)
	physical_key(KEY_D, false)
	check(owner.get_freedom_walk_state() == expected.get_freedom_walk_state() and owner.get_freedom_flight_state() == expected.get_freedom_flight_state(), "Actual accepted input diverged from C++ actor/craft tick")
	check(commit_fixture_new_game(owner, "42"), "Could not reset owned input fixture for independent long trace")
	view.update_view()
	# A consumer refusal is terminal; every menu/direct Resume remains paused.
	check(not view.advance_requested(NAN, PackedFloat64Array([0.0, 0.0, heading])), "Nonfinite consumer time accepted")
	var message: String = view.error
	view.toggle_pause()
	view.set_paused(false)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view._process(1.0)
	check(view.paused and not message.is_empty() and view.error == message and view.pause_button.disabled and owner.get_freedom_walk_state() == start, "Terminal error recovered through Resume/focus/process")


func input_map_snapshot() -> Dictionary:
	var result := {}
	for action in InputMap.get_actions():
		var events := []
		for event in InputMap.action_get_events(action): events.append([event.as_text(), event.device])
		result[action] = [InputMap.action_get_deadzone(action), events]
	return result


func check_stick_preferences(view: Control, owner: Variant, path: String, bytes: PackedByteArray) -> void:
	var controls = preload("res://scripts/ui/player_input.gd").new()
	controls.defaults()
	controls.settings.deadzone = 0.35
	controls.settings.curve = 2.0
	var document := {"version": 4, "settings": controls.settings, "bindings": controls.bindings, "extension": [null, true, {"fraction": 1.25}]}
	var config_path := path + ".controls.json"
	var config := FileAccess.open(config_path, FileAccess.WRITE)
	check(config != null, "Station response fixture could not open")
	if config == null: controls.free(); return
	config.store_string(JSON.stringify(document))
	config.close()
	controls.free()
	var config_bytes := FileAccess.get_file_as_bytes(config_path)
	var mapping := input_map_snapshot()
	var old_path: String = view.stick_settings_path
	var old_device: int = view.selected_pad
	var old_devices: Dictionary = view.available_pads.duplicate()
	view.stick_settings_path = config_path
	view.load_stick_preferences()
	check(view.stick_preferences == {"deadzone": 0.35, "curve": 2.0} and input_map_snapshot() == mapping, "Station response failed to read the provider or installed another InputMap")
	view.selected_pad = 11
	view.available_pads = {11: true, 12: true}
	view.set_paused(true)
	joy_axis(JOY_AXIS_LEFT_X, 0.2, 11)
	check(view.current_controls_neutral(), "Configured station dead zone disagreed with neutral gate")
	view.resume_requested()
	check(not view.paused and view.input_controls(0.0)[1] == 0.0, "Below-dead-zone input prevented Resume or requested walking")
	joy_axis(JOY_AXIS_LEFT_X, 0.5, 11)
	check(is_equal_approx(view.input_controls(0.0)[1], 9.0 / 169.0), "Station axis ignored the saved quadratic response")
	joy_axis(JOY_AXIS_LEFT_Y, -0.5, 12)
	check(view.input_controls(0.0)[0] == 0.0, "Foreign pad leaked into configured station shaping")
	view.set_paused(true)
	view.resume_requested()
	check(view.paused and not view.current_controls_neutral(), "Above-dead-zone station input bypassed neutral Resume")
	joy_axis(JOY_AXIS_LEFT_X, 0.0, 11)
	joy_axis(JOY_AXIS_LEFT_Y, 0.0, 12)
	check(FileAccess.get_file_as_bytes(config_path) == config_bytes, "Reading station response rewrote extension fields")
	for invalid in ["{", JSON.stringify({"version": 5}), JSON.stringify({"version": 4, "settings": {"deadzone": -1}, "bindings": {}})]:
		config = FileAccess.open(config_path, FileAccess.WRITE)
		config.store_string(invalid)
		config.close()
		view.load_stick_preferences()
		check(view.stick_preferences == {"deadzone": 0.18, "curve": 1.4} and not view.stick_preferences_status.is_empty() and FileAccess.get_file_as_string(config_path) == invalid, "Rejected station preferences lost defaults, diagnostic or source bytes")
	view.stick_settings_path = config_path + ".missing"
	view.load_stick_preferences()
	check(view.stick_preferences == {"deadzone": 0.18, "curve": 1.4} and view.stick_preferences_status.is_empty(), "Missing station preferences did not use ordinary defaults")
	view.stick_settings_path = old_path
	view.load_stick_preferences()
	view.selected_pad = old_device
	view.available_pads = old_devices
	check(input_map_snapshot() == mapping and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Station preference reads changed mappings or complete C++ save")


func check_station_settings(view: Control, owner: Variant, path: String, bytes: PackedByteArray) -> void:
	var provider: Node = view.preferences_provider
	var original_settings: Dictionary = provider.settings.duplicate(true)
	var original_source: Dictionary = provider.source_document.duplicate(true)
	var original_path: String = provider.settings_path
	var original_persist: bool = provider.persist
	var writable: bool = provider.preferences_writable
	var config_path := path + ".pending-controls.json"
	provider.settings_path = config_path
	provider.persist = true
	provider.preferences_writable = true
	check(not FileAccess.file_exists(config_path), "Station pending test must own a new file")
	view.set_paused(true)
	view.settings_button.grab_focus()
	physical_key(KEY_ENTER, true)
	physical_key(KEY_ENTER, false)
	check(view.settings_open() and not view.hud_scroll.visible and view.paused and not provider.enabled, "Station Settings failed to open with gameplay provider disabled")
	view.settings_view.pending.deadzone = 0.31
	check(provider.settings == original_settings and not FileAccess.file_exists(config_path), "Pending station settings adopted or wrote before Apply")
	view.resume_requested()
	view.open_save_dialog()
	check(view.paused and not view.save_dialog.visible, "Nested station Settings leaked Resume or Save As")
	physical_key(KEY_ESCAPE, true)
	physical_key(KEY_ESCAPE, false)
	for frame in 3: await process_frame
	check(not view.settings_open() and view.paused and root.gui_get_focus_owner() == view.settings_button and provider.settings == original_settings, "Station Cancel resumed, adopted or lost invoking focus")
	var old_device: int = view.selected_pad
	var old_devices: Dictionary = view.available_pads.duplicate()
	view.selected_pad = 41
	view.available_pads = {41: true, 42: true}
	view.open_settings()
	joy_button(JOY_BUTTON_B, true, 42)
	joy_button(JOY_BUTTON_B, false, 42)
	check(view.settings_open() and view.paused, "Foreign pad dismissed station Settings")
	joy_button(JOY_BUTTON_B, true, 41)
	joy_button(JOY_BUTTON_B, false, 41)
	check(not view.settings_open() and view.paused, "Selected station B did not return one level")
	view.open_settings()
	joy_button(JOY_BUTTON_START, true, 41)
	joy_button(JOY_BUTTON_START, false, 41)
	check(not view.settings_open() and view.paused, "Selected station Start resumed from nested Settings")
	view.selected_pad = old_device
	view.available_pads = old_devices
	view.open_settings()
	view.settings_view.pending.deadzone = 0.31
	view.settings_view.apply_pending()
	await process_frame
	check(view.settings_open() and view.paused and is_equal_approx(view.stick_preferences.deadzone, 0.31) and FileAccess.file_exists(config_path), "Station Apply failed to persist/adopt without resuming")
	var persisted := FileAccess.get_file_as_bytes(config_path)
	provider.settings_path = config_path + ".missing/controls.json"
	view.settings_view.pending.curve = 2.1
	var adopted: Dictionary = provider.settings.duplicate(true)
	view.settings_view.apply_pending()
	check(provider.settings == adopted and view.settings_view.pending.curve == 2.1 and FileAccess.get_file_as_bytes(config_path) == persisted, "Failed station Apply lost pending/live/source choices")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.settings_view.apply_pending()
	view.settings_view.cancel()
	check(view.settings_open() and provider.settings == adopted and view.paused, "Unfocused station Settings accepted Apply/Back")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	for pixels in [Vector2i(640, 450), Vector2i(800, 450), Vector2i(1280, 720)]:
		view.settings_view.cancel_button.grab_focus()
		root.size = pixels
		for frame in 4: await process_frame
		check(view.settings_view.scroll.get_global_rect().encloses(view.settings_view.cancel_button.get_global_rect()), "Station Settings Cancel clipped after resize " + str(pixels))
	view.settings_view.cancel()
	for frame in 4: await process_frame
	check(root.gui_get_focus_owner() == view.settings_button and view.hud_scroll.get_global_rect().encloses(view.settings_button.get_global_rect()), "Station Settings return focus clipped")
	view.open_settings()
	provider.settings_path = config_path
	view.settings_view.restore_defaults()
	check(provider.settings == adopted, "Station Restore Defaults adopted before Apply")
	view.settings_view.apply_pending()
	check(provider.settings == provider.DEFAULT_SETTINGS and view.paused, "Station defaults failed or resumed")
	view.settings_view.cancel()
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Station Settings changed complete C++ Save bytes")
	provider.settings = original_settings
	provider.source_document = original_source
	provider.settings_path = original_path
	provider.persist = original_persist
	provider.preferences_writable = writable
	view.stick_preferences = {"deadzone": original_settings.deadzone, "curve": original_settings.curve}
	DirAccess.remove_absolute(config_path)


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		push_error("Expected C++ fresh seed42 journey, walking trace, prepared assets")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var neutral := PackedFloat64Array([0.0, 0.0, 0.0])
	check(owner.get_freedom_walk_state().is_empty() and not owner.advance_freedom_walk(1.0 / 120.0, neutral), "Uninitialized walking accepted")
	var originals := [FileAccess.get_file_as_bytes(args[0]), FileAccess.get_file_as_bytes(args[1])]
	if not commit_fixture_new_game(owner, "42"):
		push_error("Fresh walking New Game failed: " + str(owner.get_last_error()))
		quit(1)
		return
	var start: Dictionary = owner.get_freedom_walk_state()
	if not WalkView.valid_state(start):
		push_error("Fresh walking view invalid")
		quit(1)
		return
	check(not start.continued and start.tick == "0" and start.actor_id == "1", "New Game has no valid fresh station actor")
	check(owner.get_freedom_start().is_empty(), "New walking state also claims a frozen legacy station start")
	var flight: Dictionary = owner.get_freedom_flight_state()
	check(flight.attached and flight.target_port == 1 and flight.station_id == start.station_id and flight.tick == start.tick, "Fresh actor/Wayfarer do not share D1 and time")
	var path := args[0].get_base_dir().path_join("native-walking.json")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[0], "Ordinary New Game bytes differ from independent C++ fresh journey")
	for buffer in [PackedFloat64Array(), PackedFloat64Array([0.0, 0.0]), PackedFloat64Array([0.0, 0.0, 0.0, 0.0]), PackedFloat64Array([NAN, 0.0, 0.0]), PackedFloat64Array([0.0, INF, 0.0]), PackedFloat64Array([1.01, 0.0, 0.0]), PackedFloat64Array([0.0, -1.01, 0.0]), PackedFloat64Array([0.0, 0.0, PI + 0.01])]:
		check(not owner.advance_freedom_walk(1.0 / 120.0, buffer) and owner.get_freedom_walk_state() == start, "Malformed walk buffer changed state/clock")
	for elapsed in [NAN, INF, -0.01, 60.01]:
		check(not owner.advance_freedom_walk(elapsed, neutral) and owner.get_freedom_walk_state() == start, "Malformed walk time changed state")
	check(not owner.release_freedom_port() and not owner.set_freedom_assistance(true) and owner.get_freedom_walk_state() == start, "Outside actor gained flight authority")
	var refusal: String = owner.get_last_error()
	owner.get_freedom_walk_state()
	owner.get_freedom_flight_state()
	owner.get_freedom_station_geometry()
	check(owner.get_last_error() == refusal, "Read-only query erased command refusal")
	for key in ["foot_position_metres", "actor_global_position_metres"]:
		var bad := start.duplicate(true)
		bad[key] = PackedFloat64Array([NAN, 0.0, 0.0])
		check(not WalkView.valid_state(bad), "Walk view accepted nonfinite " + key)
	var malformed := start.duplicate(true)
	malformed["station_basis"] = Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO)
	check(not WalkView.valid_state(malformed), "Walk view accepted collapsed frame")
	var view := WalkView.new()
	physical_key(KEY_W, true)
	root.add_child(view)
	view.set_process(false)
	if not view.initialize(owner, args[2]):
		push_error("Production walking view failed: " + view.error)
		view.free()
		quit(1)
		return
	check(view.station != null and view.station.camera == null and view.ship != null, "Walking lost real ship/station or used an inspection camera")
	check(view.camera.position == start.actor_eye_position and view.station.transform == Transform3D(start.station_basis, start.station_position), "Actor camera/station differs from authoritative projection")
	await check_station_controls(view, owner, path, start, originals[0])
	view.free()
	view = WalkView.new()
	root.add_child(view)
	view.set_process(false)
	check(view.initialize(owner, args[2]) and not view.paused, "Fresh already-neutral view did not retain running startup")
	check(view.light.basis.z.is_equal_approx(flight.star_direction), "Fresh station light substituted a fixed sun")
	var initial_light: Basis = view.light.basis
	view.set_paused(true)
	await check_hud_layout(view, owner, path, originals[0])
	check_stick_preferences(view, owner, path, originals[0])
	await check_station_settings(view, owner, path, originals[0])
	check(view.advance_requested(60.0, neutral) and owner.get_freedom_walk_state() == start, "Paused view advanced shared time")
	view.set_paused(false)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	check(view.paused and not view.focused and view.advance_requested(1.0, neutral) and owner.get_freedom_walk_state() == start, "Lost-focus input advanced walking")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and view.focused, "Focus return resumed time without player action")
	view.set_paused(false)
	for axis in [-1.0, 1.0]:
		for n in 960:
			check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, axis, 0.0])), "Supported walking trace refused")
	view.update_view()
	var final: Dictionary = owner.get_freedom_walk_state()
	check(view.light.basis.z.is_equal_approx(owner.get_freedom_flight_state().star_direction) and not view.light.basis.is_equal_approx(initial_light), "Advanced station light failed to follow the actual C++ clock")
	check(final.tick == "1920" and owner.get_freedom_flight_state().tick == final.tick, "Walk/ship shared clock diverged")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[1], "Native route bytes differ from independent C++ collision/motion trace")
	check(commit_fixture_continue(owner, path), "Walking Continue refused")
	var continued: Dictionary = owner.get_freedom_walk_state()
	final.continued = true
	check(continued == final, "Walking Continue changed actor pose, frame, motion or time")
	view.free()
	view = WalkView.new()
	root.add_child(view)
	view.set_process(false)
	check(view.initialize(owner, args[2]) and view.paused, "Continued walking view resumed without player action")
	check(view.light.basis.z.is_equal_approx(owner.get_freedom_flight_state().star_direction), "Continued station light lost its saved direction")
	check(view.advance_requested(1.0, neutral) and owner.get_freedom_walk_state() == continued, "Continued view advanced while initially paused")
	var other: Variant = ClassDB.instantiate("FreedomBridge")
	check(commit_fixture_new_game(other, "42"), "Cadence walking New Game refused")
	for axis in [-1.0, 1.0]:
		for n in 480:
			check(other.advance_freedom_walk(1.0 / 60.0, PackedFloat64Array([0.0, axis, 0.0])), "Cadence walking refused")
	continued.continued = false
	check(other.get_freedom_walk_state() == continued, "Presentation cadence changed supported walking")
	check(FileAccess.get_file_as_bytes(args[0]) == originals[0] and FileAccess.get_file_as_bytes(args[1]) == originals[1], "Walking modified independent source saves")
	view.free()
	print("Native station walking: %d failures" % failures)
	quit(0 if failures == 0 else 1)
