extends SceneTree
const Controls = preload("res://scripts/ui/player_input.gd")
var failures := 0
var safety_count := 0

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func check_initial_controller() -> void:
	for device in [-1, 19]:
		var inherited := Controls.new()
		inherited.persist = false
		inherited.initial_device = device
		root.add_child(inherited)
		check(inherited.device == device and inherited.needs_neutral, "Controller initialization changed its inherited owner or neutral latch")
		for action in inherited.ACTIONS:
			for event in InputMap.action_get_events("pilot_" + action):
				if event is InputEventJoypadButton or event is InputEventJoypadMotion:
					check(event.device == (device if device >= 0 else 9999), "Inherited controller did not own the installed action map")
		inherited.free()


func run() -> void:
	check_initial_controller()
	check_foot_keyboard_look()
	check_foot_controller_look()
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
	check_request_and_history(controls)
	check_replacement_controller(controls)
	controls.queue_free()
	print("Player input contracts: %d failures (synthetic events; not hardware qualification)" % failures)
	quit(0 if failures == 0 else 1)

func joy_axis(code: int, value: float, device := 0) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = device
	event.axis = code
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func joy_button(code: int, pressed: bool, device := 0) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func physical_key(code: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check_request_and_history(controls: Node) -> void:
	controls.focused = true
	controls.enabled = true
	controls.device = -1
	controls.install()
	controls.observe_neutral()
	controls.assistance_request_only = true
	controls.assist = true
	var requests := []
	controls.assist_requested.connect(func(value: bool): requests.append(value))
	physical_key(KEY_F, true)
	physical_key(KEY_F, false)
	check(requests == [false] and controls.assist, "Request-only assistance fabricated accepted world state")
	# Real held physical keys accumulate across removals; no mocked barrier.
	var held: Array[int] = []
	check(controls.rebind("forward", "key", {"kind": "key", "code": 0x400100}), "History first binding rejected")
	for i in controls.MAX_REMAP_NEUTRAL_EVENTS + 1:
		var code: int = 0x400100 + i
		physical_key(code, true)
		check(Input.is_physical_key_pressed(code), "Held-history fixture did not reach physical input")
		held.append(code)
		check(controls.rebind("forward", "key", {"kind": "key", "code": code + 1}), "History remap rejected")
	check(controls.remap_neutral_events.size() == controls.MAX_REMAP_NEUTRAL_EVENTS and controls.remap_neutral_overflow and not controls.observe_neutral() and controls.needs_neutral, "Held remap history exceeded bound or overflow authorized resume")
	for i in held.size() - 1:
		physical_key(held[i], false)
	check(not controls.observe_neutral() and controls.needs_neutral, "Untracked overflow key disappeared into permission")
	physical_key(held[-1], false)
	# Foreign-pad state used earlier must also be physically released for the
	# deliberately conservative overflow recovery boundary.
	for device in [0, 2, 7]:
		for axis in 6:
			var event := InputEventJoypadMotion.new()
			event.device = device
			event.axis = axis
			event.axis_value = 0.0
			Input.parse_input_event(event)
	Input.flush_buffered_events()
	check(controls.observe_neutral() and not controls.needs_neutral and not controls.remap_neutral_overflow and controls.remap_neutral_events.is_empty(), "Complete physical release did not recover bounded remap history")
	# A previously cleared latch is not a claim about current physical neutrality.
	physical_key(controls.bindings.forward.key.code, true)
	check(not controls.observe_neutral() and not controls.needs_neutral, "Current-neutral observation was replaced by historical latch")
	physical_key(controls.bindings.forward.key.code, false)
	check(controls.observe_neutral(), "Current neutral observation failed after release")


func check_replacement_controller(controls: Node) -> void:
	# Connection signals provide availability; real events still own action state.
	var settings: Dictionary = controls.settings.duplicate(true)
	var bindings: Dictionary = controls.bindings.duplicate(true)
	var pauses := []
	controls.pause_requested.connect(func(): pauses.append(true))
	controls.device = -1
	controls.install()
	Input.joy_connection_changed.emit(0, true)
	Input.joy_connection_changed.emit(7, true)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.6, 7)
	joy_button(JOY_BUTTON_START, true, 7)
	joy_button(JOY_BUTTON_START, false, 7)
	check(controls.device == 0 and controls.sample().thrust_axes[0] == 0 and pauses.is_empty(), "Already-connected foreign pad stole present owner")
	Input.joy_connection_changed.emit(0, false)
	check(controls.device == -1 and controls.needs_neutral, "Selected disconnect lost the neutral barrier")
	controls.focused = false
	joy_button(JOY_BUTTON_START, true, 7)
	joy_button(JOY_BUTTON_START, false, 7)
	check(controls.device == -1, "Unfocused replacement Start adopted a pad")
	controls.focused = true
	var before_safety: int = safety_count
	joy_button(JOY_BUTTON_START, true, 7)
	joy_button(JOY_BUTTON_START, false, 7)
	var adopted: bool = controls.device == 7
	check(adopted, "Already-connected replacement Start did not adopt after selected disconnect")
	if adopted:
		check(safety_count == before_safety + 1 and pauses.is_empty() and controls.needs_neutral, "Selecting Start resumed instead of safety-pausing")
		check(controls.sample().thrust_axes[0] == 0 and controls.needs_neutral, "Held replacement trigger bypassed neutral")
		joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0, 7)
		check(controls.observe_neutral() and not controls.needs_neutral, "Released replacement retained a stale gate")
		joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.6, 7)
		check(controls.sample().thrust_axes[0] > 0, "Adopted replacement axis missed installed mapping")
		joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0, 7)
		joy_axis(JOY_AXIS_TRIGGER_RIGHT, 1, 0)
		joy_button(JOY_BUTTON_START, true, 0)
		joy_button(JOY_BUTTON_START, false, 0)
		check(controls.device == 7 and controls.sample().thrust_axes[0] == 0 and pauses.is_empty(), "Stale disconnected owner regained input authority")
	check(controls.settings == settings and controls.bindings == bindings, "Replacement selection rewrote preferences or remaps")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0, 0)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0, 7)
	Input.joy_connection_changed.emit(7, false)


func check_foot_keyboard_look() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.initial_device = 0
	root.add_child(controls)
	controls.set_enabled(false)
	controls.set_process_input(false)
	var settings: Dictionary = controls.settings.duplicate(true)
	var bindings: Dictionary = controls.bindings.duplicate(true)
	check(controls.foot_keyboard_look() == Vector2.ZERO and controls.foot_keyboard_look_neutral(), "Released on-foot keys requested look")
	for item in [[KEY_LEFT, Vector2.LEFT], [KEY_RIGHT, Vector2.RIGHT], [KEY_UP, Vector2.UP], [KEY_DOWN, Vector2.DOWN]]:
		physical_key(item[0], true)
		check(controls.foot_keyboard_look().is_equal_approx(item[1] * settings.look_speed) and not controls.foot_keyboard_look_neutral(), "On-foot keyboard sign/rate/individual neutral is wrong: " + str(item[0]))
		physical_key(item[0], false)
	physical_key(KEY_RIGHT, true)
	physical_key(KEY_UP, true)
	check(controls.foot_keyboard_look().is_equal_approx(Vector2(1, -1).normalized() * settings.look_speed), "Diagonal on-foot look changed total angular speed")
	physical_key(KEY_LEFT, true)
	physical_key(KEY_DOWN, true)
	check(controls.foot_keyboard_look() == Vector2.ZERO and not controls.foot_keyboard_look_neutral(), "Opposed keyboard look bypassed physical neutral")
	for key in [KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN]: physical_key(key, false)
	check(controls.foot_keyboard_look_neutral() and controls.needs_neutral and not controls.enabled and controls.settings == settings and controls.bindings == bindings, "Read-only foot look armed provider or changed preferences/bindings")
	# The helper is keyboard-only even with both pad look axes and modifier held.
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	joy_axis(JOY_AXIS_RIGHT_Y, -0.6)
	joy_button(JOY_BUTTON_LEFT_STICK, true)
	check(controls.foot_keyboard_look() == Vector2.ZERO and controls.foot_keyboard_look_neutral(), "Pad channels entered keyboard-only foot look")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0)
	joy_button(JOY_BUTTON_LEFT_STICK, false)
	check(controls.apply_preferences({"deadzone": 0.35, "curve": 2.0, "look_speed": 2.4, "invert_look": true, "prompts": 0}), "Valid foot look preferences refused")
	physical_key(KEY_UP, true)
	check(controls.foot_keyboard_look().is_equal_approx(Vector2(0, 2.4)), "Foot look ignored speed/inversion preferences")
	physical_key(KEY_UP, false)
	physical_key(KEY_DOWN, true)
	check(controls.foot_keyboard_look().is_equal_approx(Vector2(0, -2.4)), "Inverted foot down sign is wrong")
	physical_key(KEY_DOWN, false)
	physical_key(KEY_LEFT, true)
	check(controls.foot_keyboard_look().is_equal_approx(Vector2(-2.4, 0)), "Vertical inversion changed horizontal look")
	physical_key(KEY_LEFT, false)
	check(controls.rebind("look_right", "key", {"kind": "key", "code": KEY_J}), "Valid on-foot look remap refused")
	physical_key(KEY_RIGHT, true)
	check(controls.foot_keyboard_look() == Vector2.ZERO and controls.foot_keyboard_look_neutral(), "Old physical look key survived remap")
	physical_key(KEY_RIGHT, false)
	physical_key(KEY_J, true)
	check(controls.foot_keyboard_look().is_equal_approx(Vector2(2.4, 0)) and not controls.foot_keyboard_look_neutral(), "Current mapped look key missed rate or neutral")
	physical_key(KEY_J, false)
	# All are valid flight remaps; fixed on-foot actions must win in both helpers.
	for item in [[KEY_W, "forward"], [KEY_A, "turn_left"], [KEY_S, "backward"], [KEY_D, "turn_right"], [KEY_E, "strafe_right"]]:
		controls.defaults()
		controls.install()
		check(controls.rebind(item[1], "key", {"kind": "key", "code": KEY_T}) and controls.rebind("look_right", "key", {"kind": "key", "code": item[0]}), "Fixed-key collision did not use a valid flight remap")
		check(Controls.valid_document({"version": 4, "settings": controls.settings, "bindings": controls.bindings}), "Fixed-key collision fixture violates the existing profile schema")
		physical_key(item[0], true)
		check(controls.foot_keyboard_look() == Vector2.ZERO and controls.foot_keyboard_look_neutral(), "Fixed walking/boarding key also looked or blocked look-neutral: " + str(item[0]))
		physical_key(item[0], false)
	controls.free()


func check_foot_controller_look() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.initial_device = 0
	root.add_child(controls)
	controls.set_enabled(false)
	controls.set_process_input(false)
	var bindings: Dictionary = controls.bindings.duplicate(true)
	joy_axis(JOY_AXIS_RIGHT_X, 1.0)
	joy_axis(JOY_AXIS_RIGHT_Y, -1.0)
	check(controls.foot_controller_look(controls.device).is_equal_approx(Vector2(1.8, -1.8)), "Default on-foot stick rate or per-axis diagonal shaping changed")
	check(controls.apply_preferences({"deadzone": 0.35, "curve": 2.0, "look_speed": 2.4, "invert_look": true, "prompts": 0}), "On-foot stick preferences refused")
	check(controls.foot_controller_look(controls.device).is_equal_approx(Vector2(2.7, 2.7)), "On-foot stick ignored sensitivity or vertical-only inversion")
	joy_axis(JOY_AXIS_RIGHT_X, 0.675)
	joy_axis(JOY_AXIS_RIGHT_Y, -0.35)
	check(controls.foot_controller_look(controls.device).is_equal_approx(Vector2(0.675, 0)), "On-foot stick lost configured per-axis deadzone/curve")
	joy_axis(JOY_AXIS_RIGHT_X, 0.0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0.0)
	joy_axis(JOY_AXIS_RIGHT_X, 1.0, 7)
	joy_axis(JOY_AXIS_RIGHT_Y, -1.0, 7)
	check(controls.foot_controller_look(controls.device) == Vector2.ZERO, "Foreign pad entered on-foot stick look")
	controls.device = -1
	check(controls.foot_controller_look(controls.device) == Vector2.ZERO and controls.needs_neutral and not controls.enabled and controls.bindings == bindings, "Read-only stick look armed input or changed mappings")
	joy_axis(JOY_AXIS_RIGHT_X, 0.0, 7)
	joy_axis(JOY_AXIS_RIGHT_Y, 0.0, 7)
	controls.free()
