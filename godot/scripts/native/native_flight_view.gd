extends Control
signal journey_mode_changed
signal catalog_save_requested(save_as: bool)
signal load_requested
signal title_requested
signal recovery_requested
var journey_dialog_open := false
## Ordinary saved-flight consumer. C++ owns time, state, terrain and propulsion.
const MoonState = preload("res://scripts/native/native_moon_state.gd")
const PlanetStreamView = preload("res://scripts/world/planet_stream.gd")
const MainExhaust = preload("res://scripts/native/native_main_exhaust.gd")
const HopperPresentation = preload("res://scripts/characters/hopper_presentation.gd")
const StationPresentation = preload("res://scripts/native/native_station_view.gd")
const WAYFARER_HASH = "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8"
const WAYFARER_DESCRIPTOR_HASH = "17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7"
const HomeNavigation = preload("res://scripts/native/home_navigation.gd")
const PlayerInput = preload("res://scripts/ui/player_input.gd")
const MouseLook = preload("res://scripts/native/native_mouse_look.gd")
var mouse_look := MouseLook.new()
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const CockpitDisplays = preload("res://scripts/native/native_cockpit_displays.gd")
var cockpit_displays: Node3D
const DistantContacts = preload("res://scripts/native/native_distant_contacts.gd")
const PlanetOptics = preload("res://scripts/native/native_planet_optics.gd")
var planet_optics := PlanetOptics.new()
const SurfaceAppearance = preload("res://scripts/native/native_surface_appearance.gd")
var surface_appearance := SurfaceAppearance.new()
const SurfaceLandmarks = preload("res://scripts/native/native_surface_landmarks.gd")
var surface_landmarks := SurfaceLandmarks.new()
const PauseMenu = preload("res://scripts/ui/pause_menu.gd")
const Status = preload("res://scripts/native/native_status.gd")
var observation_cue := Status.ObservationCue.new()
var surface_aid_cue := Status.SurfaceAidCue.new()
const ImpactView = preload("res://scripts/native/native_impact_view.gd")
var impact_overlay: Control
var impact_status: RichTextLabel
var impact_menu_button: Button
var surface_conditions_button: Button
var primary_status: RichTextLabel
var context_action: Button
var instruments_button: Button
var instruments_open := false
var context_source: Button
var local_moon_button: Button
var bridge: Variant
var camera: Camera3D
var terrain_camera: Camera3D
var near_viewport: SubViewport
var far_viewport: SubViewport
var ship: Node3D
var terrain: Node3D
var exhaust: Node3D
var light: DirectionalLight3D
var local_light: DirectionalLight3D
var suit_lamp: SpotLight3D
var suit_lamp_enabled := true
var suit_lamp_button: Button
var planet_environment: Environment
var near_environment: Environment
var sky_material: ShaderMaterial
var hud_scroll: ScrollContainer
var hud_column: VBoxContainer
var hud_theme: Theme
var hud_scroll_style: StyleBox
var hud_stacked := false
var orbital_forecast: RichTextLabel
var telemetry: RichTextLabel
var home_cue: RichTextLabel
var home_marker: Control
var ship_marker: Control
var contact_marker: Control
var contact_buttons: Array[Button] = []
var contact_clear_button: Button
var selected_contact_id := ""
var port_row: HBoxContainer
var pause_button: Button
var assist_button: CheckButton
var save_dialog: FileDialog
var save_status: RichTextLabel
var paused := true
var focused := true
var controls_armed := false
var last_window_size := Vector2i.ZERO
var player_input: Node
var initial_controller_device := -2
var controls_menu: CanvasLayer
var persist_controls := true
var control_settings_path := PlayerInput.SETTINGS_PATH
var look_offset := Vector2.ZERO
var hint: RichTextLabel
var cockpit := false
var pilot_eye := Vector3.ZERO
var state: Dictionary = {}
var station: Node3D
var scene: Node3D
var assets_root := ""
var station_geometry: Dictionary = {}
var dock_status: RichTextLabel
var port_buttons: Array[Button] = []
var approach_button: Button
var capture_button: Button
var unboard_menu_button: Button
var unboard_button: Button
var landing_button: Button
var stow_gear_button: Button
var liftoff_button: Button
var surface_status: RichTextLabel
var resource_status: RichTextLabel
var chart_status: RichTextLabel
var jump_status: RichTextLabel
var jump_button: Button
var jump_destinations: Array[Button] = []
var rendered_world := ""
var chart_button: CheckButton
var service_button: Button
var release_button: Button
var error := ""
var activated := false
var staged_model: Node3D
var mode_change_pending := false
var recovery_notified := false
var ship_audio: Node
var audio_preferences: RefCounted
var quit_handler: Callable
var surface_walk_button: Button
var surface_walk_menu_button: Button
var surface_marker_button: Button
var clear_surface_marker_button: Button
var show_surface_marker_button: Button
var show_personal_marker := false
var landmark_enable_button: Button
var landmark_inspect_button: Button


func surface_walking() -> bool:
	return not state.get("surface_walk", {}).is_empty()


static func actuator_fractions(axes: PackedFloat64Array) -> PackedFloat64Array:
	# Layout-4 input is semantic actuator demand, not the lab's rate controller.
	if axes.size() != 7:
		return PackedFloat64Array()
	for i in 7:
		if not is_finite(axes[i]) or absf(axes[i]) > 1.0 or (i < 2 and axes[i] < 0.0):
			return PackedFloat64Array()
	return PackedFloat64Array([maxf(axes[5], 0), maxf(axes[6], 0), axes[1], maxf(-axes[5], 0), maxf(-axes[6], 0), axes[0], maxf(axes[2], 0), maxf(-axes[3], 0), maxf(-axes[4], 0), maxf(-axes[2], 0), maxf(axes[3], 0), maxf(axes[4], 0)])


static func valid_star_direction(value: Variant) -> bool:
	return value is Vector3 and value.is_finite() and absf(value.length_squared() - 1.0) <= 0.00001


static func star_light_basis(direction: Vector3) -> Basis:
	# Both station walking and flight use this C++ direction in their shared
	# tangent presentation frame. The alternate up axis handles either pole.
	return Basis.looking_at(-direction, Vector3.UP if absf(direction.y) < 0.99 else Vector3.RIGHT)


# The admitted Wayfarer hull (-4.040,-1.259,-5.210)..(4.040,2.955,7.620)m
# and deployed supports fit inside this conservative source-bound padding.
const SURFACE_CRAFT_PADDING := 16.0

static func surface_depth_range(eye: Vector3, distant_far: float) -> Vector2:
	if not eye.is_finite() or not is_finite(distant_far) or distant_far < 200.0 or distant_far > 1000000000.0:
		return Vector2.ZERO
	var distance := eye.length()
	if not is_finite(distance): return Vector2.ZERO
	var far_clip := minf(distant_far, maxf(200.0, distance + SURFACE_CRAFT_PADDING))
	return Vector2(maxf(0.05, far_clip / 500000.0), far_clip)


static func valid_state(value: Dictionary) -> bool:
	if value.get("body_kind") == "moon": return MoonState.valid(value)
	if not valid_star_direction(value.get("star_direction")) or not NativeEnvironment.valid(value) or not PlanetOptics.valid(value) or not SurfaceAppearance.valid(value) or not SurfaceLandmarks.valid(value) or not valid_surface_marker(value): return false
	if value.get("mode") != "freedom_flight" or not value.get("body_basis") is Basis or not value.get("station_basis") is Basis or not value.get("station_position") is Vector3:
		return false
	if not value.body_basis.is_finite() or not value.station_basis.is_finite() or not value.station_position.is_finite():
		return false
	if not value.get("attached") is bool or not value.get("target_port") is int or value.target_port < 0 or value.target_port > 2 or not value.get("docking") is Dictionary:
		return false
	if not value.docking.is_empty():
		var separation: Variant = value.docking.get("separation")
		if not separation is float or not is_finite(separation) or separation < 0.0:
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
	var walking: Variant = value.get("surface_walk", {})
	if not walking is Dictionary: return false
	if not walking.is_empty():
		if walking.get("version") != 1 or not walking.get("eye_position") is Vector3 or not walking.eye_position.is_finite(): return false
		var access: Variant = walking.get("return_access")
		if not access is Dictionary or not access.get("available") is bool: return false
		if access.available:
			if not access.get("position") is Vector3 or not access.position.is_finite(): return false
			if not access.get("distance_metres") is float or not is_finite(access.distance_metres) or access.distance_metres < 0: return false
			if not access.get("within_reach") is bool: return false
		if not walking.get("basis") is Basis or not walking.basis.is_finite() or absf(walking.basis.determinant() - 1.0) > 0.00001: return false
		if not walking.get("heading") is float or not is_finite(walking.heading) or absf(walking.heading) > PI: return false
	var impact: Variant = value.get("impact", {"selected": false})
	if not impact is Dictionary or typeof(value.get("frame_version", 0)) != TYPE_INT:
		return false
	if not ImpactView.valid(impact, str(value.get("craft_id", "")), str(value.get("frame_id", "")), int(value.get("frame_version", 0))):
		return false
	var opt_in: Variant = value.get("impact_opt_in", {"available": false, "reason": "Unavailable"})
	if not opt_in is Dictionary or not opt_in.get("available") is bool or not opt_in.get("reason") is String:
		return false
	return value.planet_radius > 0.0 and valid_orbit(value) and valid_hold(value)


static func valid_surface_marker(value: Dictionary) -> bool:
	if not value.has("surface_marker"): return true
	var marker: Variant = value.surface_marker
	if not marker is Dictionary or typeof(marker.get("version")) != TYPE_INT or marker.version != 1: return false
	for field in ["selected", "present", "current_body", "can_mark", "can_clear"]:
		if typeof(marker.get(field)) != TYPE_BOOL: return false
	for field in ["system_name", "planet_name"]:
		if not marker.get(field) is String or marker[field].length() > 256: return false
	if (marker.current_body and not marker.present) or (marker.present and not marker.selected) or (marker.can_clear and not marker.present): return false
	if not marker.present:
		return not marker.has("position") and not marker.has("distance_metres")
	for field in ["system_id", "planet_id", "tick"]:
		if not NativeEnvironment.decimal(marker.get(field)): return false
	if not NativeEnvironment.decimal(value.get("tick")) or marker.tick.length() > value.tick.length() or (marker.tick.length() == value.tick.length() and marker.tick > value.tick): return false
	if not marker.current_body: return not marker.has("position") and not marker.has("distance_metres")
	return marker.system_id == value.get("system_id") and marker.planet_id == value.get("planet_id") and marker.get("position") is Vector3 and marker.position.is_finite() and marker.get("distance_metres") is float and is_finite(marker.distance_metres) and marker.distance_metres >= 0.0


static func valid_hold(value: Dictionary) -> bool:
	if not value.get("hold_enabled") is bool or not value.get("orbit_hold") is Dictionary:
		return false
	var hold: Dictionary = value.orbit_hold
	if not hold.get("available") is bool or not hold.get("refusal") is String:
		return false
	if not value.hold_enabled:
		return not hold.has("radius_metres") and not hold.has("plane_normal") and not hold.has("planet_id")
	if hold.get("planet_id") != value.get("planet_id") or not hold.get("radius_metres") is float or not is_finite(hold.radius_metres) or hold.radius_metres <= value.planet_radius + value.atmosphere_edge or hold.radius_metres > 1.0e15:
		return false
	var plane: Variant = hold.get("plane_normal")
	if not plane is PackedFloat64Array or plane.size() != 3:
		return false
	var norm := 0.0
	for component in plane:
		if not is_finite(component): return false
		norm += component * component
	return absf(norm - 1.0) <= 1.0e-12


static func valid_orbit(value: Dictionary) -> bool:
	if not value.get("orbit_classification") in ["stable", "decaying", "impact", "escape"] or not value.get("orbit_bound") is bool or not value.get("orbit_near_parabolic") is bool:
		return false
	if value.orbit_near_parabolic and value.orbit_bound:
		return false
	if value.orbit_classification in ["stable", "decaying"] and not value.orbit_bound:
		return false
	if value.orbit_classification == "escape" and value.orbit_bound:
		return false
	var peri: Variant = value.get("periapsis_radius")
	if not peri is float or not is_finite(peri) or peri < 0.0 or peri > 2.0e15 or not value.has("apoapsis_radius"):
		return false
	var apo: Variant = value.apoapsis_radius
	if apo != null and (not apo is float or not is_finite(apo) or apo < peri or apo > 2.0e15 or not value.orbit_bound):
		return false
	return true


static func orbit_text(value: Dictionary) -> String:
	var descriptions := {"stable": "Stable orbit", "decaying": "Enters atmosphere", "impact": "Intersects reference surface", "escape": "Escape"}
	var apo := "unavailable" if value.apoapsis_radius == null else "%.1f km" % ((value.apoapsis_radius - value.planet_radius) / 1000.0)
	var qualifier := " · near-parabolic" if value.orbit_near_parabolic else ""
	return "Orbital forecast: %s%s\nPeri %.1f km · Apo %s\nCentral-body prediction · thrust and drag change it\nNot terrain or landing clearance" % [descriptions[value.orbit_classification], qualifier, (value.periapsis_radius - value.planet_radius) / 1000.0, apo]


func initialize(owner: Variant, assets: String) -> bool:
	var flight: Dictionary = owner.get_freedom_flight_state()
	var model: Node3D
	if flight.get("frame_id") == "2":
		model = HopperPresentation.load_selected_asset(assets, owner.get_freedom_craft_binding())
		if model == null:
			error = "The selected Wayfarer could not be staged"
			return false
	var pending := {"flight_state": flight, "station_geometry": owner.get_freedom_station_geometry()}
	if not stage(owner, assets, pending, model):
		if model != null and model.get_parent() == null: model.free()
		return false
	if not owner.enable_streaming():
		error = str(owner.get_last_error())
		return false
	activate()
	return true

func stage(owner: Variant, assets: String, pending: Dictionary, model: Node3D, handoff := false) -> bool:
	if bridge != null or (model != null and (model.get_parent() != null or not (model.valid_current_pose() if handoff else model.valid_installed()))):
		error = "Fresh view and complete detached model required"
		return false
	set_process(false)
	set_process_input(false)
	bridge = owner
	state = pending.flight_state.duplicate(true)
	if not valid_state(state):
		error = "Saved physical flight view is unavailable: " + str(bridge.get_last_error())
		return false
	if not assets.is_absolute_path():
		error = "Prepare the native starter assets before viewing flight"
		return false
	assets_root = assets
	station_geometry = pending.station_geometry.duplicate(true)
	if state.get("station_available", true) and (not StationPresentation.valid_geometry(station_geometry, state.station_id) or FileAccess.get_sha256(assets.path_join("station-reference.glb")) != StationPresentation.STATION_HASH):
		error = "The selected Origin Station geometry/export is missing or changed"
		return false
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var container := SubViewportContainer.new()
	container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	add_child(container)
	var viewport := SubViewport.new()
	far_viewport = viewport
	viewport.size = Vector2i(1280, 720)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(viewport)
	scene = Node3D.new()
	viewport.add_child(scene)
	ship = Node3D.new()
	scene.add_child(ship)
	if state.frame_id == "2":
		if model == null:
			error = "Selected Wayfarer model is required"
			return false
		staged_model = model
		var boarding: Dictionary = state.get("boarding", {})
		if not boarding.is_empty():
			if not model.set_pose(boarding.pose):
				error = "The Wayfarer boarding pose is unavailable"
				return false
			cockpit = boarding.state == "seated"
		ship.add_child(model)
		# This camera reference is presentation only. Boarding/seat ownership is
		# a separate journey transition, not authorized by toggling this camera.
		pilot_eye = HopperPresentation.vector(model.specification.pilot_eye)
		exhaust = MainExhaust.new()
		if not exhaust.bind_skin(model, model.exterior_surface):
			exhaust.free()
			exhaust = null
			error = "The selected Wayfarer exterior exhaust interface is unavailable"
			return false
		ship.add_child(exhaust)
		cockpit_displays = CockpitDisplays.new()
		ship.add_child(cockpit_displays)
		if not cockpit_displays.bind_screens(model.specification.screens) or not cockpit_displays.refresh(state, 0.0, true):
			error = "The Wayfarer instrument screens are unavailable"
			return false
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
	var close_material := ShaderMaterial.new()
	close_material.shader = preload("res://shaders/native_close_composite.gdshader")
	near_container.material = close_material
	add_child(near_container)
	near_viewport = SubViewport.new()
	near_viewport.size = viewport.size

	near_viewport.transparent_bg = true
	near_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	near_container.add_child(near_viewport)
	near_viewport.add_child(camera)
	camera.cull_mask = 3
	# One local presentation light follows the actual eye in the shared world.
	# It illuminates both depth passes without changing either sunlight owner.
	suit_lamp = SpotLight3D.new()
	suit_lamp.name = "SuitNavigationLamp"
	suit_lamp.position = Vector3(0, -0.08, -0.1)
	suit_lamp.rotation.x = deg_to_rad(-10.0)
	suit_lamp.light_color = Color.WHITE
	suit_lamp.light_energy = 3.0
	suit_lamp.light_cull_mask = 3
	suit_lamp.spot_range = 22.0
	suit_lamp.spot_angle = 40.0
	suit_lamp.shadow_enabled = true
	suit_lamp.visible = false
	camera.add_child(suit_lamp)
	terrain_camera = Camera3D.new()
	terrain_camera.cull_mask = 1
	terrain_camera.fov = camera.fov
	scene.add_child(terrain_camera)
	light = DirectionalLight3D.new()
	# Terrain retains global sunlight; the local craft/station can be inside
	# the planet's shadow while a distant sunlit hemisphere remains visible.
	light.light_cull_mask = 1
	light.basis = star_light_basis(state.star_direction)
	scene.add_child(light)
	local_light = DirectionalLight3D.new()
	local_light.light_cull_mask = 2
	local_light.basis = light.basis
	scene.add_child(local_light)
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	planet_environment = environment
	sky_material = NativeEnvironment.install(environment)
	if not NativeEnvironment.bind_background(state, sky_material, owner.get_freedom_background_sky(pending.get("candidate_id", ""))):
		error = "The saved universe background is unavailable"
		return false
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.35, 0.45, 0.7)
	environment.ambient_light_energy = 0.6
	world.environment = environment
	scene.add_child(world)
	var close_environment: Environment = environment.duplicate()
	near_environment = close_environment
	close_environment.background_mode = Environment.BG_CLEAR_COLOR
	camera.environment = close_environment
	set_ship_layer(ship)
	if state.get("station_available", true) and state.station_position.length() < 1000.0:
		station = StationPresentation.new()
		scene.add_child(station)
		if not station.initialize({"station_id": state.station_id}, station_geometry, assets_root, false):
			error = station.error
			return false
		set_ship_layer(station)
	terrain = PlanetStreamView.new()
	terrain.bridge = bridge
	if not surface_appearance.select(state):
		error = "Canonical terrain appearance is unavailable"
		return false
	terrain.terrain_material_created.connect(func(material: ShaderMaterial):
		if not planet_optics.apply(state, [material]) or not surface_appearance.apply([material]) or not surface_landmarks.apply(state, [material]): pause_on_error("Canonical terrain appearance is unavailable"))
	if not NativeEnvironment.apply(state, planet_environment, light, sky_material, local_light):
		error = "Canonical native environment is unavailable"
		return false
	if not planet_optics.apply(state, [sky_material]):
		error = "Canonical planetary optics are unavailable"
		return false
	near_environment.ambient_light_color = planet_environment.ambient_light_color
	near_environment.ambient_light_energy = planet_environment.ambient_light_energy
	scene.add_child(terrain)
	rendered_world = str(state.system_id) + ":" + str(state.planet_id)
	impact_overlay = ImpactView.new()
	add_child(impact_overlay)
	impact_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	build_ui()
	return true

func activate() -> void:
	# Pending C++ owns preallocated streaming; this hook performs no asset reads.
	activated = true
	observation_cue.reset(state)
	surface_aid_cue.reset(state)
	if surface_walking(): look_offset.x = -float(state.surface_walk.heading)
	near_viewport.world_3d = far_viewport.find_world_3d()
	# Activation establishes the baseline; initial Control layout is not a resize.
	last_window_size = get_window().size
	get_window().size_changed.connect(window_resized)
	layout_hud()
	setup_controls()
	update_view(0.0)
	sync_mouse_look()
	set_process(true)
	set_process_input(true)

func ready_to_commit() -> bool:
	return not activated and error.is_empty() and (state.frame_id != "2" or (is_instance_valid(staged_model) and staged_model.valid_current_pose() and is_instance_valid(exhaust) and exhaust.valid_bound_skin()))


static func set_ship_layer(node: Node) -> void:
	if node is GeometryInstance3D:
		node.layers = 2
	for child in node.get_children():
		set_ship_layer(child)


func build_ui() -> void:
	var backing := StyleBoxFlat.new()
	backing.bg_color = Color(0.01, 0.015, 0.025, 0.94)
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		backing.set_content_margin(side, 0.0)
	orbital_forecast = RichTextLabel.new()
	orbital_forecast.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	orbital_forecast.scroll_active = false
	orbital_forecast.mouse_filter = Control.MOUSE_FILTER_IGNORE
	orbital_forecast.add_theme_stylebox_override("normal", backing)
	add_child(orbital_forecast)
	hud_scroll = ScrollContainer.new()
	hud_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	hud_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_ALWAYS
	hud_scroll.add_theme_stylebox_override("panel", backing)
	hud_scroll_style = hud_scroll.get_v_scroll_bar().get_theme_stylebox("scroll").duplicate()
	hud_scroll.get_v_scroll_bar().add_theme_stylebox_override("scroll", hud_scroll_style)
	add_child(hud_scroll)
	var navigation_layer := CanvasLayer.new()
	navigation_layer.layer = 3
	add_child(navigation_layer)
	home_marker = HomeNavigation.new()
	navigation_layer.add_child(home_marker)
	ship_marker = HomeNavigation.new()
	ship_marker.label_text = "SHIP"
	ship_marker.visible = false
	navigation_layer.add_child(ship_marker)
	contact_marker = HomeNavigation.new()
	contact_marker.label_text = "CONTACT"
	contact_marker.visible = false
	navigation_layer.add_child(contact_marker)
	var column := VBoxContainer.new()
	hud_column = column
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hud_scroll.add_child(column)
	hud_theme = Theme.new()
	column.theme = hud_theme
	telemetry = hud_text()
	column.add_child(telemetry)
	resource_status = hud_text()
	column.add_child(resource_status)
	impact_status = hud_text()
	impact_status.visible = false
	column.add_child(impact_status)
	chart_button = CheckButton.new()
	chart_button.text = "Starting chart"
	chart_button.focus_mode = Control.FOCUS_ALL
	chart_button.toggled.connect(func(shown: bool): chart_status.visible = shown)
	column.add_child(chart_button)
	chart_status = hud_text()
	chart_status.visible = false
	column.add_child(chart_status)
	for ordinal in 2:
		var destination := Button.new()
		destination.focus_mode = Control.FOCUS_ALL
		destination.pressed.connect(func(): select_jump_destination(ordinal))
		column.add_child(destination)
		jump_destinations.append(destination)
	jump_status = hud_text()
	column.add_child(jump_status)
	jump_button = Button.new()
	jump_button.focus_mode = Control.FOCUS_ALL
	jump_button.pressed.connect(jump_requested)
	column.add_child(jump_button)
	service_button = Button.new()
	service_button.text = "Replenish flight fuel and jump charges · free"
	service_button.focus_mode = Control.FOCUS_ALL
	service_button.pressed.connect(func(): port_command("replenish_freedom_resources"))
	column.add_child(service_button)
	home_cue = hud_text()
	column.add_child(home_cue)
	hint = hud_text()
	column.add_child(hint)
	pause_button = Button.new()
	pause_button.text = "Resume walking" if surface_walking() else "Resume flight"
	pause_button.focus_mode = Control.FOCUS_NONE
	pause_button.pressed.connect(toggle_pause)
	column.add_child(pause_button)
	assist_button = CheckButton.new()
	assist_button.text = "Flight assistance"
	assist_button.button_pressed = state.assistance
	assist_button.focus_mode = Control.FOCUS_NONE
	assist_button.toggled.connect(request_assistance)
	column.add_child(assist_button)
	if state.frame_id == "2":
		var ports := HBoxContainer.new()
		port_row = ports
		column.add_child(ports)
		for ordinal in [1, 2]:
			var button := Button.new()
			button.text = "Target D%d" % ordinal
			button.focus_mode = Control.FOCUS_NONE
			button.pressed.connect(func(): port_command("select_freedom_port", ordinal))
			ports.add_child(button)
			port_buttons.append(button)
		dock_status = hud_text()
		column.add_child(dock_status)
		approach_button = Button.new()
		approach_button.text = "Approach port with thrusters"
		approach_button.focus_mode = Control.FOCUS_NONE
		approach_button.pressed.connect(func(): port_command("cancel_freedom_port_approach" if state.get("port_approach", {}).get("active", false) else "begin_freedom_port_approach"))
		column.add_child(approach_button)
		capture_button = Button.new()
		capture_button.text = "Capture port"
		capture_button.focus_mode = Control.FOCUS_NONE
		capture_button.pressed.connect(func(): port_command("capture_freedom_port"))
		column.add_child(capture_button)
		unboard_button = Button.new()
		unboard_button.text = "Unboard to station"
		unboard_button.focus_mode = Control.FOCUS_NONE
		unboard_button.pressed.connect(unboard_requested)
		column.add_child(unboard_button)
		release_button = Button.new()
		release_button.text = "Release port"
		release_button.focus_mode = Control.FOCUS_NONE
		release_button.pressed.connect(func(): port_command("release_freedom_port"))
		column.add_child(release_button)
		local_moon_button = Button.new()
		local_moon_button.text = "Select nearby moon jump"
		local_moon_button.focus_mode = Control.FOCUS_NONE
		local_moon_button.pressed.connect(select_moon_jump)
		column.add_child(local_moon_button)
		surface_status = hud_text()
		column.add_child(surface_status)
		landing_button = Button.new()
		landing_button.focus_mode = Control.FOCUS_NONE
		landing_button.pressed.connect(func(): port_command("request_freedom_landing"))
		column.add_child(landing_button)
		stow_gear_button = Button.new()
		stow_gear_button.text = "Stow landing gear"
		stow_gear_button.focus_mode = Control.FOCUS_NONE
		stow_gear_button.pressed.connect(func(): port_command("stow_freedom_landing_gear"))
		column.add_child(stow_gear_button)
		liftoff_button = Button.new()
		liftoff_button.text = "Liftoff with thrusters"
		liftoff_button.focus_mode = Control.FOCUS_NONE
		liftoff_button.pressed.connect(func(): port_command("liftoff_freedom_surface"))
		column.add_child(liftoff_button)
		surface_walk_button = Button.new()
		surface_walk_button.focus_mode = Control.FOCUS_NONE
		surface_walk_button.pressed.connect(func(): port_command("return_from_freedom_surface_walk" if surface_walking() else "begin_freedom_surface_walk"))
		column.add_child(surface_walk_button)
	var save_button := Button.new()
	save_button.text = "Export save file…"
	save_button.focus_mode = Control.FOCUS_NONE
	column.add_child(save_button)
	save_status = hud_text()
	save_status.text = "Paused after Continue. Quit does not autosave."
	column.add_child(save_status)
	save_dialog = FileDialog.new()
	save_dialog.access = FileDialog.ACCESS_FILESYSTEM
	save_dialog.file_mode = FileDialog.FILE_MODE_SAVE_FILE
	save_dialog.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	add_child(save_dialog)
	save_button.pressed.connect(open_save_dialog)
	save_dialog.canceled.connect(func():
		if controls_menu != null:
			controls_menu.show_menu("Save canceled. Resume explicitly after neutral."))
	save_dialog.file_selected.connect(func(path: String):
		save_status.text = "Saved to " + path if bridge.save_freedom_as(path) else "Save failed: " + str(bridge.get_last_error())
		if controls_menu != null:
			controls_menu.show_menu(save_status.text)
	)
	var quit_button := Button.new()
	quit_button.text = "Quit"
	quit_button.pressed.connect(func(): get_tree().quit())
	column.add_child(quit_button)
	primary_status = hud_text()
	primary_status.fit_content = false
	primary_status.mouse_filter = Control.MOUSE_FILTER_PASS
	primary_status.add_theme_stylebox_override("normal", backing)
	add_child(primary_status)
	context_action = Button.new()
	context_action.clip_text = true
	context_action.focus_mode = Control.FOCUS_NONE
	context_action.pressed.connect(func():
		if surface_walking():
			request_surface_interaction()
			return
		if context_source != null and not context_source.disabled:
			context_source.pressed.emit()
			update_compact_hud())
	add_child(context_action)
	instruments_button = Button.new()
	instruments_button.clip_text = true
	instruments_button.text = "Esc / Start · Controls"
	instruments_button.focus_mode = Control.FOCUS_NONE
	instruments_button.pressed.connect(func():
		instruments_open = false
		pause_controls("Controls and instruments. Release controls before resuming."))
	add_child(instruments_button)
	resized.connect(layout_hud)


func hud_text() -> RichTextLabel:
	var label := RichTextLabel.new()
	label.fit_content = true
	label.scroll_active = false
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func window_resized() -> void:
	var pixels := get_window().size
	var changed := pixels != last_window_size
	last_window_size = pixels
	if changed and activated and not paused and player_input != null:
		# Neutral, valid resizes remain cosmetic and preserve inertial flight.
		# Held controls require the existing explicit neutral/resume gate.
		observe_neutral_controls()
		if not controls_armed:
			pause_controls("Window resized. Release controls, then resume explicitly.")
	layout_hud()


func layout_hud() -> void:
	if not activated or not is_inside_tree() or hud_scroll == null:
		return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or pixels.x <= 0 or pixels.y <= 0 or not size.is_finite() or size.x <= 0 or size.y <= 0:
		return
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	var margin := minf(16 * scale, minf(size.x, size.y) * 0.1)
	var usable := size - Vector2.ONE * (2 * margin)
	if usable.x <= 0 or usable.y <= 0:
		return
	hud_theme.default_font_size = roundi(18 * scale)
	# ScrollContainer reserves the intrinsic style width, not a custom minimum.
	hud_scroll_style.content_margin_left = 8 * scale
	hud_scroll_style.content_margin_right = 8 * scale
	hud_column.add_theme_constant_override("separation", roundi(8 * scale))
	for child in hud_column.get_children():
		if child is Button:
			child.custom_minimum_size.y = 40 * scale
			child.focus_mode = Control.FOCUS_ALL
	for button in port_buttons:
		button.custom_minimum_size.y = 40 * scale
	orbital_forecast.add_theme_font_size_override("normal_font_size", roundi(20 * scale))
	hud_stacked = usable.x < (260 + 460 + 16) * scale
	if hud_stacked:
		if orbital_forecast.get_parent() != hud_column:
			orbital_forecast.reparent(hud_column, false)
		orbital_forecast.fit_content = true
		orbital_forecast.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		hud_scroll.size = usable
	else:
		if orbital_forecast.get_parent() != self:
			orbital_forecast.reparent(self, false)
		orbital_forecast.fit_content = false
		orbital_forecast.size = Vector2(460 * scale, 190 * scale)
		orbital_forecast.position = Vector2(size.x - orbital_forecast.size.x - margin, margin)
		hud_scroll.size = Vector2(minf(480 * scale, usable.x - orbital_forecast.size.x - margin), minf(usable.y, maxf(260 * scale, hud_column.get_combined_minimum_size().y)))
	hud_scroll.position = Vector2.ONE * margin
	var compact := Status.layout_bounds(size, pixels)
	primary_status.position = compact.position
	primary_status.size = compact.size
	primary_status.add_theme_font_size_override("normal_font_size", roundi(18 * scale))
	for button in [context_action, instruments_button]:
		button.add_theme_font_size_override("font_size", roundi(18 * scale))
	var action_height := minf(40 * scale, usable.y * 0.4)
	var gap := minf(8 * scale, usable.y * 0.05)
	context_action.position = Vector2(margin, size.y - margin - 2.0 * action_height - gap)
	context_action.size = Vector2(compact.size.x, action_height)
	instruments_button.position = Vector2(margin, size.y - margin - action_height)
	instruments_button.size = Vector2(compact.size.x, action_height)
	primary_status.size.y = minf(primary_status.size.y, maxf(0.0, usable.y - 2.0 * action_height - 2.0 * gap))
	if hud_stacked and instruments_open and paused:
		hud_scroll.size.y = maxf(0.0, usable.y - action_height - gap)
	update_compact_hud()


func show_instruments() -> void:
	pause_controls("Flight instruments and navigation.", false)
	update_view(0.0)
	if controls_menu != null: controls_menu.hide_menu()
	instruments_open = true
	layout_hud()
	instruments_button.grab_focus()


func update_compact_hud() -> void:
	if primary_status == null: return
	var detailed := instruments_open and paused
	hud_scroll.visible = detailed
	orbital_forecast.visible = detailed and not surface_walking()
	primary_status.visible = not detailed
	context_action.visible = not detailed
	instruments_button.visible = true
	instruments_button.text = "Back to controls" if detailed else "Esc / Start · Controls"
	instruments_button.focus_mode = Control.FOCUS_ALL if detailed else Control.FOCUS_NONE
	var margin := Status.layout_bounds(size, Vector2(get_window().size)).position.x
	instruments_button.position.x = size.x - instruments_button.size.x - margin if detailed and not hud_stacked else margin
	# Quiet refreshes absorb command/load changes without inventing a finding.
	observation_cue.observe(state, false, 0.0)
	surface_aid_cue.observe(state, false, 0.0)
	primary_status.text = Status.summary(state, player_input.foot_keyboard_label() if player_input != null else "WASD") if error.is_empty() else "Flight paused\n" + error
	if error.is_empty():
		var outcome := ImpactView.feedback(state.get("impact", {"selected": false}))
		if not outcome.is_empty(): primary_status.text += "\n" + outcome
	primary_status.tooltip_text = primary_status.text
	if error.is_empty() and not surface_aid_cue.text().is_empty():
		var aid_text := surface_aid_cue.text()
		var aid_lines := primary_status.text.split("\n")
		aid_lines.insert(1, aid_text.left(180) + ("…" if aid_text.length() > 180 else ""))
		primary_status.text = "\n".join(aid_lines)
		primary_status.tooltip_text += "\n" + aid_text
	if save_status != null and not save_status.text.is_empty() and error.is_empty():
		primary_status.tooltip_text += "\n" + save_status.text
		var lines := primary_status.text.split("\n")
		lines.insert(1, save_status.text.left(180) + ("…" if save_status.text.length() > 180 else ""))
		primary_status.text = "\n".join(lines)
	if error.is_empty() and not surface_walking() and not observation_cue.text().is_empty():
		primary_status.text += "\n" + observation_cue.text()
		primary_status.tooltip_text += "\n" + observation_cue.text()
	var personal: Dictionary = state.get("surface_marker", {})
	if error.is_empty() and personal.get("present", false):
		var marker_text := ""
		if show_personal_marker and personal.current_body:
			marker_text = "MARK · %.1f m · straight-line distance" % personal.distance_metres
		elif not personal.current_body:
			marker_text = "MARK retained · %s / %s · another body" % [personal.system_name, personal.planet_name]
		if not marker_text.is_empty():
			primary_status.text += "\n" + marker_text
			primary_status.tooltip_text += "\n" + marker_text
	var exposure := SurfaceLandmarks.nearest_nearby(state)
	if error.is_empty() and not exposure.is_empty():
		var direct_inspection: bool = exposure.can_inspect and not state.get("surface_walk", {}).get("return_access", {}).get("within_reach", false)
		var exposure_text := "Nearby exposure · %s · %.1f m\n%s" % [exposure.label, exposure.distance_metres, surface_interaction_binding() + " · Inspect exposure" if direct_inspection else "Controls → Inspect exposure" if exposure.can_inspect else "Move within 8 m to inspect"]
		primary_status.text += "\n" + exposure_text
		primary_status.tooltip_text += "\n" + exposure_text
	var contact := DistantContacts.selected_contact(state, selected_contact_id)
	if error.is_empty() and not contact.is_empty():
		var contact_text := "CONTACT · %s\n%s" % [contact.status.replace("_", " "), contact.reason]
		primary_status.text += "\n" + contact_text
		primary_status.tooltip_text += "\n" + contact_text + "\nDistant optical observation only; no supported approach or landing."
	context_source = null
	if surface_walking():
		context_source = landmark_inspect_button if not exposure.is_empty() and exposure.can_inspect and not state.surface_walk.return_access.get("within_reach", false) else surface_walk_button
	elif state.get("surface", {}).get("landed", false): context_source = liftoff_button
	elif state.get("attached", false): context_source = release_button
	elif capture_button != null and not capture_button.disabled and capture_button.visible: context_source = capture_button
	elif approach_button != null and not approach_button.disabled and approach_button.visible: context_source = approach_button
	elif state.get("surface", {}).get("maneuver", "off") != "off": context_source = null
	elif landing_button != null and not landing_button.disabled and state.get("surface", {}).get("gear_deployed", false): context_source = landing_button
	elif jump_button != null and not jump_button.disabled and jump_button.visible: context_source = jump_button
	context_action.visible = not detailed and context_source != null
	if context_source != null:
		context_action.text = context_source.text
		if surface_walking(): context_action.text += " · " + surface_interaction_binding()
		context_action.tooltip_text = context_source.tooltip_text
		context_action.disabled = context_source.disabled or paused or not focused or not error.is_empty()


func setup_controls() -> void:
	player_input = PlayerInput.new()
	player_input.initial_device = initial_controller_device
	player_input.persist = persist_controls
	player_input.settings_path = control_settings_path
	player_input.thrust_mode = true
	player_input.assistance_request_only = true
	player_input.surface_interaction_enabled = surface_walking()
	player_input.assist = state.assistance
	add_child(player_input)
	player_input.set_enabled(false)
	controls_menu = PauseMenu.new()
	controls_menu.controls = player_input
	controls_menu.saved_flight = true
	controls_menu.observations_query = Callable(bridge, "get_freedom_observations")
	controls_menu.saved_wayfarer = state.frame_id == "2"
	controls_menu.ship_audio_available = ship_audio != null
	controls_menu.audio_preferences = audio_preferences
	if ship_audio != null:
		controls_menu.ship_audio_muted.connect(ship_audio.set_muted)
		controls_menu.audio_mix_changed.connect(func():
			var levels: Dictionary = audio_preferences.levels
			ship_audio.set_mix_levels(levels.master, levels.machinery, levels.propulsion, levels.atmosphere))
	add_child(controls_menu)
	var survey_column: Node = controls_menu.save_button.get_parent()
	surface_conditions_button = controls_menu.add_button(survey_column, "Surface conditions", show_surface_conditions)
	controls_menu.add_button(survey_column, "Flight instruments & navigation", show_instruments)
	controls_menu.add_button(survey_column, "Select nearby moon jump", select_moon_jump)
	surface_marker_button = controls_menu.add_button(survey_column, "Mark this spot", func(): surface_marker_command("replace_freedom_surface_marker" if state.get("surface_marker", {}).get("present", false) else "mark_freedom_surface_marker"))
	clear_surface_marker_button = controls_menu.add_button(survey_column, "Clear marker", func(): surface_marker_command("clear_freedom_surface_marker"))
	show_surface_marker_button = controls_menu.add_button(survey_column, "Show personal marker: Off", toggle_surface_marker)
	landmark_enable_button = controls_menu.add_button(survey_column, "Enable local landmarks", enable_surface_landmarks)
	landmark_inspect_button = controls_menu.add_button(survey_column, "Inspect exposure", inspect_surface_landmark)
	for index in DistantContacts.MAX_CONTACTS:
		contact_buttons.append(controls_menu.add_button(survey_column, "Track optical contact %d" % (index + 1), select_distant_contact.bind(index)))
	contact_clear_button = controls_menu.add_button(survey_column, "Stop tracking optical contact", clear_distant_contact)
	if state.frame_id == "2":
		var menu_column: Node = controls_menu.save_button.get_parent()
		refresh_impact_action()
		unboard_menu_button = controls_menu.add_button(menu_column, "Unboard to station", unboard_requested)
		menu_column.move_child(unboard_menu_button, controls_menu.title_button.get_index() + 1)
		surface_walk_menu_button = controls_menu.add_button(menu_column, "Leave Wayfarer · suit", func(): port_command("return_from_freedom_surface_walk" if surface_walking() else "begin_freedom_surface_walk"))
		suit_lamp_button = controls_menu.add_button(menu_column, "Suit lamp: On", toggle_suit_lamp)
	player_input.pause_requested.connect(toggle_pause)
	player_input.safety_pause.connect(pause_controls)
	player_input.bindings_changed.connect(func():
		pause_controls("Controls changed. Release mapped controls before resuming."))
	player_input.assist_requested.connect(request_assistance)
	player_input.camera_requested.connect(change_camera)
	player_input.recenter_requested.connect(recenter_camera)
	player_input.surface_interaction_requested.connect(request_surface_interaction)
	controls_menu.resumed.connect(toggle_pause)
	controls_menu.save_requested.connect(open_save_dialog)
	controls_menu.catalog_save_requested.connect(func(save_as: bool):
		if paused and focused and not journey_dialog_open and not save_dialog.visible: catalog_save_requested.emit(save_as))
	controls_menu.load_requested.connect(func():
		if paused and focused and not journey_dialog_open and not save_dialog.visible: load_requested.emit())
	controls_menu.title_requested.connect(func():
		if paused and focused and not journey_dialog_open and not save_dialog.visible: title_requested.emit())
	controls_menu.assistance_requested.connect(request_assistance)
	controls_menu.hold_requested.connect(request_hold)
	controls_menu.port_requested.connect(port_command)
	controls_menu.quit_requested.connect(func():
		if quit_handler.is_valid(): quit_handler.call()
		else: get_tree().quit())
	controls_menu.sync_saved_state(state, false)
	controls_menu.show_menu("On the surface. Release controls, then resume to walk." if surface_walking() else "Seated in Wayfarer. Release controls, then resume to fly." if state.get("boarding", {}).get("state") == "seated" else "Paused after Continue. Release controls, then resume explicitly.")
	refresh_control_hint()


# Preserve the shared reference vocabulary for existing scene/test consumers.
const REFERENCE_MARGIN_LABELS := Status.REFERENCE_MARGIN_LABELS


static func reference_limits_text(assessment: Dictionary) -> String:
	return Status.reference_limits_text(assessment)


func surface_action_reference_warning(command: String) -> String:
	var operations: Array = [3, 4] if command == "request_freedom_landing" else [4] if command == "liftoff_freedom_surface" else []
	var assessments: Array = []
	for operation in operations:
		assessments.append(bridge.get_freedom_environment_assessment(operation))
	return Status.surface_reference_warning(command, assessments)


func surface_conditions_text() -> String:
	var lines := PackedStringArray(["Surface reference envelope"])
	for operation in [{"name": "Touchdown", "id": 3}, {"name": "Return ascent", "id": 4}]:
		var assessment: Dictionary = bridge.get_freedom_environment_assessment(operation.id)
		if assessment.has("error"):
			return str(assessment.error)
		lines.append("%s: %s" % [operation.name, assessment.rating])
		lines.append("Shielding/thermal %s · Structure %s · Thrust %s" % [assessment.shielding_thermal, assessment.structural, assessment.propulsion])
		var limits := reference_limits_text(assessment)
		if not limits.is_empty(): lines.append(limits)
	lines.append("These are recorded potential envelopes, not current weather or landing clearance. A close pass can resolve UNKNOWN conditions. Check terrain and flight conditions before landing.")
	return "\n".join(lines)


func show_surface_conditions() -> void:
	# This action is only in the paused controls menu; querying cannot advance
	# the C++ clock, cancel an aid, or implicitly resume the flight.
	if not paused or not focused: return
	controls_menu.show_menu()
	controls_menu.show_surface_reference(surface_conditions_text(), surface_conditions_button)


func toggle_suit_lamp() -> void:
	if not paused or not focused or not surface_walking() or journey_dialog_open or save_dialog.visible or recovery_notified or not error.is_empty(): return
	suit_lamp_enabled = not suit_lamp_enabled
	refresh_suit_lamp()


func enable_surface_landmarks() -> void:
	if not marker_controls_available() or not state.get("surface_landmarks", {}).get("can_enable", false): return
	if not bridge.enable_freedom_surface_landmarks():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	report_action_status("Local landmarks enabled for this journey.")
	controls_menu.resume_button.grab_focus()


func inspect_surface_landmark() -> void:
	if not marker_controls_available() or not surface_walking(): return
	var site := SurfaceLandmarks.nearest_nearby(state)
	if site.is_empty(): return
	if not bridge.inspect_freedom_surface_landmark(site.id):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	var notice := "Exposure inspection already recorded." if site.inspected else "Exposure inspected and recorded."
	report_action_status(notice)
	controls_menu.show_surface_reference("LOCAL EXPOSURE\n%s\n\n%s\n\n%s\nThis is a geological appearance record, not a material analysis, resource deposit or landing certificate. Your personal marker is unchanged." % [site.label, site.description, notice], landmark_inspect_button)


func surface_interaction_binding() -> String:
	var result := "E / left click"
	if player_input != null and player_input.available_pads.get(player_input.device, false):
		result += " / " + ("Cross" if player_input.playstation() else "A")
	return result


func surface_interaction_neutral() -> bool:
	# E can be held to interact even though it is a mapped flight strafe key.
	# Other flight-only controls do not move a surface walker. Resume retains
	# its separate, unchanged all-flight neutral gate.
	if not player_input.foot_keyboard_look_neutral() or Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT): return false
	if not player_input.foot_keyboard_move_neutral(): return false
	if player_input.available_pads.get(player_input.device, false):
		for axis in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y, JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
			var value := Input.get_joy_axis(player_input.device, axis)
			if not is_finite(value) or PlayerInput.shape(value, player_input.settings.deadzone, player_input.settings.curve) != 0.0: return false
	return true


func request_surface_interaction() -> void:
	if not activated or not focused or paused or not surface_walking() or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or not player_input.waiting_action.is_empty(): return
	if not controls_armed or not surface_interaction_neutral():
		report_action_status("Release walking and look controls to interact.")
		return
	if state.surface_walk.return_access.get("within_reach", false):
		# The normal paused menu still uses port_command and its full neutral
		# gate. This direct foot action already checked the relevant controls.
		if not bridge.return_from_freedom_surface_walk():
			report_action_status(str(bridge.get_last_error()))
			return
		state = bridge.get_freedom_flight_state()
		look_offset = Vector2.ZERO
		pause_controls("Back in the seat. Release controls, then resume to fly.")
		update_view(0.0)
		return
	var site := SurfaceLandmarks.nearest_nearby(state)
	if site.is_empty() or not site.can_inspect:
		report_action_status("Approach RETURN or move within 8 m of an exposure to interact.")
		return
	pause_controls("Inspecting local exposure.")
	inspect_surface_landmark()


func refresh_surface_landmarks() -> void:
	if landmark_enable_button == null: return
	landmark_enable_button.visible = state.get("surface_landmarks", {}).get("can_enable", false)
	var site := SurfaceLandmarks.nearest_nearby(state)
	landmark_inspect_button.visible = not site.is_empty()
	landmark_inspect_button.disabled = site.is_empty() or not site.can_inspect
	landmark_inspect_button.tooltip_text = "%s · %.1f m\nInspect within 8 m on supported ground." % [site.label, site.distance_metres] if not site.is_empty() else ""


func marker_controls_available() -> bool:
	return paused and focused and controls_menu.panel.visible and player_input.waiting_action.is_empty() and not journey_dialog_open and not save_dialog.visible and not recovery_notified and error.is_empty() and not controls_menu.settings_view.visible and not controls_menu.basics.visible and not controls_menu.surface_reference_view.visible and (controls_menu.observations_view == null or not controls_menu.observations_view.visible)


func select_distant_contact(index: int) -> void:
	if not marker_controls_available() or not DistantContacts.valid(state): return
	var contacts: Array = state.get("distant_contacts", {}).get("contacts", [])
	if index < 0 or index >= contacts.size(): return
	selected_contact_id = contacts[index].id
	update_view(0.0)
	# Selection is presentation-only, including under held movement controls.
	controls_menu.show_surface_reference("DISTANT OPTICAL CONTACT\n%s\n\n%s\n\nCONTACT is a direction annotation, not an arrival target. Moon and minor-body close flight, gravity, collision and landing are not supported by this observation slice." % [contacts[index].label, contacts[index].reason], contact_buttons[index])


func clear_distant_contact() -> void:
	if not marker_controls_available(): return
	selected_contact_id = ""
	update_view(0.0)
	controls_menu.resume_button.grab_focus()


func refresh_distant_contacts() -> void:
	var contacts: Array = state.get("distant_contacts", {}).get("contacts", [])
	var selected := DistantContacts.selected_contact(state, selected_contact_id)
	if selected.is_empty(): selected_contact_id = ""
	for index in contact_buttons.size():
		var button := contact_buttons[index]
		button.visible = index < contacts.size()
		if index < contacts.size():
			button.text = "%s optical contact %d" % ["Tracking" if contacts[index].id == selected_contact_id else "Track", index + 1]
			button.tooltip_text = "Distant observation only · " + (contacts[index].reason if contacts[index].id == selected_contact_id else "Select to inspect current optical availability; no supported approach or landing.")
	if contact_clear_button != null: contact_clear_button.visible = not selected.is_empty()
	if contact_marker == null: return
	contact_marker.visible = not selected.is_empty() and selected.status in ["visible", "unresolved"]
	if not contact_marker.visible: return
	contact_marker.label_text = "CONTACT · unresolved" if selected.status == "unresolved" else "CONTACT"
	# A direction annotation at an arbitrary display distance, never a world
	# position, range estimate or target handed to the simulation.
	contact_marker.update_cue(camera, camera.global_position + selected.direction * 1000.0, false)
	if contact_marker.cue.is_empty(): return
	var contact_bounds: Rect2 = contact_marker.label_bounds(contact_marker.cue.position).grow(18.0 * contact_marker.render_scale)
	for preferred in [home_marker, ship_marker]:
		if preferred.visible and not preferred.cue.is_empty() and contact_bounds.intersects(preferred.label_bounds(preferred.cue.position).grow(18.0 * preferred.render_scale)):
			# Preserve HOME/RETURN/MARK; the selected contact's text remains.
			contact_marker.visible = false
			break


func surface_marker_command(command: String) -> void:
	if not marker_controls_available(): return
	if command not in ["mark_freedom_surface_marker", "replace_freedom_surface_marker", "clear_freedom_surface_marker"]: return
	if command != "clear_freedom_surface_marker" and not surface_walking(): return
	if not bridge.call(command):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	if command == "clear_freedom_surface_marker": show_personal_marker = false
	update_view(0.0)
	report_action_status("Personal marker cleared." if command == "clear_freedom_surface_marker" else "Personal marker replaced at your feet." if command == "replace_freedom_surface_marker" else "Personal marker recorded at your feet.")


func toggle_surface_marker() -> void:
	if not marker_controls_available() or not state.get("surface_marker", {}).get("current_body", false): return
	show_personal_marker = not show_personal_marker
	update_view(0.0)


func refresh_surface_marker() -> void:
	var marker: Dictionary = state.get("surface_marker", {})
	var present: bool = marker.get("present", false)
	var current: bool = marker.get("current_body", false)
	if not present or not current: show_personal_marker = false
	if surface_marker_button != null:
		surface_marker_button.visible = surface_walking()
		surface_marker_button.disabled = not marker.get("can_mark", false)
		surface_marker_button.text = "Replace marker" if present else "Mark this spot"
		clear_surface_marker_button.visible = present
		clear_surface_marker_button.disabled = not marker.get("can_clear", false)
		show_surface_marker_button.visible = present
		show_surface_marker_button.disabled = not current
		show_surface_marker_button.text = "Show personal marker: " + ("On" if show_personal_marker else "Off") if current else "Marker retained on another body"
		show_surface_marker_button.tooltip_text = "%s / %s · recorded ship time %.1f s\nDirection and straight-line distance only; no route or landing clearance." % [marker.system_name, marker.planet_name, marker.tick.to_float() / 120.0] if present else ""
	if not show_personal_marker: return
	if surface_walking():
		ship_marker.label_text = "MARK"
		ship_marker.update_cue(camera, marker.position, false)
	else:
		home_marker.visible = true
		home_marker.label_text = "MARK"
		home_marker.update_cue(camera, marker.position, false)
		home_cue.visible = false


func refresh_suit_lamp() -> void:
	var outside := surface_walking()
	suit_lamp.visible = outside and suit_lamp_enabled
	if suit_lamp_button != null:
		suit_lamp_button.visible = outside
		suit_lamp_button.text = "Suit lamp: " + ("On" if suit_lamp_enabled else "Off")


func refresh_control_hint() -> void:
	if player_input == null:
		return
	if surface_walking():
		hint.text = "%s / left stick walk · Mouse / look keys / right stick look\nEsc / Start releases the mouse · Return near the aft ground access to reboard" % player_input.foot_keyboard_label()
		return
	var family := "pad" if player_input.last_device == "pad" else "key"
	hint.text = "%s main · %s retro · %s view · Esc / Start controls\nHold right mouse to look · %s recenter head\nFlight controls request physical thrust/torque." % [player_input.binding_label("forward", family), player_input.binding_label("backward", family), player_input.binding_label("camera", family), player_input.binding_label("recenter", family)]


func request_assistance(enabled: bool) -> void:
	if surface_walking():
		assist_button.set_pressed_no_signal(state.assistance)
		save_status.text = "Return to the pilot seat to change flight assistance."
		return
	if not error.is_empty() or not focused or save_dialog.visible:
		hide_exhaust()
		assist_button.set_pressed_no_signal(state.get("assistance", false))
		return
	if not bridge.set_freedom_assistance(enabled):
		assist_button.set_pressed_no_signal(state.get("assistance", false))
		pause_on_error(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	player_input.assist = state.assistance
	assist_button.set_pressed_no_signal(state.assistance)
	controls_menu.sync_saved_state(state, false)


func report_action_status(message: String) -> void:
	save_status.text = message
	if paused and controls_menu.panel.visible:
		# The opaque controls panel covers the HUD, and its next frame reads
		# controls.status. Preserve the same notice in both visible contexts.
		player_input.status = message
		controls_menu.message.text = message
	update_compact_hud()


func request_hold(enabled: bool) -> void:
	if not paused or not focused or not error.is_empty() or journey_dialog_open or save_dialog.visible or mode_change_pending:
		return
	if not bridge.set_freedom_hold(enabled):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	controls_menu.sync_saved_state(state, false)
	report_action_status("Orbit-hold request selected. Resume explicitly to apply correction." if enabled else "Orbit-hold request disabled. Resume explicitly to coast.")


func open_save_dialog() -> void:
	pause_controls("Save As preserves the last committed state.", false)
	controls_menu.hide_menu()
	save_dialog.popup_centered_ratio(0.75)


func pause_controls(reason: String, show_menu := true) -> void:
	mouse_look.set_active(false)
	if state.get("surface", {}).get("maneuver", "off") != "off":
		bridge.cancel_freedom_surface_maneuver()
		state = bridge.get_freedom_flight_state()
	if state.get("port_approach", {}).get("active", false):
		bridge.cancel_freedom_port_approach()
		state = bridge.get_freedom_flight_state()
	paused = true
	instruments_open = false
	controls_armed = false
	if player_input != null:
		player_input.set_enabled(false)
		player_input.looking = false
		player_input.look_axes_needs_neutral = true
	look_offset = Vector2(-float(state.surface_walk.heading), 0.0) if surface_walking() else Vector2.ZERO
	hide_exhaust()
	if pause_button != null:
		pause_button.text = "Resume flight"
	if controls_menu != null:
		if bridge.has_method("get_freedom_profile_state"):
			var profile: Dictionary = bridge.get_freedom_profile_state()
			controls_menu.sync_profile_state(profile)
			if profile.get("explicit_path", false): reason += "\n" + str(profile.reason)
		controls_menu.sync_saved_state(state, not error.is_empty())
		if show_menu and not journey_dialog_open and not recovery_notified:
			controls_menu.show_menu(reason)
	if save_status != null and error.is_empty():
		save_status.text = reason
	update_compact_hud()


func change_camera() -> void:
	if error.is_empty() and focused and not paused and not save_dialog.visible and state.frame_id == "2" and not surface_walking():
		cockpit = not cockpit
		look_offset = Vector2.ZERO
		update_view(0.0)


func recenter_camera() -> void:
	if error.is_empty() and focused and not paused and not save_dialog.visible:
		look_offset = Vector2(-float(state.surface_walk.heading), 0.0) if surface_walking() else Vector2.ZERO
		update_view(0.0)


func unboard_requested() -> void:
	if not error.is_empty() or not focused or save_dialog.visible or player_input == null: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release flight controls before leaving the seat.")
		return
	if not bridge.begin_freedom_disembarking():
		report_action_status(str(bridge.get_last_error()))
		return
	pause_controls("Returning to the station.")
	if not mode_change_pending:
		mode_change_pending = true
		journey_mode_changed.emit()


func select_moon_jump() -> void:
	if not error.is_empty() or not focused or save_dialog.visible: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before selecting local travel.")
		return
	if not bridge.select_freedom_moon_jump():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	report_action_status("Nearby moon selected. One charge commits after spooling.")
	update_view(0.0)


func select_jump_destination(ordinal: int) -> void:
	if not error.is_empty() or not focused or save_dialog.visible: return
	var rows: Array = state.get("chart", {}).get("rows", [])
	if ordinal >= rows.size(): return
	if not bridge.select_freedom_jump(rows[ordinal].system_id):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)


func jump_requested() -> void:
	if not error.is_empty() or not focused or save_dialog.visible: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release flight controls before changing the jump drive.")
		return
	var cancel: bool = state.get("jump", {}).get("phase", "idle") == "spool"
	if not bridge.call("cancel_freedom_jump" if cancel else "begin_freedom_jump"):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	report_action_status("Spool canceled · no jump charge spent" if cancel else "Spooling · Resume flight explicitly." if paused else "Spooling · one charge commits after 3 seconds")
	update_view(0.0)


func port_command(command: String, ordinal: int = 0) -> void:
	if not error.is_empty() or not focused or save_dialog.visible:
		hide_exhaust()
		return
	if command in ["begin_freedom_port_approach", "request_freedom_landing", "liftoff_freedom_surface", "begin_freedom_surface_walk", "return_from_freedom_surface_walk"]:
		observe_neutral_controls()
		if not controls_armed:
			report_action_status("Release movement controls before changing modes or starting a maneuver.")
			return
	var before := state
	var accepted: bool = bridge.call(command, ordinal) if ordinal != 0 else bridge.call(command)
	if not accepted:
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	var reference_warning := surface_action_reference_warning(command)
	if not reference_warning.is_empty(): reference_warning = "\n" + reference_warning
	var canceled_aid := command == "cancel_freedom_surface_maneuver" and surface_aid_cue.accept_cancel(before, state)
	if command in ["begin_freedom_surface_walk", "return_from_freedom_surface_walk"]:
		look_offset = Vector2.ZERO
		pause_controls("On the surface. Release controls, then resume to walk." if surface_walking() else "Back in the seat. Release controls, then resume to fly.")
		update_view(0.0)
		return
	update_view(0.0)
	if command == "replenish_freedom_resources":
		report_action_status("Flight reserve and three jump charges replenished · free station service")
	elif state.attached:
		report_action_status("Attached to D%d. Release before firing propulsion." % state.target_port)
	elif command == "release_freedom_port":
		var family := "pad" if player_input.last_device == "pad" else "key"
		report_action_status("Released D%d. %s withdraws below the port; %s brakes. Clear the 12 m column before forward thrust." % [state.target_port, player_input.binding_label("fall", family), player_input.binding_label("rise", family)])
	elif command.ends_with("port_approach"):
		report_action_status(state.port_approach.note + (" · Resume flight explicitly." if paused and state.port_approach.active else ""))
	elif canceled_aid:
		report_action_status(surface_aid_cue.text())
	elif command in ["request_freedom_landing", "liftoff_freedom_surface"] and state.surface.maneuver != "off":
		report_action_status(("Landing aid requested." if command == "request_freedom_landing" else "Liftoff aid requested.") + (" Resume flight explicitly." if paused else "") + reference_warning)
	elif command in ["request_freedom_landing", "stow_freedom_landing_gear", "liftoff_freedom_surface", "cancel_freedom_surface_maneuver"]:
		report_action_status(state.surface.note + (" · Resume flight explicitly." if paused and state.surface.maneuver != "off" else "") + reference_warning)
	else:
		report_action_status("Port selected. Capture requires physical alignment and low closure.")


func enable_impacts() -> void:
	if not paused or not focused or not error.is_empty() or journey_dialog_open or save_dialog.visible:
		return
	if not bridge.enable_freedom_impact_consequences():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	pause_controls("Experimental terrain impacts selected · Save to retain this choice. Release controls before resuming.")
	update_view(0.0)


func refresh_impact_action() -> void:
	if controls_menu == null: return
	var selected: bool = state.get("impact", {}).get("selected", false)
	var opt_in: Dictionary = state.get("impact_opt_in", {"available": false, "reason": "Unavailable"})
	if state.frame_id != "2":
		if impact_menu_button != null:
			impact_menu_button.get_parent().remove_child(impact_menu_button)
			impact_menu_button.queue_free()
			impact_menu_button = null
		return
	if impact_menu_button == null:
		if not (selected or opt_in.available): return
		impact_menu_button = controls_menu.add_button(controls_menu.save_button.get_parent(), "Enable experimental terrain impacts", enable_impacts)
	impact_menu_button.text = "Experimental terrain impacts · selected" if selected else "Enable experimental terrain impacts"
	impact_menu_button.disabled = selected or not opt_in.available
	impact_menu_button.tooltip_text = opt_in.reason if not opt_in.available else "Explicit choice. Save to retain it. Terrain collisions can damage the craft or cause loss; this is a bounded experimental collider."


func toggle_pause() -> void:
	if journey_dialog_open or recovery_notified: return
	if not error.is_empty():
		hide_exhaust()
		return
	if not paused:
		pause_controls("Walking paused. Release controls before resuming." if surface_walking() else "Flight paused. Release controls before resuming.")
		return
	if not focused or save_dialog.visible:
		hide_exhaust()
		return
	if controls_menu.back_from_nested(): return
	observe_neutral_controls()
	if not controls_armed:
		save_status.text = "Release all mapped flight controls before resuming."
		player_input.status = save_status.text
		return
	paused = false
	sync_mouse_look()
	instruments_open = false
	# The already-observed neutral latch owns resume; do not reset it again.
	player_input.enabled = true
	player_input.look_axes_needs_neutral = false
	controls_menu.hide_menu()
	refresh_control_hint()
	pause_button.text = "Pause walking" if surface_walking() else "Pause flight"
	save_status.text = "Walking. Save As pauses the session." if surface_walking() else "Flight running. Save As pauses the session."
	update_compact_hud()


func observe_neutral_controls() -> void:
	if not focused or player_input == null:
		return
	var neutral: bool = player_input.observe_neutral()
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT): neutral = false
	if surface_walking():
		if not player_input.foot_keyboard_move_neutral(): neutral = false
		if player_input.device >= 0:
			for axis in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y, JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
				if absf(Input.get_joy_axis(player_input.device, axis)) > float(player_input.settings.deadzone): neutral = false
	controls_armed = neutral and not player_input.needs_neutral
	if not neutral: player_input.needs_neutral = true


func surface_controls(delta: float) -> PackedFloat64Array:
	if paused or not focused or not controls_armed or save_dialog.visible:
		return PackedFloat64Array([0.0, 0.0, float(state.surface_walk.heading)])
	var keyboard_move: Vector2 = player_input.foot_keyboard_move()
	var forward := keyboard_move.y
	var right := keyboard_move.x
	look_offset += player_input.foot_keyboard_look() * delta
	if player_input.device >= 0:
		var deadzone: float = player_input.settings.deadzone
		var curve: float = player_input.settings.curve
		forward -= player_input.shape(Input.get_joy_axis(player_input.device, JOY_AXIS_LEFT_Y), deadzone, curve)
		right += player_input.shape(Input.get_joy_axis(player_input.device, JOY_AXIS_LEFT_X), deadzone, curve)
		look_offset += player_input.foot_controller_look(player_input.device) * delta
	look_offset.x = wrapf(look_offset.x, -PI, PI)
	look_offset.y = clampf(look_offset.y, -1.3, 1.3)
	var heading := wrapf(-look_offset.x, -PI, PI)
	# Recentered yaw has one canonical save representation, including signed zero.
	if heading == 0.0: heading = 0.0
	return PackedFloat64Array([clampf(forward, -1.0, 1.0), clampf(right, -1.0, 1.0), heading])


func sync_mouse_look() -> void:
	mouse_look.set_active(activated and focused and not paused and error.is_empty() and not journey_dialog_open and not recovery_notified and save_dialog != null and not save_dialog.visible and (surface_walking() or Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)))


func _exit_tree() -> void:
	mouse_look.set_active(false)


func _input(event: InputEvent) -> void:
	if not focused or paused or journey_dialog_open or recovery_notified or not error.is_empty() or save_dialog == null or save_dialog.visible: return
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and surface_walking() and mouse_look.active:
		request_surface_interaction()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		sync_mouse_look()
	elif event is InputEventMouseMotion and (surface_walking() or Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)):
		look_offset += MouseLook.motion_delta(event.relative, player_input.settings)
		look_offset.x = wrapf(look_offset.x, -PI, PI) if surface_walking() else clampf(look_offset.x, -1.2 if cockpit else -PI, 1.2 if cockpit else PI)
		look_offset.y = clampf(look_offset.y, -1.3 if surface_walking() else -0.8 if cockpit else -1.2, 1.3 if surface_walking() else 0.8 if cockpit else 1.2)


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
		if player_input != null:
			player_input.focused = false
		pause_controls("Focus lost. Release controls, then resume explicitly.")
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
		if player_input != null:
			player_input.focused = true


func _unhandled_key_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed or event.echo or save_dialog == null or save_dialog.visible:
		return
	# Esc/Start and remapped camera input belong to PlayerInput only.
	if event.physical_keycode == KEY_F3 and error.is_empty() and focused and state.frame_id == "2" and not surface_walking():
		# Historical alias may inspect the camera while paused; no world action.
		cockpit = not cockpit
		look_offset = Vector2.ZERO
		update_view(0.0)


func hide_exhaust() -> void:
	if exhaust != null:
		exhaust.update_applied({}, 0.0, true)


func pause_on_error(message: String) -> void:
	error = message
	if impact_overlay != null: impact_overlay.visible = false
	pause_controls(message)
	if save_status != null:
		save_status.text = "Flight paused: " + error


func _process(delta: float) -> void:
	if not activated or recovery_notified: return
	sync_mouse_look()
	if bridge == null or camera == null or terrain == null or not error.is_empty():
		hide_exhaust()
		return
	if not focused:
		hide_exhaust()
		return
	if not bridge.get_freedom_walk_state().is_empty():
		hide_exhaust()
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	var resolved: Dictionary = player_input.sample()
	controls_armed = not player_input.needs_neutral
	var demand := actuator_fractions(resolved.thrust_axes)
	if state.get("attached", false) or state.get("surface", {}).get("landed", false):
		demand.fill(0.0)
	var accepted: bool = bridge.advance_freedom_surface_walk(delta, surface_controls(minf(delta, 0.1)), paused or save_dialog.visible) if surface_walking() else bridge.advance_freedom_flight(delta, demand, paused or save_dialog.visible)
	if not accepted:
		pause_on_error(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	if state.get("body_kind") == "moon":
		hide_exhaust()
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	if not valid_state(state):
		pause_on_error("C++ flight view became unavailable: " + str(bridge.get_last_error()))
		return
	if state.get("impact", {}).get("pending_loss", false):
		# The successful C++ batch already committed the actual loss. Stop
		# here before another tick can refuse or terrain can obscure recovery.
		recovery_notified = true
		pause_controls("Recorded craft loss. Continue recovery when ready.", false)
		set_process(false)
		recovery_requested.emit()
		return
	observation_cue.observe(state, not paused and not save_dialog.visible and not surface_walking() and delta > 0.0, delta)
	surface_aid_cue.observe(state, not paused and not save_dialog.visible and not surface_walking() and delta > 0.0, delta)
	if not paused and not save_dialog.visible and not surface_walking():
		if resolved.recenter:
			look_offset = Vector2.ZERO
		look_offset += resolved.look * delta
		look_offset.x = clampf(look_offset.x, -1.2 if cockpit else -PI, 1.2 if cockpit else PI)
		look_offset.y = clampf(look_offset.y, -0.8 if cockpit else -1.2, 0.8 if cockpit else 1.2)
	update_view(delta, true)
	if not error.is_empty():
		return
	terrain.tick(delta, camera.position, not paused and not save_dialog.visible)
	if not terrain.error.is_empty():
		pause_on_error(str(terrain.error))
		return
	# Placement and a complete cover swap happened above. Refresh before this
	# frame renders, including the first frame of newly admitted geometry.
	if not apply_camera_depth(camera.transform, minf(1000000000.0, maxf(100000.0, state.altitude * 8.0))): return
	if exhaust != null:
		exhaust.update_applied(state, delta, paused)


func apply_camera_depth(relation: Transform3D, minimum_far: float) -> bool:
	var distant: Vector2 = terrain.depth_range(relation, minimum_far)
	if distant == Vector2.ZERO:
		pause_on_error("Terrain camera depth is unavailable")
		return false
	var close_depth := Vector2(0.05, 200.0)
	if surface_walking():
		close_depth = surface_depth_range(state.surface_walk.eye_position, distant.y)
		if close_depth == Vector2.ZERO:
			pause_on_error("Surface camera depth is unavailable")
			return false
	# Far/500k can reach 2000m at the existing 1e9m cap. Extend only the close
	# far plane, with overlap for float projection rounding. Merely equal
	# configured planes can leave a gap in the actual Camera3D frustum.
	camera.near = close_depth.x
	camera.far = maxf(close_depth.y, distant.x * 1.01)
	terrain_camera.near = distant.x
	terrain_camera.far = distant.y
	camera.transform = relation
	terrain_camera.transform = relation
	return true


func update_view(delta: float, defer_exhaust: bool = false) -> void:
	if not valid_state(state) and error.is_empty():
		pause_on_error("Saved flight observation is invalid")
	if not error.is_empty():
		hide_exhaust()
		return
	var condition: Dictionary = state.get("impact", {"selected": false})
	if not impact_overlay.apply(condition, state.craft_id, state.frame_id, state.frame_version, cockpit, surface_walking()):
		pause_on_error("Saved craft condition is unavailable")
		return
	impact_status.visible = condition.get("selected", false) and not surface_walking()
	impact_status.text = ImpactView.condition_text(condition)
	refresh_impact_action()
	var owner_key := str(state.system_id) + ":" + str(state.planet_id)
	if owner_key != rendered_world:
		# C++ has already installed the actual world/stream. Retire old GPU
		# tiles before any of their same-shaped keys can alias the new planet.
		terrain.reset_world()
		selected_contact_id = ""
		if station != null:
			station.visible = false
			station.queue_free()
			station = null
		station_geometry = bridge.get_freedom_station_geometry()
		rendered_world = owner_key
	var boarding: Dictionary = state.get("boarding", {})
	if not boarding.is_empty():
		if not staged_model.set_pose(boarding.pose):
			pause_on_error("The Wayfarer operating pose changed unexpectedly")
			return
		if boarding.state == "seated":
			pilot_eye = Vector3(boarding.eye_craft[0], boarding.eye_craft[1], boarding.eye_craft[2])
	var surface: Dictionary = state.get("surface", {})
	if local_moon_button != null:
		local_moon_button.disabled = state.attached or surface.get("landed", false) or surface_walking() or state.get("jump", {}).get("phase", "idle") != "idle"
	if staged_model != null and not staged_model.set_gear_preview(1.0 if surface.get("gear_deployed", false) else 0.0):
		pause_on_error("The declared landing gear pose is unavailable")
		return
	if landing_button != null:
		var landed: bool = surface.get("landed", false)
		landing_button.text = "Land with thrusters" if state.assistance else "Deploy gear · manual landing"
		landing_button.disabled = state.attached or landed or state.get("jump", {}).get("phase", "idle") != "idle"
		if condition.get("selected", false) and not condition.landing_available:
			landing_button.disabled = true
			landing_button.tooltip_text = "Landing structure damaged · qualified landing unavailable"
		else:
			landing_button.tooltip_text = ""
		stow_gear_button.disabled = state.attached or landed or not surface.get("gear_deployed", false) or state.get("jump", {}).get("phase", "idle") != "idle"
		liftoff_button.disabled = not landed or surface_walking()
		if surface_walk_button != null:
			surface_walk_button.visible = landed and state.frame_id == "2"
			surface_walk_button.text = "Return through hatch" if surface_walking() else "Leave Wayfarer · suit"
			if surface_walk_menu_button != null:
				surface_walk_menu_button.disabled = surface_walk_button.disabled
				surface_walk_menu_button.text = surface_walk_button.text
			landing_button.disabled = landing_button.disabled or surface_walking()
			stow_gear_button.disabled = stow_gear_button.disabled or surface_walking()
		surface_status.text = ("Landed" if landed else "Airborne") + " · Gear " + ("deployed" if surface.get("gear_deployed", false) else "stowed") + "\n" + str(surface.get("note", "Surface aid off"))
	ship.basis = state.body_basis
	if cockpit_displays != null and not cockpit_displays.refresh(state, delta, paused):
		pause_on_error("The Wayfarer instrument readings are unavailable")
		return
	if station == null and state.get("station_available", true) and state.station_position.length() < 1000.0:
		station = StationPresentation.new()
		scene.add_child(station)
		if not station.initialize({"station_id": state.station_id}, station_geometry, assets_root, false):
			pause_on_error(station.error)
			return
		set_ship_layer(station)
	if station != null:
		station.transform = Transform3D(state.station_basis, state.station_position)
		station.visible = state.get("station_available", true) and state.station_position.length() < 1000.0
	var camera_relation := Transform3D.IDENTITY
	var look_basis := Basis(Vector3.UP, -look_offset.x) * Basis(Vector3.RIGHT, -look_offset.y)
	if surface_walking():
		var walking: Dictionary = state.surface_walk
		camera_relation = Transform3D(walking.basis * Basis(Vector3.UP, walking.heading) * Basis(Vector3.RIGHT, -look_offset.y), walking.eye_position)
	elif cockpit:
		camera_relation = Transform3D(state.body_basis * look_basis, state.body_basis * pilot_eye)
	else:
		# The overhead dock occupies the usual elevated chase viewpoint. Keep
		# the close-port camera below the craft, then ease back to its normal
		# height. Current separation alone makes approach/departure reversible
		# and pause/cadence independent, without the old16m jump at100m.
		var height := 9.0
		if not state.docking.is_empty():
			height = lerpf(-7.0, 9.0, smoothstep(100.0, 200.0, state.docking.separation))
		camera_relation = Transform3D(Basis.IDENTITY, state.body_basis * (look_basis * Vector3(17.0, height, 28.0))).looking_at(Vector3.ZERO, state.body_basis.y)
	if not apply_camera_depth(camera_relation, minf(1000000000.0, maxf(100000.0, state.altitude * 8.0))): return
	if player_input != null:
		player_input.surface_interaction_enabled = surface_walking()
		player_input.assist = state.assistance
		assist_button.set_pressed_no_signal(state.assistance)
		controls_menu.sync_saved_state(state, false)
		refresh_control_hint()
	light.basis = star_light_basis(state.star_direction)
	local_light.basis = light.basis
	if not NativeEnvironment.apply(state, planet_environment, light, sky_material, local_light):
		pause_on_error("Canonical native environment changed unexpectedly")
		return
	var optical_materials: Array[ShaderMaterial] = [sky_material]
	if not surface_appearance.select(state):
		pause_on_error("Canonical terrain appearance changed unexpectedly")
		return
	var surface_materials: Array[ShaderMaterial] = []
	for node: MeshInstance3D in terrain.nodes.values():
		if node.material_override is ShaderMaterial:
			optical_materials.append(node.material_override)
			surface_materials.append(node.material_override)
	if surface_appearance.changed and not surface_appearance.apply(surface_materials):
		pause_on_error("Canonical terrain appearance changed unexpectedly")
		return
	if not planet_optics.apply(state, optical_materials):
		pause_on_error("Canonical planetary optics changed unexpectedly")
		return
	if not surface_landmarks.apply(state, surface_materials):
		pause_on_error("Canonical local landmarks changed unexpectedly")
		return
	near_environment.ambient_light_color = planet_environment.ambient_light_color
	near_environment.ambient_light_energy = planet_environment.ambient_light_energy
	if exhaust != null and not defer_exhaust:
		exhaust.update_applied(state, delta, paused)
	telemetry.text = "APSIS DRIFT · %s\nReference altitude %.1f km · Surface speed %.1f m/s\nRadial rate %.1f m/s · Air %.5f kg/m³\nTick %s%s" % ["Wayfarer" if state.frame_id == "2" else "Legacy starter frame", state.altitude / 1000.0, state.surface_speed, state.radial_rate, state.air_density, state.tick, " · PAUSED" if paused else ""]
	var rated_rise := Status.rated_rise_text(state)
	if not rated_rise.is_empty(): telemetry.text += "\n" + rated_rise
	if surface_walking(): telemetry.text = "On foot · suit equipped\n%s\nWayfarer remains landed · %.1f m to craft centre\nTick %s%s" % [Status.surface_position_text(state.surface_walk), state.surface_walk.eye_position.length(), state.tick, " · PAUSED" if paused else ""]
	var resources: Dictionary = state.get("resources", {})
	if resources.get("selected", false):
		var burn := "No applied propulsion burn" if resources.equivalent_newtons == 0.0 else "Last applied burn %.1f kN equivalent" % (resources.equivalent_newtons / 1000.0)
		var notice := " · RESERVE" if resources.reserve else ""
		if resources.operationally_empty: notice = " · EMPTY"
		resource_status.text = "Flight fuel %.1f%%%s · Jump charges %d/3\n%.1f min at full main · %s" % [resources.fraction * 100.0, notice, resources.jump_charges, resources.main_equivalent_seconds / 60.0, burn]
		if resources.propulsion_refused:
			resource_status.text += "\nInsufficient fuel for requested tick · propulsion aids stopped"
		service_button.visible = state.attached
		service_button.disabled = not state.attached
	else:
		resource_status.text = "Historical save · resource recipe unselected"
		service_button.visible = false
	var chart: Dictionary = state.get("chart", {})
	chart_button.visible = chart.get("selected", false)
	chart_status.visible = chart_button.visible and chart_button.button_pressed
	if chart_button.visible:
		chart_status.text = "Starting chart · granted positions"
		if chart.get("local_sensors", false):
			chart_status.text += "\nShip sensors · %d recorded findings" % chart.observed_facts
		for row: Dictionary in chart.rows:
			var detail: String = " · current" if row.current else " · %.1f light-hours · %s" % [float(row.distance_light_seconds) / 3600.0, "1 charge available" if row.affordable else "No jump charges"]
			chart_status.text += "\n%s · %s%s" % [row.name, row.confidence.to_upper(), detail]
	else:
		chart_status.visible = false
	var jump: Dictionary = state.get("jump", {})
	var phase: String = jump.get("phase", "idle")
	assist_button.disabled = phase != "idle" or surface_walking()
	var rows: Array = chart.get("rows", [])
	for i in jump_destinations.size():
		var button: Button = jump_destinations[i]
		button.visible = i < rows.size() and chart_button.button_pressed
		if i < rows.size():
			button.text = "Select " + str(rows[i].name)
			button.disabled = not rows[i].get("selectable", false)
	jump_button.visible = chart.get("selected", false)
	jump_button.text = "Cancel spool · no jump charge" if phase == "spool" else "In transit" if phase == "transit" else "Spool jump · 1 charge"
	jump_button.disabled = phase == "transit" or (phase == "idle" and not jump.get("available", false))
	jump_button.tooltip_text = str(jump.get("refusal", "Select a destination first"))
	jump_status.text = "Jump drive · %s" % phase.capitalize()
	if phase != "idle": jump_status.text += " · %.1f s" % jump.get("remaining_seconds", 0.0)
	if jump.has("distance_light_hours"):
		jump_status.text += "\n%.1f light-hours · %s arrival envelope %.0f m\nHeading %.1f° · drift %.1f%%" % [jump.distance_light_hours, str(jump.quality), jump.envelope_radius_metres, jump.heading_error_degrees, jump.drift_percent]
	elif phase == "idle": jump_status.text += "\nSelect a chart destination"
	if phase == "idle" and not jump.get("available", false): jump_status.text += "\n" + str(jump.get("refusal", ""))
	orbital_forecast.text = orbit_text(state)
	var target: Vector3 = state.station_position
	home_marker.label_text = "HOME"
	home_marker.visible = state.get("station_available", true)
	home_cue.visible = home_marker.visible
	if home_marker.visible: home_marker.update_cue(camera, target, state.attached)
	var navigation: Dictionary = home_marker.cue
	var direction := " · behind you" if navigation.get("behind", false) else (" · follow HOME marker" if not navigation.get("in_view", false) else " · ahead")
	if navigation.is_empty(): direction = " · direction unavailable"
	var occluded := HomeNavigation.globe_occludes(camera.position, target, Vector3(0, -state.planet_radius - state.altitude, 0), state.planet_radius)
	home_cue.text = "Origin Station · %.1f km%s%s" % [target.length() / 1000.0, " · attached" if state.attached else direction, "\nBeyond horizon · direction only, not a safe route" if occluded and not state.attached else ""]
	home_cue.tooltip_text = "Station " + state.station_id + " · HOME marks its projected direction. It does not steer or certify a route."
	var near_port: bool = state.get("station_available", true) and (state.attached or target.length() < 1000.0)
	if dock_status != null:
		var changed: bool = dock_status.visible != near_port
		for item in [port_row, dock_status, approach_button, capture_button, release_button]: item.visible = near_port
		if changed: call_deferred("layout_hud")
	if dock_status != null:
		var assessment: Dictionary = state.docking
		dock_status.text = "Select a port for approach"
		if state.attached:
			dock_status.text = "Attached to D%d · station co-motion" % state.target_port
		elif not assessment.is_empty():
			var offset: PackedFloat64Array = assessment.offset_body_metres
			dock_status.text = "D%d · %s\nCollar %.3f m · attitude %.2f°\nInward %.3f m/s · lateral %.3f m/s\nPort offset: right %.2f · up %.2f · back %.2f m" % [state.target_port, assessment.reason, assessment.separation, rad_to_deg(assessment.alignment_radians), assessment.inward_speed, assessment.lateral_speed, offset[0], offset[1], offset[2]]
		var approach: Dictionary = state.get("port_approach", {})
		dock_status.text += "\n" + str(approach.get("note", "Approach aid off"))
		if approach.get("active", false):
			dock_status.text += " · %.1f s left" % approach.remaining_seconds
		approach_button.text = "Cancel approach aid" if approach.get("active", false) else "Approach port with thrusters"
		approach_button.disabled = not approach.get("active", false) and not approach.get("available", false)
		approach_button.tooltip_text = "Manual input cancels. Capture remains your action." if approach.get("available", false) else str(approach.get("refusal", "Select a port first."))
		capture_button.disabled = state.attached or surface.get("gear_deployed", false) or assessment.is_empty() or not assessment.get("ready", false)
		release_button.disabled = not state.attached or (not boarding.is_empty() and boarding.state != "seated")
		unboard_button.visible = state.attached and state.target_port == 1 and boarding.get("state") == "seated"
		if unboard_menu_button != null:
			unboard_menu_button.visible = unboard_button.visible
		for button in port_buttons:
			button.disabled = state.attached or not state.get("station_available", true) or phase != "idle"
	# Ground exploration keeps the return/suit controls clear of flight telemetry.
	var outside := surface_walking()
	refresh_suit_lamp()
	ship_marker.visible = outside
	if outside:
		var access: Dictionary = state.surface_walk.return_access
		# One marker avoids overlapping labels. SHIP is only a centre fallback;
		# RETURN identifies the existing ground transfer, not an art hatch.
		ship_marker.label_text = "RETURN" if access.available else "SHIP"
		ship_marker.update_cue(camera, access.position if access.available else Vector3.ZERO, false)
	orbital_forecast.visible = not outside
	resource_status.visible = not outside
	assist_button.visible = not outside
	for item in [landing_button, stow_gear_button, liftoff_button, surface_status]:
		if item != null: item.visible = not outside
	if outside:
		for item in [chart_button, chart_status, jump_status, jump_button, home_cue, home_marker]: item.visible = false
		for button in jump_destinations: button.visible = false
		if controls_menu.saved_note != null:
			controls_menu.saved_note.text = "ON FOOT · SUIT EQUIPPED\n%s / left stick: walk\nMouse / look keys / right stick: look\nSuit lamp: toggle in paused controls; release controls and Resume explicitly.\nRETURN marks the actual ground return access. If unavailable, SHIP points only to the craft centre.\nApproach RETURN until within reach, then select Return through hatch. The cue does not plan a safe terrain route.\nSave As retains your ground position and the landed ship." % player_input.foot_keyboard_label()
	refresh_surface_marker()
	refresh_surface_landmarks()
	refresh_distant_contacts()
	update_compact_hud()
