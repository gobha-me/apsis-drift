extends Control
signal journey_mode_changed
signal catalog_save_requested(save_as: bool)
signal load_requested
signal title_requested
var journey_dialog_open := false
## First-person station presentation. C++ resolves support, motion and shared time.
const StationPresentation = preload("res://scripts/native/native_station_view.gd")
const HopperPresentation = preload("res://scripts/characters/hopper_presentation.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const Controls = preload("res://scripts/ui/player_input.gd")
const ControlSettings = preload("res://scripts/ui/control_settings.gd")
var stick_settings_path := Controls.SETTINGS_PATH
var stick_preferences: Dictionary = {}
var stick_preferences_status := ""
var bridge: Variant
var state: Dictionary = {}
var station: Node3D
var ship: Node3D
var camera: Camera3D
var light: DirectionalLight3D
var scene: Node3D
var viewport: SubViewport
var telemetry: Label
var pause_button: Button
var board_button: Button
var export_save_button: Button
var replace_save_button: Button
var save_button: Button
var load_button: Button
var title_button: Button
var settings_button: Button
var settings_view: Control
var preferences_provider: Node
var settings_return_frames := 0
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
var initial_controller_device := -2
var available_pads: Dictionary = {}
var requested_heading := 0.0
var pitch := 0.0
var error := ""
var activated := false
var staged_model: Node3D
var mode_change_pending := false


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
	var model := HopperPresentation.load_selected_asset(assets, owner.get_freedom_craft_binding())
	if model == null:
		error = "The selected Wayfarer could not be staged"
		return false
	var pending := {"walk_state": owner.get_freedom_walk_state(), "flight_state": owner.get_freedom_flight_state(), "station_geometry": owner.get_freedom_station_geometry()}
	if not stage(owner, assets, pending, model):
		if model.get_parent() == null: model.free()
		return false
	activate()
	return true

func stage(owner: Variant, assets: String, pending: Dictionary, model: Node3D, handoff := false) -> bool:
	if bridge != null or model == null or model.get_parent() != null or not (model.valid_current_pose() if handoff else model.valid_installed()):
		error = "Fresh view and complete detached model required"
		return false
	set_process(false)
	set_process_input(false)
	bridge = owner
	state = pending.walk_state.duplicate(true)
	if not valid_state(state):
		error = "The pending station walking actor is unavailable"
		return false
	var flight: Dictionary = pending.flight_state
	if not FlightView.valid_state(flight) or not flight.attached or flight.target_port != 1:
		error = "The walking journey has no attached D1 Wayfarer"
		return false
	load_stick_preferences()
	staged_model = model
	var boarding: Dictionary = state.get("boarding", {})
	if not boarding.is_empty() and not model.set_pose(boarding.pose):
		error = "The Wayfarer boarding pose is unavailable"
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
	if not station.initialize(state, pending.station_geometry, assets, false):
		error = station.error
		return false
	ship = Node3D.new()
	scene.add_child(ship)
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
	light = DirectionalLight3D.new()
	light.basis = FlightView.star_light_basis(flight.star_direction)
	light.light_color = flight.lighting.star_color
	light.light_energy = 1.5 * NativeEnvironment.sun_visibility(flight)
	scene.add_child(light)
	requested_heading = state.heading_radians
	build_ui()
	if not stick_preferences_status.is_empty(): save_status.text += "\n" + stick_preferences_status
	station.transform = Transform3D(state.station_basis, state.station_position)
	ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	camera.transform = Transform3D(state.station_basis * Basis(Vector3.UP, state.heading_radians), state.actor_eye_position)
	return true

func activate() -> void:
	# Called only after the exact C++ pending token commits and view enters tree.
	activated = true
	if initial_controller_device != -2:
		selected_pad = initial_controller_device
		if selected_pad >= 0: available_pads[selected_pad] = true
	for pad in Input.get_connected_joypads():
		available_pads[pad] = true
		if initial_controller_device == -2 and selected_pad == -1: selected_pad = pad
	Input.joy_connection_changed.connect(controller_connection_changed)
	get_window().size_changed.connect(layout_hud)
	layout_hud()
	controls_armed = current_controls_neutral()
	paused = state.continued or not controls_armed
	if paused: pause_controls("Release walking and look controls, then Resume explicitly.")
	update_view()
	set_process(true)
	set_process_input(true)

func ready_to_commit() -> bool:
	return not activated and error.is_empty() and is_instance_valid(staged_model) and staged_model.valid_current_pose()


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
	hint.text = "WASD / left stick walk · Right-drag / right stick look\nEsc / Start pause · Arrows / D-pad navigate · Enter / A select\nWalk toward D1 · E / A board Wayfarer"
	column.add_child(hint)
	board_button = Button.new()
	board_button.text = "Board Wayfarer · E / A"
	board_button.focus_mode = Control.FOCUS_NONE
	board_button.pressed.connect(board_requested)
	column.add_child(board_button)
	pause_button = Button.new()
	pause_button.focus_mode = Control.FOCUS_ALL
	pause_button.text = "Resume" if paused else "Pause"
	pause_button.pressed.connect(toggle_pause)
	column.add_child(pause_button)
	replace_save_button = Button.new()
	replace_save_button.text = "Save"
	replace_save_button.pressed.connect(func(): request_catalog_save(false))
	column.add_child(replace_save_button)
	save_button = Button.new()
	save_button.text = "Save As…"
	save_button.focus_mode = Control.FOCUS_ALL
	column.add_child(save_button)
	load_button = Button.new()
	load_button.text = "Load…"
	load_button.pressed.connect(func():
		if focused and not journey_dialog_open and not save_dialog.visible and not settings_open():
			pause_controls("Load replaces the journey only after confirmation.")
			load_requested.emit())
	column.add_child(load_button)
	settings_button = Button.new()
	settings_button.text = "Settings (paused)"
	settings_button.pressed.connect(open_settings)
	column.add_child(settings_button)
	title_button = Button.new()
	title_button.text = "Title…"
	title_button.pressed.connect(func():
		if focused and not journey_dialog_open and not save_dialog.visible and not settings_open():
			pause_controls("Return to title only after discarding unsaved progress.")
			title_requested.emit())
	column.add_child(title_button)
	export_save_button = Button.new()
	export_save_button.text = "Export save file…"
	export_save_button.pressed.connect(open_save_dialog)
	column.add_child(export_save_button)
	save_status = hud_text()
	save_status.text = "Approach the D1 ladder to board."
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.title = "Save station journey"
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	save_dialog.current_file = "apsis-drift.json"
	add_child(save_dialog)
	save_button.pressed.connect(func(): request_catalog_save(true))
	refresh_profile_actions()
	save_dialog.file_selected.connect(save_selected)
	# The existing preference provider is disabled as a gameplay input source.
	# Station movement and controller selection stay explicitly owned here.
	preferences_provider = Controls.new()
	preferences_provider.settings_path = stick_settings_path
	preferences_provider.enabled = false
	add_child(preferences_provider)
	preferences_provider.set_process_input(false)
	settings_view = ControlSettings.new()
	settings_view.controls = preferences_provider
	settings_view.back_requested.connect(close_settings)
	add_child(settings_view)
	save_dialog.canceled.connect(save_canceled)
	resized.connect(layout_hud)


func hud_text() -> Label:
	var label := Label.new()
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func layout_hud() -> void:
	if not activated or not is_inside_tree() or hud_scroll == null:
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
	for button in [board_button, pause_button, replace_save_button, save_button, load_button, settings_button, title_button, export_save_button]:
		button.custom_minimum_size.y = 40 * scale
	hud_scroll.position = Vector2.ONE * margin
	# ScrollContainer lays out integer-sized children; keep its viewport integral
	# so follow-focus cannot round a bottom action past the clipping edge.
	hud_scroll.size = Vector2(minf(340 * scale, usable.x), usable.y).floor()


func set_paused(value: bool) -> void:
	if value:
		pause_controls("Walking paused. Release controls before Resume.")
	else:
		resume_requested()


func pause_controls(reason: String) -> void:
	paused = true
	controls_armed = false
	refresh_profile_actions()
	if board_button != null: board_button.disabled = true
	if pause_button != null:
		pause_button.text = "Resume"
		pause_button.disabled = not error.is_empty()
		if focused and save_dialog != null and not save_dialog.visible and not settings_open():
			(save_button if pause_button.disabled else pause_button).grab_focus()
	if save_status != null:
		save_status.text = "Walking paused: " + error if not error.is_empty() else reason
		if bridge.has_method("get_freedom_profile_state"):
			var profile: Dictionary = bridge.get_freedom_profile_state()
			if profile.get("explicit_path", false): save_status.text += "\n" + str(profile.reason)


func toggle_pause() -> void:
	if settings_open():
		settings_view.cancel()
		return
	if paused:
		resume_requested()
	else:
		pause_controls("Walking paused. Release controls before Resume.")


func resume_requested() -> void:
	if journey_dialog_open or settings_open(): return
	if not error.is_empty() or not focused or save_dialog == null or save_dialog.visible:
		return
	# A past neutral frame cannot authorize a later held press (even W+S / A+D).
	observe_neutral_controls()
	if not controls_armed:
		save_status.text = "Release WASD, both sticks and right mouse before Resume."
		return
	paused = false
	board_button.disabled = not focused
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
	if not focused or save_dialog == null or settings_open():
		return
	pause_controls("Save As preserves the last committed journey state.")
	save_dialog.popup_centered_ratio(0.75)


func save_selected(path: String) -> void:
	pause_controls("Save As closed. Resume explicitly after neutral.")
	save_status.text = "Saved to " + path if bridge.save_freedom_as(path) else "Save failed: " + str(bridge.get_last_error())


func save_canceled() -> void:
	pause_controls("Save canceled. Resume explicitly after neutral.")


func settings_open() -> bool:
	return is_instance_valid(settings_view) and settings_view.visible


func open_settings() -> void:
	if not focused or journey_dialog_open or save_dialog == null or save_dialog.visible or not error.is_empty() or settings_open(): return
	pause_controls("Control choices stay pending until Apply.")
	preferences_provider.focused = focused
	preferences_provider.device = selected_pad
	hud_scroll.hide()
	settings_view.open()


func close_settings() -> void:
	stick_preferences = {"deadzone": preferences_provider.settings.deadzone, "curve": preferences_provider.settings.curve}
	controls_armed = false
	hud_scroll.show()
	settings_button.grab_focus()
	settings_return_frames = 2


func load_stick_preferences() -> void:
	# Read the existing provider without entering the tree or installing a
	# second global InputMap. Station keys and device selection stay explicit.
	var reader := Controls.new()
	reader.load_settings(stick_settings_path)
	stick_preferences = {"deadzone": reader.settings.deadzone, "curve": reader.settings.curve}
	stick_preferences_status = reader.status
	reader.free()


func stick(value: float) -> float:
	if stick_preferences.is_empty(): return 0.0
	return Controls.shape(value, float(stick_preferences.deadzone), float(stick_preferences.curve))


func input_controls(delta: float) -> PackedFloat64Array:
	if paused or not focused or not error.is_empty() or save_dialog.visible or boarding_transition():
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


func boarding_transition() -> bool:
	return state.get("boarding", {}).get("state", "") in ["boarding", "disembarking"]


func board_requested() -> void:
	if paused or not focused or not error.is_empty() or save_dialog.visible or boarding_transition(): return
	if not current_controls_neutral():
		save_status.text = "Release movement and look controls to board."
		return
	if not bridge.begin_freedom_boarding():
		save_status.text = str(bridge.get_last_error())
		return
	pitch = 0.0
	update_view()


func update_view() -> void:
	state = bridge.get_freedom_walk_state()
	if state.is_empty() and bridge.get_freedom_boarding_state().get("state") == "seated":
		# Consume the completed C++ pose before deferred view replacement. A
		# render frame can contain several ticks; the previous walker camera is
		# then several ticks behind the moving station and floating origin.
		var seated: Dictionary = bridge.get_freedom_boarding_state()
		var flight: Dictionary = bridge.get_freedom_flight_state()
		if not FlightView.valid_state(flight) or not finite_triplet(seated.eye_craft) or not staged_model.set_pose(seated.pose):
			error = "C++ returned an invalid completed boarding pose"
			set_paused(true)
			return
		station.transform = Transform3D(flight.station_basis, flight.station_position)
		light.basis = FlightView.star_light_basis(flight.star_direction)
		light.light_color = flight.lighting.star_color
		light.light_energy = 1.5 * NativeEnvironment.sun_visibility(flight)
		ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
		camera.transform = Transform3D(flight.body_basis, flight.body_basis * Vector3(seated.eye_craft[0], seated.eye_craft[1], seated.eye_craft[2]))
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	if not valid_state(state):
		error = "C++ returned an invalid station actor pose"
		set_paused(true)
		return
	var flight: Dictionary = bridge.get_freedom_flight_state()
	if not FlightView.valid_state(flight):
		error = "C++ returned an invalid station flight projection"
		set_paused(true)
		return
	var boarding: Dictionary = state.get("boarding", {})
	if not boarding.is_empty() and not staged_model.set_pose(boarding.pose):
		error = "The Wayfarer boarding pose changed unexpectedly"
		set_paused(true)
		return
	if boarding_transition():
		requested_heading = state.heading_radians
		pitch = 0.0
	station.transform = Transform3D(state.station_basis, state.station_position)
	light.basis = FlightView.star_light_basis(flight.star_direction)
	light.light_color = flight.lighting.star_color
	light.light_energy = 1.5 * NativeEnvironment.sun_visibility(flight)
	ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	camera.transform = Transform3D(state.station_basis * Basis(Vector3.UP, state.heading_radians) * Basis(Vector3.RIGHT, pitch), state.actor_eye_position)
	board_button.visible = not boarding_transition()
	board_button.disabled = paused or not focused
	if boarding_transition():
		var cues := {"approach": "Approaching the ladder", "ladder": "Climbing through the hatch", "cabin": "Moving into the cabin", "seat": "Taking the pilot seat", "hardware": "Securing the hatch", "complete": "Ready"}
		telemetry.text = "%s · %d%%\n%s" % ["Boarding Wayfarer" if boarding.state == "boarding" else "Returning to the station", roundi(boarding.progress * 100.0), cues.get(boarding.phase, "Moving")]
	else:
		telemetry.text = "Origin Station · D1 workshop\nWalk to the Wayfarer ladder to board.%s" % ("\n" + error if not error.is_empty() else "")


func _process(delta: float) -> void:
	if bridge == null or not activated:
		return
	if settings_open():
		stick_preferences = {"deadzone": preferences_provider.settings.deadzone, "curve": preferences_provider.settings.curve}
	if settings_return_frames > 0:
		settings_return_frames -= 1
		if settings_return_frames == 0 and not settings_open(): hud_scroll.ensure_control_visible(settings_button)
	if paused:
		if not settings_open(): observe_neutral_controls()
		return
	if not focused or not error.is_empty() or save_dialog.visible:
		return
	advance_requested(delta, input_controls(minf(delta, 0.1)))


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
		if preferences_provider != null: preferences_provider.focused = false
		pause_controls("Focus lost. Resume explicitly after current neutral input.")
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
		if preferences_provider != null: preferences_provider.focused = true
		controls_armed = false


func _input(event: InputEvent) -> void:
	if journey_dialog_open: return
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
			if preferences_provider != null: preferences_provider.device = selected_pad
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
	elif settings_open():
		# Let the shared screen handle ordinary selected-controller UI actions.
		return
	elif paused and pressed and (key in [KEY_UP, KEY_DOWN, KEY_TAB] or button in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]):
		var current := get_viewport().gui_get_focus_owner()
		var buttons := [pause_button, replace_save_button, save_button, load_button, settings_button, title_button, export_save_button]
		var direction := -1 if key == KEY_UP or button == JOY_BUTTON_DPAD_UP else 1
		var index := buttons.find(current)
		for offset in range(1, buttons.size() + 1):
			var next: Button = buttons[posmod(index + direction * offset, buttons.size())]
			if not next.disabled:
				next.grab_focus()
				hud_scroll.ensure_control_visible(next)
				break
		get_viewport().set_input_as_handled()
	elif paused and pressed and (key in [KEY_ENTER, KEY_KP_ENTER, KEY_SPACE] or button == JOY_BUTTON_A):
		var current := get_viewport().gui_get_focus_owner()
		if current == settings_button:
			open_settings()
		elif current == title_button:
			title_button.pressed.emit()
		elif current == load_button:
			load_button.pressed.emit()
		elif current == export_save_button:
			open_save_dialog()
		elif current == save_button or current == replace_save_button:
			current.pressed.emit()
		else:
			resume_requested()
		get_viewport().set_input_as_handled()
	elif pressed and not paused and (key == KEY_E or button == JOY_BUTTON_A):
		board_requested()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and not boarding_transition() and not paused and error.is_empty() and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		requested_heading = wrapf(requested_heading - event.relative.x * 0.003, -PI, PI)
		pitch = clampf(pitch - event.relative.y * 0.003, -1.3, 1.3)

func refresh_profile_actions() -> void:
	if replace_save_button == null or bridge == null or not bridge.has_method("get_freedom_profile_state"): return
	var profile: Dictionary = bridge.get_freedom_profile_state()
	replace_save_button.disabled = not profile.get("can_save", false)
	save_button.disabled = not profile.get("can_save_as", false)
	replace_save_button.tooltip_text = str(profile.get("reason", ""))
	save_button.tooltip_text = replace_save_button.tooltip_text

func request_catalog_save(save_as: bool) -> void:
	if not focused or journey_dialog_open or settings_open() or save_dialog.visible: return
	pause_controls("Save uses the last committed journey state.")
	catalog_save_requested.emit(save_as)
