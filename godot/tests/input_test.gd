extends SceneTree
const Controls = preload("res://scripts/ui/player_input.gd")
const FlightMouse = preload("res://scripts/native/native_flight_mouse.gd")
const ViewportSize = preload("res://scripts/native/native_viewport_size.gd")
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
	check_viewport_dimensions()
	check_initial_controller()
	check_flight_mouse()
	check_rebind_transaction()
	check_foot_keyboard_look()
	check_foot_controller_look()
	check_surface_interaction()
	check_landed_hatch_neutral()
	check_cockpit_interaction()
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


func check_viewport_dimensions() -> void:
	for bad in [Vector2.ZERO, Vector2(-1, 720), Vector2(1280, 0), Vector2(NAN, 720), Vector2(1280, INF), Vector2(1e20, 720)]:
		check(ViewportSize.target(bad, Vector2.ONE) == Vector2i.ZERO, "Invalid render dimensions escaped validation")
	for bad in [Vector2.ZERO, Vector2(-1, 1), Vector2(NAN, 1), Vector2(1, INF)]:
		check(ViewportSize.target(Vector2(1920, 1080), bad) == Vector2i.ZERO, "Invalid output density escaped validation")
	check(ViewportSize.target(Vector2(1920, 1080), Vector2.ONE * (2.0 / 3.0)) == Vector2i(1280, 720), "Windowed render target still supersamples logical UI pixels")
	check(ViewportSize.target(Vector2(1920, 1080), Vector2.ONE * 2.0) == Vector2i(1920, 1080), "Larger output increased the existing rendering budget")
	check(ViewportSize.target(Vector2(1920, 1080), Vector2.ONE * 1e20) == Vector2i(1920, 1080), "Large finite output density overflowed before clamping")
	check(ViewportSize.target(Vector2.ONE, Vector2.ONE * 0.01) == Vector2i.ONE, "Small positive target collapsed to zero")
	var container := SubViewportContainer.new()
	container.position = Vector2(3, 4)
	container.size = Vector2(100, 100)
	var before := [container.position, container.size, container.scale]
	check(not ViewportSize.fit(container, Vector2(NAN, 720), Vector2.ONE) and [container.position, container.size, container.scale] == before, "Rejected render dimensions changed the accepted layout")
	container.free()


func check_flight_mouse() -> void:
	var mouse := FlightMouse.new()
	var other := FlightMouse.new()
	var settings: Dictionary = Controls.DEFAULT_SETTINGS.duplicate(true)
	var unchanged := settings.duplicate(true)
	var dimensions := Vector2(1280, 720)
	check(mouse.mouse_filter == Control.MOUSE_FILTER_IGNORE and mouse.focus_mode == Control.FOCUS_NONE, "Mouse steering intercepted pointer or keyboard focus")
	check(mouse.offset == Vector2.ZERO and mouse.demands(settings) == Vector2.ZERO, "Mouse steering did not start neutral")
	check(mouse.motion(Vector2(24, -12), dimensions, settings), "Ordinary captured relative mouse travel refused")
	var demand: Vector2 = mouse.demands(settings)
	check(demand.x > 0 and demand.y > 0 and demand.length() < 1, "Right/up mouse travel did not command bounded positive yaw/pitch")
	check(mouse.demands(settings) == demand, "Reading virtual stick aged or consumed its demand")
	check(other.motion(Vector2(48, -24), dimensions * 2, settings) and other.offset.is_equal_approx(mouse.offset) and other.demands(settings).is_equal_approx(demand), "Equivalent relative viewport travel changed steering")
	other.clear()
	check(other.motion(Vector2(12, -6), dimensions, settings) and other.motion(Vector2(12, -6), dimensions, settings) and other.demands(settings).is_equal_approx(demand), "Splitting one mouse gesture changed unsaturated steering")
	check(other.motion(Vector2(-24, 12), dimensions, settings) and other.demands(settings) == Vector2.ZERO, "Opposite mouse travel failed to recenter virtual stick")
	var inverted := settings.duplicate(true)
	inverted.invert_look = true
	var reversed: Vector2 = mouse.demands(inverted)
	check(is_equal_approx(reversed.x, -demand.x) and is_equal_approx(reversed.y, demand.y), "Mouse inversion changed yaw or failed to reverse pitch")
	var curved := settings.duplicate(true)
	curved.curve = 3.0
	var linear := settings.duplicate(true)
	linear.curve = 1.0
	check(mouse.demands(curved).length() < mouse.demands(linear).length(), "Existing response curve did not soften partial mouse steering")
	other.clear()
	var fast := settings.duplicate(true)
	fast.look_speed = 3.2
	check(other.motion(Vector2(24, -12), dimensions, fast) and other.demands(fast).length() > demand.length(), "Mouse sensitivity did not change travel response")
	mouse.clear()
	check(mouse.motion(Vector2(9, 0), dimensions, settings) and mouse.demands(settings) == Vector2.ZERO, "Small mouse offset escaped the fixed steering deadzone")
	check(mouse.motion(Vector2(12, 0), dimensions, settings) and mouse.demands(settings).y > 0, "Mouse steering failed to leave its deadzone")
	mouse.clear()
	check(mouse.motion(Vector2(10000, -10000), dimensions, settings), "Finite large mouse travel failed to saturate")
	var diagonal: Vector2 = mouse.demands(settings)
	check(mouse.offset.length() <= 1.000001 and diagonal.length() <= 1.000001 and diagonal.x > 0.6 and diagonal.x < 0.8 and diagonal.y > 0.6 and diagonal.y < 0.8, "Diagonal virtual stick exceeded radial saturation or doubled full-scale intent")
	mouse.clear()
	check(mouse.offset == Vector2.ZERO and mouse.demands(settings) == Vector2.ZERO, "Explicit mouse reset retained steering")
	check(mouse.motion(Vector2(-24, 12), Vector2i(1280, 720), settings) and mouse.demands(settings).x < 0 and mouse.demands(settings).y < 0, "Integer viewport or negative steering signs refused")
	for bad in [null, [], "1280x720", Vector3.ONE, Vector2.ZERO, Vector2(0, 720), Vector2(-1, 720), Vector2(1280, -1), Vector2(0.25, 720), Vector2(NAN, 720), Vector2(1280, INF), Vector2(65537, 720)]:
		mouse.clear()
		check(mouse.motion(Vector2(24, -12), dimensions, settings), "Invalid-dimension fixture could not establish prior mouse intent")
		check(not mouse.motion(Vector2.ONE, bad, settings) and mouse.offset == Vector2.ZERO and mouse.demands(settings) == Vector2.ZERO, "Invalid viewport retained mouse intent")
	for bad in [null, [], "mouse", Vector3.ONE, Vector2(NAN, 0), Vector2(0, INF), Vector2(1000001, 0), Vector2(0, -1000001)]:
		check(mouse.motion(Vector2(24, -12), dimensions, settings), "Invalid-delta fixture could not establish prior mouse intent")
		check(not mouse.motion(bad, dimensions, settings) and mouse.offset == Vector2.ZERO, "Invalid or excessive mouse delta retained steering")
	var malformed: Array = [{}]
	for item in [["look_speed", null], ["look_speed", true], ["look_speed", NAN], ["look_speed", INF], ["look_speed", 0.29], ["look_speed", 4.01], ["curve", false], ["curve", "1.4"], ["curve", NAN], ["curve", 0.99], ["curve", 3.01], ["invert_look", 1]]:
		var bad: Dictionary = settings.duplicate(true)
		bad[item[0]] = item[1]
		malformed.append(bad)
	for bad in malformed:
		check(mouse.motion(Vector2(24, -12), dimensions, settings), "Invalid-settings fixture could not establish prior mouse intent")
		check(not mouse.motion(Vector2.ONE, dimensions, bad) and mouse.offset == Vector2.ZERO, "Malformed mouse settings accepted motion")
		check(mouse.motion(Vector2(24, -12), dimensions, settings) and mouse.demands(bad) == Vector2.ZERO and mouse.offset == Vector2.ZERO, "Malformed demand settings failed to clear mouse-owned intent")
	mouse.offset = Vector2(NAN, 0)
	check(mouse.demands(settings) == Vector2.ZERO and mouse.offset == Vector2.ZERO, "Nonfinite retained mouse offset escaped demands")
	mouse.offset = Vector2(2, 0)
	check(mouse.demands(settings) == Vector2.ZERO and mouse.offset == Vector2.ZERO, "Unbounded retained mouse offset escaped demands")
	check(settings == unchanged, "Mouse helper changed shared control preferences")
	mouse.free()
	other.free()

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

func binding_snapshot(controls: Node) -> Dictionary:
	var action_map := {}
	for action in InputMap.get_actions():
		if not str(action).begins_with("pilot_") and action not in ["ui_accept", "ui_cancel", "ui_up", "ui_down", "ui_left", "ui_right"]: continue
		var events := []
		for event in InputMap.action_get_events(action):
			events.append([event.get_class(), event.device, event.as_text()])
		action_map[action] = [InputMap.action_get_deadzone(action), events, Input.get_action_raw_strength(action)]
	var retained := []
	for event in controls.remap_neutral_events:
		retained.append([event.get_class(), event.device, event.as_text()])
	# Failure may explain itself through status, but may change none of these
	# live owners, mapped actions, held-input barriers or pending capture fields.
	return {"settings": controls.settings.duplicate(true), "bindings": controls.bindings.duplicate(true), "source": controls.source_document.duplicate(true), "writable": controls.preferences_writable, "waiting": controls.waiting_action, "family": controls.waiting_family, "enabled": controls.enabled, "focused": controls.focused, "device": controls.device, "neutral": controls.needs_neutral, "looking": controls.looking, "look_neutral": controls.look_axes_needs_neutral, "retained": retained, "overflow": controls.remap_neutral_overflow, "foot_keys": controls.foot_remap_keys.duplicate(), "actions": action_map}

func capture_binding_key(controls: Node, code: int) -> void:
	# The isolated provider has automatic _input disabled. Physical events still
	# reach InputMap; forward the pressed event through its actual capture path.
	physical_key(code, true)
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = true
	controls._input(event)
	physical_key(code, false)

func write_binding_fixture(path: String, text: String) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	check(file != null, "Could not open private binding fixture")
	if file == null: return false
	file.store_string(text)
	file.flush()
	var written := file.get_error() == OK
	file.close()
	check(written, "Could not write private binding fixture")
	return written

func check_rebind_transaction() -> void:
	var folder := "user://input-remap-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()]
	var made := DirAccess.make_dir_absolute(folder)
	check(made == OK, "Private binding fixture directory unavailable")
	if made != OK: return
	var invalid_path := folder.path_join("invalid.json")
	var durable_path := folder.path_join("controls.json")
	var directory_path := folder.path_join("directory-target")
	if not write_binding_fixture(invalid_path, "{invalid controls"):
		DirAccess.remove_absolute(invalid_path)
		DirAccess.remove_absolute(folder)
		return
	var invalid_bytes := FileAccess.get_file_as_bytes(invalid_path)
	var blocked := Controls.new()
	blocked.settings_path = invalid_path
	blocked.initial_device = -1
	root.add_child(blocked)
	blocked.set_process_input(false)
	var blocked_installs := [0]
	blocked.bindings_changed.connect(func(): blocked_installs[0] += 1)
	check(not blocked.preferences_writable and blocked.bindings.forward.key.code == KEY_W, "Invalid binding source did not retain protected defaults")
	blocked.waiting_action = "forward"
	blocked.waiting_family = "key"
	physical_key(KEY_W, true)
	var protected_before := binding_snapshot(blocked)
	capture_binding_key(blocked, KEY_T)
	check(binding_snapshot(blocked) == protected_before and blocked_installs[0] == 0 and not blocked.status.is_empty(), "Protected-source capture changed controls, InputMap, barriers or pending capture")
	check(FileAccess.get_file_as_bytes(invalid_path) == invalid_bytes and not FileAccess.file_exists(invalid_path + ".tmp"), "Protected-source capture replaced its invalid file or left a temporary")
	# An explicit session-only run may remap even when the old file is protected.
	blocked.persist = false
	capture_binding_key(blocked, KEY_T)
	check(blocked.waiting_action.is_empty() and blocked.bindings.forward.key.code == KEY_T and blocked_installs[0] == 1 and blocked.status == "Binding applied for this session only.", "Session-only capture failed to adopt once or claimed durable success")
	check(FileAccess.get_file_as_bytes(invalid_path) == invalid_bytes and not blocked.preferences_writable, "Session-only remap overwrote or unprotected the original file")
	check(blocked.needs_neutral and not blocked.remap_neutral_events.is_empty() and not blocked.observe_neutral(), "Session-only capture lost the physically held old-key barrier")
	physical_key(KEY_W, false)
	check(blocked.observe_neutral() and not blocked.needs_neutral and blocked.remap_neutral_events.is_empty(), "Released session-only old key did not recover neutral")
	var document := {"version": 4, "settings": blocked.settings.duplicate(true), "bindings": blocked.bindings.duplicate(true), "future": {"retained": [7, "extension"]}}
	document.bindings.forward.key = {"kind": "key", "code": KEY_W}
	document.settings.future_scalar = "retained"
	document.bindings.forward.future_action = {"retained": true}
	document.bindings.backward.key.future_key = "retained"
	blocked.free()
	if not write_binding_fixture(durable_path, JSON.stringify(document)):
		DirAccess.remove_absolute(durable_path)
		DirAccess.remove_absolute(invalid_path)
		DirAccess.remove_absolute(folder)
		return
	var original_bytes := FileAccess.get_file_as_bytes(durable_path)
	var controls := Controls.new()
	controls.settings_path = durable_path
	controls.initial_device = -1
	root.add_child(controls)
	controls.set_process_input(false)
	var installs := [0]
	controls.bindings_changed.connect(func(): installs[0] += 1)
	check(controls.preferences_writable and controls.bindings.forward.key.code == KEY_W, "Valid durable remap fixture failed to load")
	controls.waiting_action = "forward"
	controls.waiting_family = "key"
	physical_key(KEY_W, true)
	var directory_made := DirAccess.make_dir_absolute(directory_path)
	check(directory_made == OK, "Private rename-refusal directory unavailable")
	for path in [folder.path_join("missing-parent/controls.json"), directory_path]:
		controls.settings_path = path
		var before := binding_snapshot(controls)
		capture_binding_key(controls, KEY_T)
		check(binding_snapshot(controls) == before and installs[0] == 0 and not controls.status.is_empty(), "Failed open/rename adopted a binding, installed input or cleared capture: " + path)
		check(not FileAccess.file_exists(path + ".tmp") and FileAccess.get_file_as_bytes(durable_path) == original_bytes, "Failed remap left a temporary or changed the retained file")
	check(DirAccess.dir_exists_absolute(directory_path), "Failed remap replaced its directory target")
	controls.settings_path = durable_path
	var expected := controls.source_document.duplicate(true)
	expected.version = 4
	expected.bindings.forward.key = {"kind": "key", "code": KEY_T}
	# JSON decodes numeric fields as floats, including the layout version and
	# key codes. Compare reloads with that representation, not the live integers.
	var durable_expected: Dictionary = JSON.parse_string(JSON.stringify(expected))
	capture_binding_key(controls, KEY_T)
	check(controls.waiting_action.is_empty() and installs[0] == 1 and controls.status == "Binding saved." and controls.source_document == expected and controls.bindings == expected.bindings and controls.settings == expected.settings, "Durable capture failed exact candidate adoption or changed another setting/binding/extension")
	check(JSON.parse_string(FileAccess.get_file_as_string(durable_path)) == durable_expected and not FileAccess.file_exists(durable_path + ".tmp"), "Durable capture did not atomically write its complete candidate")
	check(controls.needs_neutral and not controls.remap_neutral_events.is_empty() and not controls.observe_neutral(), "Successful durable capture forgot the held removed key")
	physical_key(KEY_W, false)
	check(controls.observe_neutral() and not controls.needs_neutral and controls.remap_neutral_events.is_empty(), "Durable capture did not rearm after actual old-key release")
	physical_key(KEY_T, true)
	check(controls.strength("forward") == 1.0, "Durable capture did not install the accepted actual key")
	physical_key(KEY_T, false)
	var reloaded := Controls.new()
	reloaded.load_settings(durable_path)
	check(reloaded.preferences_writable and reloaded.bindings == durable_expected.bindings and reloaded.settings == durable_expected.settings and reloaded.source_document == durable_expected, "Durable remap failed complete restart/extension preservation")
	reloaded.free()
	controls.free()
	for path in [durable_path, invalid_path, directory_path, folder]:
		check(DirAccess.remove_absolute(path) == OK, "Could not clean private remap fixture: " + path)

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


func check_surface_interaction() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.initial_device = 19
	root.add_child(controls)
	controls.available_pads = {19: true, 9: true}
	var calls := [0, 0]
	controls.surface_interaction_requested.connect(func(): calls[0] += 1)
	controls.camera_requested.connect(func(): calls[1] += 1)
	var event := InputEventKey.new()
	event.physical_keycode = KEY_E
	event.pressed = true
	controls._input(event)
	check(calls == [0, 0], "Flight unexpectedly acquired the fixed on-foot action")
	check(controls.rebind("strafe_right", "key", {"kind": "key", "code": KEY_T}) and controls.rebind("camera", "key", {"kind": "key", "code": KEY_E}), "Context precedence fixture could not install valid remaps")
	controls.surface_interaction_enabled = true
	controls._input(event)
	check(calls == [1, 0], "Foot interaction lost priority to a remapped flight camera")
	event.echo = true
	controls._input(event)
	check(calls == [1, 0], "Repeated key echo triggered foot interaction")
	event.echo = false
	for device in [9, 19]:
		var pad := InputEventJoypadButton.new()
		pad.device = device
		pad.button_index = JOY_BUTTON_A
		pad.pressed = true
		controls._input(pad)
	check(calls == [2, 0], "Foot interaction admitted a foreign controller or refused its selected owner")
	controls.available_pads[19] = false
	var disconnected := InputEventJoypadButton.new()
	disconnected.device = 19
	disconnected.button_index = JOY_BUTTON_A
	disconnected.pressed = true
	controls._input(disconnected)
	check(calls == [2, 0], "Disconnected selected controller retained foot interaction")
	controls.available_pads[19] = true
	controls.set_enabled(false)
	controls._input(event)
	check(calls == [2, 0], "Paused controls requested a foot interaction")
	controls.set_enabled(true)
	controls.focused = false
	controls._input(event)
	check(calls == [2, 0], "Unfocused controls requested a foot interaction")
	controls.focused = true
	controls.waiting_action = "camera"
	controls.waiting_family = "key"
	controls._input(event)
	check(calls == [2, 0] and controls.waiting_action.is_empty(), "Remapping triggered a foot interaction")
	controls.surface_interaction_enabled = false
	controls._input(event)
	check(calls == [2, 1], "Leaving foot mode failed to restore the remapped flight camera")
	controls.landed_hatch_enabled = true
	controls._input(event)
	check(calls == [3, 1], "Landed hatch E lost priority to its remapped camera action")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_Y}) and controls.rebind("camera", "key", {"kind": "key", "code": KEY_W}), "Landed walking-key/camera overlap remap refused")
	event.physical_keycode = KEY_W
	controls._input(event)
	check(calls == [3, 2], "Landed hatch context suppressed a non-E walking-key flight camera")
	controls.surface_interaction_enabled = true
	controls._input(event)
	check(calls == [3, 2], "Outside context stopped suppressing a walking-key flight camera")
	controls.surface_interaction_enabled = false
	for device in [9, 19]:
		var pad := InputEventJoypadButton.new()
		pad.device = device
		pad.button_index = JOY_BUTTON_A
		pad.pressed = true
		controls._input(pad)
	check(calls == [4, 2], "Landed hatch A admitted a foreign controller or refused selected owner")
	controls.available_pads[19] = false
	var lost := InputEventJoypadButton.new()
	lost.device = 19
	lost.button_index = JOY_BUTTON_A
	lost.pressed = true
	controls._input(lost)
	check(calls == [4, 2], "Disconnected landed controller retained contextual A")
	controls.free()


func check_landed_hatch_neutral() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	controls.initial_device = 0
	root.add_child(controls)
	controls.available_pads = {0: true, 9: true}
	controls.set_process_input(false)
	var snapshot := func() -> Array:
		return [controls.enabled, controls.focused, controls.needs_neutral, controls.waiting_action, controls.remap_neutral_overflow, controls.remap_neutral_events.duplicate(), controls.foot_remap_keys.duplicate(), controls.settings.duplicate(true), controls.bindings.duplicate(true), controls.source_document.duplicate(true), controls.status, controls.device, controls.looking, controls.look_axes_needs_neutral, controls.assist, controls.surface_interaction_enabled, controls.landed_hatch_enabled]
	var probe := func(expected: bool, label: String) -> void:
		var before: Array = snapshot.call()
		check(controls.landed_hatch_neutral() == expected, label)
		check(snapshot.call() == before, "Read-only hatch query changed provider state: " + label)
	probe.call(false, "Hatch query bypassed initial neutral latch")
	check(controls.observe_neutral(), "Hatch fixture could not establish ordinary neutral")
	probe.call(true, "Released active controls blocked landed hatch")
	physical_key(KEY_E, true)
	check(Input.is_physical_key_pressed(KEY_E) and controls.sample().thrust_axes[5] == 1.0, "Physical E lost its ordinary free-flight strafe demand")
	probe.call(true, "Physical interaction E alone blocked landed hatch")
	# Each independent key/action is inspected; opposed demands cannot cancel.
	for action in controls.ACTIONS:
		var code: int = int(controls.bindings[action].key.code)
		if code == KEY_E: continue
		physical_key(code, true)
		probe.call(false, "Held flight key bypassed landed hatch: " + action)
		physical_key(code, false)
		Input.action_press("pilot_" + action, 0.7)
		probe.call(false, "Raw flight demand bypassed landed hatch: " + action)
		Input.action_release("pilot_" + action)
	for axis in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y, JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
		for value in [-0.7, 0.7]:
			joy_axis(axis, value, 0)
			probe.call(false, "Selected stick demand bypassed landed hatch")
		joy_axis(axis, 0.0, 0)
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0.7, 0)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.7, 0)
	probe.call(false, "Opposed selected engines canceled into hatch permission")
	joy_axis(JOY_AXIS_TRIGGER_LEFT, 0.0, 0)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0, 0)
	# This is the SAME action as E: its pad channel must remain authoritative.
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, true, 0)
	probe.call(false, "E exemption also ignored its action's selected-pad strafe")
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, false, 0)
	joy_button(JOY_BUTTON_LEFT_SHOULDER, true, 0)
	probe.call(false, "Selected-pad opposite strafe bypassed landed hatch")
	joy_button(JOY_BUTTON_LEFT_SHOULDER, false, 0)
	joy_button(JOY_BUTTON_LEFT_STICK, true, 0)
	probe.call(false, "Selected look modifier bypassed landed hatch")
	joy_button(JOY_BUTTON_LEFT_STICK, false, 0)
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.8, 9)
	probe.call(true, "Foreign pad took ownership of landed hatch neutrality")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0, 9)
	joy_button(JOY_BUTTON_A, true, 0)
	probe.call(true, "Reserved selected interaction A blocked landed hatch")
	joy_button(JOY_BUTTON_A, false, 0)
	controls.available_pads[0] = false
	probe.call(false, "Unavailable selected owner retained hatch permission")
	controls.available_pads[0] = true
	controls.enabled = false
	probe.call(false, "Disabled provider granted landed hatch")
	controls.enabled = true
	controls.focused = false
	probe.call(false, "Unfocused provider granted landed hatch")
	controls.focused = true
	controls.waiting_action = "camera"
	probe.call(false, "Pending remap granted landed hatch")
	controls.waiting_action = ""
	controls.needs_neutral = true
	probe.call(false, "Held E cleared an existing neutral latch")
	physical_key(KEY_E, false)
	check(controls.observe_neutral(), "Released hatch fixture did not recover ordinary neutral")
	Input.action_press("pilot_strafe_right", 0.7)
	probe.call(false, "Unheld physical E exempted a raw action demand")
	Input.action_release("pilot_strafe_right")
	# Rebinding retains a real removed physical key. Queries must not prune it.
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_T}), "Hatch history fixture first remap refused")
	check(controls.observe_neutral(), "Hatch history fixture first remap remained blocked")
	physical_key(KEY_T, true)
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_Y}), "Hatch history fixture second remap refused")
	check(not controls.remap_neutral_events.is_empty(), "Hatch history did not retain the actual removed key")
	# Inspect the retained barrier independently of the already-tested latch.
	controls.needs_neutral = false
	probe.call(false, "Held removed flight key bypassed hatch barrier")
	physical_key(KEY_T, false)
	probe.call(true, "Released retained event still blocked hatch without mutating history")
	check(not controls.remap_neutral_events.is_empty(), "Pure hatch query pruned released remap history")
	check(controls.observe_neutral(), "Ordinary observer could not prune released hatch history")
	controls.remap_neutral_overflow = true
	probe.call(false, "Hatch query cleared or ignored remap overflow")
	check(controls.observe_neutral(), "Ordinary observer could not recover fully released overflow")
	# A retained walking key can be unrelated to every current flight mapping.
	physical_key(KEY_J, true)
	controls.foot_remap_keys.append(KEY_J)
	probe.call(false, "Held old walking key bypassed hatch barrier")
	physical_key(KEY_J, false)
	probe.call(true, "Released walking barrier still blocked hatch")
	check(controls.foot_remap_keys == [KEY_J], "Pure hatch query pruned walking history")
	# E can be remapped to a discrete camera action; contextual priority is
	# already checked above, and its unrelated pad camera binding must not vanish.
	check(controls.rebind("strafe_right", "key", {"kind": "key", "code": KEY_T}) and controls.rebind("camera", "key", {"kind": "key", "code": KEY_E}), "Hatch camera-overlap fixture remap refused")
	check(controls.observe_neutral(), "Camera-overlap fixture could not establish ordinary neutral")
	physical_key(KEY_E, true)
	probe.call(true, "Remapped physical E camera action blocked landed hatch")
	joy_button(JOY_BUTTON_X, true, 0)
	probe.call(false, "Remapped E exemption ignored independent selected camera button")
	joy_button(JOY_BUTTON_X, false, 0)
	physical_key(KEY_E, false)
	controls.free()


func check_cockpit_interaction() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	controls.initial_device = 0
	root.add_child(controls)
	controls.set_process_input(false)
	controls.available_pads = {0: true, 9: true}
	controls.observe_neutral()
	var calls := [0]
	controls.surface_interaction_requested.connect(func(): calls[0] += 1)
	var send_key := func(down: bool) -> void:
		physical_key(KEY_E, down)
		var event := InputEventKey.new()
		event.physical_keycode = KEY_E
		event.pressed = down
		controls._input(event)
	controls.cockpit_interaction_enabled = true
	send_key.call(true)
	check(calls[0] == 1 and controls.cockpit_interaction_neutral() and controls.sample().thrust_axes[5] == 0.0, "Aimed cockpit E did not act once without firing strafe")
	var repeated := InputEventKey.new()
	repeated.physical_keycode = KEY_E
	repeated.pressed = true
	controls._input(repeated)
	repeated.echo = true
	controls._input(repeated)
	check(calls[0] == 1, "Held/echoed cockpit E repeated its discrete action")
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, true, 0)
	check(not controls.cockpit_interaction_neutral() and controls.sample().thrust_axes[5] == 1.0, "Cockpit E suppression erased the same action's independent pad channel")
	joy_button(JOY_BUTTON_RIGHT_SHOULDER, false, 0)
	controls.cockpit_interaction_enabled = false
	check(controls.sample().thrust_axes[5] == 0.0, "Leaving a cockpit target while E was held created strafe")
	send_key.call(false)
	send_key.call(true)
	check(controls.sample().thrust_axes[5] == 1.0 and calls[0] == 1, "Ordinary free-flight E did not recover after release")
	controls.cockpit_interaction_enabled = true
	repeated.echo = false
	controls._input(repeated)
	check(calls[0] == 1 and controls.sample().thrust_axes[5] == 0.0, "Entering a cockpit target with held E activated it")
	send_key.call(false)
	controls.observe_neutral()
	send_key.call(true)
	check(calls[0] == 2, "A fresh E press after release could not actuate the cockpit")
	send_key.call(false)
	for gate in ["focus", "disabled", "remap", "neutral"]:
		controls.focused = gate != "focus"
		controls.enabled = gate != "disabled"
		controls.waiting_action = "camera" if gate == "remap" else ""
		controls.needs_neutral = gate == "neutral"
		check(not controls.cockpit_interaction_neutral(), "Cockpit neutral gate admitted " + gate)
	controls.focused = true
	controls.enabled = true
	controls.waiting_action = ""
	controls.needs_neutral = false
	var pad := InputEventJoypadButton.new()
	pad.button_index = JOY_BUTTON_A
	pad.pressed = true
	pad.device = 9
	controls._input(pad)
	check(calls[0] == 2, "Foreign pad actuated the cockpit")
	pad.device = 0
	controls._input(pad)
	controls._input(pad)
	check(calls[0] == 3, "Selected pad cockpit action failed or repeated while held")
	pad.pressed = false
	controls._input(pad)
	joy_button(JOY_BUTTON_LEFT_STICK, true, 0)
	check(controls.cockpit_interaction_neutral(), "Stationary head-look modifier blocked an aimed cockpit action")
	joy_button(JOY_BUTTON_LEFT_STICK, false, 0)
	check(controls.rebind("strafe_right", "key", {"kind": "key", "code": KEY_T}) and controls.rebind("camera", "key", {"kind": "key", "code": KEY_E}), "Cockpit E overlap remap refused")
	controls.observe_neutral()
	var camera_calls := [0]
	controls.camera_requested.connect(func(): camera_calls[0] += 1)
	send_key.call(true)
	check(calls[0] == 4 and camera_calls[0] == 0, "Aimed gear interaction also fired its remapped camera key")
	controls.cockpit_interaction_enabled = false
	send_key.call(false)
	send_key.call(true)
	check(camera_calls[0] == 1 and calls[0] == 4, "Released ordinary E camera binding did not recover outside the target")
	send_key.call(false)
	controls.free()
