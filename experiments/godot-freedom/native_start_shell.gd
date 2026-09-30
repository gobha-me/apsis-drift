extends Control
## Docked-state presentation from a selected C++ Freedom save or recipe.

var bridge: Variant = null
var selected: Dictionary = {}


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
	selected = bridge.get_freedom_start()
	if not valid_start(selected):
		fail("C++ bridge returned an invalid docked-state view")
		return
	var geometry := dock_geometry(selected)
	if options.get("validate_only", false):
		print("Freedom native shell validated: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d radius=%.1f distance=%.3f phase=%.12f" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count, geometry.radius, geometry.distance, selected.station_phase_radians])
		get_tree().quit(0)
		return
	build_view(geometry)


func build_view(geometry: Dictionary) -> void:
	var viewport_frame := SubViewportContainer.new()
	viewport_frame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	viewport_frame.stretch = true
	add_child(viewport_frame)
	var spatial := SubViewport.new()
	spatial.size = Vector2i(1280, 720)
	spatial.own_world_3d = true
	spatial.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport_frame.add_child(spatial)
	var stage := Node3D.new()
	stage.name = "StationLocalPresentation"
	spatial.add_child(stage)
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.018, 0.032, 0.065)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.75, 0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	stage.add_child(world)
	var relative := selected.station_relative_position_metres as PackedFloat64Array
	var radial := Vector3(relative[0], relative[1], relative[2]).normalized()
	var reference := Vector3.UP if abs(radial.dot(Vector3.UP)) < 0.9 else Vector3.RIGHT
	var tangent := radial.cross(reference).normalized()
	var host := MeshInstance3D.new()
	host.name = "SelectedHomePlanetProxy"
	var globe := SphereMesh.new()
	globe.radius = geometry.radius
	globe.height = geometry.radius * 2.0
	globe.radial_segments = 96
	globe.rings = 48
	host.mesh = globe
	host.position = Vector3(-relative[0], -relative[1], -relative[2])
	var host_material := StandardMaterial3D.new()
	host_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	host_material.albedo_color = Color(0.16, 0.34, 0.58)
	host.material_override = host_material
	stage.add_child(host)
	var station := MeshInstance3D.new()
	station.name = "ProvisionalStationPoseMarker"
	var marker := CylinderMesh.new()
	marker.top_radius = 9.0
	marker.bottom_radius = 9.0
	marker.height = 2.0
	station.mesh = marker
	var marker_material := StandardMaterial3D.new()
	marker_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	marker_material.albedo_color = Color(0.78, 0.84, 0.88)
	station.material_override = marker_material
	stage.add_child(station)
	var camera := Camera3D.new()
	camera.position = radial * 80.0 + tangent * 30.0
	camera.near = 0.25
	camera.far = geometry.distance + geometry.radius + 100000.0
	stage.add_child(camera)
	camera.look_at(Vector3.ZERO, reference)
	camera.current = true
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	add_child(margin)
	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(590, 0)
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
	column.add_theme_constant_override("separation", 18)
	panel.add_child(column)
	var title := Label.new()
	title.text = "APSIS DRIFT"
	title.add_theme_font_size_override("font_size", 40)
	column.add_child(title)
	var location := Label.new()
	location.text = "Docked at Origin Station"
	location.add_theme_font_size_override("font_size", 25)
	column.add_child(location)
	var details := Label.new()
	details.text = "Universe %s  •  System %s\nStation %s  •  Craft %s\nSimulation tick %s  •  Discoveries %d\nHost clearance %.0f km" % [selected.universe_seed, selected.system_id, selected.station_id, selected.craft_id, selected.tick, selected.discovery_count, geometry.clearance / 1000.0]
	details.add_theme_font_size_override("font_size", 18)
	column.add_child(details)
	var note := Label.new()
	note.text = "Station pose marker and host silhouette are provisional.\nFlight from this save is still in development."
	column.add_child(note)
	var quit_button := Button.new()
	quit_button.text = "Quit"
	quit_button.pressed.connect(func() -> void: get_tree().quit(0))
	column.add_child(quit_button)
	quit_button.grab_focus()
	print("Freedom native shell opened: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d radius=%.1f distance=%.3f phase=%.12f dock_view=3d" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count, globe.radius, host.position.length(), selected.station_phase_radians])
