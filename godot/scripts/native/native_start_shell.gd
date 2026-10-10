extends Control
## Docked-state presentation from a selected C++ Freedom save or recipe.

const StationView = preload("res://scripts/native/native_station_view.gd")
const HostSky = preload("res://shaders/native_host_sky.gdshader")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const HopperPresentation = preload("res://scripts/characters/hopper_presentation.gd")

var bridge: Variant = null
var selected: Dictionary = {}
var save_dialog: FileDialog
var save_status: Label
var save_button: Button
var assets_root := ""
var station_view: Node3D
var port_status: Label
var current_view: Control
var presentation_only := false
var error := ""
var recovery_overlay: Control
var recovery_status: Label


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


static func decimal_text(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57:
			return false
	return true


static func dock_geometry(start: Dictionary) -> Dictionary:
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


static func valid_start(start: Dictionary) -> bool:
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


static func valid_pending(value: Variant) -> bool:
	if not value is Dictionary or not value.keys().size() == 8:
		return false
	for key in ["candidate_id", "mode", "craft_binding", "walk_state", "flight_state", "station_state", "station_geometry", "streaming_ready"]:
		if not value.has(key): return false
	if not decimal_text(value.candidate_id) or value.candidate_id == "0" or not value.streaming_ready is bool or not HopperPresentation.valid_binding(value.craft_binding):
		return false
	for key in ["walk_state", "flight_state", "station_state", "station_geometry"]:
		if not value[key] is Dictionary: return false
	match value.mode:
		"freedom_walk":
			return value.streaming_ready and WalkView.valid_state(value.walk_state) and FlightView.valid_state(value.flight_state) and value.station_state.is_empty()
		"freedom_flight":
			return value.streaming_ready and value.walk_state.is_empty() and FlightView.valid_state(value.flight_state) and value.station_state.is_empty()
		"freedom":
			return value.walk_state.is_empty() and value.flight_state.is_empty() and valid_start(value.station_state)
	return false

static func log_selection(pending: Dictionary, verb: String, candidate: Control = null) -> void:
	if pending.mode == "freedom_walk":
		var walking: Dictionary = pending.walk_state
		print("Freedom native walking shell %s: seed=%s tick=%s system=%s planet=%s station=%s craft=%s actor=%s" % [verb, walking.universe_seed, walking.tick, walking.system_id, walking.planet_id, walking.station_id, walking.craft_id, walking.actor_id])
	elif pending.mode == "freedom_flight":
		var flight: Dictionary = pending.flight_state
		print("Freedom native flight shell %s: seed=%s tick=%s system=%s planet=%s station=%s craft=%s checksum=%s" % [verb, flight.universe_seed, flight.tick, flight.system_id, flight.planet_id, flight.station_id, flight.craft_id, flight.checksum])
	else:
		var start: Dictionary = pending.station_state
		var geometry := dock_geometry(start)
		var suffix := " dock_view=3d far=%.1f" % candidate.station_view.camera.far if candidate != null else ""
		print("Freedom native shell %s: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d radius=%.1f distance=%.3f phase=%.12f%s" % [verb, start.universe_seed, start.tick, start.system_id, start.home_planet_id, start.station_id, start.craft_id, start.discovery_count, start.world_delta_count, geometry.radius, geometry.distance, start.station_phase_radians, suffix])


func stage_view(owner: Variant, assets: String, pending: Dictionary) -> Control:
	if not valid_pending(pending):
		error = "C++ returned an invalid pending native selection"
		return null
	var model: Node3D
	if pending.mode == "freedom_walk" or (pending.mode == "freedom_flight" and pending.flight_state.frame_id == "2"):
		model = HopperPresentation.load_selected_asset(assets, pending.craft_binding)
		if model == null:
			error = "Selected Wayfarer assets could not be staged"
			return null
	var candidate: Control
	if pending.mode == "freedom_walk":
		candidate = WalkView.new()
	elif pending.mode == "freedom_flight":
		candidate = FlightView.new()
	else:
		candidate = load("res://scripts/native/native_start_shell.gd").new()
		candidate.presentation_only = true
		candidate.bridge = owner
		candidate.selected = pending.station_state.duplicate(true)
		candidate.assets_root = assets
		if not candidate.build_view(dock_geometry(pending.station_state), pending.station_geometry):
			error = candidate.error
			candidate.free()
			return null
		return candidate
	if not candidate.stage(owner, assets, pending, model):
		error = candidate.error
		if model != null and model.get_parent() == null: model.free()
		candidate.free()
		return null
	return candidate

func select_start(owner: Variant, options: Dictionary) -> bool:
	var staged: bool = owner.stage_freedom_recovery() if options.mode == "recovery" else owner.stage_freedom_new_game(options.value) if options.mode == "new_game" else owner.stage_freedom_continue(options.value)
	if not staged:
		error = str(owner.get_last_error())
		return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	if not valid_pending(pending):
		error = "Invalid pending source projection"
		if decimal_text(pending.get("candidate_id")): owner.discard_pending_freedom_start(pending.candidate_id)
		return false
	if options.get("validate_only", false):
		owner.discard_pending_freedom_start(pending.candidate_id)
		log_selection(pending, "validated")
		return true
	var assets: String = options.get("assets", "")
	var candidate := stage_view(owner, assets, pending)
	if candidate == null:
		owner.discard_pending_freedom_start(pending.candidate_id)
		return false
	if not candidate.ready_to_commit() or owner.get_pending_freedom_start() != pending or (pending.mode != "freedom" and pending.flight_state.frame_id == "2" and not HopperPresentation.sources_unchanged(assets, pending.craft_binding)):
		error = "Pending session or complete model changed before commit"
		candidate.free()
		owner.discard_pending_freedom_start(pending.candidate_id)
		return false
	if not owner.commit_pending_freedom_start(pending.candidate_id):
		error = str(owner.get_last_error())
		candidate.free()
		owner.discard_pending_freedom_start(pending.candidate_id)
		return false
	# No yield or asset operation between the C++ commit and ready-view swap.
	var previous := current_view
	if previous != null:
		remove_child(previous)
	add_child(candidate)
	current_view = candidate
	bridge = owner
	assets_root = assets
	connect_journey_view(candidate)
	candidate.activate()
	refresh_recovery()
	log_selection(pending, "opened", candidate)
	if previous != null: previous.free()
	error = ""
	return true

func refresh_recovery() -> void:
	if recovery_overlay != null:
		remove_child(recovery_overlay)
		recovery_overlay.free()
		recovery_overlay = null
	var recovery: Dictionary = bridge.get_freedom_recovery_state()
	if not recovery.get("pending", false): return
	current_view.pause_controls("Recorded craft loss. Continue with a replacement when ready.")
	current_view.set_process(false)
	current_view.set_process_input(false)
	recovery_overlay = ColorRect.new()
	recovery_overlay.color = Color(0.02, 0.025, 0.04, 0.94)
	recovery_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	recovery_overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(recovery_overlay)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	recovery_overlay.add_child(center)
	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(520, 0)
	column.add_theme_constant_override("separation", 16)
	center.add_child(column)
	var title := Label.new()
	title.text = "CRAFT LOSS · STANDARD RECOVERY"
	column.add_child(title)
	var explanation := Label.new()
	explanation.text = recovery.explanation
	explanation.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(explanation)
	var resume := Button.new()
	resume.text = "Continue at Origin Station"
	resume.pressed.connect(continue_recovery)
	column.add_child(resume)
	var save := Button.new()
	save.text = "Save As…"
	save.pressed.connect(func() -> void: current_view.save_dialog.popup_centered_ratio(0.75))
	column.add_child(save)
	recovery_status = Label.new()
	recovery_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(recovery_status)
	resume.grab_focus()


func continue_recovery() -> void:
	if not select_start(bridge, {"mode": "recovery", "assets": assets_root}):
		recovery_status.text = error
		recovery_status.modulate = Color(1.0, 0.75, 0.65)


func connect_journey_view(view: Control) -> void:
	if view.has_signal("journey_mode_changed"):
		view.journey_mode_changed.connect(switch_journey_view, CONNECT_DEFERRED)


func switch_journey_view() -> void:
	if bridge == null or current_view == null: return
	var walk: Dictionary = bridge.get_freedom_walk_state()
	var wants_walk := not walk.is_empty()
	if (wants_walk and current_view is WalkView) or (not wants_walk and current_view is FlightView): return
	var previous := current_view
	var model: Node3D = previous.staged_model
	if not is_instance_valid(model) or not model.valid_current_pose() or model.selected_binding != bridge.get_freedom_craft_binding():
		previous.error = "The current Wayfarer presentation changed unexpectedly"
		previous.pause_controls(previous.error)
		return
	var parent := model.get_parent()
	parent.remove_child(model)
	var candidate: Control = WalkView.new() if wants_walk else FlightView.new()
	var pending := {"walk_state": walk, "flight_state": bridge.get_freedom_flight_state(), "station_geometry": bridge.get_freedom_station_geometry()}
	if not candidate.stage(bridge, assets_root, pending, model, true) or not candidate.ready_to_commit():
		error = candidate.error if not candidate.error.is_empty() else "Wayfarer view handoff failed"
		if model.get_parent() != null: model.get_parent().remove_child(model)
		parent.add_child(model)
		candidate.free()
		previous.error = error
		previous.pause_controls(error)
		return
	# Only presentation changes here: C++ already owns the committed journey.
	remove_child(previous)
	add_child(candidate)
	current_view = candidate
	connect_journey_view(candidate)
	candidate.activate()
	previous.free()
	error = ""


func _ready() -> void:
	if presentation_only: return
	var options := parse_selection(OS.get_cmdline_user_args())
	if options.is_empty():
		fail("supply exactly one --new-game=SEED or --continue=ABSOLUTE_PATH")
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		fail("native C++ bridge is unavailable")
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not select_start(owner, options):
		fail(error)
		return
	if options.get("validate_only", false): get_tree().quit(0)

func ready_to_commit() -> bool:
	return presentation_only and error.is_empty() and station_view != null

func activate() -> void:
	save_button.grab_focus()


func build_view(geometry: Dictionary, station_geometry: Dictionary) -> bool:
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
	if not station_view.initialize(selected, station_geometry, assets_root):
		error = station_view.error
		return false
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


	return true
