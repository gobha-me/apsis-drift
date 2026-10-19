extends SceneTree
const Controls = preload("res://scripts/ui/player_input.gd")
const Title = preload("res://scripts/native/native_title.gd")
var failures := 0

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	call_deferred("run")

func settle() -> void:
	for frame in 4: await process_frame

func key(code: Key) -> void:
	for down in [true, false]:
		var event := InputEventKey.new()
		event.physical_keycode = code
		event.keycode = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame

func pad(code: JoyButton) -> void:
	for down in [true, false]:
		var event := InputEventJoypadButton.new()
		event.device = 0
		event.button_index = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame

func write(path: String, value: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	check(file != null, "Could not create isolated controls fixture")
	if file != null: file.store_string(value); file.close()

func physical(code: Key, down: bool, echo := false) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = down
	event.echo = echo
	Input.parse_input_event(event)
	await process_frame

func walking_settings(title: Control, controls: Node, view: Control, path: String) -> void:
	var before: Dictionary = controls.settings.duplicate(true)
	var source: Dictionary = controls.source_document.duplicate(true)
	var original := FileAccess.get_file_as_bytes(path)
	for bad in [KEY_S, KEY_ESCAPE, KEY_V, KEY_F3, KEY_E, KEY_ENTER, KEY_KP_ENTER, KEY_SPACE, 0, -1, 0x800000, 65.5, NAN, INF, true, "T"]:
		var invalid := before.duplicate(true)
		invalid.walk_forward = bad
		check(not controls.apply_preferences(invalid) and controls.settings == before and controls.source_document == source and FileAccess.get_file_as_bytes(path) == original, "Invalid/duplicate walking key changed live/file: " + str(bad))
	var custom := before.duplicate(true)
	custom.merge({"walk_forward": KEY_T, "walk_backward": KEY_G, "walk_left": KEY_F, "walk_right": KEY_H}, true)
	check(not controls.apply_preferences(custom, "user://missing-parent/walk.json") and controls.settings == before and controls.source_document == source and controls.foot_remap_keys.is_empty(), "Failed walking write adopted candidate/barrier")
	check(not controls.apply_preferences(custom, "user://rename-target") and controls.settings == before and controls.source_document == source, "Failed walking rename adopted live candidate")

	# Real UI capture is pending only and cannot route reserved actions or UI
	# activation, echoes, pointer or pad events into a background provider.
	view.refresh_fields()
	var pause_calls := [0]
	var on_pause := func(): pause_calls[0] += 1
	controls.pause_requested.connect(on_pause)
	view.fields.walk_forward.control.pressed.emit()
	check(view.capturing_walk_field == "walk_forward" and not controls.is_processing_input(), "Walking capture did not own input")
	await physical(KEY_T, true, true)
	await physical(KEY_T, false)
	await pad(JOY_BUTTON_A)
	var mouse := InputEventMouseButton.new()
	mouse.button_index = MOUSE_BUTTON_LEFT
	mouse.pressed = true
	Input.parse_input_event(mouse)
	await process_frame
	mouse.pressed = false
	Input.parse_input_event(mouse)
	await process_frame
	check(view.capturing_walk_field == "walk_forward" and view.pending == before and controls.settings == before, "Echo/pad/mouse changed capture")
	for reserved in [KEY_V, KEY_F3, KEY_E, KEY_ENTER, KEY_KP_ENTER, KEY_SPACE, KEY_S]:
		await key(reserved)
		check(view.capturing_walk_field == "walk_forward" and view.pending == before and pause_calls[0] == 0 and view.visible, "Reserved/duplicate capture escaped into UI/background")
	await key(KEY_ESCAPE)
	check(view.capturing_walk_field.is_empty() and view.visible and view.pending == before and controls.is_processing_input() and pause_calls[0] == 0, "Esc cancelled Settings/applied instead of cancelling capture")
	for item in [["walk_forward", KEY_T], ["walk_backward", KEY_G], ["walk_left", KEY_F], ["walk_right", KEY_H]]:
		view.fields[item[0]].control.pressed.emit()
		await key(item[1])
		check(view.capturing_walk_field.is_empty() and view.pending[item[0]] == item[1], "Physical key capture did not change pending field")
	check(controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Key capture adopted/wrote before Apply")
	view.cancel()
	check(controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Cancelled walking edits changed live/file")
	title.open_settings()
	view.begin_walk_capture("walk_forward")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	await key(KEY_T)
	check(view.capturing_walk_field.is_empty() and view.pending == before and controls.settings == before and view.visible, "Focus loss retained capture or remapped in background")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	controls.pause_requested.disconnect(on_pause)
	view.pending = custom.duplicate(true)
	view.refresh_fields()
	# Both the previously mapped W and newly mapped T are held at Apply.
	await physical(KEY_W, true)
	await physical(KEY_T, true)
	view.apply_pending()
	check(controls.settings == custom and controls.foot_keyboard_label() == "T/F/G/H" and not controls.enabled and controls.needs_neutral, "Apply failed custom walking keys or armed live input")
	check(controls.foot_keyboard_move() == Vector2(0, 1) and not controls.foot_keyboard_move_neutral() and KEY_W in controls.foot_remap_keys, "New movement/old held-key barrier missing")
	await physical(KEY_T, false)
	check(controls.foot_keyboard_move() == Vector2.ZERO and not controls.foot_keyboard_move_neutral(), "Old held key disappeared across Apply")
	await physical(KEY_W, false)
	check(controls.foot_keyboard_move_neutral() and controls.foot_remap_keys.is_empty(), "Walking release barrier did not recover")
	var applied: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(applied.extension == source.extension and applied.settings.extension == source.settings.extension and applied.bindings == source.bindings, "Walking Apply dropped unknown extensions/bindings")
	var reader := Controls.new()
	reader.defaults()
	reader.settings_path = path
	reader.load_settings()
	check(reader.foot_key_codes() == controls.foot_key_codes(), "Custom walking keys did not survive reload")
	reader.free()
	var scalar_only := custom.duplicate(true)
	for field in Controls.WALK_FIELDS: scalar_only.erase(field)
	scalar_only.look_speed = 2.0
	check(controls.apply_preferences(scalar_only) and controls.foot_key_codes() == [KEY_T, KEY_G, KEY_F, KEY_H], "Historical five-scalar candidate reset custom movement")
	view.pending = controls.settings.duplicate(true)
	view.restore_defaults()
	check(view.pending.walk_forward == KEY_W and controls.settings.walk_forward == KEY_T, "Restore walking defaults applied before Apply")
	view.cancel()
	# Configured movement overrides flight camera/recenter/assist and look.
	var overlaps: Dictionary = controls.settings.duplicate(true)
	overlaps.merge({"walk_left": KEY_C, "walk_right": KEY_HOME}, true)
	check(controls.apply_preferences(overlaps), "Valid flight-overlapping movement key rejected")
	controls.install()
	controls.surface_interaction_enabled = true
	controls.set_enabled(true)
	controls.needs_neutral = false
	var actions := [0, 0, 0]
	var on_camera := func(): actions[0] += 1
	var on_recenter := func(): actions[1] += 1
	var on_assist := func(_value: bool): actions[2] += 1
	controls.camera_requested.connect(on_camera)
	controls.recenter_requested.connect(on_recenter)
	controls.assist_requested.connect(on_assist)
	for code in [KEY_C, KEY_HOME]:
		await physical(code, true)
		check(controls.foot_keyboard_move().x == (-1.0 if code == KEY_C else 1.0), "Flight-overlap key failed actual movement polling")
		await physical(code, false)
	# F is flight assist, and now walking-left again.
	overlaps.walk_left = KEY_F
	check(controls.apply_preferences(overlaps), "Valid assist-overlapping movement rejected")
	controls.install()
	controls.needs_neutral = false
	await physical(KEY_F, true)
	await physical(KEY_F, false)
	check(actions == [0, 0, 0], "Walking key fired flight camera/recenter/assist")
	controls.camera_requested.disconnect(on_camera)
	controls.recenter_requested.disconnect(on_recenter)
	controls.assist_requested.disconnect(on_assist)
	var look_bindings: Dictionary = controls.bindings.duplicate(true)
	controls.bindings.look_left.key.code = KEY_T
	await physical(KEY_T, true)
	check(controls.foot_keyboard_look() == Vector2.ZERO and controls.foot_keyboard_look_neutral() and not controls.foot_keyboard_move_neutral(), "Configured movement also activated on-foot look")
	await physical(KEY_T, false)
	controls.bindings.look_left.key.code = KEY_W
	await physical(KEY_W, true)
	check(controls.foot_keyboard_look().x < 0 and not controls.foot_keyboard_look_neutral() and controls.foot_keyboard_move() == Vector2.ZERO, "Old default W stayed excluded after movement remap")
	await physical(KEY_W, false)
	controls.bindings = look_bindings
	controls.surface_interaction_enabled = false
	controls.set_enabled(false)
	check(controls.apply_preferences(before), "Walking test did not restore default settings")
	controls.install()
	title.open_settings()

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy": quit(1); return
	var title := Title.new()
	title.persist_controls = false
	root.add_child(title)
	await settle()
	var controls: Node = title.controls
	var defaults: Dictionary = controls.settings.duplicate(true)
	var bindings: Dictionary = controls.bindings.duplicate(true)
	var path := "user://settings-contract.json"
	check(not FileAccess.file_exists(path), "Isolated preferences unexpectedly exist")
	var document := {"version": 4, "settings": defaults.duplicate(true), "bindings": bindings.duplicate(true), "extension": {"list": [null, true, "future", 4.5]}}
	document.settings.extension = {"meaning": "unowned", "value": [1, 2]}
	document.bindings.future_action = {"payload": false}
	document.bindings.forward.extension = "keep"
	# Historical v4 files contain only the original scalar settings.
	for field in Controls.WALK_FIELDS: document.settings.erase(field)
	write(path, JSON.stringify(document))
	document = JSON.parse_string(FileAccess.get_file_as_string(path))
	controls.settings_path = path
	controls.persist = true
	controls.load_settings()
	check(controls.foot_key_codes() == [KEY_W, KEY_S, KEY_A, KEY_D] and controls.foot_keyboard_label() == "WASD", "Historical v4 omission did not default to WASD")
	var original := FileAccess.get_file_as_bytes(path)
	var before: Dictionary = controls.settings.duplicate(true)
	for bad in [NAN, INF, -1.0, 0.46, "0.2", true]:
		var candidate := before.duplicate(true)
		candidate.deadzone = bad
		check(not controls.apply_preferences(candidate), "Invalid dead zone applied")
		check(controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Invalid candidate mutated live/file")
	check(not controls.apply_preferences({}), "Incomplete preferences applied")
	for item in [["curve", 0.9], ["curve", 3.1], ["look_speed", 0.2], ["look_speed", INF], ["invert_look", 1], ["prompts", 1.5], ["prompts", 3]]:
		var invalid := before.duplicate(true)
		invalid[item[0]] = item[1]
		check(not controls.apply_preferences(invalid) and controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Invalid owned setting changed live/file: " + str(item))
	var candidate := before.duplicate(true)
	candidate.deadzone = 0.35
	check(not controls.apply_preferences(candidate, "user://missing-parent/settings.json"), "Unwritable path applied")
	check(controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Failed write adopted settings")
	DirAccess.make_dir_absolute("user://rename-target")
	check(not controls.apply_preferences(candidate, "user://rename-target"), "Directory target replaced")
	check(not FileAccess.file_exists("user://rename-target.tmp") and controls.settings == before, "Failed rename left temporary file/live changes")
	title.settings_button.grab_focus()
	await key(KEY_ENTER)
	var view: Control = title.settings_view
	check(view.visible and not title.panel.visible and view.fields.deadzone.control.has_focus(), "Settings opening/focus failed")
	view.fields.deadzone.control.value = 0.35
	view.fields.curve.control.value = 2.0
	view.fields.look_speed.control.value = 2.4
	view.fields.invert_look.control.button_pressed = true
	view.fields.prompts.control.item_selected.emit(2)
	check(is_equal_approx(view.pending.deadzone, 0.35) and view.pending.curve == 2.0 and is_equal_approx(view.pending.look_speed, 2.4) and view.pending.invert_look and view.pending.prompts == 2, "UI fields did not update their own pending values")
	check(controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Editing wrote/applied before Apply")
	await key(KEY_ESCAPE)
	check(not view.visible and title.settings_button.has_focus() and controls.settings == before and FileAccess.get_file_as_bytes(path) == original, "Cancel mutated settings/file or lost invoking focus")
	title.open_settings()
	view.fields.deadzone.control.value = 0.35
	view.apply_button.pressed.emit()
	check(is_equal_approx(controls.settings.deadzone, 0.35) and not controls.enabled and controls.needs_neutral, "Apply did not adopt settings or armed flight")
	var applied: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(applied.extension == document.extension and applied.settings.extension == document.settings.extension and applied.bindings == document.bindings, "Apply dropped unowned extensions/bindings")
	check(is_equal_approx(InputMap.action_get_deadzone("pilot_forward"), 0.35), "Successful Apply did not install input dead zone")
	var applied_bytes := FileAccess.get_file_as_bytes(path)
	view.restore_defaults()
	check(view.pending.deadzone == defaults.deadzone and is_equal_approx(controls.settings.deadzone, 0.35) and FileAccess.get_file_as_bytes(path) == applied_bytes, "Restore Defaults applied/wrote before Apply")
	view.apply_pending()
	applied = JSON.parse_string(FileAccess.get_file_as_string(path))
	check(controls.settings.deadzone == defaults.deadzone and applied.extension == document.extension and applied.settings.extension == document.settings.extension and applied.bindings == document.bindings, "Applied defaults erased extensions/bindings")
	await walking_settings(title, controls, view, path)
	controls.device = 0
	controls.install()
	await pad(JOY_BUTTON_B)
	check(not view.visible and title.settings_button.has_focus(), "Controller Back did not cancel Settings")
	await pad(JOY_BUTTON_A)
	check(view.visible, "Controller could not reopen Settings")
	await pad(JOY_BUTTON_START)
	check(not view.visible and title.settings_button.has_focus(), "Start did not leave Settings")
	for size in [Vector2i(1280, 720), Vector2i(800, 450), Vector2i(640, 450)]:
		root.size = size
		title.open_settings()
		await settle()
		check(root.get_visible_rect().encloses(view.status.get_global_rect()), "Settings status clipped at " + str(size))
		for field in view.fields.values():
			field.control.grab_focus()
			await settle()
			check(view.scroll.get_global_rect().encloses(field.control.get_global_rect()), "Focused %s clipped at %s: field=%s scroll=%s" % [field.control.get_class(), size, field.control.get_global_rect(), view.scroll.get_global_rect()])
		for button in [view.defaults_button, view.apply_button, view.cancel_button]:
			button.grab_focus()
			await settle()
			check(view.scroll.get_global_rect().encloses(button.get_global_rect()), "Focused settings action clipped at " + str(size))
		view.cancel()
	title.open_settings()
	view.fields.curve.control.value = 2.0
	var frozen := FileAccess.get_file_as_bytes(path)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.apply_pending()
	view.cancel()
	check(view.visible and FileAccess.get_file_as_bytes(path) == frozen and controls.settings.curve == defaults.curve, "Unfocused settings accepted")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.cancel()
	for bad in ["{broken", JSON.stringify({"version": 5}), "x".repeat(65537)]:
		var reader := Controls.new()
		reader.defaults()
		reader.settings_path = path
		write(path, bad)
		var rejected_bytes := FileAccess.get_file_as_bytes(path)
		reader.load_settings()
		check(not reader.preferences_writable and not reader.apply_preferences(defaults), "Rejected source became writable")
		view.controls = reader
		view.open()
		view.restore_defaults()
		check(view.apply_button.disabled, "Rejected source left Apply enabled")
		view.apply_pending()
		check(FileAccess.get_file_as_bytes(path) == rejected_bytes, "Rejected source bytes were replaced")
		view.cancel()
		view.controls = controls
		reader.free()
	DirAccess.remove_absolute(path)
	DirAccess.remove_absolute("user://rename-target")
	title.free()
	await settle()
	print("Control settings: %d failures" % failures)
	quit(0 if failures == 0 else 1)
