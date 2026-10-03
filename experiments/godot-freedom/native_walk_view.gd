extends Control
## First-person station presentation. C++ resolves support, motion and shared time.
const StationPresentation = preload("res://native_station_view.gd")
const HopperPresentation = preload("res://hopper_presentation.gd")
const FlightView = preload("res://native_flight_view.gd")
var bridge: Variant
var state: Dictionary = {}
var station: Node3D
var ship: Node3D
var camera: Camera3D
var scene: Node3D
var viewport: SubViewport
var telemetry: Label
var pause_button: Button
var save_button: Button
var save_dialog: FileDialog
var save_status: Label
var hud_scroll: ScrollContainer
var hud_column: VBoxContainer
var hud_theme: Theme
var hud_scroll_style: StyleBox
var hud_hint: Label
var paused := false
var focused := true
var controls_armed := false
var selected_pad := -1
var available_pads: Dictionary = {}
var requested_heading := 0.0
var pitch := 0.0
var error := ""


static func finite_triplet(value: Variant) -> bool:
	if not value is PackedFloat64Array or value.size() != 3:
		return false
	for v in value:
		if not is_finite(v):
			return false
	return true


static func valid_state(value: Dictionary) -> bool:
	if value.get("mode") != "freedom_walk" or not value.get("geometry_version") is int or value.get("geometry_version") != 1:
		return false
	for key in ["foot_position_metres", "eye_position_metres", "velocity_metres_per_second", "actor_position_metres", "actor_global_position_metres"]:
		if not finite_triplet(value.get(key)):
			return false
	for key in ["station_position", "actor_eye_position"]:
		if not value.get(key) is Vector3 or not value[key].is_finite():
			return false
	if not value.get("station_basis") is Basis or not value.station_basis.is_finite() or abs(value.station_basis.determinant() - 1.0) > 0.00001:
		return false
	if not value.get("heading_radians") is float or not is_finite(value.heading_radians) or abs(value.heading_radians) > PI or not value.get("continued") is bool:
		return false
	for key in ["universe_seed", "system_id", "planet_id", "station_id", "craft_id", "actor_id", "tick"]:
		var text: Variant = value.get(key)
		if not text is String or text.is_empty() or text.length() > 20:
			return false
		for digit in text.to_utf8_buffer():
			if digit < 48 or digit > 57:
				return false
	return true


func initialize(owner: Variant, assets: String) -> bool:
	bridge = owner
	state = bridge.get_freedom_walk_state()
	if not valid_state(state):
		error = "The station walking actor is unavailable"
		return false
	var flight: Dictionary = bridge.get_freedom_flight_state()
	if not FlightView.valid_state(flight) or not flight.attached or flight.target_port != 1:
		error = "The walking journey has no attached D1 Wayfarer"
		return false
	if not assets.is_absolute_path() or FileAccess.get_sha256(assets.path_join("hopper-wayfarer-01.glb")) != FlightView.WAYFARER_HASH or FileAccess.get_sha256(assets.path_join("hopper-wayfarer-01.json")) != FlightView.WAYFARER_DESCRIPTOR_HASH:
		error = "Prepare the verified native starter assets"
		return false
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var container := SubViewportContainer.new()
	container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	add_child(container)
	viewport = SubViewport.new()
	viewport.size = Vector2i(1280, 720)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(viewport)
	scene = Node3D.new()
	viewport.add_child(scene)
	station = StationPresentation.new()
	scene.add_child(station)
	if not station.initialize(state, bridge.get_freedom_station_geometry(), assets, false):
		error = station.error
		return false
	ship = Node3D.new()
	scene.add_child(ship)
	var model := HopperPresentation.load_asset(assets)
	if model == null:
		error = "The verified Wayfarer could not be imported"
		return false
	ship.add_child(model)
	camera = Camera3D.new()
	camera.name = "StationActorEye"
	camera.near = 0.03
	camera.far = 180.0
	camera.fov = 75.0
	scene.add_child(camera)
	camera.current = true
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004, 0.009, 0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.75, 0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	scene.add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-40, -30, 0)
	light.light_energy = 1.5
	scene.add_child(light)
	requested_heading = state.heading_radians
	for pad in Input.get_connected_joypads():
		available_pads[pad] = true
		if selected_pad == -1:
			selected_pad = pad
	Input.joy_connection_changed.connect(controller_connection_changed)
	build_ui()
	controls_armed = current_controls_neutral()
	paused = state.continued or not controls_armed
	if paused:
		pause_controls("Release walking and look controls, then Resume explicitly.")
	update_view()
	return true


func build_ui() -> void:
	var backing := StyleBoxFlat.new()
	backing.bg_color = Color(0.01, 0.015, 0.025, 0.94)
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		backing.set_content_margin(side, 0.0)
	hud_scroll = ScrollContainer.new()
	hud_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	hud_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_ALWAYS
	hud_scroll.follow_focus = true
	hud_scroll.add_theme_stylebox_override("panel", backing)
	hud_scroll_style = hud_scroll.get_v_scroll_bar().get_theme_stylebox("scroll").duplicate()
	hud_scroll.get_v_scroll_bar().add_theme_stylebox_override("scroll", hud_scroll_style)
	add_child(hud_scroll)
	var column := VBoxContainer.new()
	hud_column = column
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hud_scroll.add_child(column)
	hud_theme = Theme.new()
	column.theme = hud_theme
	telemetry = hud_text()
	column.add_child(telemetry)
	var hint := hud_text()
	hud_hint = hint
	hint.text = "WASD / left stick walk · Right-drag / right stick look\nEsc / Start pause · Arrows / D-pad navigate · Enter / A select\nWalk through the workshop toward D1"
	column.add_child(hint)
	pause_button = Button.new()
	pause_button.focus_mode = Control.FOCUS_ALL
	pause_button.text = "Resume" if paused else "Pause"
	pause_button.pressed.connect(toggle_pause)
	column.add_child(pause_button)
	save_button = Button.new()
	save_button.text = "Save As…"
	save_button.focus_mode = Control.FOCUS_ALL
	column.add_child(save_button)
	save_status = hud_text()
	save_status.text = "D1 access: hatch, ladder and seating are in development."
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.title = "Save station journey"
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	save_dialog.current_file = "apsis-drift.json"
	add_child(save_dialog)
	save_button.pressed.connect(open_save_dialog)
	save_dialog.file_selected.connect(save_selected)
	save_dialog.canceled.connect(save_canceled)
	resized.connect(layout_hud)
	get_window().size_changed.connect(layout_hud)
	layout_hud()


func hud_text() -> Label:
	var label := Label.new()
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func layout_hud() -> void:
	if hud_scroll == null:
		return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or pixels.x <= 0 or pixels.y <= 0 or not size.is_finite() or size.x <= 0 or size.y <= 0:
		return
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	var margin := 16 * scale
	var usable := size - Vector2.ONE * (2 * margin)
	if usable.x <= 0 or usable.y <= 0:
		return
	hud_theme.default_font_size = roundi(18 * scale)
	hud_column.add_theme_constant_override("separation", roundi(8 * scale))
	# Intrinsic scrollbar style width reserves a real gutter from wrapped text.
	hud_scroll_style.content_margin_left = 8 * scale
	hud_scroll_style.content_margin_right = 8 * scale
	for button in [pause_button, save_button]:
		button.custom_minimum_size.y = 40 * scale
	hud_scroll.position = Vector2.ONE * margin
	hud_scroll.size = Vector2(minf(340 * scale, usable.x), usable.y)


func set_paused(value: bool) -> void:
	if value:
		pause_controls("Walking paused. Release controls before Resume.")
	else:
		resume_requested()


func pause_controls(reason: String) -> void:
	paused = true
	controls_armed = false
	if pause_button != null:
		pause_button.text = "Resume"
		pause_button.disabled = not error.is_empty()
		if focused and save_dialog != null and not save_dialog.visible:
			(save_button if pause_button.disabled else pause_button).grab_focus()
	if save_status != null:
		save_status.text = "Walking paused: " + error if not error.is_empty() else reason


func toggle_pause() -> void:
	if paused:
		resume_requested()
	else:
		pause_controls("Walking paused. Release controls before Resume.")


func resume_requested() -> void:
	if not error.is_empty() or not focused or save_dialog == null or save_dialog.visible:
		return
	# A past neutral frame cannot authorize a later held press (even W+S / A+D).
	observe_neutral_controls()
	if not controls_armed:
		save_status.text = "Release WASD, both sticks and right mouse before Resume."
		return
	paused = false
	pause_button.text = "Pause"
	pause_button.release_focus()
	save_button.release_focus()
	save_status.text = "Walking. Save As pauses the journey."


func current_controls_neutral() -> bool:
	for key in [KEY_W, KEY_A, KEY_S, KEY_D]:
		if Input.is_physical_key_pressed(key):
			return false
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		return false
	if available_pads.get(selected_pad, false):
		for axis in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y, JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
			var value := Input.get_joy_axis(selected_pad, axis)
			if not is_finite(value) or stick(value) != 0.0:
				return false
	return true


func observe_neutral_controls() -> void:
	controls_armed = focused and error.is_empty() and current_controls_neutral()


func controller_connection_changed(device: int, connected: bool) -> void:
	available_pads[device] = connected
	if device == selected_pad:
		pause_controls("Selected controller changed. Release controls, then Resume explicitly.")
	elif connected and not available_pads.get(selected_pad, false):
		pause_controls("Controller available. Press its Start to select it, then Resume after neutral.")


func open_save_dialog() -> void:
	if not focused or save_dialog == null:
		return
	pause_controls("Save As preserves the last committed journey state.")
	save_dialog.popup_centered_ratio(0.75)


func save_selected(path: String) -> void:
	pause_controls("Save As closed. Resume explicitly after neutral.")
	save_status.text = "Saved to " + path if bridge.save_freedom_as(path) else "Save failed: " + str(bridge.get_last_error())


func save_canceled() -> void:
	pause_controls("Save canceled. Resume explicitly after neutral.")


static func stick(value: float) -> float:
	return 0.0 if abs(value) < 0.15 else sign(value) * (abs(value) - 0.15) / 0.85


func input_controls(delta: float) -> PackedFloat64Array:
	if paused or not focused or not error.is_empty() or save_dialog.visible:
		return PackedFloat64Array([0.0, 0.0, requested_heading])
	var forward := float(Input.is_physical_key_pressed(KEY_W)) - float(Input.is_physical_key_pressed(KEY_S))
	var right := float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A))
	if available_pads.get(selected_pad, false):
		forward -= stick(Input.get_joy_axis(selected_pad, JOY_AXIS_LEFT_Y))
		right += stick(Input.get_joy_axis(selected_pad, JOY_AXIS_LEFT_X))
		requested_heading -= stick(Input.get_joy_axis(selected_pad, JOY_AXIS_RIGHT_X)) * delta * 1.8
		pitch = clampf(pitch - stick(Input.get_joy_axis(selected_pad, JOY_AXIS_RIGHT_Y)) * delta * 1.8, -1.3, 1.3)
	requested_heading = wrapf(requested_heading, -PI, PI)
	return PackedFloat64Array([clampf(forward, -1.0, 1.0), clampf(right, -1.0, 1.0), requested_heading])


func advance_requested(delta: float, commands: PackedFloat64Array) -> bool:
	if paused or not focused or not error.is_empty() or (save_dialog != null and save_dialog.visible):
		return true
	if not bridge.advance_freedom_walk(delta, commands):
		error = str(bridge.get_last_error())
		set_paused(true)
		return false
	update_view()
	return true


func update_view() -> void:
	state = bridge.get_freedom_walk_state()
	if not valid_state(state):
		error = "C++ returned an invalid station actor pose"
		set_paused(true)
		return
	var flight: Dictionary = bridge.get_freedom_flight_state()
	station.transform = Transform3D(state.station_basis, state.station_position)
	ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	camera.transform = Transform3D(state.station_basis * Basis(Vector3.UP, state.heading_radians) * Basis(Vector3.RIGHT, pitch), state.actor_eye_position)
	telemetry.text = "Origin Station · Tick %s\nD1 workshop access · %.2f, %.2f, %.2f m%s" % [state.tick, state.foot_position_metres[0], state.foot_position_metres[1], state.foot_position_metres[2], "\n" + error if not error.is_empty() else ""]


func _process(delta: float) -> void:
	if bridge == null:
		return
	if paused:
		observe_neutral_controls()
		return
	if not focused or not error.is_empty() or save_dialog.visible:
		return
	advance_requested(delta, input_controls(minf(delta, 0.1)))


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
		pause_controls("Focus lost. Resume explicitly after current neutral input.")
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
		controls_armed = false


func _input(event: InputEvent) -> void:
	if save_dialog == null:
		return
	var pad_event := event is InputEventJoypadButton or event is InputEventJoypadMotion
	if pad_event and not available_pads.get(event.device, false):
		# Queued events from a disconnected selection carry no menu authority.
		get_viewport().set_input_as_handled()
		return
	if pad_event and event.device != selected_pad:
		# Start selects only when the current device is absent; it never resumes.
		if focused and not save_dialog.visible and event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_START and available_pads.get(event.device, false) and not available_pads.get(selected_pad, false):
			selected_pad = event.device
			pause_controls("Controller selected. Release controls, then Resume explicitly.")
		get_viewport().set_input_as_handled()
		return
	if not focused or save_dialog == null or save_dialog.visible:
		return
	var pressed := event.is_pressed() and not event.is_echo()
	var key: int = event.physical_keycode if event is InputEventKey else KEY_NONE
	var button: int = event.button_index if event is InputEventJoypadButton else -1
	if pressed and (key == KEY_ESCAPE or button == JOY_BUTTON_START or (paused and button == JOY_BUTTON_B)):
		toggle_pause()
		get_viewport().set_input_as_handled()
	elif paused and pressed and (key in [KEY_UP, KEY_DOWN, KEY_TAB] or button in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]):
		var current := get_viewport().gui_get_focus_owner()
		(save_button if current == pause_button or pause_button.disabled else pause_button).grab_focus()
		get_viewport().set_input_as_handled()
	elif paused and pressed and (key in [KEY_ENTER, KEY_KP_ENTER, KEY_SPACE] or button == JOY_BUTTON_A):
		var current := get_viewport().gui_get_focus_owner()
		if current == save_button:
			open_save_dialog()
		else:
			resume_requested()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and not paused and error.is_empty() and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		requested_heading = wrapf(requested_heading - event.relative.x * 0.003, -PI, PI)
		pitch = clampf(pitch - event.relative.y * 0.003, -1.3, 1.3)
