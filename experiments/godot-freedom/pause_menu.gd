extends CanvasLayer
## Native focus-navigation controls. Menu input never advances flight.
signal resumed
signal quit_requested
signal reset_flight
signal debug_changed(value: bool)
signal camera_distance_changed(value: float)
signal practice_requested(reentry: bool)
signal guidance_requested
signal ship_audio_muted(value: bool)
signal audio_mix_changed
var ship_audio_available := false
var audio_preferences: RefCounted
var audio_sliders: Dictionary = {}
var control_sliders: Dictionary = {}
var controls: Node
var rotational_coasting := false
var orbit_preserving_assist := false
var panel: Control
var resume_button: Button
var message: Label
var binding_buttons: Array[Dictionary] = []
var diagnostic_toggle: CheckButton
var camera_slider: HSlider
var audio_toggle: CheckButton
var left_scroll: ScrollContainer
var menu_margin: MarginContainer
var basics: Control
var basics_button: Button

func _ready() -> void:
	layer = 20
	panel = ColorRect.new()
	panel.color = Color(0.015, 0.025, 0.04, 0.98)
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(panel)
	var margin := MarginContainer.new()
	menu_margin = margin
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	panel.add_child(margin)
	var layout := VBoxContainer.new()
	layout.add_theme_constant_override("separation", 24)
	margin.add_child(layout)
	var heading := Label.new()
	heading.text = "APSIS DRIFT / FLIGHT CONTROLS"
	heading.add_theme_font_size_override("font_size", 34)
	layout.add_child(heading)
	var columns := HBoxContainer.new()
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 40)
	layout.add_child(columns)
	left_scroll = ScrollContainer.new()
	left_scroll.custom_minimum_size.x = 600
	left_scroll.follow_focus = true
	left_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	columns.add_child(left_scroll)
	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 12)
	left_scroll.add_child(left)
	var theme := Theme.new()
	theme.default_font_size = 26
	margin.theme = theme
	var note := Label.new()
	if controls.thrust_mode:
		note.text = "THRUST LAB / CURRENT BINDINGS ON THE RIGHT\nFlight basics explains movement, orbit and limits.\n\nD-pad: navigate · A / Cross: select\nB / Circle or Esc / Start: resume"
	else:
		note.text = "LEGACY FOUR-AXIS FLIGHT / LAYOUT 4\nRT / R2: forward · LT / L2: reverse\nRight stick: heading / rise-fall\nLB / L1: strafe left · RB / R1: strafe right\nHold left stick click / L3: head-look\nRelease: snap to ship centerline\nCenter right stick to resume yaw / rise-fall.\nLeft-stick pitch / roll require thrust model.\n\nD-pad: navigate · A / Cross: select\nB / Circle or Esc / Start: resume"
	note.add_theme_font_size_override("font_size", 23)
	left.add_child(note)
	resume_button = add_button(left, "Resume flight", func(): resumed.emit())
	if controls.thrust_mode:
		basics_button = add_button(left, "Flight basics (paused)", show_basics)
	if ship_audio_available:
		audio_toggle = CheckButton.new()
		audio_toggle.text = "Mute ship audio" if audio_preferences != null else "Mute ship audio prototype (this session)"
		if audio_preferences != null:
			audio_toggle.button_pressed = audio_preferences.muted
		audio_toggle.toggled.connect(func(value: bool):
			if audio_preferences != null:
				audio_preferences.muted = value
				audio_preferences.save_settings()
			ship_audio_muted.emit(value))
		left.add_child(audio_toggle)
		if audio_preferences != null:
			for item in [["master", "Ship audio master"], ["machinery", "Background machinery"],
				["propulsion", "Propulsion"], ["atmosphere", "Atmospheric airflow"]]:
				add_audio_slider(left, item[1], item[0])
			add_button(left, "Restore ship audio defaults", func():
				audio_preferences.reset_defaults()
				audio_preferences.save_settings()
				audio_toggle.set_pressed_no_signal(false)
				for key in audio_sliders:
					var item: Dictionary = audio_sliders[key]
					item.slider.set_value_no_signal(audio_preferences.levels[key])
					item.label.text = "%s: %d%%" % [item.title, roundi(audio_preferences.levels[key]*100)]
				ship_audio_muted.emit(false)
				audio_mix_changed.emit())
	add_button(left, "Reset experimental flight", func(): reset_flight.emit())
	if controls.thrust_mode:
		add_button(left, "Flight guidance — flight continues; no autopilot", func(): guidance_requested.emit())
		add_button(left, "Orbit practice — relocates unsaved flight", func(): practice_requested.emit(false))
		add_button(left, "Re-entry practice — relocates unsaved flight", func(): practice_requested.emit(true))
	add_button(left, "Toggle fullscreen", func():
		var full := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if full else DisplayServer.WINDOW_MODE_FULLSCREEN))
	add_slider(left, "Stick / trigger dead zone", "deadzone", 0.05, 0.45, 0.01)
	add_slider(left, "Response curve", "curve", 1.0, 3.0, 0.1)
	add_slider(left, "Head-look speed", "look_speed", 0.3, 4.0, 0.1)
	var debug := CheckButton.new()
	diagnostic_toggle = debug
	debug.text = "Engineering diagnostics (F3 in flight)"
	debug.toggled.connect(func(value: bool): debug_changed.emit(value))
	left.add_child(debug)
	var zoom_label := Label.new()
	zoom_label.text = "Chase camera distance (mouse wheel outside)"
	left.add_child(zoom_label)
	var zoom := HSlider.new()
	camera_slider = zoom
	zoom.min_value = 22
	zoom.max_value = 100
	zoom.value = 36
	zoom.step = 1
	zoom.custom_minimum_size.y = 30
	zoom.value_changed.connect(func(value: float): camera_distance_changed.emit(value))
	left.add_child(zoom)
	var invert := CheckButton.new()
	invert.text = "Invert vertical head-look"
	invert.button_pressed = controls.settings.invert_look
	invert.toggled.connect(func(value: bool):
		controls.settings.invert_look = value
		controls.save_settings())
	left.add_child(invert)
	var prompts := OptionButton.new()
	for option in ["Prompts: automatic", "Prompts: Xbox", "Prompts: PlayStation"]:
		prompts.add_item(option)
	prompts.selected = int(controls.settings.prompts)
	prompts.item_selected.connect(func(index: int):
		controls.settings.prompts = index
		controls.save_settings()
		refresh_bindings())
	left.add_child(prompts)
	add_button(left, "Restore default bindings", func():
		controls.defaults()
		controls.install()
		controls.save_settings()
		refresh_bindings())
	add_button(left, "Quit study", func(): quit_requested.emit())
	message = Label.new()
	message.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	message.custom_minimum_size.y = 64
	message.add_theme_font_size_override("font_size", 21)
	# Capture/conflict and safety messages must remain visible while either
	# settings column scrolls; capture deliberately consumes navigation input.
	layout.add_child(message)
	var scroll := ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.follow_focus = true
	columns.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 10)
	scroll.add_child(rows)
	var title := Label.new()
	title.text = "REMAP  /  choose a binding, then press its replacement"
	title.add_theme_font_size_override("font_size", 22)
	rows.add_child(title)
	for i in controls.ACTIONS.size():
		var action: String = controls.ACTIONS[i]
		var label := Label.new()
		label.text = controls.TITLES[i]
		rows.add_child(label)
		var row := HBoxContainer.new()
		rows.add_child(row)
		for family in ["key", "pad"]:
			var bind := add_button(row, "", func():
				controls.waiting_action = action
				controls.waiting_family = family
				controls.status = "Press new %s for %s. Esc / Start cancels." % [family, action])
			bind.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			bind.custom_minimum_size.x = 260
			binding_buttons.append({"button": bind, "action": action, "family": family})
	controls.bindings_changed.connect(refresh_bindings)
	if controls.thrust_mode:
		basics = preload("res://flight_basics.gd").new()
		basics.controls = controls
		basics.rotational_coasting = rotational_coasting
		basics.orbit_preserving_assist = orbit_preserving_assist
		panel.add_child(basics)
		basics.back_requested.connect(close_basics)
	refresh_bindings()
	panel.hide()

func add_button(parent: Node, text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 42
	button.pressed.connect(action)
	parent.add_child(button)
	return button

func add_slider(parent: Node, title: String, key: String, low: float, high: float, step: float) -> void:
	var label := Label.new()
	label.text = "%s: %.2f" % [title, controls.settings[key]]
	parent.add_child(label)
	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.step = step
	slider.value = controls.settings[key]
	control_sliders[key] = slider
	slider.custom_minimum_size.y = 30
	slider.value_changed.connect(func(value: float):
		controls.settings[key] = value
		label.text = "%s: %.2f" % [title, value]
		controls.install()
		controls.save_settings())
	parent.add_child(slider)

func refresh_bindings() -> void:
	for item in binding_buttons:
		item.button.text = ("Key: " if item.family == "key" else "Pad: ") + controls.binding_label(item.action, item.family)
	if is_instance_valid(basics):
		basics.refresh()

func show_basics() -> void:
	if not panel.visible or not controls.focused or not controls.waiting_action.is_empty() or not is_instance_valid(basics):
		return
	menu_margin.hide()
	basics.open()

func close_basics() -> void:
	if not is_instance_valid(basics):
		return
	basics.hide()
	menu_margin.show()
	if panel.visible:
		basics_button.grab_focus()
		left_scroll.ensure_control_visible(basics_button)

func add_audio_slider(parent: Node, title: String, key: String) -> void:
	var label := Label.new()
	label.text = "%s: %d%%" % [title, roundi(audio_preferences.levels[key]*100)]
	parent.add_child(label)
	var slider := HSlider.new()
	slider.min_value = 0
	slider.max_value = 1
	slider.step = 0.05
	slider.value = audio_preferences.levels[key]
	slider.custom_minimum_size.y = 34
	parent.add_child(slider)
	audio_sliders[key] = {"slider": slider, "label": label, "title": title}
	slider.value_changed.connect(func(value: float):
		if audio_preferences.set_level(key, value):
			label.text = "%s: %d%%" % [title, roundi(value*100)]
			audio_preferences.save_settings()
			audio_mix_changed.emit())

func show_menu(reason := "") -> void:
	if is_instance_valid(basics):
		basics.hide()
	menu_margin.show()
	controls.status = reason if not reason.is_empty() else controls.status
	panel.show()
	refresh_bindings()
	resume_button.grab_focus()
	ensure_entry_focus_visible()

func ensure_entry_focus_visible() -> void:
	# First show can precede container layout. follow_focus alone scrolls using
	# the old geometry, leaving the controller's focused Resume button offscreen.
	await get_tree().process_frame
	await get_tree().process_frame
	if panel.visible and resume_button.has_focus():
		left_scroll.ensure_control_visible(resume_button)

func hide_menu() -> void:
	controls.waiting_action = ""
	panel.hide()
	get_viewport().gui_release_focus()

func sync_view_controls(debug: bool, distance: float) -> void:
	diagnostic_toggle.set_pressed_no_signal(debug)
	camera_slider.set_value_no_signal(distance)

func _process(_delta: float) -> void:
	if panel.visible:
		var device_name := Input.get_joy_name(controls.device) if controls.device >= 0 else ""
		message.text = ("No controller detected. " if controls.device < 0 else "Controller: %s. " % (device_name if not device_name.is_empty() else "mapped test device")) + controls.status
		if audio_preferences != null and not audio_preferences.status.is_empty():
			message.text += "\n" + audio_preferences.status
		refresh_bindings()

func _input(_event: InputEvent) -> void:
	# Stop queued GUI accepts/navigation while the window safety pause owns focus.
	if panel.visible and not controls.focused:
		get_viewport().set_input_as_handled()

func _unhandled_input(event: InputEvent) -> void:
	if panel.visible and controls.waiting_action.is_empty() and event.is_action_pressed("ui_cancel"):
		resumed.emit()
		get_viewport().set_input_as_handled()
