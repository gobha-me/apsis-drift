extends SceneTree
## Standalone presentation test; no bridge, physics, asset load or settings writes.
const Controls = preload("res://player_input.gd")
const FlightPlanMenu = preload("res://flight_plan_menu.gd")
var failures := 0
var chosen: Array[int] = []

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func key(code: Key, pressed := true, echo := false, shift := false) -> InputEventKey:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	event.echo = echo
	event.shift_pressed = shift
	return event

func pad(code: JoyButton, device := 2, pressed := true) -> InputEventJoypadButton:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = code
	event.pressed = pressed
	return event

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	root.add_child(controls)
	controls.device = 2
	controls.install()
	controls.sample()
	var menu := FlightPlanMenu.new()
	menu.setup(controls)
	root.add_child(menu)
	menu.plan_selected.connect(func(plan: int): chosen.append(plan))
	check(not menu.active and not menu.panel.visible, "Flight-plan menu did not start closed")
	check(not menu.handle_event(pad(JOY_BUTTON_A)) and chosen.is_empty(), "Closed menu took controller input")
	check(menu.choices.size() == 4, "Menu lost a guidance option")
	for choice in menu.choices:
		check(choice.focus_mode == Control.FOCUS_NONE, "GUI navigation could bypass active-pad/binding checks")
		check(not choice.text.to_lower().contains("execute"), "Guidance selector exposed an actuation button")
	check(not menu.open(-1) and not menu.open(4) and not menu.active, "Invalid plan index opened menu")
	check(menu.open(1) and menu.active and menu.selected_plan == 1, "Opening did not preserve current plan")
	check(not menu.handle_event(pad(JOY_BUTTON_DPAD_DOWN, 7)) and menu.selected_plan == 1, "Inactive controller navigated")
	check(not menu.handle_event(pad(JOY_BUTTON_A, 7)) and chosen.is_empty(), "Inactive controller selected")
	check(not menu.handle_event(pad(JOY_BUTTON_DPAD_DOWN, 2, false)) and menu.selected_plan == 1, "Button release navigated")
	check(not menu.handle_event(key(KEY_TAB, true, true)) and menu.selected_plan == 1, "Key autorepeat navigated")
	check(not menu.handle_event(key(KEY_ENTER, false)), "Key release selected")
	var shortcut := key(KEY_TAB)
	shortcut.alt_pressed = true
	check(not menu.handle_event(shortcut), "OS keyboard shortcut was captured")
	var motion := InputEventJoypadMotion.new()
	motion.device = 2
	motion.axis = JOY_AXIS_LEFT_X
	motion.axis_value = 1
	check(not menu.handle_event(motion), "Flight-plan menu took analog roll axis")
	motion.axis = JOY_AXIS_TRIGGER_RIGHT
	check(not menu.handle_event(motion), "Flight-plan menu took engine trigger")
	check(menu.handle_event(pad(JOY_BUTTON_DPAD_DOWN)) and menu.selected_plan == 2, "D-pad down failed")
	check(menu.handle_event(pad(JOY_BUTTON_DPAD_UP)) and menu.selected_plan == 1, "D-pad up failed")
	check(menu.handle_event(key(KEY_TAB)) and menu.selected_plan == 2, "Keyboard Tab failed")
	check(menu.handle_event(key(KEY_TAB, true, false, true)) and menu.selected_plan == 1, "Shift-Tab failed")
	check(menu.handle_event(pad(JOY_BUTTON_B)) and not menu.active and chosen.is_empty(), "Cancel selected or left menu open")
	for plan in 4:
		check(menu.open(plan), "Could not open guidance option")
		check(menu.handle_event(pad(JOY_BUTTON_A)) and not menu.active, "A/Cross selection did not close")
	check(chosen == [0, 1, 2, 3], "Guidance signal mode mapping changed")
	check(menu.open(0) and menu.handle_event(pad(JOY_BUTTON_DPAD_UP)) and menu.selected_plan == 3, "Navigation did not wrap")
	check(menu.handle_event(key(KEY_ENTER)) and chosen.back() == 3, "Enter did not select highlighted guidance")
	check(menu.toggle(1) and menu.active and menu.toggle() and not menu.active, "Toggle state inconsistent")

	# Holding engine input across opening/selection must not change any demand,
	# assistance, neutral gate, profile or simulation pause state.
	var original_bindings: Dictionary = controls.bindings.duplicate(true)
	var original_settings: Dictionary = controls.settings.duplicate(true)
	Input.action_press("pilot_forward", 0.6)
	var before: Dictionary = controls.sample()
	var assist: bool = controls.assist
	var enabled: bool = controls.enabled
	var neutral: bool = controls.needs_neutral
	check(menu.open(1), "Guidance failed while thrust held")
	check(controls.sample() == before, "Opening guidance stole flight demand")
	check(menu.handle_event(key(KEY_ENTER)), "Selecting guidance while thrust held failed")
	check(controls.sample() == before and controls.assist == assist and controls.enabled == enabled and controls.needs_neutral == neutral, "Guidance selection changed flight controls")
	check(not paused and controls.bindings == original_bindings and controls.settings == original_settings, "Guidance selection paused or rewrote player preferences")
	Input.action_release("pilot_forward")

	# Existing remaps retain ownership even while the live guidance menu is open.
	check(controls.rebind("strafe_left", "pad", Controls.button(JOY_BUTTON_DPAD_UP)), "Custom D-pad fixture refused")
	controls.sample()
	check(menu.open(1), "Menu cannot open with legacy D-pad flight binding")
	check(not menu.handle_event(pad(JOY_BUTTON_DPAD_UP)) and menu.selected_plan == 1, "Menu stole custom D-pad strafe")
	check(menu.handle_event(pad(JOY_BUTTON_DPAD_DOWN)) and menu.selected_plan == 2, "Unmapped D-pad direction unavailable")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_TAB}), "Tab remap fixture refused")
	check(controls.rebind("backward", "key", {"kind": "key", "code": KEY_ENTER}), "Enter remap fixture refused")
	check(not menu.handle_event(key(KEY_TAB)) and menu.selected_plan == 2, "Menu stole remapped Tab thrust")
	check(not menu.handle_event(key(KEY_ENTER)) and menu.active, "Menu stole remapped Enter thrust")
	check(not menu.handle_event(key(KEY_UP)), "Menu stole default arrow-key head-look binding")
	check(menu.handle_event(key(KEY_ESCAPE)) and not menu.active, "Reserved Escape failed to cancel")
	check(menu.open(2), "Focus test menu failed to open")
	controls.focused = false
	check(not menu.handle_event(pad(JOY_BUTTON_A)), "Unfocused menu selected a plan")
	menu._process(0)
	check(not menu.active and not menu.open(1), "Focus loss did not close/block menu")
	controls.focused = true
	check(menu.open(2), "Could not reopen after focus returned")
	controls.waiting_action = "forward"
	check(not menu.handle_event(key(KEY_ENTER)), "Binding capture leaked into guidance selector")
	menu._process(0)
	check(not menu.active, "Rebinding capture did not dismiss live selector")
	controls.waiting_action = ""
	check(menu.open(1), "Pointer selection test failed to open")
	menu.choices[3].pressed.emit()
	check(not menu.active and chosen.back() == 3, "Mouse-accessible choice bypassed mode mapping")
	menu.queue_free()
	controls.queue_free()
	print("Flight-plan menu contracts: %d failures (synthetic input; no hardware or flight qualification)" % failures)
	quit(0 if failures == 0 else 1)
