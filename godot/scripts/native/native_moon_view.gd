extends "res://scripts/native/native_flight_view.gd"
## Same native voyage and controls; lunar rendering consumes the typed C++ owner.
const MoonTerrain = preload("res://scripts/native/native_moon_terrain.gd")
const NavigationSky = preload("res://scripts/world/navigation_sky.gd")
var moon_jump_button: Button
var moon_select_button: Button
var companion_panel: PanelContainer

func stage(owner: Variant, assets: String, pending: Dictionary, model: Node3D, handoff := false) -> bool:
	if bridge != null or model == null or model.get_parent() != null or not (model.valid_current_pose() if handoff else model.valid_installed()):
		error = "The complete detached Wayfarer is required"
		return false
	set_process(false)
	set_process_input(false)
	state = pending.flight_state.duplicate(true)
	if not valid_body_state(state):
		error = "The typed lunar presentation is unavailable"
		return false
	bridge = owner
	assets_root = assets
	staged_model = model
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var container := SubViewportContainer.new()
	far_container = container
	container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	add_child(container)
	far_viewport = SubViewport.new()
	far_viewport.size = Vector2i(1280, 720)
	far_viewport.own_world_3d = true
	far_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(far_viewport)
	scene = Node3D.new()
	far_viewport.add_child(scene)
	ship = Node3D.new()
	scene.add_child(ship)
	ship.add_child(model)
	pilot_eye = HopperPresentation.vector(model.specification.pilot_eye)
	cockpit = true
	exhaust = MainExhaust.new()
	if not exhaust.bind_skin(model, model.exterior_surface):
		error = "The Wayfarer exhaust interface is unavailable"
		return false
	ship.add_child(exhaust)
	cockpit_displays = CockpitDisplays.new()
	ship.add_child(cockpit_displays)
	if not cockpit_displays.bind_screens(model.specification.screens) or not cockpit_displays.refresh(state, 0.0, true):
		error = "The Wayfarer lunar instruments are unavailable"
		return false
	camera = Camera3D.new()
	camera.near = 0.05
	camera.far = 200.0
	camera.fov = 75.0
	camera.cull_mask = 3
	# Reuse planetary flight's close/distant composition. A single cockpit-to-
	# lunar-diameter frustum loses nearby geometry in the renderer's float math.
	near_container = SubViewportContainer.new()
	near_container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	near_container.stretch = true
	near_container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var close_material := ShaderMaterial.new()
	close_material.shader = preload("res://shaders/native_close_composite.gdshader")
	near_container.material = close_material
	add_child(near_container)
	near_viewport = SubViewport.new()
	near_viewport.size = far_viewport.size
	near_viewport.transparent_bg = true
	near_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	near_container.add_child(near_viewport)
	near_viewport.add_child(camera)
	camera.make_current()
	terrain_camera = Camera3D.new()
	terrain_camera.cull_mask = 1
	terrain_camera.fov = camera.fov
	scene.add_child(terrain_camera)
	terrain_camera.make_current()
	light = DirectionalLight3D.new()
	light.light_cull_mask = 1
	scene.add_child(light)
	local_light = DirectionalLight3D.new()
	local_light.light_cull_mask = 2
	local_light.shadow_enabled = true
	scene.add_child(local_light)
	planet_environment = Environment.new()
	planet_environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	planet_environment.ambient_light_color = Color(0.17, 0.18, 0.21)
	planet_environment.ambient_light_energy = 0.12
	planet_environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var packet: Dictionary = bridge.get_freedom_background_sky(pending.get("candidate_id", ""))
	if not NativeEnvironment.valid_background(state, packet):
		error = "The universe background is unavailable"
		return false
	sky_material = NavigationSky.material(packet.stars, Color.BLACK, 0.0)
	sky_material.set_shader_parameter("physical_lighting", true)
	var sky := Sky.new()
	sky.sky_material = sky_material
	planet_environment.sky = sky
	planet_environment.background_mode = Environment.BG_SKY
	var environment_node := WorldEnvironment.new()
	environment_node.environment = planet_environment
	scene.add_child(environment_node)
	near_environment = planet_environment.duplicate()
	near_environment.background_mode = Environment.BG_CLEAR_COLOR
	camera.environment = near_environment
	set_ship_layer(ship)
	terrain = create_body_geometry(pending)
	scene.add_child(terrain)
	if not terrain.error.is_empty():
		error = terrain.error
		return false
	suit_lamp = SpotLight3D.new()
	suit_lamp.spot_range = 25.0
	suit_lamp.spot_angle = 48.0
	suit_lamp.light_energy = 2.0
	camera.add_child(suit_lamp)
	build_ui()
	update_view(0.0)
	return error.is_empty()

func build_ui() -> void:
	setup_flight_mouse()
	# The same direction cue used on planets; targets remain C++ observations.
	home_marker = HomeNavigation.new()
	home_marker.visible = false
	add_child(home_marker)
	ship_marker = HomeNavigation.new()
	ship_marker.visible = false
	add_child(ship_marker)
	hud_theme = Theme.new()
	theme = hud_theme
	companion_panel = PanelContainer.new()
	companion_panel.clip_contents = true
	add_child(companion_panel)
	hud_scroll = ScrollContainer.new()
	hud_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	hud_scroll.follow_focus = true
	companion_panel.add_child(hud_scroll)
	hud_scroll_style = hud_scroll.get_v_scroll_bar().get_theme_stylebox("scroll").duplicate()
	hud_scroll.get_v_scroll_bar().add_theme_stylebox_override("scroll", hud_scroll_style)
	var column := VBoxContainer.new()
	hud_column = column
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hud_scroll.add_child(column)
	primary_status = hud_text()
	column.add_child(primary_status)
	assist_button = CheckButton.new()
	assist_button.text = "Flight assistance"
	assist_button.set_pressed_no_signal(state.assistance)
	assist_button.toggled.connect(request_assistance)
	column.add_child(assist_button)
	context_action = Button.new()
	context_action.pressed.connect(request_surface_interaction)
	column.add_child(context_action)
	landing_button = Button.new()
	landing_button.text = "Deploy gear · manual descent"
	landing_button.pressed.connect(func(): port_command("request_freedom_landing"))
	column.add_child(landing_button)
	stow_gear_button = Button.new()
	stow_gear_button.text = "Stow landing gear"
	stow_gear_button.pressed.connect(func(): port_command("stow_freedom_landing_gear"))
	column.add_child(stow_gear_button)
	liftoff_button = Button.new()
	liftoff_button.text = "Release support · use up thrusters"
	liftoff_button.pressed.connect(func(): port_command("liftoff_freedom_surface"))
	column.add_child(liftoff_button)
	moon_select_button = Button.new()
	moon_select_button.text = "Select jump home"
	moon_select_button.pressed.connect(select_home)
	column.add_child(moon_select_button)
	moon_jump_button = Button.new()
	moon_jump_button.text = "Begin local jump"
	moon_jump_button.pressed.connect(jump_requested)
	column.add_child(moon_jump_button)
	pause_button = Button.new()
	pause_button.pressed.connect(toggle_pause)
	add_child(pause_button)
	save_status = hud_text()
	column.add_child(save_status)
	for child in column.get_children():
		if child is Button:
			child.clip_text = true
	resized.connect(layout_hud)
	save_dialog = FileDialog.new()
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	save_dialog.file_selected.connect(func(path: String):
		if not bridge.save_freedom_flight_as(path): report_action_status(str(bridge.get_last_error()))
		else: report_action_status("Voyage saved"))
	add_child(save_dialog)

func activate() -> void:
	activated = true
	near_viewport.world_3d = far_viewport.find_world_3d()
	if surface_walking(): look_offset.x = -float(state.surface_walk.heading)
	last_window_size = get_window().size
	get_window().size_changed.connect(window_resized)
	layout_hud()
	setup_controls()
	update_view(0.0)
	set_process(true)
	set_process_input(true)

func layout_hud() -> void:
	if not activated or not is_inside_tree() or companion_panel == null:
		return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or pixels.x <= 0 or pixels.y <= 0 or not size.is_finite() or size.x <= 0 or size.y <= 0:
		return
	var density := get_viewport().get_final_transform().get_scale().abs()
	ViewportSize.fit(far_container, size, density)
	ViewportSize.fit(near_container, size, density)
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	var margin := minf(16.0 * scale, minf(size.x, size.y) * 0.1)
	var usable := size - Vector2.ONE * (2.0 * margin)
	if usable.x <= 0.0 or usable.y <= 0.0: return
	hud_theme.default_font_size = roundi(18.0 * scale)
	hud_scroll_style.content_margin_left = 8.0 * scale
	hud_scroll_style.content_margin_right = 8.0 * scale
	hud_column.add_theme_constant_override("separation", roundi(8.0 * scale))
	for child in hud_column.get_children():
		if child is Button:
			child.custom_minimum_size.y = 44.0 * scale
	var pause_height := minf(44.0 * scale, usable.y * 0.4)
	var gap := minf(8.0 * scale, usable.y * 0.05)
	var width := minf(420.0 * scale, usable.x)
	pause_button.position = Vector2(margin, size.y - margin - pause_height)
	pause_button.size = Vector2(width, pause_height)
	companion_panel.position = Vector2.ONE * margin
	companion_panel.size = Vector2(width, maxf(0.0, usable.y - pause_height - gap))

func setup_controls() -> void:
	player_input = PlayerInput.new()
	player_input.initial_device = initial_controller_device
	player_input.persist = persist_controls
	player_input.settings_path = control_settings_path
	player_input.thrust_mode = true
	player_input.assistance_request_only = true
	player_input.assist = state.assistance
	add_child(player_input)
	player_input.set_enabled(false)
	controls_menu = PauseMenu.new()
	controls_menu.controls = player_input
	controls_menu.saved_flight = true
	controls_menu.saved_wayfarer = supports_surface_controls()
	controls_menu.ship_audio_available = ship_audio != null
	controls_menu.audio_preferences = audio_preferences
	if ship_audio != null:
		controls_menu.ship_audio_muted.connect(ship_audio.set_muted)
		controls_menu.audio_mix_changed.connect(func():
			var levels: Dictionary = audio_preferences.levels
			ship_audio.set_mix_levels(levels.master, levels.machinery, levels.propulsion, levels.atmosphere))
	add_child(controls_menu)
	setup_frame_diagnostics(controls_menu.save_button.get_parent())
	add_body_controls(controls_menu.save_button.get_parent())
	player_input.pause_requested.connect(toggle_pause)
	player_input.safety_pause.connect(pause_controls)
	player_input.bindings_changed.connect(func(): pause_controls("Controls changed. Release controls, then resume."))
	player_input.assist_requested.connect(request_assistance)
	player_input.camera_requested.connect(change_camera)
	player_input.recenter_requested.connect(recenter_camera)
	player_input.surface_interaction_requested.connect(request_surface_interaction)
	controls_menu.resumed.connect(toggle_pause)
	controls_menu.save_requested.connect(open_save_dialog)
	controls_menu.catalog_save_requested.connect(func(save_as: bool): catalog_save_requested.emit(save_as))
	controls_menu.load_requested.connect(func(): load_requested.emit())
	controls_menu.title_requested.connect(func(): title_requested.emit())
	controls_menu.assistance_requested.connect(request_assistance)
	controls_menu.port_requested.connect(port_command)
	controls_menu.quit_requested.connect(func():
		if quit_handler.is_valid(): quit_handler.call()
		else: get_tree().quit())
	controls_menu.sync_saved_state(state, false)
	controls_menu.show_menu(body_start_message())
	refresh_control_hint()

func select_home() -> void:
	if not focused or not error.is_empty() or save_dialog.visible: return
	if not bridge.select_freedom_home_body_jump():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	report_action_status("Home selected. One charge commits after spooling.")
	update_view(0.0)

func landed_hatch_available() -> bool:
	return state.get("body_kind") == "moon" and state.get("surface", {}).get("landed", false) and not surface_walking() and state.get("boarding", {}).get("seated", false) and state.get("jump", {}).get("phase", "") == "idle" and not state.get("recovery_pending", true)

func request_surface_interaction() -> void:
	if surface_walking():
		# Share the existing foot-only neutral/modal gates: E is also a flight key.
		super.request_surface_interaction()
		return
	if not activated or not focused or paused or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or player_input == null or not player_input.waiting_action.is_empty() or not landed_hatch_available(): return
	if not controls_armed or not player_input.landed_hatch_neutral() or not surface_interaction_neutral():
		report_action_status("Release flight and look controls to use the hatch.")
		return
	# The ordinary port-command gate treats the held E interaction as strafe.
	# This landed-only gate checked every other flight demand independently.
	if not bridge.begin_freedom_surface_walk():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	look_offset = Vector2.ZERO
	pause_controls("On the surface. Release controls, then resume to walk.")
	update_view(0.0)

func surface_action_reference_warning(_command: String) -> String:
	return ""

func refresh_control_hint() -> void:
	if player_input != null:
		player_input.surface_interaction_enabled = surface_walking()
		player_input.landed_hatch_enabled = landed_hatch_available()

func update_compact_hud() -> void:
	if primary_status == null: return
	var resources: Dictionary = state.get("resources", {})
	if surface_walking():
		primary_status.text = "AIRLESS MOON\nOn foot · Suit equipped\n%s / left stick walk · Mouse / look keys / right stick look" % (player_input.foot_keyboard_label() if player_input != null else "WASD")
	else:
		primary_status.text = "AIRLESS MOON\nDatum altitude %.1f m · radial %+.1f m/s\nSurface speed %.1f m/s\nLat %+.3f° · lon %+.3f°\nFuel %.1f%% · jump charges %d" % [state.get("altitude", 0.0), state.get("radial_rate", 0.0), state.get("surface_speed", 0.0), rad_to_deg(state.latitude), rad_to_deg(state.longitude), float(resources.get("fraction", 0.0)) * 100.0, int(resources.get("jump_charges", 0))]
		primary_status.text += "\n" + MoonState.gear_clearance_text(state)
	var marker: Dictionary = state.surface_marker
	if marker.get("present", false):
		if show_personal_marker and marker.current_body:
			primary_status.text += "\nMARK · %.1f m · straight-line distance" % marker.distance_metres
		elif not marker.current_body:
			primary_status.text += "\nMARK retained · %s / %s · another body" % [marker.system_name, SurfaceMarker.body_name(marker)]
	if surface_walking() and state.surface_walk.return_access.get("available", false):
		primary_status.text += "\nHatch · %.1f m" % state.surface_walk.return_access.distance_metres
	if surface_walking():
		var blocked := Status.movement_feedback(state.surface_walk.movement_blockage)
		if not blocked.is_empty(): primary_status.text += "\n" + blocked
	primary_status.tooltip_text = primary_status.text if surface_walking() else "Signed terrain-normal gaps beneath all deployed gear pads. Negative values indicate gear-stroke overlap. This does not certify hull clearance or a safe landing; it is not radar altitude."
	var landed: bool = state.get("surface", {}).get("landed", false)
	var jumping: bool = state.get("jump", {}).get("phase", "idle") != "idle"
	assist_button.set_pressed_no_signal(state.assistance)
	assist_button.disabled = surface_walking() or jumping
	context_action.visible = landed or surface_walking()
	var hatch_binding := "E"
	if player_input != null and player_input.available_pads.get(player_input.device, false):
		hatch_binding += " / " + ("Cross" if player_input.playstation() else "A")
	var exposure := SurfaceLandmarks.nearest_nearby(state)
	var within_return: bool = surface_walking() and state.surface_walk.get("return_access", {}).get("within_reach", false)
	var can_inspect: bool = surface_walking() and not exposure.is_empty() and exposure.can_inspect
	context_action.text = ("Return through hatch" if within_return else "Inspect " + exposure.label if can_inspect else "Approach hatch or exposure") if surface_walking() else "Leave Wayfarer · suit · " + hatch_binding
	if not exposure.is_empty(): primary_status.text += "\n%s · %.1f m%s" % [exposure.label, exposure.distance_metres, " · recorded" if exposure.inspected else ""]
	if landed_hatch_available(): primary_status.text += "\n" + hatch_binding + ": leave Wayfarer · suit"
	context_action.disabled = surface_walking() and not within_return and not can_inspect
	if surface_walking() and not context_action.disabled: context_action.text += " · " + surface_interaction_binding()
	landing_button.disabled = landed or surface_walking() or jumping
	stow_gear_button.disabled = landed or surface_walking() or jumping or not state.get("surface", {}).get("gear_deployed", false)
	liftoff_button.disabled = not landed or surface_walking() or jumping
	moon_select_button.disabled = landed or surface_walking() or jumping
	moon_jump_button.disabled = not state.get("jump", {}).get("available", false) and state.get("jump", {}).get("phase", "idle") != "spool"
	moon_jump_button.text = "Cancel local spool" if state.get("jump", {}).get("phase", "idle") == "spool" else "Begin local jump"
	var jump_reason := str(state.jump.get("refusal", "Local jump unavailable."))
	moon_jump_button.tooltip_text = jump_reason if moon_jump_button.disabled else ""
	if moon_jump_button.disabled and not landed and not surface_walking() and not jumping:
		primary_status.text += "\nJump unavailable · " + jump_reason
	pause_button.text = "Resume" if paused else "Pause"

func update_view(delta: float, _defer_exhaust: bool = false) -> void:
	if not valid_body_state(state):
		if error.is_empty(): error = "Invalid typed lunar observation"
		return
	ship.basis = state.body_basis
	if cockpit_displays != null and not cockpit_displays.refresh(state, delta, paused):
		pause_on_error("The lunar instrument readings are unavailable")
		return
	var boarding: Dictionary = state.get("boarding", {})
	if not boarding.is_empty():
		if not staged_model.set_pose(boarding.pose):
			pause_on_error("The Wayfarer operating pose is unavailable")
			return
		pilot_eye = Vector3(boarding.eye_craft[0], boarding.eye_craft[1], boarding.eye_craft[2])
	if not staged_model.set_gear_preview(1.0 if state.surface.gear_deployed else 0.0):
		pause_on_error("The Wayfarer landing gear pose is unavailable")
		return
	var look := Basis(Vector3.UP, -look_offset.x) * Basis(Vector3.RIGHT, -look_offset.y)
	if surface_walking():
		camera.transform = Transform3D(state.surface_walk.basis * Basis(Vector3.UP, state.surface_walk.heading) * Basis(Vector3.RIGHT, -look_offset.y), state.surface_walk.eye_position)
	elif cockpit:
		camera.transform = Transform3D(state.body_basis * look, state.body_basis * pilot_eye)
	else:
		camera.transform = Transform3D(Basis.IDENTITY, state.body_basis * (look * Vector3(17, 9, 28))).looking_at(Vector3.ZERO, state.body_basis.y)
	if not apply_moon_camera_depth(): return
	light.basis = star_light_basis(state.star_direction)
	light.light_color = state.star_color
	light.light_energy = 1.4
	local_light.basis = light.basis
	local_light.light_color = light.light_color
	var horizon := -acos(clampf(state.body_radius / (state.body_radius + maxf(0.0, state.altitude)), 0.0, 1.0))
	local_light.light_energy = 1.4 if asin(clampf(state.star_direction.y, -1.0, 1.0)) > horizon else 0.0
	sky_material.set_shader_parameter("view_to_inertial", state.station_basis.transposed())
	sky_material.set_shader_parameter("radius", state.body_radius)
	sky_material.set_shader_parameter("altitude", state.altitude)
	sky_material.set_shader_parameter("sun_direction", state.star_direction)
	sky_material.set_shader_parameter("sun_color", state.get("star_color", Color.WHITE))
	sky_material.set_shader_parameter("sun_angular_radius", state.get("star_angular_radius", 0.00465))
	suit_lamp.visible = surface_walking() and suit_lamp_enabled
	if exhaust != null: exhaust.update_applied(state, delta, paused or surface_walking())
	# Staging builds this view detached; window/camera projections begin only
	# after activation attaches it to the scene tree.
	if is_inside_tree():
		home_marker.visible = false
		ship_marker.visible = surface_walking()
		if ship_marker.visible:
			var access: Dictionary = state.surface_walk.return_access
			ship_marker.label_text = "RETURN" if access.available else "SHIP"
			ship_marker.update_cue(camera, access.position if access.available else Vector3.ZERO, false)
		refresh_surface_marker()
	if not surface_landmarks.apply(state, [terrain.material]):
		pause_on_error("The lunar exposure appearance is unavailable")
		return
	refresh_surface_landmarks()
	refresh_control_hint()
	update_compact_hud()

func _process(delta: float) -> void:
	sample_frame_diagnostics(delta)
	if not activated or recovery_notified or bridge == null or not error.is_empty(): return
	sync_mouse_look()
	if not focused: return
	var resolved: Dictionary = player_input.sample()
	controls_armed = not player_input.needs_neutral
	var demand := flight_demand(resolved.thrust_axes)
	if state.surface.landed: demand.fill(0.0)
	var accepted: bool = bridge.advance_freedom_surface_walk(delta, surface_controls(minf(delta, 0.1)), paused or save_dialog.visible) if surface_walking() else bridge.advance_freedom_flight(delta, demand, paused or save_dialog.visible)
	if not accepted:
		pause_on_error(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	if state.get("body_kind") != current_body_kind():
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	if not valid_body_state(state):
		pause_on_error("The actual Moon observation is unavailable")
		return
	if state.get("recovery_pending", false):
		recovery_notified = true
		pause_controls("Recorded craft loss. Continue recovery when ready.", false)
		set_process(false)
		recovery_requested.emit()
		return
	if not paused and not save_dialog.visible and not surface_walking():
		if resolved.recenter: look_offset = Vector2.ZERO
		look_offset += resolved.look * delta
		look_offset.x = clampf(look_offset.x, -1.2 if cockpit else -PI, 1.2 if cockpit else PI)
		look_offset.y = clampf(look_offset.y, -0.8 if cockpit else -1.2, 0.8 if cockpit else 1.2)
	update_view(delta)
	terrain.tick(delta, camera.position, paused or save_dialog.visible)
	if not terrain.error.is_empty(): pause_on_error(terrain.error)
	else: apply_moon_camera_depth()

func apply_moon_camera_depth() -> bool:
	var distant := PlanetStreamView.mesh_depth_range(terrain.nodes, camera.transform, 100000.0)
	if distant == Vector2.ZERO:
		pause_on_error("Lunar camera depth is unavailable")
		return false
	var close_depth := surface_depth_range(state.surface_walk.eye_position, distant.y) if surface_walking() else Vector2(0.05, 200.0)
	if close_depth == Vector2.ZERO:
		pause_on_error("Lunar surface camera depth is unavailable")
		return false
	camera.near = close_depth.x
	camera.far = maxf(close_depth.y, distant.x * 1.01)
	terrain_camera.near = distant.x
	terrain_camera.far = distant.y
	terrain_camera.transform = camera.transform
	return true

# The two supported companion views share the occupied-craft shell and input
# lifecycle. Their typed observations and C++ geometry remain separate.
func valid_body_state(value: Dictionary) -> bool:
	return MoonState.valid(value)

func current_body_kind() -> String:
	return "moon"

func supports_surface_controls() -> bool:
	return true

func body_start_message() -> String:
	return "Lunar flight. Release controls before resuming."

func create_body_geometry(_pending: Dictionary) -> Node3D:
	var geometry := MoonTerrain.new()
	geometry.bridge = bridge
	geometry.expected_body_id = state.body_id
	if not surface_landmarks.apply(state, [geometry.material]): geometry.error = "The lunar exposure packet is invalid"
	return geometry

func add_body_controls(column: Node) -> void:
	controls_menu.add_button(column, "Select jump home", select_home)
	controls_menu.add_button(column, "Begin / cancel local spool", jump_requested)
	controls_menu.add_button(column, "Use Wayfarer hatch", func(): port_command("return_from_freedom_surface_walk" if surface_walking() else "begin_freedom_surface_walk"))
	var marker_column: Node = column
	surface_marker_button = controls_menu.add_button(marker_column, "Mark this spot", func(): surface_marker_command("replace_freedom_surface_marker" if state.get("surface_marker", {}).get("present", false) else "mark_freedom_surface_marker"))
	clear_surface_marker_button = controls_menu.add_button(marker_column, "Clear marker", func(): surface_marker_command("clear_freedom_surface_marker"))
	show_surface_marker_button = controls_menu.add_button(marker_column, "Show personal marker: Off", toggle_surface_marker)
	landmark_enable_button = controls_menu.add_button(column, "Enable local landmarks", enable_surface_landmarks)
	landmark_inspect_button = controls_menu.add_button(column, "Inspect exposure", inspect_surface_landmark)
