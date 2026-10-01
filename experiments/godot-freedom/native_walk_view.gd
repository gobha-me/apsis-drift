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
var save_dialog: FileDialog
var save_status: Label
var paused := false
var focused := true
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
	paused = state.continued
	build_ui()
	update_view()
	return true


func build_ui() -> void:
	var column := VBoxContainer.new()
	column.position = Vector2(16, 16)
	add_child(column)
	telemetry = Label.new()
	column.add_child(telemetry)
	var hint := Label.new()
	hint.text = "WASD / left stick walk · Right-drag / right stick look\nEsc pause · Walk through the workshop toward D1"
	column.add_child(hint)
	pause_button = Button.new()
	pause_button.focus_mode = Control.FOCUS_NONE
	pause_button.text = "Resume" if paused else "Pause"
	pause_button.pressed.connect(toggle_pause)
	column.add_child(pause_button)
	var save_button := Button.new()
	save_button.text = "Save As…"
	save_button.focus_mode = Control.FOCUS_NONE
	column.add_child(save_button)
	save_status = Label.new()
	save_status.text = "D1 access: hatch, ladder and seating are in development."
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.title = "Save station journey"
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	save_dialog.current_file = "apsis-drift.json"
	add_child(save_dialog)
	save_button.pressed.connect(func():
		set_paused(true)
		save_dialog.popup_centered_ratio(0.75)
	)
	save_dialog.file_selected.connect(func(path: String):
		save_status.text = "Saved to " + path if bridge.save_freedom_as(path) else "Save failed: " + str(bridge.get_last_error())
	)


func set_paused(value: bool) -> void:
	paused = value
	if pause_button != null:
		pause_button.text = "Resume" if paused else "Pause"


func toggle_pause() -> void:
	set_paused(not paused)


static func stick(value: float) -> float:
	return 0.0 if abs(value) < 0.15 else sign(value) * (abs(value) - 0.15) / 0.85


func input_controls(delta: float) -> PackedFloat64Array:
	var forward := float(Input.is_physical_key_pressed(KEY_W)) - float(Input.is_physical_key_pressed(KEY_S))
	var right := float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A))
	var pads := Input.get_connected_joypads()
	if not pads.is_empty():
		var pad := pads[0]
		forward -= stick(Input.get_joy_axis(pad, JOY_AXIS_LEFT_Y))
		right += stick(Input.get_joy_axis(pad, JOY_AXIS_LEFT_X))
		requested_heading -= stick(Input.get_joy_axis(pad, JOY_AXIS_RIGHT_X)) * delta * 1.8
		pitch = clampf(pitch - stick(Input.get_joy_axis(pad, JOY_AXIS_RIGHT_Y)) * delta * 1.8, -1.3, 1.3)
	requested_heading = wrapf(requested_heading, -PI, PI)
	return PackedFloat64Array([clampf(forward, -1.0, 1.0), clampf(right, -1.0, 1.0), requested_heading])


func advance_requested(delta: float, commands: PackedFloat64Array) -> bool:
	if paused or not focused:
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
	if bridge == null or paused or not focused:
		return
	advance_requested(delta, input_controls(minf(delta, 0.1)))


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
		set_paused(true)
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_ESCAPE:
		toggle_pause()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and not paused and focused and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		requested_heading = wrapf(requested_heading - event.relative.x * 0.003, -PI, PI)
		pitch = clampf(pitch - event.relative.y * 0.003, -1.3, 1.3)
