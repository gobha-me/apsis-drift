extends Control
## Ordinary saved-flight consumer. C++ owns time, state, terrain and propulsion.
const PlanetStreamView = preload("res://planet_stream.gd")
const MainExhaust = preload("res://native_main_exhaust.gd")
const HopperPresentation = preload("res://hopper_presentation.gd")
const StationPresentation = preload("res://native_station_view.gd")
const WAYFARER_HASH = "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8"
const WAYFARER_DESCRIPTOR_HASH = "17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7"
var bridge: Variant
var camera: Camera3D
var terrain_camera: Camera3D
var near_viewport: SubViewport
var ship: Node3D
var terrain: Node3D
var exhaust: Node3D
var light: DirectionalLight3D
var telemetry: Label
var home_cue: Label
var pause_button: Button
var assist_button: CheckButton
var save_dialog: FileDialog
var save_status: Label
var paused := true
var cockpit := false
var pilot_eye := Vector3.ZERO
var state: Dictionary = {}
var station: Node3D
var scene: Node3D
var assets_root := ""
var station_geometry: Dictionary = {}
var dock_status: Label
var port_buttons: Array[Button] = []
var capture_button: Button
var release_button: Button
var error := ""


static func controls() -> PackedFloat64Array:
	var fractions := PackedFloat64Array()
	fractions.resize(12)
	var keys := [KEY_D, KEY_SPACE, KEY_S, KEY_A, KEY_CTRL, KEY_W, KEY_UP, KEY_LEFT, KEY_Q, KEY_DOWN, KEY_RIGHT, KEY_E]
	for i in 12:
		fractions[i] = 1.0 if Input.is_physical_key_pressed(keys[i]) else 0.0
	return fractions


static func valid_state(value: Dictionary) -> bool:
	if value.get("mode") != "freedom_flight" or not value.get("body_basis") is Basis or not value.get("station_basis") is Basis or not value.get("station_position") is Vector3:
		return false
	if not value.body_basis.is_finite() or not value.station_basis.is_finite() or not value.station_position.is_finite():
		return false
	if not value.get("attached") is bool or not value.get("target_port") is int or value.target_port < 0 or value.target_port > 2 or not value.get("docking") is Dictionary:
		return false
	for key in ["position_metres", "velocity_metres_per_second", "angular_velocity_body", "positive_force_body", "negative_force_body", "positive_force_ratings", "negative_force_ratings"]:
		var vector: Variant = value.get(key)
		if not vector is PackedFloat64Array or vector.size() != 3:
			return false
		for component in vector:
			if not is_finite(component):
				return false
	for key in ["latitude", "longitude", "altitude", "planet_radius", "atmosphere_edge", "air_density", "surface_speed", "radial_rate"]:
		if not value.get(key) is float or not is_finite(value[key]):
			return false
	return value.planet_radius > 0.0


func initialize(owner: Variant, assets: String) -> bool:
	bridge = owner
	state = bridge.get_freedom_flight_state()
	if not valid_state(state):
		error = "Saved physical flight view is unavailable: " + str(bridge.get_last_error())
		return false
	if not assets.is_absolute_path():
		error = "Prepare the native starter assets before viewing flight"
		return false
	assets_root = assets
	station_geometry = bridge.get_freedom_station_geometry()
	if not StationPresentation.valid_geometry(station_geometry, state.station_id) or FileAccess.get_sha256(assets.path_join("station-reference.glb")) != StationPresentation.STATION_HASH:
		error = "The selected Origin Station geometry/export is missing or changed"
		return false
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var container := SubViewportContainer.new()
	container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	add_child(container)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1280, 720)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(viewport)
	scene = Node3D.new()
	viewport.add_child(scene)
	ship = Node3D.new()
	scene.add_child(ship)
	if state.frame_id == "2":
		var path := assets.path_join("hopper-wayfarer-01.glb")
		if FileAccess.get_sha256(path) != WAYFARER_HASH or FileAccess.get_sha256(assets.path_join("hopper-wayfarer-01.json")) != WAYFARER_DESCRIPTOR_HASH:
			error = "The selected Wayfarer export is missing or changed"
			return false
		var model := HopperPresentation.load_asset(assets)
		if model == null:
			error = "The Wayfarer descriptor or verified export could not be loaded"
			return false
		ship.add_child(model)
		# This camera reference is presentation only. Boarding/seat ownership is
		# a separate journey transition, not authorized by toggling this camera.
		pilot_eye = HopperPresentation.vector(model.specification.pilot_eye)
		exhaust = MainExhaust.new()
		if not exhaust.bind_skin(model):
			exhaust.free()
			exhaust = null
			error = "The selected Wayfarer exterior exhaust interface is unavailable"
			return false
		ship.add_child(exhaust)
	else:
		# Preserve the registered legacy frame without relabelling it Wayfarer.
		var placeholder := MeshInstance3D.new()
		var mesh := BoxMesh.new()
		mesh.size = Vector3(14.0, 3.935, 19.32)
		placeholder.mesh = mesh
		ship.add_child(placeholder)
	camera = Camera3D.new()
	camera.near = 0.05
	camera.far = 200.0
	camera.fov = 75.0
	# Terrain uses a safe distant frustum; nearby terrain also draws with the
	# ship so real foreground terrain can occlude it in this close-depth pass.
	# These cameras share one C++-supplied scene and exactly the same view/FOV.
	var near_container := SubViewportContainer.new()
	near_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	near_container.stretch = true
	near_container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(near_container)
	near_viewport = SubViewport.new()
	near_viewport.size = viewport.size
	near_viewport.world_3d = viewport.find_world_3d()
	near_viewport.transparent_bg = true
	near_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	near_container.add_child(near_viewport)
	near_viewport.add_child(camera)
	camera.cull_mask = 3
	terrain_camera = Camera3D.new()
	terrain_camera.cull_mask = 1
	terrain_camera.fov = camera.fov
	scene.add_child(terrain_camera)
	light = DirectionalLight3D.new()
	scene.add_child(light)
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004, 0.009, 0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.35, 0.45, 0.7)
	environment.ambient_light_energy = 0.6
	world.environment = environment
	scene.add_child(world)
	var close_environment: Environment = environment.duplicate()
	close_environment.background_mode = Environment.BG_CLEAR_COLOR
	camera.environment = close_environment
	set_ship_layer(ship)
	if not bridge.enable_streaming():
		error = str(bridge.get_last_error())
		return false
	terrain = PlanetStreamView.new()
	terrain.bridge = bridge
	scene.add_child(terrain)
	build_ui()
	update_view(0.0)
	return true


static func set_ship_layer(node: Node) -> void:
	if node is GeometryInstance3D:
		node.layers = 2
	for child in node.get_children():
		set_ship_layer(child)


func build_ui() -> void:
	var column := VBoxContainer.new()
	column.position = Vector2(16, 16)
	add_child(column)
	telemetry = Label.new()
	column.add_child(telemetry)
	home_cue = Label.new()
	column.add_child(home_cue)
	var hint := Label.new()
	hint.text = "W/S main/retro · A/D strafe · Space/Ctrl rise/fall\nArrows pitch/yaw · Q/E roll · F3 camera · Esc pause"
	column.add_child(hint)
	pause_button = Button.new()
	pause_button.text = "Resume flight"
	pause_button.focus_mode = Control.FOCUS_NONE
	pause_button.pressed.connect(toggle_pause)
	column.add_child(pause_button)
	assist_button = CheckButton.new()
	assist_button.text = "Assisted piloting"
	assist_button.button_pressed = state.assistance
	assist_button.focus_mode = Control.FOCUS_NONE
	assist_button.toggled.connect(func(enabled: bool):
		if not error.is_empty():
			return
		if not bridge.set_freedom_assistance(enabled):
			pause_on_error(str(bridge.get_last_error()))
	)
	column.add_child(assist_button)
	if state.frame_id == "2":
		var ports := HBoxContainer.new()
		column.add_child(ports)
		for ordinal in [1, 2]:
			var button := Button.new()
			button.text = "Target D%d" % ordinal
			button.focus_mode = Control.FOCUS_NONE
			button.pressed.connect(func(): port_command("select_freedom_port", ordinal))
			ports.add_child(button)
			port_buttons.append(button)
		dock_status = Label.new()
		column.add_child(dock_status)
		capture_button = Button.new()
		capture_button.text = "Capture port"
		capture_button.focus_mode = Control.FOCUS_NONE
		capture_button.pressed.connect(func(): port_command("capture_freedom_port"))
		column.add_child(capture_button)
		release_button = Button.new()
		release_button.text = "Release port"
		release_button.focus_mode = Control.FOCUS_NONE
		release_button.pressed.connect(func(): port_command("release_freedom_port"))
		column.add_child(release_button)
	var save_button := Button.new()
	save_button.text = "Save As…"
	save_button.focus_mode = Control.FOCUS_NONE
	column.add_child(save_button)
	save_status = Label.new()
	save_status.text = "Paused after Continue. Quit does not autosave."
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	add_child(save_dialog)
	save_button.pressed.connect(func():
		paused = true
		hide_exhaust()
		pause_button.text = "Resume flight"
		save_dialog.popup_centered_ratio(0.75)
	)
	save_dialog.file_selected.connect(func(path: String):
		save_status.text = "Saved to " + path if bridge.save_freedom_as(path) else "Save failed: " + str(bridge.get_last_error())
	)
	var quit_button := Button.new()
	quit_button.text = "Quit"
	quit_button.pressed.connect(func(): get_tree().quit())
	column.add_child(quit_button)


func port_command(command: String, ordinal: int = 0) -> void:
	if not error.is_empty():
		hide_exhaust()
		return
	var accepted: bool = bridge.call(command, ordinal) if ordinal != 0 else bridge.call(command)
	if not accepted:
		save_status.text = str(bridge.get_last_error())
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if state.attached:
		save_status.text = "Attached to D%d. Release before firing propulsion." % state.target_port
	elif command == "release_freedom_port":
		save_status.text = "Released D%d. Use thrusters to depart." % state.target_port
	else:
		save_status.text = "Port selected. Capture requires physical alignment and low closure."


func toggle_pause() -> void:
	if not error.is_empty():
		hide_exhaust()
		return
	paused = not paused
	if paused:
		hide_exhaust()
	pause_button.text = "Resume flight" if paused else "Pause flight"
	save_status.text = "Flight paused. Save As keeps the committed state." if paused else "Flight running. Save As pauses the session."


func _unhandled_key_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed or event.echo or save_dialog == null or save_dialog.visible:
		return
	if event.physical_keycode == KEY_ESCAPE:
		toggle_pause()
	elif event.physical_keycode == KEY_F3 and state.frame_id == "2":
		cockpit = not cockpit


func hide_exhaust() -> void:
	if exhaust != null:
		exhaust.update_applied({}, 0.0, true)


func pause_on_error(message: String) -> void:
	error = message
	paused = true
	hide_exhaust()
	if save_status != null:
		save_status.text = "Flight paused: " + error


func _process(delta: float) -> void:
	if bridge == null or camera == null or terrain == null or not error.is_empty():
		hide_exhaust()
		return
	var demand := controls()
	if state.get("attached", false):
		demand.fill(0.0)
	if not bridge.advance_freedom_flight(delta, demand, paused or save_dialog.visible):
		pause_on_error(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	if not valid_state(state):
		pause_on_error("C++ flight view became unavailable: " + str(bridge.get_last_error()))
		return
	update_view(delta, true)
	if not error.is_empty():
		return
	terrain.tick(delta, camera.position)
	if not terrain.error.is_empty():
		pause_on_error(str(terrain.error))
		return
	if exhaust != null:
		exhaust.update_applied(state, delta, paused)


func update_view(delta: float, defer_exhaust: bool = false) -> void:
	if not error.is_empty():
		hide_exhaust()
		return
	ship.basis = state.body_basis
	if station == null and state.station_position.length() < 1000.0:
		station = StationPresentation.new()
		scene.add_child(station)
		if not station.initialize({"station_id": state.station_id}, station_geometry, assets_root, false):
			pause_on_error(station.error)
			return
		set_ship_layer(station)
	if station != null:
		station.transform = Transform3D(state.station_basis, state.station_position)
		station.visible = state.station_position.length() < 1000.0
	terrain_camera.far = minf(1000000000.0, maxf(100000.0, state.altitude * 8.0))
	terrain_camera.near = maxf(0.2, terrain_camera.far / 500000.0)
	if cockpit:
		camera.transform = Transform3D(state.body_basis, state.body_basis * pilot_eye)
	else:
		# The overhead dock occupies the usual elevated chase viewpoint. Keep
		# the near-port camera below the craft so it has a real exterior view.
		var height := -7.0 if not state.docking.is_empty() and state.docking.separation < 100.0 else 9.0
		camera.position = state.body_basis * Vector3(17.0, height, 28.0)
		camera.look_at(Vector3.ZERO, state.body_basis.y)
	terrain_camera.transform = camera.transform
	if state.star_direction.length_squared() > 0.5:
		light.look_at(-state.star_direction, Vector3.UP if absf(state.star_direction.y) < 0.99 else Vector3.RIGHT)
	if exhaust != null and not defer_exhaust:
		exhaust.update_applied(state, delta, paused)
	telemetry.text = "APSIS DRIFT · %s\nAltitude %.1f km · Surface speed %.1f m/s\nRadial rate %.1f m/s · Air %.5f kg/m³\nTick %s%s" % ["Wayfarer" if state.frame_id == "2" else "Legacy starter frame", state.altitude / 1000.0, state.surface_speed, state.radial_rate, state.air_density, state.tick, " · PAUSED" if paused else ""]
	var target: Vector3 = state.station_position
	var suffix := " · off screen" if camera.is_position_behind(target) or not Rect2(Vector2.ZERO, camera.get_viewport().size).has_point(camera.unproject_position(target)) else ""
	home_cue.text = "Origin Station %s · %.1f km%s" % [state.station_id, target.length() / 1000.0, suffix]
	if dock_status != null:
		var assessment: Dictionary = state.docking
		dock_status.text = "Select a port for approach"
		if state.attached:
			dock_status.text = "Attached to D%d · station co-motion" % state.target_port
		elif not assessment.is_empty():
			var offset: PackedFloat64Array = assessment.offset_body_metres
			dock_status.text = "D%d · %s\nCollar %.3f m · attitude %.2f°\nInward %.3f m/s · lateral %.3f m/s\nPort offset: right %.2f · up %.2f · back %.2f m" % [state.target_port, assessment.reason, assessment.separation, rad_to_deg(assessment.alignment_radians), assessment.inward_speed, assessment.lateral_speed, offset[0], offset[1], offset[2]]
		capture_button.disabled = state.attached or assessment.is_empty() or not assessment.get("ready", false)
		release_button.disabled = not state.attached
		for button in port_buttons:
			button.disabled = state.attached
