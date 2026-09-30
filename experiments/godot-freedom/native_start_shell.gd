extends Control
## Docked-state presentation from a selected C++ Freedom save or recipe.

const StationView = preload("res://native_station_view.gd")
const HostSky = preload("res://native_host_sky.gdshader")
const FlightView = preload("res://native_flight_view.gd")

var bridge: Variant = null
var selected: Dictionary = {}
var save_dialog: FileDialog
var save_status: Label
var save_button: Button
var assets_root := ""
var station_view: Node3D
var port_status: Label


func _process(_delta: float) -> void:
	if station_view == null or port_status == null:
		return
	var lines := PackedStringArray()
	for ordinal in [1, 2]:
		var cue: Dictionary = station_view.port_cue(ordinal)
		if not cue.is_empty():
			lines.append("%s  %.1f m%s" % [cue.label, cue.range_metres, " · off screen" if cue.offscreen else ""])
	port_status.text = "\n".join(lines)


func open_save_as() -> void:
	save_dialog.popup_centered_ratio(0.75)


func cancel_save_as() -> void:
	save_button.grab_focus()


func save_selected(path: String) -> void:
	if bridge.save_freedom_as(path):
		save_status.text = "Saved to %s" % path
		save_status.modulate = Color(0.7, 0.95, 0.8)
	else:
		save_status.text = "Save failed: %s" % bridge.get_last_error()
		save_status.modulate = Color(1.0, 0.75, 0.65)
	save_button.grab_focus()


func fail(message: String) -> void:
	push_error("Native start rejected: " + message)
	get_tree().quit(1)


func parse_selection(arguments: PackedStringArray) -> Dictionary:
	var selection := {}
	for argument in arguments:
		if argument == "--validate-only":
			if selection.has("validate_only"):
				return {}
			selection["validate_only"] = true
		elif argument.begins_with("--assets="):
			if selection.has("assets") or not argument.trim_prefix("--assets=").is_absolute_path():
				return {}
			selection["assets"] = argument.trim_prefix("--assets=")
		elif argument.begins_with("--new-game="):
			if selection.has("mode"):
				return {}
			selection["mode"] = "new_game"
			selection["value"] = argument.trim_prefix("--new-game=")
		elif argument.begins_with("--continue="):
			if selection.has("mode"):
				return {}
			selection["mode"] = "continue"
			selection["value"] = argument.trim_prefix("--continue=")
		else:
			return {}
	if not selection.has("mode") or selection.value.is_empty():
		return {}
	return selection


func decimal_text(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57:
			return false
	return true


func dock_geometry(start: Dictionary) -> Dictionary:
	var radius: Variant = start.get("home_planet_radius_metres")
	var relative: Variant = start.get("station_relative_position_metres")
	if not radius is float or not is_finite(radius) or radius < 1000000.0 or radius > 100000000.0:
		return {}
	if not relative is PackedFloat64Array or relative.size() != 3:
		return {}
	var distance_squared := 0.0
	for component in relative:
		if not is_finite(component):
			return {}
		distance_squared += component * component
	var distance := sqrt(distance_squared)
	if not is_finite(distance) or distance > 1000000000.0 or distance - radius < 10000.0:
		return {}
	return {"radius": radius, "distance": distance, "clearance": distance - radius}


func valid_start(start: Dictionary) -> bool:
	if start.get("mode") != "freedom":
		return false
	for key in ["universe_seed", "system_seed", "system_id", "home_planet_id", "station_id", "craft_id", "tick", "cycle_tick"]:
		if not decimal_text(start.get(key)):
			return false
	for key in ["host_position_metres", "host_velocity_metres_per_second", "station_position_metres", "station_velocity_metres_per_second", "station_relative_position_metres", "station_relative_velocity_metres_per_second"]:
		var coordinates: Variant = start.get(key)
		if not coordinates is PackedFloat64Array or coordinates.size() != 3:
			return false
		for component in coordinates:
			if not is_finite(component):
				return false
	if not start.get("continued") is bool:
		return false
	if not start.get("discovery_count") is int or start.discovery_count < 0:
		return false
	if not start.get("world_delta_count") is int or start.world_delta_count < 0:
		return false
	if not start.get("station_phase_radians") is float or not is_finite(start.station_phase_radians):
		return false
	return not dock_geometry(start).is_empty()


func _ready() -> void:
	var options := parse_selection(OS.get_cmdline_user_args())
	if options.is_empty():
		fail("supply exactly one --new-game=SEED or --continue=ABSOLUTE_PATH")
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		fail("native C++ bridge is unavailable")
		return
	bridge = ClassDB.instantiate("FreedomBridge")
	var accepted: bool = bridge.initialize_freedom_new_game(options.value) if options.mode == "new_game" else bridge.initialize_freedom_continue(options.value)
	if not accepted:
		fail(str(bridge.get_last_error()))
		return
	var flight: Dictionary = bridge.get_freedom_flight_state()
	if not flight.is_empty():
		if not FlightView.valid_state(flight):
			fail("C++ bridge returned an invalid saved-flight view")
			return
		if options.get("validate_only", false):
			print("Freedom native flight shell validated: seed=%s tick=%s system=%s planet=%s station=%s craft=%s checksum=%s" % [flight.universe_seed, flight.tick, flight.system_id, flight.planet_id, flight.station_id, flight.craft_id, flight.checksum])
			get_tree().quit(0)
			return
		set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		var view := FlightView.new()
		add_child(view)
		if not view.initialize(bridge, options.get("assets", "")):
			fail(view.error)
			return
		print("Freedom native flight shell opened: seed=%s tick=%s system=%s planet=%s station=%s craft=%s checksum=%s" % [flight.universe_seed, flight.tick, flight.system_id, flight.planet_id, flight.station_id, flight.craft_id, flight.checksum])
		return
	selected = bridge.get_freedom_start()
	if not valid_start(selected):
		fail("C++ bridge returned an invalid docked-state view")
		return
	var geometry := dock_geometry(selected)
	if options.get("validate_only", false):
		print("Freedom native shell validated: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d radius=%.1f distance=%.3f phase=%.12f" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count, geometry.radius, geometry.distance, selected.station_phase_radians])
		get_tree().quit(0)
		return
	assets_root = options.get("assets", "")
	build_view(geometry)


func build_view(geometry: Dictionary) -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var viewport_frame := SubViewportContainer.new()
	viewport_frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	viewport_frame.stretch = true
	add_child(viewport_frame)
	var spatial := SubViewport.new()
	spatial.size = Vector2i(1280, 720)
	spatial.own_world_3d = true
	spatial.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport_frame.add_child(spatial)
	station_view = StationView.new()
	spatial.add_child(station_view)
	if not station_view.initialize(selected, bridge.get_freedom_station_geometry(), assets_root):
		fail(station_view.error)
		return
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	var sky := Sky.new()
	var sky_material := ShaderMaterial.new()
	sky_material.shader = HostSky
	var relative: PackedFloat64Array = selected.station_relative_position_metres
	sky_material.set_shader_parameter("host_direction", -Vector3(relative[0], relative[1], relative[2]).normalized())
	sky_material.set_shader_parameter("radius_ratio", geometry.radius / geometry.distance)
	sky.sky_material = sky_material
	environment.sky = sky
	environment.background_mode = Environment.BG_SKY
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.75, 0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	station_view.add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-40, -30, 0)
	light.light_energy = 1.5
	station_view.add_child(light)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for side in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 16)
	add_child(margin)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(300, 0)
	panel.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	var panel_style := StyleBoxFlat.new()
	panel_style.bg_color = Color(0.01, 0.02, 0.04, 0.84)
	panel_style.content_margin_left = 24
	panel_style.content_margin_top = 22
	panel_style.content_margin_right = 24
	panel_style.content_margin_bottom = 22
	panel.add_theme_stylebox_override("panel", panel_style)
	margin.add_child(panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 12)
	panel.add_child(column)
	var title := Label.new()
	title.text = "APSIS DRIFT"
	title.add_theme_font_size_override("font_size", 24)
	column.add_child(title)
	var location := Label.new()
	location.text = "Docked at Origin Station"
	location.add_theme_font_size_override("font_size", 20)
	column.add_child(location)
	var details := Label.new()
	details.text = "Station %s\nHost clearance %.0f km" % [selected.station_id, geometry.clearance / 1000.0]
	details.add_theme_font_size_override("font_size", 14)
	column.add_child(details)
	var note := Label.new()
	note.text = "Right-drag to orbit; scroll to inspect.\nFlight from this save is still in development."
	column.add_child(note)
	save_button = Button.new()
	save_button.text = "Save As…"
	save_button.pressed.connect(open_save_as)
	column.add_child(save_button)
	save_status = Label.new()
	save_status.text = "Choose Save As to keep this session."
	save_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.title = "Save Apsis Drift session"
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift saves"])
	save_dialog.current_dir = OS.get_user_data_dir()
	save_dialog.current_file = "apsis-drift.json"
	save_dialog.file_selected.connect(save_selected)
	save_dialog.canceled.connect(cancel_save_as)
	add_child(save_dialog)
	var quit_button := Button.new()
	quit_button.text = "Quit"
	quit_button.pressed.connect(func() -> void: get_tree().quit(0))
	column.add_child(quit_button)
	port_status = Label.new()
	port_status.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	port_status.offset_left = -240
	port_status.offset_top = -76
	port_status.offset_right = -16
	port_status.offset_bottom = -16
	port_status.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	port_status.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(port_status)
	save_button.grab_focus()
	print("Freedom native shell opened: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d radius=%.1f distance=%.3f phase=%.12f dock_view=3d far=%.1f" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count, geometry.radius, geometry.distance, selected.station_phase_radians, station_view.camera.far])
