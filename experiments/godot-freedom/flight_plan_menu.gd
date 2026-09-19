extends CanvasLayer
## Optional live-flight guidance selection. Never pauses or writes pilot state.
signal plan_selected(plan: int)

const PLAN_NAMES := ["Clear flight plan", "Orbit", "Atmospheric return", "Escape"]
const PLAN_DETAILS := [
	"Remove the guidance target. Keep flying freely.",
	"Show guidance toward an orbit. You fly the ship.",
	"Show guidance for an atmospheric return. You fly the ship.",
	"Show guidance toward an escape trajectory. You fly the ship.",
]

var controls: Node
var active := false
var selected_plan := 0
var current_plan := 0
var panel: PanelContainer
var detail: Label
var hint: Label
var choices: Array[Button] = []

func setup(controls_ref: Node) -> void:
	controls = controls_ref

func _ready() -> void:
	layer = 19
	panel = PanelContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	panel.position = Vector2(-492, 120)
	panel.custom_minimum_size = Vector2(468, 0)
	panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var background := StyleBoxFlat.new()
	background.bg_color = Color(0.018, 0.032, 0.05, 0.96)
	background.border_color = Color(0.25, 0.55, 0.66, 0.85)
	background.set_border_width_all(1)
	background.set_corner_radius_all(8)
	panel.add_theme_stylebox_override("panel", background)
	add_child(panel)
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 20)
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(margin)
	var layout := VBoxContainer.new()
	layout.add_theme_constant_override("separation", 12)
	layout.mouse_filter = Control.MOUSE_FILTER_IGNORE
	margin.add_child(layout)
	var heading := Label.new()
	heading.text = "FLIGHT PLAN"
	heading.add_theme_font_size_override("font_size", 25)
	layout.add_child(heading)
	var note := Label.new()
	note.text = "GUIDANCE ONLY  /  FLIGHT STAYS LIVE"
	note.add_theme_font_size_override("font_size", 16)
	note.modulate = Color(0.7, 0.87, 0.9)
	layout.add_child(note)
	for index in PLAN_NAMES.size():
		var button := Button.new()
		button.custom_minimum_size.y = 46
		button.add_theme_font_size_override("font_size", 22)
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		# Navigate explicitly: built-in GUI focus would also accept arbitrary
		# ui_accept actions, bypassing active-device and custom-binding checks.
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(func(): _choose(index))
		choices.append(button)
		layout.add_child(button)
	detail = Label.new()
	detail.custom_minimum_size = Vector2(410, 52)
	detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	detail.add_theme_font_size_override("font_size", 18)
	layout.add_child(detail)
	hint = Label.new()
	hint.custom_minimum_size.x = 410
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.add_theme_font_size_override("font_size", 16)
	hint.modulate = Color(0.7, 0.8, 0.85)
	layout.add_child(hint)
	panel.hide()
	_refresh()

func open(plan: int = 0) -> bool:
	if plan < 0 or plan >= PLAN_NAMES.size() or not _available() or not is_instance_valid(panel):
		return false
	current_plan = plan
	selected_plan = plan
	active = true
	panel.show()
	_refresh()
	return true

func toggle(plan: int = 0) -> bool:
	if active:
		close()
		return true
	return open(plan)

func close() -> void:
	active = false
	if is_instance_valid(panel):
		panel.hide()

func _available() -> bool:
	return is_instance_valid(controls) and controls.focused and controls.waiting_action.is_empty()

func _bound_to_flight(kind: String, code: int) -> bool:
	if not is_instance_valid(controls):
		return true
	var family := "key" if kind == "key" else "pad"
	for action in controls.bindings:
		var binding: Dictionary = controls.bindings[action][family]
		if binding.get("kind", "") == kind and int(binding.get("code", -1)) == code:
			return true
	return false

func _refresh() -> void:
	if not is_instance_valid(detail):
		return
	for index in choices.size():
		choices[index].text = ("> " if index == selected_plan else "   ") + PLAN_NAMES[index] + ("  [current]" if index == current_plan else "")
		choices[index].modulate = Color(0.55, 0.95, 1.0) if index == selected_plan else Color.WHITE
	detail.text = PLAN_DETAILS[selected_plan]
	var navigation := "D-pad: choose  ·  A / Cross: select\nB / Circle: close"
	if _bound_to_flight("button", JOY_BUTTON_DPAD_UP) or _bound_to_flight("button", JOY_BUTTON_DPAD_DOWN):
		navigation += "\nMapped D-pad directions stay flight controls."
	navigation += "\nTab / Shift-Tab: choose  ·  Enter: select  ·  Esc: close"
	if _bound_to_flight("key", KEY_TAB) or _bound_to_flight("key", KEY_ENTER):
		navigation += "\nMapped keys stay flight controls; use controller or mouse."
	hint.text = navigation

func _choose(plan: int) -> void:
	if not active or not _available() or plan < 0 or plan >= PLAN_NAMES.size():
		return
	close()
	plan_selected.emit(plan)

func handle_event(event: InputEvent) -> bool:
	# Returning whether this exact event was consumed also gives the owning
	# scene a testable routing boundary. No InputMap or pilot state is altered.
	if not active or not _available() or not event.is_pressed() or event.is_echo():
		return false
	var direction := 0
	var accept := false
	var cancel := false
	if event is InputEventJoypadButton:
		if controls.device < 0 or event.device != controls.device:
			return false
		if _bound_to_flight("button", event.button_index):
			return false
		match event.button_index:
			JOY_BUTTON_DPAD_UP: direction = -1
			JOY_BUTTON_DPAD_DOWN: direction = 1
			JOY_BUTTON_A: accept = true
			JOY_BUTTON_B: cancel = true
	elif event is InputEventKey:
		if event.alt_pressed or event.ctrl_pressed or event.meta_pressed:
			return false
		var code: int = event.physical_keycode if event.physical_keycode != 0 else event.keycode
		if _bound_to_flight("key", code):
			return false
		match code:
			KEY_TAB: direction = -1 if event.shift_pressed else 1
			KEY_UP: direction = -1
			KEY_DOWN: direction = 1
			KEY_ENTER, KEY_KP_ENTER: accept = true
			KEY_ESCAPE: cancel = true
	else:
		return false
	if cancel:
		close()
	elif accept:
		_choose(selected_plan)
	elif direction != 0:
		selected_plan = posmod(selected_plan + direction, PLAN_NAMES.size())
		_refresh()
	else:
		return false
	return true

func _input(event: InputEvent) -> void:
	if handle_event(event):
		get_viewport().set_input_as_handled()

func _process(_delta: float) -> void:
	if active and not _available():
		close()
