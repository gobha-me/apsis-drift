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
	write(path, JSON.stringify(document))
	document = JSON.parse_string(FileAccess.get_file_as_string(path))
	controls.settings_path = path
	controls.persist = true
	controls.load_settings()
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
