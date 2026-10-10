extends CanvasLayer
## Native focus-navigation controls. Menu input never advances flight.
signal save_requested
signal load_requested
signal title_requested
signal assistance_requested(enabled: bool)
signal hold_requested(enabled: bool)
signal port_requested(command: String, ordinal: int)
signal resumed
signal quit_requested
signal reset_flight
signal debug_changed(value: bool)
signal camera_distance_changed(value: float)
signal practice_requested(reentry: bool)
signal guidance_requested
signal ship_audio_muted(value: bool)
signal audio_mix_changed
var saved_flight := false
var saved_wayfarer := false
var saved_assist_button: CheckButton
var saved_hold_button: Button
var saved_hold_off_button: Button
var saved_hold_note: Label
var saved_port_buttons: Dictionary = {}
var saved_note: Label
var save_button: Button
var load_button: Button
var title_button: Button
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
var settings_view: Control
var settings_button: Button
var entry_focus_frames := 0
var menu_theme: Theme
var menu_heading: Label
var binding_heading: Label
var menu_layout: VBoxContainer
var content_columns: BoxContainer
var settings_scroll: ScrollContainer
var sized_controls: Array[Control] = []
var readable_scale := -1.0

static func saved_layout_scale(logical: Vector2, pixels: Vector2) -> float:
	if not logical.is_finite() or not pixels.is_finite() or minf(logical.x, logical.y) < 1.0 or minf(pixels.x, pixels.y) < 1.0:
		return -1.0
	var scale := maxf(1.0, maxf(logical.x / pixels.x, logical.y / pixels.y))
	return scale if is_finite(scale) and scale <= 64.0 else -1.0

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
	menu_layout = layout
	layout.add_theme_constant_override("separation", 24)
	margin.add_child(layout)
	var heading := Label.new()
	menu_heading = heading
	heading.text = "APSIS DRIFT / FLIGHT CONTROLS"
	heading.add_theme_font_size_override("font_size", 34)
	layout.add_child(heading)
	var columns: BoxContainer = BoxContainer.new() if saved_flight else HBoxContainer.new()
	content_columns = columns
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	columns.add_theme_constant_override("separation", 40)
	layout.add_child(columns)
	left_scroll = ScrollContainer.new()
	left_scroll.custom_minimum_size.x = 600
	left_scroll.follow_focus = true
	left_scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL if saved_flight else Control.SIZE_FILL
	left_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	left_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	columns.add_child(left_scroll)
	var left := VBoxContainer.new()
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_theme_constant_override("separation", 12)
	left_scroll.add_child(left)
	var theme := Theme.new()
	menu_theme = theme
	theme.default_font_size = 26
	margin.theme = theme
	var note := Label.new()
	if saved_flight:
		saved_note = note
		note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	elif controls.thrust_mode:
		note.text = "THRUST LAB / CURRENT BINDINGS ON THE RIGHT\nFlight basics explains movement, orbit and limits.\n\nD-pad: navigate · A / Cross: select\nB / Circle or Esc / Start: resume"
	else:
		note.text = "LEGACY FOUR-AXIS FLIGHT / LAYOUT 4\nRT / R2: forward · LT / L2: reverse\nRight stick: heading / rise-fall\nLB / L1: strafe left · RB / R1: strafe right\nHold left stick click / L3: head-look\nRelease: snap to ship centerline\nCenter right stick to resume yaw / rise-fall.\nLeft-stick pitch / roll require thrust model.\n\nD-pad: navigate · A / Cross: select\nB / Circle or Esc / Start: resume"
	note.add_theme_font_size_override("font_size", 23)
	left.add_child(note)
	resume_button = add_button(left, "Resume flight", func(): resumed.emit())
	if saved_flight:
		save_button = add_button(left, "Save As…", func(): save_requested.emit())
		load_button = add_button(left, "Load…", func(): load_requested.emit())
		settings_button = add_button(left, "Settings (paused)", show_settings)
		title_button = add_button(left, "Title…", func(): title_requested.emit())
		saved_assist_button = CheckButton.new()
		saved_assist_button.text = "Assisted piloting"
		saved_assist_button.toggled.connect(func(value: bool): assistance_requested.emit(value))
		left.add_child(saved_assist_button)
		sized_controls.append(saved_assist_button)
		saved_hold_button = add_button(left, "Hold current orbit\nradius and plane", func(): hold_requested.emit(true))
		saved_hold_off_button = add_button(left, "Disable orbit-hold\nrequest", func(): hold_requested.emit(false))
		saved_hold_note = Label.new()
		saved_hold_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		saved_hold_note.custom_minimum_size.x = 560
		left.add_child(saved_hold_note)
		get_window().size_changed.connect(layout_saved_hold)
		layout_saved_hold()
		if saved_wayfarer:
			for item in [["Target D1", "select_freedom_port", 1], ["Target D2", "select_freedom_port", 2], ["Approach port with thrusters", "begin_freedom_port_approach", 0], ["Cancel approach aid", "cancel_freedom_port_approach", 0], ["Capture selected port", "capture_freedom_port", 0], ["Release attached port", "release_freedom_port", 0], ["Land / deploy gear", "request_freedom_landing", 0], ["Stow landing gear", "stow_freedom_landing_gear", 0], ["Liftoff with thrusters", "liftoff_freedom_surface", 0], ["Cancel surface aid", "cancel_freedom_surface_maneuver", 0]]:
				var button := add_button(left, item[0], func(): port_requested.emit(item[1], item[2]))
				saved_port_buttons[item[0]] = button
	if saved_flight or controls.thrust_mode:
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
		sized_controls.append(audio_toggle)
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
	if not saved_flight:
		add_button(left, "Reset experimental flight", func(): reset_flight.emit())
		if controls.thrust_mode:
			add_button(left, "Flight guidance — flight continues; no autopilot", func(): guidance_requested.emit())
			add_button(left, "Orbit practice — relocates unsaved flight", func(): practice_requested.emit(false))
			add_button(left, "Re-entry practice — relocates unsaved flight", func(): practice_requested.emit(true))
	add_button(left, "Toggle fullscreen", func():
		var full := DisplayServer.window_get_mode() == DisplayServer.WINDOW_MODE_FULLSCREEN
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED if full else DisplayServer.WINDOW_MODE_FULLSCREEN))
	if not saved_flight:
		add_slider(left, "Stick / trigger dead zone", "deadzone", 0.05, 0.45, 0.01)
		add_slider(left, "Response curve", "curve", 1.0, 3.0, 0.1)
		add_slider(left, "Head-look speed", "look_speed", 0.3, 4.0, 0.1)
	if not saved_flight:
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
	if not saved_flight:
		var invert := CheckButton.new()
		invert.text = "Invert vertical head-look"
		invert.button_pressed = controls.settings.invert_look
		invert.toggled.connect(func(value: bool):
			controls.settings.invert_look = value
			controls.save_settings())
		left.add_child(invert)
		sized_controls.append(invert)
		var prompts := OptionButton.new()
		for option in ["Prompts: automatic", "Prompts: Xbox", "Prompts: PlayStation"]:
			prompts.add_item(option)
		prompts.selected = int(controls.settings.prompts)
		prompts.item_selected.connect(func(index: int):
			controls.settings.prompts = index
			controls.save_settings()
			refresh_bindings())
		left.add_child(prompts)
		sized_controls.append(prompts)
	add_button(left, "Restore default bindings", func():
		controls.defaults()
		controls.install()
		controls.save_settings()
		refresh_bindings())
	add_button(left, "Quit without autosave" if saved_flight else "Quit study", func(): quit_requested.emit())
	message = Label.new()
	message.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	message.custom_minimum_size.y = 64
	message.add_theme_font_size_override("font_size", 21)
	# Capture/conflict and safety messages must remain visible while either
	# settings column scrolls; capture deliberately consumes navigation input.
	layout.add_child(message)
	var scroll := ScrollContainer.new()
	settings_scroll = scroll
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.follow_focus = true
	columns.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 10)
	scroll.add_child(rows)
	var title := Label.new()
	binding_heading = title
	title.text = "REMAP  /  choose a binding, then press its replacement"
	if saved_flight: title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
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
	if saved_flight or controls.thrust_mode:
		basics = preload("res://scripts/flight/flight_basics.gd").new()
		basics.saved_flight = saved_flight
		basics.saved_wayfarer = saved_wayfarer
		basics.controls = controls
		basics.rotational_coasting = rotational_coasting
		basics.orbit_preserving_assist = orbit_preserving_assist
		panel.add_child(basics)
		basics.back_requested.connect(close_basics)
	if saved_flight:
		settings_view = preload("res://scripts/ui/control_settings.gd").new()
		settings_view.controls = controls
		panel.add_child(settings_view)
		settings_view.back_requested.connect(close_settings)
	refresh_bindings()
	if saved_flight:
		get_window().size_changed.connect(layout_saved_menu)
		layout_saved_menu()
	panel.hide()

func add_button(parent: Node, text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 42
	button.pressed.connect(action)
	parent.add_child(button)
	sized_controls.append(button)
	if saved_flight and readable_scale > 0:
		button.custom_minimum_size.y = ceili(44 * readable_scale)
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
	sized_controls.append(slider)

func layout_saved_menu() -> void:
	if not saved_flight or menu_theme == null: return
	var pixels := Vector2(get_window().size)
	var scale := saved_layout_scale(get_viewport().get_visible_rect().size, pixels)
	if scale <= 0: return
	var stacked := pixels.x < 1000.0
	if is_equal_approx(scale, readable_scale) and content_columns.vertical == stacked: return
	readable_scale = scale
	menu_theme.default_font_size = ceili(18 * scale)
	menu_heading.add_theme_font_size_override("font_size", ceili(26 * scale))
	binding_heading.add_theme_font_size_override("font_size", ceili(18 * scale))
	saved_note.add_theme_font_size_override("font_size", ceili(18 * scale))
	message.add_theme_font_size_override("font_size", ceili(18 * scale))
	message.custom_minimum_size.y = ceili(44 * scale)
	for side in ["left", "right", "top", "bottom"]:
		menu_margin.add_theme_constant_override("margin_" + side, ceili(24 * scale))
	menu_layout.add_theme_constant_override("separation", ceili(12 * scale))
	content_columns.add_theme_constant_override("separation", ceili(20 * scale))
	content_columns.vertical = stacked
	left_scroll.custom_minimum_size.x = minf(320.0, maxf(1.0, pixels.x - 48.0)) * scale
	for item in sized_controls:
		item.custom_minimum_size.y = ceili(44 * scale)
	layout_saved_hold()


func layout_saved_hold() -> void:
	if saved_hold_note == null or get_window().size.y <= 0: return
	var logical_height: float = get_viewport().get_visible_rect().size.y
	if not is_finite(logical_height) or logical_height <= 0.0: return
	var scale := logical_height / float(get_window().size.y)
	for item in [saved_hold_button, saved_hold_off_button, saved_hold_note]:
		item.add_theme_font_size_override("font_size", ceili(18.0 * scale))
	for button in [saved_hold_button, saved_hold_off_button]:
		button.custom_minimum_size.y = ceili(44.0 * scale)


func sync_saved_state(state: Dictionary, failed: bool) -> void:
	if not saved_flight:
		return
	if is_instance_valid(basics):
		basics.saved_context = state.duplicate(true)
		if basics.visible: basics.refresh()
	saved_assist_button.set_pressed_no_signal(state.get("assistance", false))
	saved_assist_button.text = "Assisted piloting · ON" if state.get("assistance", false) else "Assisted piloting · OFF"
	var jumping: bool = state.get("jump", {}).get("phase", "idle") != "idle"
	var walking: bool = not state.get("surface_walk", {}).is_empty()
	saved_assist_button.disabled = failed or jumping or walking
	var hold: Dictionary = state.get("orbit_hold", {})
	var requested: bool = state.get("hold_enabled", false)
	saved_hold_button.disabled = failed or jumping or walking or not hold.get("available", false)
	saved_hold_off_button.disabled = failed or jumping or walking or not requested
	saved_hold_button.tooltip_text = "Select the current radius and angular-momentum plane; correction uses real thrusters." if hold.get("available", false) else str(hold.get("refusal", "Orbit-hold selection unavailable."))
	saved_hold_note.text = "Hold request: %.1f km radius.\nCorrection seeks a circular orbit in the selected plane.\nUses fuel; manual translation pauses correction.\n%s" % [float(hold.get("radius_metres", 0.0)) / 1000.0, "Assistance OFF: request retained, correction paused." if not state.get("assistance", false) else "Assistance ON: correction remains subject to flight conditions and available thrust."] if requested else "No hold requested. Assisted spaceflight still coasts.\n" + ("Select a radius and plane from a stable orbit above the air boundary." if hold.get("available", false) else str(hold.get("refusal", "Orbit-hold selection unavailable.")))
	resume_button.disabled = failed
	var attached: bool = state.get("attached", false)
	var assessment: Dictionary = state.get("docking", {})
	for name in saved_port_buttons:
		var button: Button = saved_port_buttons[name]
		button.disabled = failed
		if jumping or walking:
			button.disabled = true
			continue
		var surface: Dictionary = state.get("surface", {})
		var landed: bool = surface.get("landed", false)
		if name == "Land / deploy gear":
			button.text = "Land with thrusters" if state.get("assistance", false) else "Deploy gear · manual landing"
			button.disabled = failed or attached or landed
		elif name == "Stow landing gear":
			button.disabled = failed or attached or landed or not surface.get("gear_deployed", false)
		elif name == "Liftoff with thrusters":
			button.disabled = failed or not landed
		elif name == "Cancel surface aid":
			button.disabled = failed or surface.get("maneuver", "off") == "off"
		elif name.begins_with("Target"):
			button.disabled = failed or attached or not state.get("station_available", true)
			button.tooltip_text = "Release the attached port before selecting another." if attached else "Select the port to approach."
		elif name == "Release attached port":
			button.disabled = failed or not attached
			button.tooltip_text = "Release the physical attachment." if attached else "No port is attached."
		elif name == "Approach port with thrusters":
			var approach: Dictionary = state.get("port_approach", {})
			button.disabled = failed or approach.get("active", false) or not approach.get("available", false)
			button.tooltip_text = "Resume explicitly; manual control cancels. Capture is separate." if approach.get("available", false) else str(approach.get("refusal", "Select a port first."))
		elif name == "Cancel approach aid":
			button.disabled = failed or not state.get("port_approach", {}).get("active", false)
			button.tooltip_text = "Cancel thruster approach."
		else:
			button.disabled = failed or attached or surface.get("gear_deployed", false) or not assessment.get("ready", false)
			button.tooltip_text = "Already attached." if attached else str(assessment.get("reason", "Select a port for approach."))

func refresh_bindings() -> void:
	if saved_flight and saved_note != null:
		var family := "pad" if controls.last_device == "pad" else "key"
		saved_note.text = "SAVED FLIGHT / BINDINGS IN THE REMAP LIST\n%s main · %s weaker retro\n%s look hold · %s view · %s assist\nRelease look to recenter; center shared yaw/heave before reuse.\nSticks/keys request physical actuator torque, not target turn rate.\nAssistance supports piloting; it is not autopilot.\nSave As preserves committed flight. Quit does not autosave.\n\nD-pad: navigate · A / Cross: select\nB / Circle or Esc / Start: explicit resume after neutral." % [controls.binding_label("forward", family), controls.binding_label("backward", family), controls.binding_label("look_hold", family), controls.binding_label("camera", family), controls.binding_label("assist", family)]
	for item in binding_buttons:
		item.button.text = ("Key: " if item.family == "key" else "Pad: ") + controls.binding_label(item.action, item.family)
	if is_instance_valid(basics):
		basics.refresh()

func show_settings() -> void:
	if not panel.visible or not menu_margin.visible or not controls.focused or not controls.waiting_action.is_empty() or not is_instance_valid(settings_view): return
	entry_focus_frames = 0
	menu_margin.hide()
	settings_view.open()

func close_settings() -> void:
	if not is_instance_valid(settings_view): return
	settings_view.pending.clear()
	settings_view.hide()
	menu_margin.show()
	refresh_bindings()
	if panel.visible:
		settings_button.grab_focus()
		left_scroll.ensure_control_visible(settings_button)
		ensure_entry_focus_visible()

func back_from_nested() -> bool:
	if not saved_flight or not panel.visible or not controls.focused: return false
	if is_instance_valid(settings_view) and settings_view.visible:
		settings_view.cancel()
		return true
	if is_instance_valid(basics) and basics.visible:
		close_basics()
		return true
	return false

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
	sized_controls.append(slider)
	audio_sliders[key] = {"slider": slider, "label": label, "title": title}
	slider.value_changed.connect(func(value: float):
		if audio_preferences.set_level(key, value):
			label.text = "%s: %d%%" % [title, roundi(value*100)]
			audio_preferences.save_settings()
			audio_mix_changed.emit())

func show_menu(reason := "") -> void:
	layout_saved_menu()
	# A safety pause retains pending preferences; it never resumes or writes.
	if is_instance_valid(settings_view) and settings_view.visible:
		if not reason.is_empty(): controls.status = reason
		settings_view.refresh_status()
		return
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
	# Count owned process frames instead of leaving a coroutine alive when a
	# rapid Title/Continue transition frees this menu before layout finishes.
	entry_focus_frames = 2

func hide_menu() -> void:
	entry_focus_frames = 0
	if is_instance_valid(settings_view):
		settings_view.pending.clear()
		settings_view.hide()
	controls.waiting_action = ""
	panel.hide()
	get_viewport().gui_release_focus()

func sync_view_controls(debug: bool, distance: float) -> void:
	diagnostic_toggle.set_pressed_no_signal(debug)
	camera_slider.set_value_no_signal(distance)

func _process(_delta: float) -> void:
	if saved_flight: layout_saved_menu()
	if entry_focus_frames > 0:
		entry_focus_frames -= 1
		if entry_focus_frames == 0 and panel.visible and menu_margin.visible:
			var target := get_viewport().gui_get_focus_owner()
			if is_instance_valid(target) and left_scroll.is_ancestor_of(target):
				left_scroll.ensure_control_visible(target)
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
		if not back_from_nested(): resumed.emit()
		get_viewport().set_input_as_handled()
