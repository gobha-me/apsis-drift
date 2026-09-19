extends Node
## Presentation input only. Forces and attitude are owned by C++ at 120 Hz.
signal pause_requested
signal camera_requested
signal recenter_requested
signal safety_pause(reason: String)
signal bindings_changed

const SETTINGS_PATH := "user://freedom-controls-v4.json"
const ACTIONS := ["forward", "backward", "turn_left", "turn_right", "strafe_left", "strafe_right", "rise", "fall", "look_left", "look_right", "look_up", "look_down", "camera", "recenter", "look_hold", "pitch_up", "pitch_down", "roll_left", "roll_right", "assist"]
const TITLES := ["Main thrust", "Retro thrust", "Yaw left", "Yaw right", "Strafe left", "Strafe right", "Rise", "Fall", "Look left (modifier)", "Look right (modifier)", "Look up (modifier)", "Look down (modifier)", "Cockpit / chase", "Recenter head", "Hold to look", "Pitch up", "Pitch down", "Roll left", "Roll right", "Toggle flight assist"]
const KEYS := [KEY_W, KEY_S, KEY_A, KEY_D, KEY_Q, KEY_E, KEY_SPACE, KEY_CTRL, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_C, KEY_HOME, KEY_ALT, KEY_I, KEY_K, KEY_Z, KEY_X, KEY_F]
var settings := {"deadzone": 0.18, "curve": 1.4, "look_speed": 1.6, "invert_look": false, "prompts": 0}
var bindings: Dictionary = {}
var device := -1
var enabled := true
var focused := true
var needs_neutral := true
var last_device := "keyboard"
var waiting_action := ""
var waiting_family := ""
var status := ""
var persist := true
var looking := false
var look_axes_needs_neutral := false
var assist := true
var thrust_mode := false

func _ready() -> void:
	defaults()
	if persist:
		load_settings()
	var pads := Input.get_connected_joypads()
	device = pads[0] if not pads.is_empty() else -1
	install()
	if device >= 0:
		print("Controller selected: %s / mapped=%s" % [Input.get_joy_name(device), Input.is_joy_known(device)])
	Input.joy_connection_changed.connect(connection_changed)
	Input.ignore_joypad_on_unfocused_application = true

func defaults() -> void:
	bindings.clear()
	# Triggers are independent propulsion demands, not a signed target speed.
	var pads := [axis(5, 1), axis(4, 1), axis(2, -1), axis(2, 1), button(9), button(10), axis(3, -1), axis(3, 1), axis(2, -1), axis(2, 1), axis(3, -1), axis(3, 1), button(2), button(4), button(7), axis(1, 1), axis(1, -1), axis(0, -1), axis(0, 1), button(3)]
	for i in ACTIONS.size():
		bindings[ACTIONS[i]] = {"key": {"kind": "key", "code": KEYS[i]}, "pad": pads[i]}

static func axis(code: int, direction: int) -> Dictionary:
	return {"kind": "axis", "code": code, "sign": direction}

static func button(code: int) -> Dictionary:
	return {"kind": "button", "code": code}

static func valid_binding(value: Variant, family: String) -> bool:
	if not value is Dictionary or not value.get("kind") is String:
		return false
	var code: Variant = value.get("code")
	if not (code is int or code is float) or not is_finite(float(code)) or code != int(code):
		return false
	if family == "key":
		return value.kind == "key" and code > 0 and code < 0x800000 and int(code) not in [KEY_ESCAPE, KEY_V, KEY_F3]
	if value.kind == "button":
		return code >= 0 and code < JOY_BUTTON_MAX and int(code) not in [JOY_BUTTON_START, JOY_BUTTON_A, JOY_BUTTON_B]
	var direction: Variant = value.get("sign")
	return value.kind == "axis" and code >= 0 and code < 6 and (direction is int or direction is float) and (direction == -1 or direction == 1) and (code < 4 or direction == 1)

static func valid_document(value: Variant) -> bool:
	if not value is Dictionary or value.get("version") != 4 or not value.get("settings") is Dictionary or not value.get("bindings") is Dictionary:
		return false
	var s: Dictionary = value.settings
	for pair in [["deadzone", 0.05, 0.45], ["curve", 1.0, 3.0], ["look_speed", 0.3, 4.0], ["prompts", 0, 2]]:
		var n: Variant = s.get(pair[0])
		if not (n is float or n is int) or not is_finite(float(n)) or n < pair[1] or n > pair[2]:
			return false
	if s.prompts != int(s.prompts) or not s.get("invert_look") is bool:
		return false
	for family in ["key", "pad"]:
		var used := {}
		for action in ACTIONS:
			if not value.bindings.get(action) is Dictionary:
				return false
			var binding: Variant = value.bindings[action].get(family)
			if not valid_binding(binding, family):
				return false
			var token := binding_token(binding)
			for other in used.get(token, []):
				if not shared_context(other, action, family):
					return false
			if not used.has(token):
				used[token] = []
			used[token].append(action)
	return true

static func shared_context(a: String, b: String, family: String) -> bool:
	# Only the same directional stick channel can be shared across contexts.
	for group in [["turn_left", "look_left"], ["turn_right", "look_right"], ["rise", "look_up"], ["fall", "look_down"]]:
		if family == "pad" and a != b and a in group and b in group:
			return true
	return false

static func binding_token(binding: Dictionary) -> String:
	return "%s:%d:%d" % [binding.kind, binding.code, binding.get("sign", 0)]

func load_settings(path: String = SETTINGS_PATH) -> void:
	if not FileAccess.file_exists(path):
		return
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() > 65536:
		status = "Controls file unreadable / too large; using defaults."
		return
	var value: Variant = JSON.parse_string(file.get_as_text())
	if not valid_document(value):
		status = "Invalid controls file; using defaults."
		return
	settings = value.settings
	bindings = value.bindings

func save_settings(path: String = SETTINGS_PATH) -> void:
	if not persist:
		return
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null:
		status = "Could not save controls."
		return
	file.store_string(JSON.stringify({"version": 4, "settings": settings, "bindings": bindings}, "\t"))
	file.flush()
	if file.get_error() != OK:
		status = "Could not finish writing controls; previous file retained."
		return
	file.close()
	if DirAccess.rename_absolute(path + ".tmp", path) != OK:
		status = "Could not replace controls file."

func install() -> void:
	# Built-in UI actions do not consistently include pad bindings. Keep native
	# keyboard navigation, but bind menu controls explicitly to the active pad.
	var menu_buttons := {"ui_accept": JOY_BUTTON_A, "ui_cancel": JOY_BUTTON_B,
		"ui_up": JOY_BUTTON_DPAD_UP, "ui_down": JOY_BUTTON_DPAD_DOWN,
		"ui_left": JOY_BUTTON_DPAD_LEFT, "ui_right": JOY_BUTTON_DPAD_RIGHT}
	for action in menu_buttons:
		for old_event in InputMap.action_get_events(action):
			if old_event is InputEventJoypadButton or old_event is InputEventJoypadMotion:
				InputMap.action_erase_event(action, old_event)
		var event := InputEventJoypadButton.new()
		event.device = device if device >= 0 else 9999
		event.button_index = menu_buttons[action]
		InputMap.action_add_event(action, event)
	for action in ACTIONS:
		var name: String = "pilot_" + action
		if not InputMap.has_action(name):
			InputMap.add_action(name)
		Input.action_release(name)
		InputMap.action_erase_events(name)
		InputMap.action_set_deadzone(name, float(settings.deadzone))
		for family in ["key", "pad"]:
			var binding: Dictionary = bindings[action][family]
			var event: InputEvent
			if binding.kind == "key":
				event = InputEventKey.new()
				event.physical_keycode = int(binding.code)
			elif binding.kind == "button":
				event = InputEventJoypadButton.new()
				event.button_index = int(binding.code)
			else:
				event = InputEventJoypadMotion.new()
				event.axis = int(binding.code)
				event.axis_value = float(binding.sign)
			if family == "pad":
				event.device = device if device >= 0 else 9999
			InputMap.action_add_event(name, event)
	needs_neutral = true
	bindings_changed.emit()

func connection_changed(id: int, connected: bool) -> void:
	if id == device and not connected:
		device = -1
		install()
		safety_pause.emit("Controller disconnected. Reconnect or use keyboard; resume explicitly.")
	elif device == -1 and connected:
		device = id
		install()
		safety_pause.emit("Controller connected. Center sticks / release triggers before resuming.")

func set_enabled(value: bool) -> void:
	enabled = value
	needs_neutral = true

func rebind(action: String, family: String, binding: Dictionary) -> bool:
	if action not in ACTIONS or family not in ["key", "pad"] or not valid_binding(binding, family):
		status = "Reserved or unsupported control. Try another; Esc / Start cancels."
		return false
	for other in ACTIONS:
		if other != action and binding_token(bindings[other][family]) == binding_token(binding) and not shared_context(other, action, family):
			status = "Already assigned to %s. Choose another control." % TITLES[ACTIONS.find(other)]
			return false
	bindings[action][family] = binding
	status = "Binding saved."
	install()
	save_settings()
	return true

func _input(event: InputEvent) -> void:
	if not focused:
		return
	var pad := event is InputEventJoypadButton or event is InputEventJoypadMotion
	if pad and event.device != device:
		return
	var pressed := event.is_pressed() and not event.is_echo()
	var escape: bool = event is InputEventKey and event.physical_keycode == KEY_ESCAPE and pressed
	var start: bool = event is InputEventJoypadButton and event.button_index == JOY_BUTTON_START and pressed
	if not waiting_action.is_empty():
		get_viewport().set_input_as_handled()
		if escape or start:
			waiting_action = ""
			status = "Binding cancelled."
			return
		var binding := {}
		if waiting_family == "key" and event is InputEventKey and pressed:
			binding = {"kind": "key", "code": event.physical_keycode}
		elif waiting_family == "pad" and event is InputEventJoypadButton and pressed:
			binding = button(event.button_index)
		elif waiting_family == "pad" and event is InputEventJoypadMotion and absf(event.axis_value) > 0.75:
			binding = axis(event.axis, 1 if event.axis_value > 0 else -1)
		if not binding.is_empty() and rebind(waiting_action, waiting_family, binding):
			waiting_action = ""
		return
	if (pad and (pressed or (event is InputEventJoypadMotion and absf(event.axis_value) > 0.3))):
		last_device = "pad"
	elif event is InputEventKey and pressed:
		last_device = "keyboard"
	if escape or start or (event is InputEventKey and event.physical_keycode == KEY_V and pressed):
		pause_requested.emit()
		get_viewport().set_input_as_handled()
	elif enabled and pressed:
		if event.is_action_pressed("pilot_camera"):
			camera_requested.emit()
			get_viewport().set_input_as_handled()
		elif event.is_action_pressed("pilot_recenter"):
			recenter_requested.emit()
			get_viewport().set_input_as_handled()
		elif event.is_action_pressed("pilot_assist") and thrust_mode and not needs_neutral:
			assist = not assist
			get_viewport().set_input_as_handled()

static func shape(value: float, deadzone: float, curve: float) -> float:
	if not is_finite(value) or not is_finite(deadzone) or not is_finite(curve) or deadzone < 0 or deadzone >= 1 or curve < 1:
		return 0.0
	return signf(value) * pow(clampf((absf(value) - deadzone) / (1.0 - deadzone), 0, 1), curve)

func strength(action: String) -> float:
	return Input.get_action_raw_strength("pilot_" + action)

func sample() -> Dictionary:
	var raw: Array[float] = []
	for pair in [["backward", "forward"], ["turn_left", "turn_right"], ["strafe_left", "strafe_right"], ["fall", "rise"], ["look_left", "look_right"], ["look_up", "look_down"]]:
		raw.append(strength(pair[1]) - strength(pair[0]))
	var neutral := true
	# Check individual actions, not differences: two held opposing triggers
	# must not unlock the gate merely because they cancel numerically.
	for action in ACTIONS:
		if strength(action) > float(settings.deadzone):
			neutral = false
		# Remapping rebuilds action state. Also inspect physical held controls so
		# a stationary, already-deflected stick cannot appear newly centered.
		if Input.is_physical_key_pressed(int(bindings[action].key.code)):
			neutral = false
		if device >= 0:
			var binding: Dictionary = bindings[action].pad
			if binding.kind == "axis" and Input.get_joy_axis(device, int(binding.code)) * float(binding.sign) > float(settings.deadzone):
				neutral = false
			elif binding.kind == "button" and Input.is_joy_button_pressed(device, int(binding.code)):
				neutral = false
	if needs_neutral and neutral:
		needs_neutral = false
	var result := {"axes": PackedFloat64Array([0, 0, 0, 0]), "thrust_axes": PackedFloat64Array([0, 0, 0, 0, 0, 0, 0]), "look": Vector2.ZERO, "recenter": false}
	if not enabled or not focused or needs_neutral:
		result.recenter = looking
		looking = false
		look_axes_needs_neutral = true
		return result
	var was_looking := looking
	# Hysteresis avoids repeated snap-back from small trigger fluctuations.
	looking = strength("look_hold") > (0.35 if looking else 0.55)
	if was_looking and not looking:
		result.recenter = true
		look_axes_needs_neutral = true
	for i in 4:
		result.axes[i] = shape(raw[i], settings.deadzone, settings.curve)
	var look_axes_centered := true
	for action in ["turn_left", "turn_right", "rise", "fall", "look_left", "look_right", "look_up", "look_down"]:
		if strength(action) > float(settings.deadzone):
			look_axes_centered = false
	if look_axes_centered:
		look_axes_needs_neutral = false
	if looking or look_axes_needs_neutral:
		# Suppress yaw/heave while looking and until their controls recenter.
		# Pitch, roll, bumper strafe and both engines remain independently usable.
		result.axes[1] = 0
		result.axes[3] = 0
	var roll := shape(strength("roll_right") - strength("roll_left"), settings.deadzone, settings.curve)
	result.thrust_axes = PackedFloat64Array([
		shape(strength("forward"), settings.deadzone, 1.0),
		shape(strength("backward"), settings.deadzone, 1.0),
		shape(strength("pitch_up") - strength("pitch_down"), settings.deadzone, settings.curve),
		result.axes[1], roll, result.axes[2], result.axes[3]])
	if not looking:
		return result
	# Radial deadzone for head-look, avoiding a square dead area.
	var look := Vector2(raw[4], raw[5])
	result.look = look.normalized() * shape(look.length(), settings.deadzone, settings.curve) * settings.look_speed
	if settings.invert_look:
		result.look.y *= -1
	return result

func playstation() -> bool:
	if int(settings.prompts) != 0:
		return int(settings.prompts) == 2
	var name := Input.get_joy_name(device).to_lower() if device >= 0 else ""
	return "sony" in name or "playstation" in name or "dualsense" in name or "dualshock" in name or "ps5" in name or "ps4" in name

func binding_label(action: String, family: String) -> String:
	var b: Dictionary = bindings[action][family]
	if b.kind == "key":
		return OS.get_keycode_string(int(b.code))
	if b.kind == "axis":
		if int(b.code) >= 4:
			return (["L2", "R2"] if playstation() else ["LT", "RT"])[int(b.code) - 4]
		return ["Left stick X", "Left stick Y", "Right stick X", "Right stick Y"][int(b.code)] + (" −" if b.sign < 0 else " +")
	var names := {0: "Cross" if playstation() else "A", 1: "Circle" if playstation() else "B", 2: "Square" if playstation() else "X", 3: "Triangle" if playstation() else "Y", 4: "Create" if playstation() else "View", 9: "L1" if playstation() else "LB", 10: "R1" if playstation() else "RB", 11: "D-pad up", 12: "D-pad down", 13: "D-pad left", 14: "D-pad right"}
	names[7] = "L3" if playstation() else "Left stick click"
	names[8] = "R3" if playstation() else "Right stick click"
	return names.get(int(b.code), "Button %d" % int(b.code))
