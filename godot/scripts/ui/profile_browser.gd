extends Control
## A read-only C++ catalog snapshot. Activation is revalidated by the provider.
signal selected(index: int)
signal canceled
var provider: Variant
var device := -1
var snapshot: Dictionary = {}
var buttons: Array[Button] = []
var panel: MarginContainer
var scroll: ScrollContainer
var column: VBoxContainer
var heading: Label
var status: Label
var back: Button
var theme_owner: Theme
var focused := true
var readable_scale := -1.0

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var background := ColorRect.new()
	background.color = Color(0.015, 0.025, 0.04, 1.0)
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	panel = MarginContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(panel)
	theme_owner = Theme.new()
	panel.theme = theme_owner
	scroll = ScrollContainer.new()
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	column = VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(column)
	heading = Label.new()
	heading.text = "SAVED JOURNEYS"
	column.add_child(heading)
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(status)
	hide()

func open() -> void:
	for button in buttons:
		column.remove_child(button)
		button.free()
	buttons.clear()
	if back != null:
		column.remove_child(back)
		back.free()
	snapshot = provider.list_freedom_profiles()
	status.text = str(snapshot.get("diagnostic", ""))
	var entries: Array = snapshot.get("entries", [])
	for index in entries.size():
		var row: Dictionary = entries[index]
		var button := Button.new()
		button.text = "Slot %s · seed %s · %s · tick %s" % [row.id, row.seed, str(row.location).replace("_", " "), row.tick] if row.get("available", false) else "%s · %s" % [row.filename, row.diagnostic]
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		button.disabled = not row.get("available", false)
		button.pressed.connect(func():
			if focused: selected.emit(index))
		column.add_child(button)
		buttons.append(button)
	back = Button.new()
	back.text = "Back"
	back.pressed.connect(func(): canceled.emit())
	column.add_child(back)
	show()
	layout_browser()
	back.grab_focus()
	for button in buttons:
		if not button.disabled:
			button.grab_focus()
			break

func layout_browser() -> void:
	if panel == null: return
	var pixels := Vector2(get_window().size)
	if not size.is_finite() or not pixels.is_finite() or minf(size.x, size.y) < 1.0 or minf(pixels.x, pixels.y) < 1.0: return
	readable_scale = maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	theme_owner.default_font_size = roundi(20 * readable_scale)
	for side in ["left", "right", "top", "bottom"]:
		panel.add_theme_constant_override("margin_" + side, roundi(24 * readable_scale))
	column.add_theme_constant_override("separation", roundi(12 * readable_scale))
	heading.add_theme_font_size_override("font_size", roundi(30 * readable_scale))
	for button in buttons:
		button.custom_minimum_size.y = 44 * readable_scale
	if back != null: back.custom_minimum_size.y = 44 * readable_scale

func _process(_delta: float) -> void:
	if visible: layout_browser()

func _input(event: InputEvent) -> void:
	if not visible or not focused: return
	if (event is InputEventJoypadButton or event is InputEventJoypadMotion) and event.device != device:
		get_viewport().set_input_as_handled()
		return
	var cancel: bool = event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_ESCAPE
	if event is InputEventJoypadButton:
		if event.device != device or not event.pressed: return
		cancel = event.button_index in [JOY_BUTTON_B, JOY_BUTTON_START]
		if event.button_index in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]:
			var choices: Array[Button] = buttons.duplicate()
			choices.append(back)
			var current := choices.find(get_viewport().gui_get_focus_owner())
			var direction := -1 if event.button_index == JOY_BUTTON_DPAD_UP else 1
			for offset in choices.size():
				current = posmod(current + direction, choices.size())
				if not choices[current].disabled:
					choices[current].grab_focus()
					scroll.ensure_control_visible(choices[current])
					break
			get_viewport().set_input_as_handled()
			return
		if event.button_index == JOY_BUTTON_A:
			var button := get_viewport().gui_get_focus_owner() as Button
			if button != null and not button.disabled and is_ancestor_of(button): button.pressed.emit()
			get_viewport().set_input_as_handled()
			return
	if cancel:
		canceled.emit()
		get_viewport().set_input_as_handled()

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT: focused = false
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN: focused = true
