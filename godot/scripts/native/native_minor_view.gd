extends "res://scripts/native/native_moon_view.gd"
## The same occupied-craft shell; a distinct C++ minor owner and close mesh.
const MinorState = preload("res://scripts/native/native_minor_state.gd")
const MinorGeometry = preload("res://scripts/native/native_minor_geometry.gd")

func valid_body_state(value: Dictionary) -> bool:
	return MinorState.valid(value)

func current_body_kind() -> String:
	return "minor"

func supports_surface_controls() -> bool:
	return false

func body_start_message() -> String:
	return "Asteroid inspection. Stay outside the marked stand-off limit; landing is unavailable. Release controls before resuming."

func create_body_geometry(pending: Dictionary) -> Node3D:
	var geometry := MinorGeometry.new()
	if not geometry.stage(bridge, pending, state) and geometry.error.is_empty():
		geometry.error = "The minor-body geometry could not be staged"
	return geometry

func add_body_controls(column: Node) -> void:
	controls_menu.add_button(column, "Select jump home", select_home)
	controls_menu.add_button(column, "Begin / cancel local spool", jump_requested)

func build_ui() -> void:
	super.build_ui()
	for button in [context_action, landing_button, stow_gear_button, liftoff_button]:
		button.visible = false

func landed_hatch_available() -> bool:
	return false

func refresh_control_hint() -> void:
	if player_input != null:
		player_input.surface_interaction_enabled = false
		player_input.landed_hatch_enabled = false

func update_compact_hud() -> void:
	if primary_status == null: return
	primary_status.text = "ASTEROID / CLOSE INSPECTION\nRange above spherical proxy %.1f km\nRadial rate %+.1f m/s · speed %.1f m/s\nStand-off gap %.1f m\nFuel %.1f%% · jump charges %d\nInspection only · no landing" % [state.altitude / 1000.0, state.radial_rate, state.surface_speed, state.stand_off_gap, state.resources.fraction * 100.0, state.resources.jump_charges]
	primary_status.tooltip_text = "The source catalog currently supplies a spherical proxy, not an asteroid terrain or landing surface. The C++ stand-off constraint stops inward motion before that proxy. Rotation and applied-thrust fuel accounting continue."
	var note := str(state.get("inspection_note", ""))
	if not note.is_empty(): primary_status.text += "\n" + note
	var jumping: bool = state.jump.phase != "idle"
	assist_button.set_pressed_no_signal(state.assistance)
	assist_button.disabled = jumping
	moon_select_button.disabled = jumping
	moon_jump_button.disabled = not state.jump.available and state.jump.phase != "spool"
	moon_jump_button.text = "Cancel local spool" if state.jump.phase == "spool" else "Begin local jump"
	moon_jump_button.tooltip_text = str(state.jump.refusal) if moon_jump_button.disabled else ""
	pause_button.text = "Resume" if paused else "Pause"

func update_view(delta: float, _defer_exhaust: bool = false) -> void:
	if not MinorState.valid(state):
		if error.is_empty(): error = "Invalid typed minor-body observation"
		return
	ship.basis = state.body_basis
	if cockpit_displays != null and not cockpit_displays.refresh(state, delta, paused):
		pause_on_error("The minor-body instrument readings are unavailable")
		return
	if not staged_model.set_pose(state.boarding.pose) or not staged_model.set_gear_preview(0.0):
		pause_on_error("The occupied Wayfarer pose is unavailable")
		return
	pilot_eye = Vector3(state.boarding.eye_craft[0], state.boarding.eye_craft[1], state.boarding.eye_craft[2])
	var look := Basis(Vector3.UP, -look_offset.x) * Basis(Vector3.RIGHT, -look_offset.y)
	if cockpit:
		camera.transform = Transform3D(state.body_basis * look, state.body_basis * pilot_eye)
	else:
		camera.transform = Transform3D(Basis.IDENTITY, state.body_basis * (look * Vector3(17, 9, 28))).looking_at(Vector3.ZERO, state.body_basis.y)
	if not terrain.apply_state(state) or not apply_moon_camera_depth():
		if error.is_empty(): error = terrain.error
		return
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
	sky_material.set_shader_parameter("sun_color", state.star_color)
	sky_material.set_shader_parameter("sun_angular_radius", state.star_angular_radius)
	suit_lamp.visible = false
	if exhaust != null: exhaust.update_applied(state, delta, paused)
	home_marker.visible = false
	ship_marker.visible = false
	refresh_control_hint()
	update_compact_hud()
