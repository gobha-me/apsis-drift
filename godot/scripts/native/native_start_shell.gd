extends Control
## Docked-state presentation from a selected C++ Freedom save or recipe.

const StationView = preload("res://scripts/native/native_station_view.gd")
const HostSky = preload("res://shaders/native_host_sky.gdshader")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const HopperPresentation = preload("res://scripts/characters/hopper_presentation.gd")
const NativeAudio = preload("res://scripts/audio/native_ship_audio.gd")
const NativeTitle = preload("res://scripts/native/native_title.gd")

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
var audio_session: Node
var closing := false
var previous_auto_quit := true
var quit_handler: Callable
var load_dialog: FileDialog
var load_confirmation: ConfirmationDialog
var load_theme: Theme
var load_origin: Control
var load_previous_mode: int
var load_path := ""
var title_view: Control
var title_options: Dictionary = {}


func configure_audio(options: Dictionary) -> void:
	if audio_session != null or not options.has("audio_hum"): return
	var candidate := NativeAudio.new()
	if not candidate.configure(options.audio_hum, options.audio_propulsion, options.get("audio_persist", true)):
		candidate.free()
		push_warning("Recorded ship audio disabled: invalid or missing loop WAVs.")
		return
	audio_session = candidate
	add_child(audio_session)
	previous_auto_quit = get_tree().auto_accept_quit
	get_tree().auto_accept_quit = false


func prepare_view_audio(view: Control) -> void:
	if view is FlightView and audio_session != null:
		view.ship_audio = audio_session.audio
		view.audio_preferences = audio_session.preferences
		view.quit_handler = request_native_quit
	elif audio_session != null and view.has_method("finish_native_quit"):
		view.quit_handler = request_native_quit


func bind_view_audio() -> void:
	if audio_session == null: return
	audio_session.view = current_view
	# Observe the view's committed C++ batch before updating playback targets.
	move_child(audio_session, get_child_count() - 1)
	audio_session.refresh()


func request_native_quit() -> void:
	if closing: return
	closing = true
	if audio_session != null:
		if current_view != null and current_view.has_method("pause_controls"):
			current_view.pause_controls("Closing the game.")
		var drain: float = audio_session.begin_shutdown()
		var deadline := Time.get_ticks_msec() + ceili(drain * 1000.0)
		while not audio_session.shutdown_drained() and Time.get_ticks_msec() < deadline:
			await get_tree().process_frame
	finish_native_quit()


func finish_native_quit() -> void:
	get_tree().quit()


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST and audio_session != null:
		request_native_quit()


func _exit_tree() -> void:
	if audio_session != null:
		get_tree().auto_accept_quit = previous_auto_quit


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
		elif argument.begins_with("--audio-hum=") or argument.begins_with("--audio-propulsion="):
			var hum := argument.begins_with("--audio-hum=")
			var key := "audio_hum" if hum else "audio_propulsion"
			var path := argument.trim_prefix("--audio-hum=" if hum else "--audio-propulsion=")
			if selection.has(key) or not path.is_absolute_path() or path.length() > 4096: return {}
			selection[key] = path
		elif argument.begins_with("--audio-persist="):
			var value := argument.trim_prefix("--audio-persist=")
			if selection.has("audio_persist") or value not in ["true", "false"]: return {}
			selection["audio_persist"] = value == "true"
		else:
			return {}
	if not selection.has("mode"):
		if selection.get("validate_only", false): return {}
		selection["mode"] = "title"
	elif selection.value.is_empty():
		return {}
	if selection.has("audio_hum") != selection.has("audio_propulsion") or (selection.has("audio_persist") and not selection.has("audio_hum")): return {}
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
	configure_audio(options)
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
	prepare_view_audio(candidate)
	candidate.activate()
	bind_view_audio()
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
	if view.has_signal("load_requested"):
		view.load_requested.connect(request_load)
	if view.has_signal("journey_mode_changed"):
		view.journey_mode_changed.connect(switch_journey_view, CONNECT_DEFERRED)


# Root owns replacement dialogs; the current view cannot resume beneath them.
func request_load() -> void:
	if closing or load_origin != null or current_view == null or recovery_overlay != null: return
	if not (current_view is WalkView or current_view is FlightView) or not current_view.focused or not current_view.paused or current_view.save_dialog.visible or current_view.mode_change_pending: return
	if load_dialog == null:
		load_dialog = FileDialog.new()
		load_dialog.title = "Load a saved journey"
		load_dialog.access = FileDialog.ACCESS_FILESYSTEM
		load_dialog.file_mode = FileDialog.FILE_MODE_OPEN_FILE
		load_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
		load_dialog.exclusive = true
		load_theme = Theme.new()
		load_dialog.theme = load_theme
		add_child(load_dialog)
		load_dialog.file_selected.connect(choose_load)
		load_dialog.canceled.connect(func(): finish_load("Load canceled. Resume explicitly after neutral."))
		load_confirmation = ConfirmationDialog.new()
		load_confirmation.title = "Replace the current journey?"
		load_confirmation.dialog_text = "Loading replaces the current journey. Unsaved progress will be lost.\nNo automatic save is made."
		load_confirmation.ok_button_text = "Load journey"
		load_confirmation.exclusive = true
		load_confirmation.theme = load_theme
		load_confirmation.dialog_autowrap = true
		add_child(load_confirmation)
		load_confirmation.confirmed.connect(confirm_load, CONNECT_DEFERRED)
		load_confirmation.canceled.connect(func(): finish_load("Load canceled. Current journey retained."))
		get_window().size_changed.connect(layout_load_dialogs)
	load_origin = current_view
	load_previous_mode = load_origin.process_mode
	load_origin.journey_dialog_open = true
	load_origin.process_mode = Node.PROCESS_MODE_DISABLED
	load_path = ""
	layout_load_dialogs()
	load_dialog.popup_centered_ratio(0.75)


func layout_load_dialogs() -> void:
	if load_theme == null or current_view == null: return
	var pixels := Vector2(get_window().size)
	var logical: Vector2 = current_view.size
	if not pixels.is_finite() or not logical.is_finite() or pixels.x <= 0 or pixels.y <= 0 or logical.x <= 0 or logical.y <= 0: return
	var scale := maxf(1.0, maxf(logical.x / pixels.x, logical.y / pixels.y))
	load_theme.default_font_size = roundi(18 * scale)
	for kind in ["Label", "Button", "LineEdit", "ItemList", "Tree", "PopupMenu"]:
		load_theme.set_font_size("font_size", kind, roundi(18 * scale))
	load_theme.set_font_size("title_font_size", "Window", roundi(18 * scale))
	# Embedded dialog text shares the logical canvas scaling of the native UI.
	# Wrap the warning before requesting a bounded, physically readable width.
	load_confirmation.get_label().custom_minimum_size.x = minf(580 * scale, logical.x * 0.8)
	load_confirmation.size = Vector2i(roundi(minf(620 * scale, logical.x * 0.9)), roundi(minf(180 * scale, logical.y * 0.8)))
	if load_confirmation.visible:
		load_confirmation.popup_centered(load_confirmation.size)
	elif load_dialog.visible:
		load_dialog.popup_centered_ratio(0.75)


func choose_load(path: String) -> void:
	if load_origin == null: return
	load_dialog.hide()
	if not path.is_absolute_path() or path.is_empty() or path.length() > 4096:
		finish_load("Load refused: choose a bounded absolute save path.")
		return
	load_path = path
	layout_load_dialogs()
	load_confirmation.popup_centered(load_confirmation.size)
	load_confirmation.get_cancel_button().grab_focus()


func finish_load(message: String) -> void:
	if load_dialog != null: load_dialog.hide()
	if load_confirmation != null: load_confirmation.hide()
	load_path = ""
	if is_instance_valid(load_origin) and current_view == load_origin:
		load_origin.process_mode = load_previous_mode
		load_origin.journey_dialog_open = false
		load_origin.save_status.text = message
		var button: Button
		if load_origin is FlightView:
			load_origin.controls_menu.controls.status = message
			load_origin.controls_menu.message.text = message
			load_origin.update_compact_hud()
			button = load_origin.controls_menu.load_button
		else:
			button = load_origin.load_button
		if load_origin.focused: button.grab_focus()
	load_origin = null


func confirm_load() -> void:
	if load_origin == null or load_path.is_empty(): return
	if current_view != load_origin or not load_origin.focused or closing:
		finish_load("Load canceled. Restore focus before trying again.")
		return
	load_confirmation.hide()
	# select_start validates/stages everything before the C++ commit and view swap.
	if not select_start(bridge, {"mode": "continue", "value": load_path, "assets": assets_root}):
		var refusal := error
		error = ""
		finish_load("Load refused: " + refusal)
		return
	load_origin = null  # The successfully replaced view was freed by select_start.
	load_path = ""
	load_dialog.hide()


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
	prepare_view_audio(candidate)
	candidate.activate()
	bind_view_audio()
	previous.free()
	error = ""


func _ready() -> void:
	if presentation_only: return
	var options := parse_selection(OS.get_cmdline_user_args())
	if options.is_empty():
		fail("use the title, or supply one --new-game=SEED or --continue=ABSOLUTE_PATH; validation requires a selection")
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		fail("native C++ bridge is unavailable")
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if options.mode == "title":
		open_title(owner, options)
		return
	if not select_start(owner, options):
		fail(error)
		return
	if options.get("validate_only", false): get_tree().quit(0)


func open_title(owner: Variant, options: Dictionary) -> void:
	bridge = owner
	title_options = options.duplicate(true)
	title_view = NativeTitle.new()
	title_view.persist_controls = options.get("persist_controls", true)
	add_child(title_view)
	title_view.start_requested.connect(start_from_title, CONNECT_DEFERRED)
	title_view.quit_requested.connect(request_native_quit)
	print("Freedom native title opened: no journey selected")


func start_from_title(selection: Dictionary) -> void:
	if closing or title_view == null: return
	if not title_view.focused:
		title_view.refuse("Restore application focus before opening a journey.")
		return
	var options := title_options.duplicate(true)
	options.merge(selection, true)
	if not select_start(bridge, options):
		title_view.refuse(error)
		error = ""
		return
	# The existing complete C++/view transaction succeeded; retire only the title.
	var previous := title_view
	title_view = null
	remove_child(previous)
	previous.free()
	title_options.clear()

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
	quit_button.pressed.connect(func() -> void:
		if quit_handler.is_valid(): quit_handler.call()
		else: request_native_quit())
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
