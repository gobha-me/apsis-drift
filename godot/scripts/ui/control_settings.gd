extends Control
## Pending UI over the existing v4 input provider; owns no simulation or bindings.
signal back_requested
const Controls = preload("res://scripts/ui/player_input.gd")
var controls: Node
var pending: Dictionary = {}
var fields: Dictionary = {}
var status: Label
var scroll: ScrollContainer
var column: VBoxContainer
var margin: MarginContainer
var theme_owner: Theme
var apply_button: Button
var cancel_button: Button
var defaults_button: Button
var focused := true
var readable_scale := -1.0
var focus_scroll_frames := 0

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var background := ColorRect.new()
	background.color = Color(0.015, 0.025, 0.04)
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	margin = MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(margin)
	theme_owner = Theme.new()
	margin.theme = theme_owner
	var layout := VBoxContainer.new()
	margin.add_child(layout)
	scroll = ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	layout.add_child(scroll)
	column = VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(column)
	var heading := Label.new()
	heading.text = "SETTINGS / CONTROLS"
	column.add_child(heading)
	var note := Label.new()
	note.text = "Changes stay pending until Apply. Cancel leaves your controls unchanged.\nFlight bindings can be remapped in the paused flight controls.\nStation walking uses the dead zone and curve; flight uses all choices below."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(note)
	for item in [["deadzone", "Stick / trigger dead zone", 0.05, 0.45, 0.01], ["curve", "Response curve", 1.0, 3.0, 0.1], ["look_speed", "Flight head-look speed", 0.3, 4.0, 0.1]]:
		var label := Label.new()
		column.add_child(label)
		var slider := HSlider.new()
		slider.min_value = item[2]
		slider.max_value = item[3]
		slider.step = item[4]
		slider.accessibility_name = item[1]
		column.add_child(slider)
		fields[item[0]] = {"control": slider, "label": label, "title": item[1]}
		slider.value_changed.connect(func(value: float):
			pending[item[0]] = value
			refresh_status()
			label.text = "%s: %.2f" % [item[1], value])
	var invert := CheckButton.new()
	invert.text = "Invert vertical flight head-look"
	column.add_child(invert)
	fields.invert_look = {"control": invert}
	invert.toggled.connect(func(value: bool):
		pending.invert_look = value
		refresh_status())
	var prompts := OptionButton.new()
	for option in ["Prompts: automatic", "Prompts: Xbox", "Prompts: PlayStation"]: prompts.add_item(option)
	column.add_child(prompts)
	fields.prompts = {"control": prompts}
	prompts.item_selected.connect(func(value: int):
		pending.prompts = value
		refresh_status())
	defaults_button = add_button("Restore Defaults (pending)", restore_defaults)
	apply_button = add_button("Apply", apply_pending)
	cancel_button = add_button("Cancel / Back", cancel)
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	layout.add_child(status)
	get_window().size_changed.connect(func(): focus_scroll_frames = 2)
	hide()

func add_button(text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.pressed.connect(action)
	column.add_child(button)
	return button

func open() -> void:
	pending = controls.settings.duplicate(true)
	show()
	refresh_fields()
	refresh_status()
	layout_settings()
	fields.deadzone.control.grab_focus()
	focus_scroll_frames = 2

func refresh_fields() -> void:
	for key in ["deadzone", "curve", "look_speed"]:
		fields[key].control.set_value_no_signal(pending[key])
		fields[key].label.text = "%s: %.2f" % [fields[key].title, pending[key]]
	fields.invert_look.control.set_pressed_no_signal(pending.invert_look)
	fields.prompts.control.select(int(pending.prompts))

func refresh_status() -> void:
	var dirty := false
	for key in Controls.DEFAULT_SETTINGS:
		if pending[key] != controls.settings[key]: dirty = true
	status.text = "Pending changes. Apply to keep them." if dirty else "No pending changes."
	apply_button.disabled = not focused or not controls.focused or (controls.persist and not controls.preferences_writable)
	if controls.persist and not controls.preferences_writable:
		status.text += "\nExisting controls file could not be read safely; Apply is disabled. " + controls.status
	elif not controls.persist:
		status.text += "\nPersistence is disabled for this run. Apply affects this session only."

func restore_defaults() -> void:
	if not focused or not controls.focused: return
	for key in Controls.DEFAULT_SETTINGS: pending[key] = Controls.DEFAULT_SETTINGS[key]
	refresh_fields()
	refresh_status()

func apply_pending() -> void:
	if not focused or not controls.focused: return
	if not controls.apply_preferences(pending):
		status.text = controls.status + " Pending changes retained."
		return
	controls.install()
	controls.set_enabled(false)
	pending = controls.settings.duplicate(true)
	refresh_status()
	status.text = controls.status

func cancel() -> void:
	if not focused or not controls.focused: return
	pending.clear()
	hide()
	back_requested.emit()

func layout_settings() -> void:
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or not size.is_finite() or minf(pixels.x, pixels.y) <= 0 or minf(size.x, size.y) <= 0: return
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	if is_equal_approx(scale, readable_scale): return
	readable_scale = scale
	focus_scroll_frames = 2
	theme_owner.default_font_size = ceili(20 * scale)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, roundi(32 * scale))
	column.add_theme_constant_override("separation", roundi(12 * scale))
	for field in fields.values(): field.control.custom_minimum_size.y = ceili(44 * scale)
	for button in [defaults_button, apply_button, cancel_button]: button.custom_minimum_size.y = ceili(44 * scale)

func _process(_delta: float) -> void:
	if not visible: return
	layout_settings()
	if focus_scroll_frames > 0:
		focus_scroll_frames -= 1
		if focus_scroll_frames == 0:
			# Resize/theme layout completes after the original focus event.
			var target := get_viewport().gui_get_focus_owner()
			if is_instance_valid(target) and column.is_ancestor_of(target):
				scroll.ensure_control_visible(target)

func _input(_event: InputEvent) -> void:
	if visible and (not focused or not controls.focused): get_viewport().set_input_as_handled()

func _unhandled_input(event: InputEvent) -> void:
	if visible and event.is_action_pressed("ui_cancel"):
		cancel()
		get_viewport().set_input_as_handled()

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
	else: return
	if visible: refresh_status()
