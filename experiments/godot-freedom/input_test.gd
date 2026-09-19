extends SceneTree
const Controls = preload("res://player_input.gd")
var failures := 0
var safety_count := 0

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	root.add_child(controls)
	controls.safety_pause.connect(func(_reason: String): safety_count += 1)
	var document := {"version": 4, "settings": controls.settings.duplicate(true), "bindings": controls.bindings.duplicate(true)}
	check(Controls.valid_document(document), "Default controls invalid")
	check(Controls.valid_document(JSON.parse_string(JSON.stringify(document))), "JSON round trip invalid")
	var old_document: Dictionary = document.duplicate(true)
	old_document.version = 3
	check(not Controls.valid_document(old_document), "Old profile accepted as new layout")
	for bad in [null, {}, {"version": 2}, {"version": 1, "settings": [], "bindings": {}}]:
		check(not Controls.valid_document(bad), "Malformed settings accepted")
	for value in [NAN, INF, -0.1, 0, 0.99, "0.18", true]:
		var bad: Dictionary = document.duplicate(true)
		bad.settings.deadzone = value
		check(not Controls.valid_document(bad), "Invalid deadzone accepted")
	for value in [NAN, INF, -1, 10000000, 1.5, "3", true]:
		var bad: Dictionary = document.duplicate(true)
		bad.bindings.forward.key.code = value
		check(not Controls.valid_document(bad), "Invalid key accepted")
	var duplicate: Dictionary = document.duplicate(true)
	duplicate.bindings.forward.pad = duplicate.bindings.backward.pad
	check(not Controls.valid_document(duplicate), "Duplicate binding accepted")
	check(Controls.shape(NAN, 0.18, 1.4) == 0, "Nonfinite axis escaped")
	check(Controls.shape(0.1, 0.18, 1.4) == 0, "Drift escaped deadzone")
	check(Controls.shape(1, 0.18, 1.4) == 1, "Full range lost")
	check(Controls.shape(-1, 0.18, 1.4) == -1, "Negative range lost")
	check(Controls.shape(0.5, 0.18, 1.4) > 0 and Controls.shape(0.5, 0.18, 1.4) < 1, "Fractional input collapsed")
	controls.sample()
	check(not controls.needs_neutral, "Neutral gate not cleared")
	Input.action_press("pilot_forward", 0.5)
	var axes: PackedFloat64Array = controls.sample().axes
	check(axes[0] > 0 and axes[0] < 1, "Analog action not proportional")
	controls.set_enabled(false)
	check(controls.sample().axes == PackedFloat64Array([0, 0, 0, 0]), "Menu leaked input")
	controls.set_enabled(true)
	Input.action_press("pilot_backward", 0.5)
	controls.sample()
	check(controls.needs_neutral, "Opposing held controls bypassed neutral gate")
	Input.action_release("pilot_forward")
	Input.action_release("pilot_backward")
	controls.sample()
	check(not controls.needs_neutral, "Released controls still blocked")
	check(not controls.rebind("forward", "key", {"kind": "key", "code": KEY_ESCAPE}), "Reserved pause rebound")
	check(not controls.rebind("forward", "pad", Controls.button(JOY_BUTTON_A)), "Menu accept rebound")
	check(not controls.rebind("forward", "key", document.bindings.backward.key), "Conflict accepted")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_T}), "Valid rebind rejected")
	check(controls.bindings.forward.pad == document.bindings.forward.pad, "Key rebind removed pad mapping")
	var test_path := "user://input-contract-%d-%d.json" % [OS.get_process_id(), Time.get_ticks_usec()]
	check(not FileAccess.file_exists(test_path), "Test settings path unexpectedly exists")
	controls.persist = true
	controls.save_settings(test_path)
	controls.persist = false
	controls.defaults()
	controls.load_settings(test_path)
	check(controls.bindings.forward.key.code == KEY_T, "Saved binding did not reload")
	check(DirAccess.remove_absolute(test_path) == OK, "Could not clean test-owned settings fixture")
	controls.settings.prompts = 2
	check(controls.binding_label("camera", "pad") == "Square", "PS prompt override failed")
	controls.settings.prompts = 1
	check(controls.binding_label("camera", "pad") == "X", "Xbox prompt override failed")
	controls.device = 0
	controls.install()
	controls.sample()
	var joy := InputEventJoypadMotion.new()
	joy.device = 0
	joy.axis = JOY_AXIS_TRIGGER_RIGHT
	joy.axis_value = 0.6
	Input.parse_input_event(joy)
	Input.flush_buffered_events()
	check(controls.sample().axes[0] > 0, "Joypad event did not reach named action")
	joy = joy.duplicate()
	joy.axis_value = 0
	Input.parse_input_event(joy)
	Input.flush_buffered_events()
	var other := InputEventJoypadMotion.new()
	other.device = 7
	other.axis = JOY_AXIS_TRIGGER_RIGHT
	other.axis_value = 1
	Input.parse_input_event(other)
	Input.flush_buffered_events()
	check(controls.sample().axes[0] == 0, "Second controller took over flight")
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	var moving: Dictionary = controls.sample()
	check(moving.axes[1] > 0 and moving.axes[2] == 0 and moving.look == Vector2.ZERO, "Right stick should yaw without modifier")
	Input.action_press("pilot_look_hold", 0.8)
	var looking: Dictionary = controls.sample()
	check(looking.axes[0] == 0 and looking.axes[1] == 0 and looking.look.x > 0, "Look modifier leaked yaw input")
	Input.action_press("pilot_look_hold", 0.45)
	check(controls.sample().look.x > 0, "Modifier hysteresis dropped look early")
	Input.action_release("pilot_look_hold")
	var released: Dictionary = controls.sample()
	check(released.recenter and released.look == Vector2.ZERO and released.axes[1] == 0, "Release failed to snap and suppress held yaw")
	check(not controls.sample().recenter and controls.sample().axes[1] == 0, "Snap repeated or held stick yawed ship")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	controls.sample()
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	check(controls.sample().axes[1] > 0, "Yaw failed to rearm after centering")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	controls.sample()
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.6)
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0.4)
	var both: PackedFloat64Array = controls.sample().thrust_axes
	check(both[0] > both[1] and both[1] > 0, "Independent analog engines collapsed into signed speed")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0)
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0)
	joy_axis(JOY_AXIS_LEFT_Y, 0.7)
	check(controls.sample().thrust_axes[2] > 0, "Pull-back pitch missing")
	joy_axis(JOY_AXIS_LEFT_Y, 0)
	joy_axis(JOY_AXIS_LEFT_X, 0.7)
	var roll: PackedFloat64Array = controls.sample().thrust_axes
	check(roll[4] > 0 and roll[4] < 1 and roll[3] == 0 and roll[5] == 0, "Direct analog roll missing or leaking yaw/strafe")
	joy_axis(JOY_AXIS_RIGHT_X, 0.6)
	joy_axis(JOY_AXIS_RIGHT_Y, -0.5)
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, true)
	var simultaneous: PackedFloat64Array = controls.sample().thrust_axes
	check(simultaneous[3] > 0 and simultaneous[4] > 0 and simultaneous[5] == 1 and simultaneous[6] > 0, "Independent yaw/roll/strafe/heave unavailable")
	joy_button(JOY_BUTTON_LEFT_STICK, true)
	var priority: Dictionary = controls.sample()
	check(priority.look.x > 0 and priority.look.y < 0 and priority.thrust_axes[3] == 0 and priority.thrust_axes[6] == 0, "Stick-click look leaked yaw/heave")
	check(priority.thrust_axes[4] > 0 and priority.thrust_axes[5] == 1, "Look blocked independent roll/bumper thrust")
	joy_button(JOY_BUTTON_LEFT_STICK, false)
	check(controls.sample().recenter and controls.sample().thrust_axes[3] == 0 and controls.sample().thrust_axes[6] == 0, "Look release leaked held yaw/heave")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0)
	controls.sample()
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	check(controls.sample().thrust_axes[3] > 0, "Look context failed to rearm while roll/bumper held")
	joy_axis(JOY_AXIS_LEFT_X, 0)
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, false)
	joy_button(JOY_BUTTON_LEFT_SHOULDER, true)
	check(controls.sample().thrust_axes[5] == -1, "Left bumper does not strafe left")
	joy_button(JOY_BUTTON_LEFT_SHOULDER, false)
	controls.connection_changed(0, false)
	check(safety_count == 1 and controls.device == -1 and controls.needs_neutral, "Disconnect not safe")
	controls.connection_changed(2, true)
	check(safety_count == 2 and controls.device == 2, "Reconnect not gated")
	controls.focused = false
	Input.action_press("pilot_forward", 1)
	check(controls.sample().axes[0] == 0, "Unfocused input leaked")
	Input.action_release("pilot_forward")
	controls.queue_free()
	print("Player input contracts: %d failures (synthetic events; not hardware qualification)" % failures)
	quit(0 if failures == 0 else 1)

func joy_axis(code: int, value: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = 0
	event.axis = code
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func joy_button(code: int, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = 0
	event.button_index = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
