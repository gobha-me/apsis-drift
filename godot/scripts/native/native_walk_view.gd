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
const CockpitDisplays = preload("res://scripts/native/native_cockpit_displays.gd")
var cockpit_displays: Node3D
const PlanetOptics = preload("res://scripts/native/native_planet_optics.gd")
var planet_optics := PlanetOptics.new()
const Controls = preload("res://scripts/ui/player_input.gd")
const MouseLook = preload("res://scripts/native/native_mouse_look.gd")
const ViewportSize = preload("res://scripts/native/native_viewport_size.gd")
const FrameDiagnostics = preload("res://scripts/native/native_frame_diagnostics.gd")
var frame_diagnostics: PanelContainer
var frame_diagnostics_button: Button
var initial_frame_diagnostics := false
var mouse_look := MouseLook.new()
const ControlSettings = preload("res://scripts/ui/control_settings.gd")
const StationCrew = preload("res://scripts/native/native_station_crew.gd")
const CrewConversation = preload("res://scripts/native/station_crew_conversation.gd")
var crew_actor: Node3D
var crew_reader: Control
var crew_packet: Dictionary = {}
var crew_query_elapsed := 0.1
var crew_nearby := false
var crew_remark_tick := -3600
var crew_remark_until := -1
var pending_crew_talk: Dictionary = {}
var crew_feedback := ""
var stick_settings_path := Controls.SETTINGS_PATH
var stick_preferences: Dictionary = {}
var stick_preferences_status := ""
const ServiceCraft = preload("res://scripts/native/native_service_craft_view.gd")
var service_craft: Node3D
var service_binding: Dictionary = {}
var assets_root := ""
var bridge: Variant
var state: Dictionary = {}
var station: Node3D
var ship: Node3D
var camera: Camera3D
var light: DirectionalLight3D
var station_environment: Environment
var sky_material: ShaderMaterial
var sky_owner: Dictionary = {}
var scene: Node3D
var viewport: SubViewport
var scene_container: SubViewportContainer
var telemetry: Label
var pause_button: Button
var board_button: Button
var boarding_prompt: Label
var boarding_availability: Dictionary = {}
var boarding_query_elapsed := 0.1
var export_save_button: Button
var replace_save_button: Button
var save_button: Button
var load_button: Button
var title_button: Button
var settings_button: Button
var settings_view: Control
var observations_button: Button
var observations_view: Control
var observations_backing: ColorRect
var observations_return_frames := 0
var preferences_provider: Node
var settings_return_frames := 0
var save_dialog: FileDialog
var save_status: Label
var hud_scroll: ScrollContainer
var hud_column: VBoxContainer
var hud_theme: Theme
var hud_scroll_style: StyleBox
var hud_hint: Label
var active_card: PanelContainer
var active_status: Label
var active_card_style: StyleBox
var paused := false
var focused := true
var controls_armed := false
var last_window_size := Vector2i.ZERO
var selected_pad := -1
var initial_controller_device := -2
var available_pads: Dictionary = {}
var requested_heading := 0.0
var pitch := 0.0
var boarding_look := Vector2.ZERO
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
	if value.has("walk_access") and not StationPresentation.valid_walk_access(value.walk_access): return false
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


static func valid_boarding_availability(value: Variant, actor: Dictionary) -> bool:
	if not value is Dictionary or value.size() != 7 or value.get("version") != 1 or not value.get("version") is int or not value.get("available") is bool or not value.get("reason") is String or value.reason.is_empty() or value.reason.length() > 512:
		return false
	for key in ["universe_seed", "system_id", "craft_id", "tick"]:
		if not NativeEnvironment.decimal(value.get(key)) or value[key] != actor.get(key): return false
	return true


func valid_environment(flight: Dictionary, actor: Dictionary) -> bool:
	if not FlightView.valid_state(flight): return false
	for key in ["universe_seed", "system_id", "planet_id", "station_id", "craft_id", "tick"]:
		if not NativeEnvironment.decimal(flight.get(key)): return false
		if key != "tick" and not sky_owner.is_empty() and flight[key] != sky_owner[key]: return false
	if not sky_owner.is_empty():
		var tick: String = flight.tick
		var previous: String = sky_owner.tick
		if tick.length() < previous.length() or (tick.length() == previous.length() and tick < previous): return false
	if actor.is_empty():
		# The final seated projection owns its boarding pose and clock together.
		return flight.get("boarding", {}).get("state") == "seated"
	if not valid_state(actor): return false
	for key in ["universe_seed", "system_id", "planet_id", "station_id", "craft_id", "tick"]:
		if actor[key] != flight[key]: return false
	return true

func refresh_environment(flight: Dictionary, actor: Dictionary) -> bool:
	if not valid_environment(flight, actor) or not NativeEnvironment.apply_sky(flight, sky_material) or not planet_optics.apply(flight, [sky_material]): return false
	light.basis = FlightView.star_light_basis(flight.star_direction)
	light.light_color = flight.lighting.star_color
	light.light_energy = 1.5 * NativeEnvironment.sun_visibility(flight)
	for key in ["universe_seed", "system_id", "planet_id", "station_id", "craft_id", "tick"]:
		sky_owner[key] = flight[key]
	return true


func initialize(owner: Variant, assets: String) -> bool:
	var model := HopperPresentation.load_selected_asset(assets, owner.get_freedom_craft_binding())
	if model == null:
		error = "The selected Wayfarer could not be staged"
		return false
	var pending := {"walk_state": owner.get_freedom_walk_state(), "flight_state": owner.get_freedom_flight_state(), "station_geometry": owner.get_freedom_station_geometry(), "craft_binding": owner.get_freedom_craft_binding()}
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
	service_binding = pending.get("craft_binding", {}).duplicate(true)
	assets_root = assets
	bridge = owner
	state = pending.walk_state.duplicate(true)
	if not valid_state(state):
		error = "The pending station walking actor is unavailable"
		return false
	var flight: Dictionary = pending.flight_state
	if not valid_environment(flight, state) or not flight.attached or flight.target_port != 1:
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
	scene_container = container
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
	crew_actor = StationCrew.new()
	station.add_child(crew_actor)
	if not crew_actor.configure(assets):
		error = crew_actor.error
		return false
	ship = Node3D.new()
	scene.add_child(ship)
	ship.add_child(model)
	cockpit_displays = CockpitDisplays.new()
	ship.add_child(cockpit_displays)
	if not cockpit_displays.bind_screens(model.specification.screens) or not cockpit_displays.refresh(flight, 0.0, true):
		error = "The Wayfarer instrument screens are unavailable"
		return false
	camera = Camera3D.new()
	camera.name = "StationActorEye"
	camera.near = 0.03
	camera.far = 180.0
	camera.fov = 75.0
	scene.add_child(camera)
	camera.current = true
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	station_environment = environment
	sky_material = NativeEnvironment.install(environment)
	if not NativeEnvironment.bind_background(flight, sky_material, owner.get_freedom_background_sky(pending.get("candidate_id", ""))):
		error = "The saved universe background is unavailable"
		return false
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.75, 0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	scene.add_child(world)
	light = DirectionalLight3D.new()
	scene.add_child(light)
	if not refresh_environment(flight, state):
		error = "The station sky projection is unavailable"
		return false
	if not refresh_service_craft(flight, 0.0):
		error = "The service Wayfarer presentation is unavailable"
		return false
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
	# Godot enables scripted _input on tree entry, overriding the staging flags.
	# Keep station device filtering in this view after the children are ready.
	preferences_provider.set_process_input(false)
	observations_view.set_process_input(false)
	if initial_controller_device != -2:
		selected_pad = initial_controller_device
		if selected_pad >= 0: available_pads[selected_pad] = true
	for pad in Input.get_connected_joypads():
		available_pads[pad] = true
		if initial_controller_device == -2 and selected_pad == -1: selected_pad = pad
	Input.joy_connection_changed.connect(controller_connection_changed)
	# Activation establishes the baseline; initial Control layout is not a resize.
	last_window_size = get_window().size
	get_window().size_changed.connect(window_resized)
	layout_hud()
	controls_armed = current_controls_neutral()
	paused = state.continued or not controls_armed
	if paused: pause_controls("Release walking and look controls, then Resume explicitly.")
	update_view()
	sync_mouse_look()
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
	active_card = PanelContainer.new()
	active_card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	active_card.theme = hud_theme
	active_card_style = backing.duplicate()
	active_card.add_theme_stylebox_override("panel", active_card_style)
	active_status = hud_text()
	active_card.add_child(active_status)
	# Wrapping resolves after the container assigns the label's actual width.
	active_status.minimum_size_changed.connect(active_card.reset_size)
	add_child(active_card)
	active_card.hide()
	telemetry = hud_text()
	column.add_child(telemetry)
	var hint := hud_text()
	hud_hint = hint
	hint.text = "Walking keys / left stick walk · Mouse / look keys / right stick look\nEsc / Start releases the mouse and opens controls\nWalk toward D1 · E / A climb aboard Wayfarer"
	column.add_child(hint)
	board_button = Button.new()
	board_button.text = "Board Wayfarer · E / A"
	board_button.focus_mode = Control.FOCUS_NONE
	board_button.pressed.connect(board_requested)
	column.add_child(board_button)
	boarding_prompt = Label.new()
	boarding_prompt.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	boarding_prompt.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	boarding_prompt.mouse_filter = Control.MOUSE_FILTER_IGNORE
	boarding_prompt.add_theme_font_size_override("font_size", 22)
	boarding_prompt.add_theme_color_override("font_shadow_color", Color.BLACK)
	boarding_prompt.add_theme_constant_override("shadow_offset_x", 2)
	boarding_prompt.add_theme_constant_override("shadow_offset_y", 2)
	add_child(boarding_prompt)
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
		if focused and not journey_dialog_open and not save_dialog.visible and not settings_open() and not observations_open() and not crew_open():
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
		if focused and not journey_dialog_open and not save_dialog.visible and not settings_open() and not observations_open() and not crew_open():
			pause_controls("Return to title only after discarding unsaved progress.")
			title_requested.emit())
	column.add_child(title_button)
	export_save_button = Button.new()
	export_save_button.text = "Export save file…"
	export_save_button.pressed.connect(open_save_dialog)
	column.add_child(export_save_button)
	observations_button = Button.new()
	observations_button.text = "Recorded observations"
	observations_button.pressed.connect(open_observations)
	column.add_child(observations_button)
	frame_diagnostics_button = Button.new()
	frame_diagnostics_button.pressed.connect(toggle_frame_diagnostics)
	column.add_child(frame_diagnostics_button)
	frame_diagnostics = FrameDiagnostics.new()
	add_child(frame_diagnostics)
	frame_diagnostics.set_enabled(initial_frame_diagnostics)
	refresh_frame_diagnostics_button()
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
	observations_backing = ColorRect.new()
	observations_backing.color = Color(0.015, 0.025, 0.04)
	observations_backing.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(observations_backing)
	observations_backing.hide()
	observations_view = preload("res://scripts/native/recorded_observations.gd").new()
	observations_view.controls = preferences_provider
	observations_view.query = Callable(bridge, "get_freedom_observations")
	observations_view.back_requested.connect(close_observations)
	add_child(observations_view)
	# The station owns device/focus filtering before forwarding reader scrolling.
	# Letting the child process first would admit stale disconnected-pad events.
	observations_view.set_process_input(false)
	crew_reader = CrewConversation.new()
	crew_reader.topic_requested.connect(select_crew_topic)
	crew_reader.back_requested.connect(close_crew_conversation)
	add_child(crew_reader)
	save_dialog.canceled.connect(save_canceled)
	resized.connect(layout_hud)


func refresh_frame_diagnostics_button() -> void:
	frame_diagnostics_button.text = "Frame diagnostics: " + ("On" if frame_diagnostics.enabled else "Off")
	frame_diagnostics_button.tooltip_text = "Optional local frame/clock readings while playing. No automatic tuning, recording or upload. Resume explicitly."


func toggle_frame_diagnostics() -> void:
	if not paused or not focused or journey_dialog_open or settings_open() or observations_open() or crew_open() or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible: return
	frame_diagnostics.set_enabled(not frame_diagnostics.enabled)
	refresh_frame_diagnostics_button()
	save_status.text = "Frame diagnostics selected. Resume explicitly to continue."


func hud_text() -> Label:
	var label := Label.new()
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func window_resized() -> void:
	var pixels := get_window().size
	var changed := pixels != last_window_size
	last_window_size = pixels
	if changed and activated and not paused:
		# Neutral, valid resizes remain cosmetic and preserve running station movement.
		# Held controls require the existing explicit neutral/resume gate.
		if not current_controls_neutral():
			pause_controls("Window resized. Release controls, then resume explicitly.")
	layout_hud()


func layout_hud() -> void:
	if not activated or not is_inside_tree() or hud_scroll == null:
		return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or pixels.x <= 0 or pixels.y <= 0 or not size.is_finite() or size.x <= 0 or size.y <= 0:
		return
	ViewportSize.fit(scene_container, size, get_viewport().get_final_transform().get_scale().abs())
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
	for button in [board_button, pause_button, replace_save_button, save_button, load_button, settings_button, title_button, export_save_button, observations_button, frame_diagnostics_button]:
		button.custom_minimum_size.y = 40 * scale
	hud_scroll.position = Vector2.ONE * margin
	# ScrollContainer lays out integer-sized children; keep its viewport integral
	# so follow-focus cannot round a bottom action past the clipping edge.
	hud_scroll.size = Vector2(minf(340 * scale, usable.x), usable.y).floor()
	active_card.position = Vector2.ONE * margin
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		active_card_style.set_content_margin(side, 10 * scale)
	active_card.custom_minimum_size.x = minf(340 * scale, usable.x)
	active_card.size.x = active_card.custom_minimum_size.x
	active_card.reset_size()
	if boarding_prompt != null:
		boarding_prompt.position = Vector2(margin, size.y - margin - 70 * scale)
		boarding_prompt.size = Vector2(usable.x, 64 * scale)
		boarding_prompt.add_theme_font_size_override("font_size", roundi(22 * scale))


func refresh_hud() -> void:
	if hud_scroll == null or active_card == null: return
	var nested := settings_open() or observations_open() or crew_open()
	hud_scroll.visible = paused and not nested
	active_card.visible = not paused and not nested and not journey_dialog_open and save_dialog != null and not save_dialog.visible and error.is_empty()
	hud_hint.text = "%s / left stick walk · Mouse / look keys / right stick look\nEsc / Start releases the mouse and opens controls\nWalk toward D1 · E / A climb aboard Wayfarer" % preferences_provider.foot_keyboard_label()
	var text := telemetry.text + "\nEsc / Start: controls" if boarding_transition() else "Origin Station · D1\n%s / left stick: walk\nMouse / look keys / right stick: look\nEsc / Start: controls" % preferences_provider.foot_keyboard_label()
	if crew_talk_nearby() and not boarding_transition():
		# The world prompt already names the worker. Keep the running card
		# compact while leaving immediate stopping/refusal feedback readable.
		text = "%s: walk · Mouse: look\nE / left click: Talk\nEsc / Start: controls" % preferences_provider.foot_keyboard_label()
		text += "\n" + ("Stopping to talk…" if not pending_crew_talk.is_empty() else "Release walking and look controls to talk." if not current_controls_neutral() else crew_feedback if not crew_feedback.is_empty() else str(crew_packet.reason))
	if active_status.text != text:
		active_status.text = text
		active_card.reset_size()


func set_paused(value: bool) -> void:
	if value:
		pause_controls("Walking paused. Release controls before Resume.")
	else:
		resume_requested()


func pause_controls(reason: String) -> void:
	pending_crew_talk.clear()
	paused = true
	if frame_diagnostics != null: frame_diagnostics.observe(0.0, {}, [], false)
	if crew_actor != null and crew_actor.bubble != null: crew_actor.bubble.hide()
	refresh_hud()
	mouse_look.set_active(false)
	controls_armed = false
	refresh_profile_actions()
	if board_button != null: board_button.disabled = true
	if boarding_prompt != null: boarding_prompt.hide()
	if pause_button != null:
		pause_button.text = "Resume"
		pause_button.disabled = not error.is_empty()
		if focused and not journey_dialog_open and save_dialog != null and not save_dialog.visible and not settings_open() and not observations_open() and not crew_open():
			(save_button if pause_button.disabled else pause_button).grab_focus()
	if save_status != null:
		save_status.text = "Walking paused: " + error if not error.is_empty() else reason
		if bridge.has_method("get_freedom_profile_state"):
			var profile: Dictionary = bridge.get_freedom_profile_state()
			if profile.get("explicit_path", false): save_status.text += "\n" + str(profile.reason)


func toggle_pause() -> void:
	if crew_open():
		close_crew_conversation()
		return
	if observations_open():
		close_observations()
		return
	if settings_open():
		settings_view.cancel()
		return
	if paused:
		resume_requested()
	else:
		pause_controls("Walking paused. Release controls before Resume.")


func resume_requested() -> void:
	if journey_dialog_open or settings_open() or observations_open() or crew_open(): return
	if not error.is_empty() or not focused or save_dialog == null or save_dialog.visible:
		return
	# A past neutral frame cannot authorize a later held press (even W+S / A+D).
	observe_neutral_controls()
	if not controls_armed:
		save_status.text = "Release walking and look controls before Resume."
		return
	paused = false
	sync_mouse_look()
	refresh_boarding_action()
	refresh_crew()
	pause_button.text = "Pause"
	# Resume may come from Escape while any paused HUD action owns focus.
	# Active arrow look must not also navigate that action's GUI neighbors.
	var current := get_viewport().gui_get_focus_owner()
	if current != null: current.release_focus()
	save_status.text = "Walking. Save As pauses the journey."
	refresh_hud()


func current_controls_neutral() -> bool:
	if preferences_provider != null and (not preferences_provider.foot_keyboard_look_neutral() or not preferences_provider.foot_keyboard_move_neutral()):
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
	if not focused or save_dialog == null or settings_open() or observations_open() or crew_open():
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


func observations_open() -> bool:
	return is_instance_valid(observations_view) and observations_view.visible

func crew_open() -> bool:
	return is_instance_valid(crew_reader) and crew_reader.visible

func crew_talk_nearby() -> bool:
	# This is prompt/click routing only. The fresh C++ query still owns every
	# physical eligibility check, including the qualified clear interaction area.
	if crew_packet.is_empty() or not state.has("foot_position_metres"): return false
	var foot: PackedFloat64Array = state.foot_position_metres
	var worker: PackedFloat64Array = crew_packet.foot_position_metres
	return foot[0] >= -1.0 and Vector3(foot[0] - worker[0], foot[1] - worker[1], foot[2] - worker[2]).length_squared() <= 6.25

func crew_talk_owner() -> Array:
	return [state.get("universe_seed"), state.get("system_id"), state.get("planet_id"), state.get("station_id"), state.get("craft_id"), state.get("actor_id")]

func pending_crew_talk_valid() -> bool:
	return activated and not paused and focused and not journey_dialog_open and not settings_open() and not observations_open() and not crew_open() and not mode_change_pending and error.is_empty() and save_dialog != null and not save_dialog.visible and not boarding_transition() and controls_armed and current_controls_neutral() and pending_crew_talk.get("owner") == crew_talk_owner()

func request_crew_conversation(allow_pending := true) -> void:
	if not activated or paused or not focused or journey_dialog_open or settings_open() or observations_open() or crew_open() or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or boarding_transition(): return
	var boarding: Variant = bridge.get_freedom_boarding_availability()
	if valid_boarding_availability(boarding, state) and boarding.available: return
	if not controls_armed or not current_controls_neutral():
		pending_crew_talk.clear()
		crew_feedback = "Release walking and look controls to talk."
		save_status.text = crew_feedback
		refresh_hud()
		return
	var packet: Variant = bridge.get_freedom_station_crew()
	if not StationCrew.valid_packet(packet, state) or not packet.available:
		crew_feedback = str(packet.get("reason", "Approach and face the station worker to talk.")) if packet is Dictionary else "Station conversation unavailable."
		save_status.text = crew_feedback
		if StationCrew.valid_packet(packet, state):
			crew_packet = packet
			# A released key can precede the next ordinary fixed walking step.
			# Retain one request until that step stops the real actor; never add a
			# tick here, change its pose, or weaken C++ eligibility for a modal.
			if allow_pending and pending_crew_talk.is_empty() and crew_talk_nearby() and packet.reason == "Stop moving to talk":
				pending_crew_talk = {"owner": crew_talk_owner(), "tick": state.tick}
		refresh_hud()
		return
	pending_crew_talk.clear()
	crew_feedback = ""
	pause_controls("Talking with " + str(packet.name) + ". The journey stays paused.")
	crew_reader.open(packet)
	crew_actor.bubble.hide()
	boarding_prompt.hide()
	refresh_hud()

func select_crew_topic(id: int) -> void:
	if not crew_open() or not paused or not focused or journey_dialog_open or save_dialog.visible or not error.is_empty(): return
	# Eligibility and topic text come from the actual current owner on every
	# selection. A stale UI topic cannot fabricate a new interaction or progress.
	var packet: Variant = bridge.get_freedom_station_crew()
	if not StationCrew.valid_packet(packet, state) or not packet.available: return
	for topic in packet.topics:
		if topic.id == id:
			crew_reader.text.text = topic.text
			return

func close_crew_conversation() -> void:
	if not crew_open() or not focused: return
	crew_reader.hide()
	controls_armed = false
	refresh_hud()
	pause_button.grab_focus()
	save_status.text = "Conversation closed. Release controls, then Resume explicitly."

func refresh_crew() -> void:
	if crew_actor == null or not activated: return
	if boarding_transition() or mode_change_pending:
		crew_actor.hide()
		crew_actor.bubble.hide()
		return
	if crew_query_elapsed >= 0.1:
		crew_query_elapsed = 0.0
		var packet: Variant = bridge.get_freedom_station_crew()
		if packet is Dictionary and packet.is_empty():
			crew_packet.clear()
		elif StationCrew.valid_packet(packet, state):
			crew_packet = packet
			crew_feedback = ""
		else:
			error = "The station crew observation is unavailable."
			pause_controls(error)
			return
	if crew_packet.is_empty():
		pending_crew_talk.clear()
		crew_actor.hide()
		crew_actor.bubble.hide()
		return
	if not crew_actor.present(crew_packet, station.to_local(camera.global_position)):
		error = "The station crew presentation is unavailable."
		pause_controls(error)
		return
	refresh_crew_remark()
	if not boarding_availability.get("available", false) and crew_packet.available and focused and not paused and not journey_dialog_open and not save_dialog.visible:
		boarding_prompt.text = "E / left click" + (" / " + ("Cross" if preferences_provider.playstation() else "A") if available_pads.get(selected_pad, false) else "") + " · Talk to " + str(crew_packet.name)
		boarding_prompt.show()

func refresh_crew_remark() -> void:
	if crew_actor == null or crew_actor.bubble == null: return
	if paused or not focused or journey_dialog_open or crew_open() or boarding_transition() or mode_change_pending or save_dialog == null or save_dialog.visible or not error.is_empty() or crew_packet.is_empty():
		crew_actor.bubble.hide()
		return
	var p: PackedFloat64Array = state.foot_position_metres
	var worker: PackedFloat64Array = crew_packet.foot_position_metres
	var nearby := p[0] >= -1.0 and Vector3(p[0] - worker[0], p[1] - worker[1], p[2] - worker[2]).length_squared() <= 6.25
	var head := crew_actor.global_transform * Vector3(0, 1.7, 0)
	nearby = nearby and camera.is_position_in_frustum(head) and not camera.is_position_behind(head)
	var tick := str(state.tick).to_int()
	if nearby and not crew_nearby and tick >= crew_remark_tick and tick - crew_remark_tick >= 3600:
		crew_remark_tick = tick
		crew_remark_until = tick + 480
		crew_actor.bubble.text = crew_packet.remark
	crew_nearby = nearby
	crew_actor.bubble.visible = nearby and tick >= crew_remark_tick and tick < crew_remark_until


func open_observations() -> void:
	if not focused or journey_dialog_open or save_dialog == null or save_dialog.visible or not error.is_empty() or settings_open() or observations_open() or crew_open(): return
	pause_controls("Reading retained observations does not operate sensors.")
	preferences_provider.focused = focused
	preferences_provider.device = selected_pad
	settings_return_frames = 0
	observations_return_frames = 0
	hud_scroll.hide()
	observations_backing.show()
	observations_view.open()


func close_observations() -> void:
	if not observations_open(): return
	observations_view.hide()
	observations_backing.hide()
	controls_armed = false
	hud_scroll.show()
	if focused: observations_button.grab_focus()
	observations_return_frames = 2


func open_settings() -> void:
	if not focused or journey_dialog_open or save_dialog == null or save_dialog.visible or not error.is_empty() or settings_open() or observations_open() or crew_open(): return
	pause_controls("Control choices stay pending until Apply.")
	preferences_provider.focused = focused
	preferences_provider.device = selected_pad
	# The staged provider initially installs menus for its launch-time device.
	# Rebind accepted mappings before this modal delegates navigation to Godot.
	preferences_provider.install()
	hud_scroll.hide()
	settings_view.open()


func close_settings() -> void:
	stick_preferences = {"deadzone": preferences_provider.settings.deadzone, "curve": preferences_provider.settings.curve}
	controls_armed = false
	hud_scroll.show()
	refresh_hud()
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
	if paused or not focused or not error.is_empty() or save_dialog.visible:
		return PackedFloat64Array([0.0, 0.0, requested_heading])
	var keyboard_move: Vector2 = preferences_provider.foot_keyboard_move()
	var forward := keyboard_move.y
	var right := keyboard_move.x
	var keyboard_look: Vector2 = preferences_provider.foot_keyboard_look()
	apply_look_rate(keyboard_look, delta)
	if available_pads.get(selected_pad, false):
		forward -= stick(Input.get_joy_axis(selected_pad, JOY_AXIS_LEFT_Y))
		right += stick(Input.get_joy_axis(selected_pad, JOY_AXIS_LEFT_X))
		var controller_look: Vector2 = preferences_provider.foot_controller_look(selected_pad)
		apply_look_rate(controller_look, delta)
	if boarding_transition():
		# Looking around never steers the authored C++ actor route.
		return PackedFloat64Array([0.0, 0.0, requested_heading])
	requested_heading = wrapf(requested_heading, -PI, PI)
	return PackedFloat64Array([clampf(forward, -1.0, 1.0), clampf(right, -1.0, 1.0), requested_heading])


func apply_look(motion: Vector2) -> void:
	if not motion.is_finite(): return
	if boarding_transition():
		boarding_look.x = clampf(boarding_look.x + motion.x, -PI, PI)
		boarding_look.y = clampf(boarding_look.y + motion.y, -1.3, 1.3)
	else:
		requested_heading = wrapf(requested_heading - motion.x, -PI, PI)
		pitch = clampf(pitch - motion.y, -1.3, 1.3)


func apply_look_rate(rate: Vector2, delta: float) -> void:
	if boarding_transition():
		apply_look(rate * delta)
	else:
		# Keep the existing scalar arithmetic and single final heading wrap.
		# Multiplying a Vector2 first would round its components to float32.
		requested_heading -= rate.x * delta
		pitch = clampf(pitch - rate.y * delta, -1.3, 1.3)


static func boarding_look_weight(progress: float) -> float:
	if not is_finite(progress) or progress < 0.0 or progress > 1.0: return 0.0
	# The route lasts twelve seconds. Settle the camera during its final second,
	# using authoritative progress rather than a render-rate-dependent timer.
	return 1.0 - smoothstep(11.0 / 12.0, 1.0, progress)


func advance_requested(delta: float, commands: PackedFloat64Array) -> bool:
	if paused or not focused or not error.is_empty() or (save_dialog != null and save_dialog.visible):
		return true
	if not bridge.advance_freedom_walk(delta, commands):
		error = str(bridge.get_last_error())
		set_paused(true)
		return false
	update_view(delta)
	if not pending_crew_talk.is_empty():
		if not pending_crew_talk_valid():
			pending_crew_talk.clear()
		elif state.tick != pending_crew_talk.tick:
			pending_crew_talk.clear()
			request_crew_conversation(false)
	return true


func boarding_transition() -> bool:
	return state.get("boarding", {}).get("state", "") in ["boarding", "disembarking"]


func board_requested() -> void:
	pending_crew_talk.clear()
	if paused or not focused or not error.is_empty() or save_dialog.visible or boarding_transition(): return
	if not current_controls_neutral():
		save_status.text = "Release movement and look controls to board."
		return
	if not bridge.begin_freedom_boarding():
		save_status.text = str(bridge.get_last_error())
		boarding_query_elapsed = 0.1
		return
	pitch = 0.0
	boarding_look = Vector2.ZERO
	update_view()
	sync_mouse_look()


func sync_mouse_look() -> void:
	mouse_look.set_active(activated and focused and not paused and error.is_empty() and not journey_dialog_open and save_dialog != null and not save_dialog.visible and not settings_open() and not observations_open() and not crew_open() and not mode_change_pending)


func _exit_tree() -> void:
	mouse_look.set_active(false)


func refresh_boarding_action() -> void:
	if board_button == null or boarding_prompt == null: return
	if not boarding_transition() and bridge.has_method("get_freedom_boarding_availability") and boarding_query_elapsed >= 0.1:
		var queried: Variant = bridge.get_freedom_boarding_availability()
		boarding_availability = queried if valid_boarding_availability(queried, state) else {}
		boarding_query_elapsed = 0.0
	var available: bool = boarding_availability.get("available", false) and not boarding_transition()
	board_button.visible = available
	board_button.disabled = not available or paused or not focused or not current_controls_neutral()
	var binding := "E / left click"
	if available_pads.get(selected_pad, false):
		preferences_provider.device = selected_pad
		binding += " / " + ("Cross" if preferences_provider.playstation() else "A")
	board_button.text = "Climb aboard Wayfarer · " + binding
	board_button.tooltip_text = str(boarding_availability.get("reason", "Approach the D1 ladder."))
	boarding_prompt.visible = available and focused and not paused and not journey_dialog_open and not boarding_transition()
	boarding_prompt.text = binding + " · Climb aboard Wayfarer" if current_controls_neutral() else "Release movement controls · " + binding + " to climb aboard"


func refresh_service_craft(flight: Dictionary, delta: float) -> bool:
	var service: Dictionary = flight.get("origin_service", {})
	if service.is_empty():
		if service_craft != null: service_craft.hide()
		return true
	var packet: Dictionary = service.get("craft", {})
	if not ServiceCraft.valid_packet(packet, delta): return false
	if service_craft == null:
		service_craft = ServiceCraft.new()
		if not service_craft.initialize(assets_root, service_binding, packet.craft_id):
			service_craft.free()
			service_craft = null
			return false
		scene.add_child(service_craft)
	return service_craft.present(packet, delta, paused, camera.far)


func update_view(delta: float = 0.0) -> void:
	var next_state: Dictionary = bridge.get_freedom_walk_state()
	if next_state.is_empty() and bridge.get_freedom_boarding_state().get("state") == "seated":
		# Consume the completed C++ pose before deferred view replacement. A
		# render frame can contain several ticks; the previous walker camera is
		# then several ticks behind the moving station and floating origin.
		var seated: Dictionary = bridge.get_freedom_boarding_state()
		var flight: Dictionary = bridge.get_freedom_flight_state()
		if not valid_environment(flight, {}) or flight.get("boarding") != seated or not finite_triplet(seated.eye_craft) or not staged_model.set_pose(seated.pose):
			error = "C++ returned an invalid completed boarding pose"
			set_paused(true)
			return
		if not refresh_environment(flight, {}):
			error = "The completed boarding sky projection is unavailable"
			set_paused(true)
			return
		state = next_state
		boarding_look = Vector2.ZERO
		station.transform = Transform3D(flight.station_basis, flight.station_position)
		ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
		camera.transform = Transform3D(flight.body_basis, flight.body_basis * Vector3(seated.eye_craft[0], seated.eye_craft[1], seated.eye_craft[2]))
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	if not valid_state(next_state):
		error = "C++ returned an invalid station actor pose"
		set_paused(true)
		return
	var flight: Dictionary = bridge.get_freedom_flight_state()
	if not valid_environment(flight, next_state):
		error = "C++ returned an invalid station flight projection"
		set_paused(true)
		return
	if not refresh_service_craft(flight, delta):
		error = "The service Wayfarer presentation is unavailable"
		set_paused(true)
		return
	var boarding: Dictionary = next_state.get("boarding", {})
	if not boarding.is_empty() and not staged_model.set_pose(boarding.pose):
		error = "The Wayfarer boarding pose changed unexpectedly"
		set_paused(true)
		return
	if not refresh_environment(flight, next_state):
		error = "The station sky projection is unavailable"
		set_paused(true)
		return
	if not cockpit_displays.refresh(flight, delta, paused):
		error = "The Wayfarer instrument readings are unavailable"
		set_paused(true)
		return
	state = next_state
	if boarding_transition():
		requested_heading = state.heading_radians
		pitch = 0.0
	else:
		boarding_look = Vector2.ZERO
	station.transform = Transform3D(state.station_basis, state.station_position)
	ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	var look := boarding_look * boarding_look_weight(float(boarding.get("progress", 0.0))) if boarding_transition() else Vector2.ZERO
	camera.transform = Transform3D(state.station_basis * Basis(Vector3.UP, state.heading_radians - look.x) * Basis(Vector3.RIGHT, pitch - look.y), state.actor_eye_position)
	refresh_boarding_action()
	refresh_crew()
	if boarding_transition():
		var cues := {"approach": "Approaching the ladder", "ladder": "Climbing through the hatch", "cabin": "Moving into the cabin", "seat": "Taking the pilot seat", "hardware": "Securing the hatch", "complete": "Ready"}
		telemetry.text = "%s · %d%%\n%s\nLook around · view settles near the end" % ["Boarding Wayfarer" if boarding.state == "boarding" else "Returning to the station", roundi(boarding.progress * 100.0), cues.get(boarding.phase, "Moving")]
	else:
		telemetry.text = "Origin Station · D1 workshop\nWalk to the Wayfarer ladder to board.%s" % ("\n" + error if not error.is_empty() else "")
	refresh_hud()


func _process(delta: float) -> void:
	if frame_diagnostics != null and frame_diagnostics.enabled:
		frame_diagnostics.observe(delta, state, [viewport.size] if viewport != null else [], activated and focused and not paused and not journey_dialog_open and not settings_open() and not observations_open() and not crew_open() and not mode_change_pending and error.is_empty() and save_dialog != null and not save_dialog.visible)
	if bridge == null or not activated:
		return
	if not pending_crew_talk.is_empty() and not pending_crew_talk_valid(): pending_crew_talk.clear()
	sync_mouse_look()
	if settings_open():
		stick_preferences = {"deadzone": preferences_provider.settings.deadzone, "curve": preferences_provider.settings.curve}
	if settings_return_frames > 0:
		settings_return_frames -= 1
		if settings_return_frames == 0 and not settings_open(): hud_scroll.ensure_control_visible(settings_button)
	if observations_return_frames > 0:
		observations_return_frames -= 1
		if observations_return_frames == 0 and not observations_open() and not settings_open(): hud_scroll.ensure_control_visible(observations_button)
	if paused:
		refresh_crew_remark()
		if not settings_open(): observe_neutral_controls()
		return
	if not focused or not error.is_empty() or save_dialog.visible:
		return
	if is_finite(delta) and delta > 0.0: boarding_query_elapsed = minf(0.1, boarding_query_elapsed + delta)
	if is_finite(delta) and delta > 0.0: crew_query_elapsed = minf(0.1, crew_query_elapsed + delta)
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


func try_select_controller(event: InputEvent) -> bool:
	# Ownership can change in a paused journey modal without enabling movement.
	if focused and save_dialog != null and not save_dialog.visible and event is InputEventJoypadButton and event.pressed and event.button_index == JOY_BUTTON_START and available_pads.get(event.device, false) and not available_pads.get(selected_pad, false):
		selected_pad = event.device
		if preferences_provider != null: preferences_provider.device = selected_pad
		if settings_open(): preferences_provider.install()
		pause_controls("Controller selected. Release controls, then Resume explicitly.")
		return true
	return false

func _input(event: InputEvent) -> void:
	# Cancel at the press too, even if a renewed movement key is released again
	# before the next process frame. Focus/modal changes are checked separately.
	if not pending_crew_talk.is_empty() and not pending_crew_talk_valid(): pending_crew_talk.clear()
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
		try_select_controller(event)
		get_viewport().set_input_as_handled()
		return
	if not focused or save_dialog == null or save_dialog.visible:
		if observations_open(): get_viewport().set_input_as_handled()
		return
	if settings_open() and settings_view.handle_keyboard_capture(event): return
	var pressed := event.is_pressed() and not event.is_echo()
	var key: int = event.physical_keycode if event is InputEventKey else KEY_NONE
	var button: int = event.button_index if event is InputEventJoypadButton else -1
	if crew_open():
		if pressed and (key == KEY_ESCAPE or button in [JOY_BUTTON_START, JOY_BUTTON_B]):
			close_crew_conversation()
			get_viewport().set_input_as_handled()
		elif pressed and (key in [KEY_UP, KEY_DOWN, KEY_TAB] or button in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]):
			crew_reader.navigate(-1 if key == KEY_UP or button == JOY_BUTTON_DPAD_UP else 1)
			get_viewport().set_input_as_handled()
		elif pressed and (key in [KEY_ENTER, KEY_KP_ENTER, KEY_SPACE] or button == JOY_BUTTON_A):
			crew_reader.accept()
			get_viewport().set_input_as_handled()
		return
	if pressed and (key == KEY_ESCAPE or button == JOY_BUTTON_START or (paused and button == JOY_BUTTON_B)):
		toggle_pause()
		get_viewport().set_input_as_handled()
	elif observations_open():
		# Station device selection does not reinstall the flight UI InputMap.
		# Resolve this small reader's pad actions after the ownership guards above.
		var buttons := [observations_view.previous_button, observations_view.next_button, observations_view.back_button]
		if pressed and button == JOY_BUTTON_A:
			var current := get_viewport().gui_get_focus_owner()
			if current in buttons and not current.disabled: current.pressed.emit()
			get_viewport().set_input_as_handled()
		elif pressed and button in [JOY_BUTTON_DPAD_LEFT, JOY_BUTTON_DPAD_RIGHT]:
			var direction := -1 if button == JOY_BUTTON_DPAD_LEFT else 1
			var index := buttons.find(get_viewport().gui_get_focus_owner())
			for offset in range(1, buttons.size() + 1):
				var next: Button = buttons[posmod(index + direction * offset, buttons.size())]
				if not next.disabled:
					next.grab_focus()
					break
			get_viewport().set_input_as_handled()
		elif pressed and button in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]:
			var direction := -1 if button == JOY_BUTTON_DPAD_UP else 1
			observations_view.scroll.scroll_vertical += direction * roundi(64 * observations_view._readable_scale)
			get_viewport().set_input_as_handled()
		else:
			observations_view._input(event)
	elif settings_open():
		# Let the shared screen handle ordinary selected-controller UI actions.
		return
	elif paused and pressed and (key in [KEY_UP, KEY_DOWN, KEY_TAB] or button in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN]):
		var current := get_viewport().gui_get_focus_owner()
		var buttons := [pause_button, replace_save_button, save_button, load_button, settings_button, title_button, export_save_button, observations_button, frame_diagnostics_button]
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
		elif current == observations_button:
			open_observations()
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
	elif pressed and not paused and (key == KEY_E or button == JOY_BUTTON_A or (event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT and mouse_look.active and (boarding_availability.get("available", false) or crew_talk_nearby()))):
		# Keyboard/controller boarding previously queried the real command even
		# before the 0.1s prompt refresh. Keep that priority and responsiveness.
		var boarding: Variant = bridge.get_freedom_boarding_availability()
		if valid_boarding_availability(boarding, state) and boarding.available: board_requested()
		else: request_crew_conversation()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseMotion and not paused and error.is_empty():
		var motion := MouseLook.motion_delta(event.relative, preferences_provider.settings)
		apply_look(motion)

func refresh_profile_actions() -> void:
	if replace_save_button == null or bridge == null or not bridge.has_method("get_freedom_profile_state"): return
	var profile: Dictionary = bridge.get_freedom_profile_state()
	replace_save_button.disabled = not profile.get("can_save", false)
	save_button.disabled = not profile.get("can_save_as", false)
	replace_save_button.tooltip_text = str(profile.get("reason", ""))
	save_button.tooltip_text = replace_save_button.tooltip_text

func request_catalog_save(save_as: bool) -> void:
	if not focused or journey_dialog_open or settings_open() or observations_open() or crew_open() or save_dialog.visible: return
	pause_controls("Save uses the last committed journey state.")
	catalog_save_requested.emit(save_as)
