extends Control
signal journey_mode_changed
signal catalog_save_requested(save_as: bool)
signal load_requested
signal title_requested
signal recovery_requested
var journey_dialog_open := false
## Ordinary saved-flight consumer. C++ owns time, state, terrain and propulsion.
const MoonState = preload("res://scripts/native/native_moon_state.gd")
const MinorState = preload("res://scripts/native/native_minor_state.gd")
const PlanetStreamView = preload("res://scripts/world/planet_stream.gd")
const MainExhaust = preload("res://scripts/native/native_main_exhaust.gd")
const HopperPresentation = preload("res://scripts/characters/hopper_presentation.gd")
const StationPresentation = preload("res://scripts/native/native_station_view.gd")
const WAYFARER_HASH = "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8"
const WAYFARER_DESCRIPTOR_HASH = "17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7"
const HomeNavigation = preload("res://scripts/native/home_navigation.gd")
const SurfaceMarker = preload("res://scripts/native/native_surface_marker.gd")
const PlayerInput = preload("res://scripts/ui/player_input.gd")
const MouseLook = preload("res://scripts/native/native_mouse_look.gd")
var mouse_look := MouseLook.new()
const FlightMouse = preload("res://scripts/native/native_flight_mouse.gd")
var flight_mouse: Control
var mouse_free_look := false
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const CockpitDisplays = preload("res://scripts/native/native_cockpit_displays.gd")
var cockpit_displays: Node3D
const CockpitControl = preload("res://scripts/native/native_cockpit_control.gd")
var cockpit_control: Node3D
var cockpit_mouse_held := false
const DistantContacts = preload("res://scripts/native/native_distant_contacts.gd")
const PlanetOptics = preload("res://scripts/native/native_planet_optics.gd")
var planet_optics := PlanetOptics.new()
const SurfaceAppearance = preload("res://scripts/native/native_surface_appearance.gd")
var surface_appearance := SurfaceAppearance.new()
const SurfaceLandmarks = preload("res://scripts/native/native_surface_landmarks.gd")
var surface_landmarks := SurfaceLandmarks.new()
const PauseMenu = preload("res://scripts/ui/pause_menu.gd")
const Status = preload("res://scripts/native/native_status.gd")
const OriginRepair = preload("res://scripts/native/origin_repair.gd")
var repair_action_frame := -1
const SurveyCommissions = preload("res://scripts/native/survey_commissions.gd")
const PaidOperations = preload("res://scripts/native/paid_operations.gd")
const SpecimenSale = preload("res://scripts/native/specimen_sale.gd")
const RecoverySale = preload("res://scripts/native/derelict_recovery_sale.gd")
const DerelictRecovery = preload("res://scripts/native/derelict_recovery.gd")
const DerelictMotion = preload("res://scripts/native/derelict_motion.gd")
const ChartedArrivals = preload("res://scripts/native/charted_arrivals.gd")
const QuietTravel = preload("res://scripts/native/quiet_travel.gd")
const ReturnChargeCheck = preload("res://scripts/native/native_return_charge_check.gd")
const KnownChartMap = preload("res://scripts/native/known_chart_map.gd")
var known_chart_map
var known_chart_map_legend: RichTextLabel
var return_charge_status: RichTextLabel
var quiet_travel_select: OptionButton
var quiet_travel_status: RichTextLabel
var quiet_travel_packet: Dictionary = {}
var quiet_travel_action_frame := -1
var commission_action_frame := -1
var charted_arrival_action_frame := -1
const OrbitalSurvey = preload("res://scripts/native/native_orbital_survey.gd")
const StarSearch = preload("res://scripts/native/native_star_search.gd")
const DerelictView = preload("res://scripts/native/native_derelict_view.gd")
var derelict_view: Node3D
var derelict_marker: Control
var derelict_scan_button: Button
var derelict_inspect_button: Button
var derelict_motion_note: Label
var derelict_recovery_button: Button
var derelict_recovery_cancel_button: Button
const FrameDiagnostics = preload("res://scripts/native/native_frame_diagnostics.gd")
var frame_diagnostics: PanelContainer
var frame_diagnostics_button: Button
var initial_frame_diagnostics := false
var survey_start_button: Button
var survey_cancel_button: Button
var star_search_start_button: Button
var star_search_cancel_button: Button
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
var local_minor_button: Button
const ServiceCraft = preload("res://scripts/native/native_service_craft_view.gd")
var service_craft: Node3D
var service_binding: Dictionary = {}
var service_status: RichTextLabel
var service_request: Button
var service_cancel: Button
var service_query_elapsed := 0.25
var service_availability: Dictionary = {}
var bridge: Variant
var camera: Camera3D
var terrain_camera: Camera3D
var near_viewport: SubViewport
var far_viewport: SubViewport
const ViewportSize = preload("res://scripts/native/native_viewport_size.gd")
var far_container: SubViewportContainer
var near_container: SubViewportContainer
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
var hud_tracking_mode := "home"
var home_tracking_button: Button
var port_tracking_button: Button
var clear_tracking_button: Button
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
var docking_button: Button
var rendezvous_button: Button
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
var charted_arrival_button: CheckButton
var charted_arrival_status: RichTextLabel
var charted_arrival_packet: Dictionary = {}
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
var sample_collect_button: Button
var sample_pouch_button: Button
var sample_discard_buttons: Array[Button] = []
var sample_packet: Dictionary = {}
var sample_collection_id := ""
var exposure_collection_frame := -1
var planet_route_label: Label
var planet_route_buttons: Array[Button] = []
var planet_route_packet: Dictionary = {}
var planet_jump_button: Button


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


static func valid_commission_progress(value: Variant) -> bool:
	if not value is Dictionary: return false
	if value.has("version"):
		if value.size() != 7 or not value.get("version") is int or value.version != 2 or not value.get("kind") is int or value.kind != 2: return false
		for field in ["active", "ready", "expired"]:
			if not value.get(field) is bool: return false
		if not value.active or not value.get("remaining_ticks") is int or value.remaining_ticks < 0 or value.remaining_ticks > 1728000: return false
		if not NativeEnvironment.decimal(value.get("earnings")) or SurveyCommissions.decimal_before("429496729500", value.earnings) or int(value.earnings) % 100 != 0: return false
		return value.expired == (value.remaining_ticks == 0) and not (value.expired and value.ready)
	if value.size() != 7: return false
	if not value.get("status") is String: return false
	var status: String = value.status
	if status.to_utf8_buffer().size() > 256: return false
	for index in status.length():
		var code: int = status.unicode_at(index)
		if code < 32 or (code >= 127 and code <= 159): return false
	if not value.get("acquired_ticks") is int or value.acquired_ticks < 0 or value.acquired_ticks > 3600: return false
	for key in ["active", "completed", "expired"]:
		if not value.get(key) is bool: return false
	if not NativeEnvironment.decimal(value.get("earnings")) or SurveyCommissions.decimal_before("429496729500", value.earnings): return false
	if int(value.earnings) % 100 != 0 or value.completed != (value.acquired_ticks == 3600): return false
	if not value.get("remaining_ticks") is int or value.remaining_ticks < 0 or value.remaining_ticks > 1728000: return false
	if value.expired != (value.active and value.remaining_ticks == 0): return false
	if status.is_empty() != (not value.active or value.completed or value.expired): return false
	return value.active or (value.acquired_ticks == 0 and not value.completed and not value.expired and value.remaining_ticks == 0)


static func commission_progress_text(value: Variant) -> String:
	if not valid_commission_progress(value) or not value.active: return ""
	if value.expired: return "Commission · deadline passed"
	var work: String
	if value.get("version") == 2:
		work = "fresh specimen carried · deliver at Origin" if value.ready else "collect a fresh specimen on the accepted planet"
	else:
		work = "report ready" if value.completed else "%.1f / 30 s" % (floorf(float(value.acquired_ticks) / 12.0) / 10.0)
	var remaining := "less than 1 s" if value.remaining_ticks < 120 else SurveyCommissions.seconds_text(floori(float(value.remaining_ticks) / 120.0))
	var line: String = "Commission · %s · %s left" % [work, remaining]
	return line if value.get("version") == 2 or value.completed else line + "\n" + value.status


static func valid_state(value: Dictionary) -> bool:
	if value.has("quiet_travel") and not QuietTravel.valid_packet(value.quiet_travel): return false
	if value.has("orbital_survey") and not OrbitalSurvey.valid(value.orbital_survey): return false
	if value.has("survey_commission") and not valid_commission_progress(value.survey_commission): return false
	if value.has("star_search") and not StarSearch.valid(value.star_search): return false
	if value.has("derelict") and not DerelictView.valid_packet(value.derelict): return false
	if value.has("derelict_motion") and not DerelictMotion.valid_state(value): return false
	if value.has("derelict_recovery") and not DerelictRecovery.valid_packet(value.derelict_recovery): return false
	if value.get("body_kind") == "moon": return MoonState.valid(value)
	if value.get("body_kind") == "minor": return MinorState.valid(value)
	if value.has("port_docking"):
		var docking_aid: Variant = value.port_docking
		if not docking_aid is Dictionary or docking_aid.size() != 6: return false
		if not docking_aid.get("available") is bool or not docking_aid.get("active") is bool: return false
		if not docking_aid.get("remaining_seconds") is float or not is_finite(docking_aid.remaining_seconds) or docking_aid.remaining_seconds < 0.0 or docking_aid.remaining_seconds > 180.0: return false
		if not docking_aid.get("phase") in ["off", "aligning", "closing", "complete"]: return false
		for key in ["refusal", "note"]:
			if not docking_aid.get(key) is String or docking_aid[key].length() > 1024: return false
	if value.has("port_rendezvous"):
		var rendezvous: Variant = value.port_rendezvous
		if not rendezvous is Dictionary or rendezvous.size() != 5: return false
		if not rendezvous.get("available") is bool or not rendezvous.get("active") is bool: return false
		if not rendezvous.get("remaining_seconds") is float or not is_finite(rendezvous.remaining_seconds) or rendezvous.remaining_seconds < 0.0 or rendezvous.remaining_seconds > 2400.0: return false
		for key in ["refusal", "note"]:
			if not rendezvous.get(key) is String or rendezvous[key].length() > 1024: return false
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
	return SurfaceMarker.valid(value)


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
	if flight.get("frame_id") == "2": pending["craft_binding"] = owner.get_freedom_craft_binding()
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
	service_binding = pending.get("craft_binding", {}).duplicate(true)
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
	far_container = container
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
		if not setup_cockpit_control(): return false
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
	near_container = SubViewportContainer.new()
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
	if not refresh_service_craft(state, 0.0):
		error = "The service Wayfarer presentation is unavailable"
		return false
	if not refresh_derelict(state):
		error = "The derelict Wayfarer presentation is unavailable"
		return false
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


func setup_flight_mouse() -> void:
	flight_mouse = FlightMouse.new()
	add_child(flight_mouse)
	flight_mouse.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	flight_mouse.hide()


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


func build_origin_service_ui(column: VBoxContainer) -> void:
	if not state.has("origin_service"): return
	service_status = hud_text()
	column.add_child(service_status)


func refresh_derelict(flight: Dictionary) -> bool:
	if not flight.has("derelict"):
		if derelict_view != null: derelict_view.hide()
		return true
	var packet: Dictionary = flight.derelict
	if not DerelictView.valid_packet(packet):
		if derelict_view != null: derelict_view.hide()
		return false
	if derelict_view == null:
		if not packet.local or packet.distance_metres > terrain_camera.far: return true
		derelict_view = DerelictView.new()
		if not derelict_view.initialize(assets_root, service_binding, packet.id):
			derelict_view.free()
			derelict_view = null
			return false
		scene.add_child(derelict_view)
	return derelict_view.present(packet, paused, camera.far, terrain_camera.far)


func setup_origin_service_controls(column: Node) -> void:
	if not state.has("origin_service"): return
	service_request = controls_menu.add_button(column, "Request fuel from Origin", origin_service_command)
	service_cancel = controls_menu.add_button(column, "Cancel fuel request · return home", func(): origin_service_command(true))


func origin_service_command(cancel := false) -> void:
	if not activated or mode_change_pending or not marker_controls_available(): return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement and look controls before changing the fuel request.")
		return
	var command := "cancel_freedom_origin_assistance" if cancel else "request_freedom_origin_assistance"
	if not bridge.call(command):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	service_query_elapsed = 0.25
	update_view(0.0)
	report_action_status("Return requested · Resume to advance the service ship." if cancel else "Fuel request accepted · Resume to advance the service ship.")


func refresh_origin_service_ui(delta: float) -> void:
	if service_status == null or service_request == null or service_cancel == null: return
	var service: Dictionary = state.get("origin_service", {})
	if service.is_empty():
		service_status.hide()
		service_request.hide()
		service_cancel.hide()
		return
	service_query_elapsed += delta
	if activated and service_query_elapsed >= 0.25:
		service_query_elapsed = 0.0
		service_availability = bridge.get_freedom_origin_assistance_state()
	service_status.text = "Origin fuel service · %s\nCargo remaining %.1f%% · service jump charges %d" % [service.phase, service.cargo_fraction * 100.0, service.jump_charges]
	service_request.disabled = not service_availability.get("request_available", false)
	service_request.tooltip_text = service_availability.get("refusal", "Resume to check service availability")
	service_cancel.disabled = not service.get("cancel_available", false)


func build_ui() -> void:
	setup_flight_mouse()
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
	hud_scroll.follow_focus = true
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
	derelict_marker = HomeNavigation.new()
	derelict_marker.label_text = "DERELICT"
	derelict_marker.visible = false
	navigation_layer.add_child(derelict_marker)
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
	build_origin_service_ui(column)
	impact_status = hud_text()
	impact_status.visible = false
	column.add_child(impact_status)
	quiet_travel_select = OptionButton.new()
	quiet_travel_select.focus_mode = Control.FOCUS_ALL
	quiet_travel_select.add_item("Normal", 1)
	quiet_travel_select.add_item("Up to 2×", 2)
	quiet_travel_select.add_item("Up to 4×", 4)
	quiet_travel_select.tooltip_text = "Quiet pilot controls only. Assistance, hold, fuel and deadlines continue normally. Actual acceleration depends on frame rate; thrust or steering returns to Normal."
	quiet_travel_select.item_selected.connect(func(index: int): quiet_travel_command(quiet_travel_select.get_item_id(index)))
	quiet_travel_select.get_popup().window_input.connect(quiet_travel_popup_input)
	column.add_child(quiet_travel_select)
	quiet_travel_status = hud_text()
	column.add_child(quiet_travel_status)
	chart_button = CheckButton.new()
	chart_button.text = "Starting chart"
	chart_button.focus_mode = Control.FOCUS_ALL
	chart_button.toggled.connect(func(shown: bool):
		chart_status.visible = shown
		refresh_known_chart_map()
		refresh_charted_arrivals())
	column.add_child(chart_button)
	chart_status = hud_text()
	chart_status.visible = false
	column.add_child(chart_status)
	known_chart_map = KnownChartMap.new()
	known_chart_map.visible = false
	column.add_child(known_chart_map)
	known_chart_map_legend = hud_text()
	known_chart_map_legend.visible = false
	column.add_child(known_chart_map_legend)
	charted_arrival_button = CheckButton.new()
	charted_arrival_button.text = "Closer charted arrivals"
	charted_arrival_button.focus_mode = Control.FOCUS_ALL
	charted_arrival_button.tooltip_text = "Choose a closer arrival when the primary planet is charted. Arrival keeps your velocity; orbit and landing remain manual."
	charted_arrival_button.toggled.connect(set_charted_arrivals)
	column.add_child(charted_arrival_button)
	charted_arrival_status = hud_text()
	column.add_child(charted_arrival_status)
	for ordinal in 8:
		var destination := Button.new()
		destination.focus_mode = Control.FOCUS_ALL
		destination.pressed.connect(func(): select_jump_destination(ordinal))
		column.add_child(destination)
		jump_destinations.append(destination)
	jump_status = hud_text()
	column.add_child(jump_status)
	return_charge_status = hud_text()
	column.add_child(return_charge_status)
	jump_button = Button.new()
	jump_button.focus_mode = Control.FOCUS_ALL
	jump_button.pressed.connect(jump_requested)
	column.add_child(jump_button)
	service_button = Button.new()
	service_button.text = "Replenish flight fuel and jump charges · free"
	service_button.focus_mode = Control.FOCUS_ALL
	service_button.pressed.connect(func(): port_command("replenish_freedom_resources"))
	refresh_paid_service_label()
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
		docking_button = Button.new()
		docking_button.text = "Dock with thrusters"
		docking_button.focus_mode = Control.FOCUS_NONE
		docking_button.pressed.connect(func(): port_command("cancel_freedom_port_docking" if state.get("port_docking", {}).get("active", false) else "begin_freedom_port_docking"))
		column.add_child(docking_button)
		approach_button = Button.new()
		approach_button.text = "Approach port with thrusters"
		approach_button.focus_mode = Control.FOCUS_NONE
		approach_button.pressed.connect(func(): port_command("cancel_freedom_port_approach" if state.get("port_approach", {}).get("active", false) else "begin_freedom_port_approach"))
		column.add_child(approach_button)
		rendezvous_button = Button.new()
		rendezvous_button.text = "Rendezvous with Origin Station"
		rendezvous_button.focus_mode = Control.FOCUS_NONE
		rendezvous_button.pressed.connect(func(): port_command("cancel_freedom_port_rendezvous" if state.get("port_rendezvous", {}).get("active", false) else "begin_freedom_port_rendezvous"))
		column.add_child(rendezvous_button)
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
		local_minor_button = Button.new()
		local_minor_button.text = "Select asteroid inspection jump"
		local_minor_button.focus_mode = Control.FOCUS_NONE
		local_minor_button.pressed.connect(select_minor_jump)
		column.add_child(local_minor_button)
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
		if surface_walking() or landed_hatch_available():
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
		var mouse_held: bool = flight_mouse != null and flight_mouse.offset != Vector2.ZERO
		clear_flight_mouse()
		if not controls_armed or mouse_held:
			pause_controls("Window resized. Release controls, then resume explicitly.")
	layout_hud()


func layout_hud() -> void:
	if not activated or not is_inside_tree() or hud_scroll == null:
		return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or pixels.x <= 0 or pixels.y <= 0 or not size.is_finite() or size.x <= 0 or size.y <= 0:
		return
	var density := get_viewport().get_final_transform().get_scale().abs()
	ViewportSize.fit(far_container, size, density)
	ViewportSize.fit(near_container, size, density)
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	var margin := minf(16 * scale, minf(size.x, size.y) * 0.1)
	var usable := size - Vector2.ONE * (2 * margin)
	if usable.x <= 0 or usable.y <= 0:
		return
	hud_theme.default_font_size = roundi(18 * scale)
	if known_chart_map != null: known_chart_map.set_ui_scale(scale)
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
	if commissions_open() or origin_repair_open(): return
	pause_controls("Flight instruments and navigation.", false)
	update_view(0.0)
	if controls_menu != null: controls_menu.hide_menu()
	instruments_open = true
	layout_hud()
	refresh_charted_arrivals()
	refresh_return_charge_check()
	refresh_known_chart_map()
	refresh_quiet_travel()
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
	var compact_pixels: Vector2 = Vector2(get_window().size)
	var compact_bounds: Rect2 = Status.layout_bounds(size, compact_pixels)
	var margin: float = compact_bounds.position.x
	var show_acquisition_status: bool = false
	if compact_pixels.is_finite() and compact_pixels.x > 0.0 and compact_pixels.y > 0.0 and size.is_finite() and size.x > 0.0 and size.y > 0.0:
		var compact_scale: float = maxf(1.0, maxf(size.x / compact_pixels.x, size.y / compact_pixels.y))
		var acquisition: Variant = state.get("survey_commission")
		show_acquisition_status = not detailed and valid_commission_progress(acquisition) and not acquisition.has("version") and acquisition.active and not acquisition.completed and not acquisition.expired
		# Width depends only on the current mode/state, never deferred text
		# height. Restore the original compact width as soon as work is ready.
		primary_status.size.x = minf(560.0 * compact_scale, maxf(0.0, size.x - 2.0 * margin)) if show_acquisition_status else compact_bounds.size.x
	instruments_button.position.x = size.x - instruments_button.size.x - margin if detailed and not hud_stacked else margin
	# Quiet refreshes absorb command/load changes without inventing a finding.
	observation_cue.observe(state, false, 0.0)
	surface_aid_cue.observe(state, false, 0.0)
	primary_status.text = Status.summary(state, player_input.foot_keyboard_label() if player_input != null else "WASD") if error.is_empty() else "Flight paused\n" + error
	if show_acquisition_status:
		# Preserve both telemetry values on one row; the full service result,
		# acquisition reason and unchanged 18 px body font still need room.
		primary_status.text = primary_status.text.replace("\nFlight assistance on\nFuel ", "\nFlight assistance on · Fuel ")
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
	if state.has("survey_commission") and state.survey_commission.active:
		var work: Dictionary = state.survey_commission
		var commission_text := commission_progress_text(work)
		primary_status.text += "\n" + commission_text
		primary_status.tooltip_text += "\n" + commission_text
		var geological: bool = work.get("version", 1) == 2
		var ready: bool = work.ready if geological else work.completed
		if ready and not work.expired:
			var action: String = "deliver the named specimen" if geological else "submit"
			primary_status.tooltip_text += "\nReturn to Origin and %s before the deadline." % action
	var quiet_text := QuietTravel.text(state.get("quiet_travel"))
	if not quiet_text.is_empty():
		primary_status.text += "\n" + quiet_text
		primary_status.tooltip_text += "\n" + quiet_text + " · Assistance, hold, fuel and deadlines continue normally. Actual acceleration depends on frame rate."
	var survey_text := OrbitalSurvey.text(state.get("orbital_survey"))
	if error.is_empty() and not surface_walking() and not observation_cue.text().is_empty():
		# The completed survey already names the retained result and Records.
		# Preserve the actual observation notice in the tooltip without stacking
		# its duplicate destination over the compact completion line.
		if survey_text.is_empty() or not state.orbital_survey.completed:
			primary_status.text += "\n" + observation_cue.text()
		primary_status.tooltip_text += "\n" + observation_cue.text()
	if error.is_empty() and not survey_text.is_empty():
		primary_status.tooltip_text += "\n" + survey_text
		# Command/refusal and maneuver outcomes retain their existing priority.
		primary_status.text += "\n" + OrbitalSurvey.compact_text(state.orbital_survey, not observation_cue.text().is_empty())
		primary_status.tooltip_text += "\n" + str(state.orbital_survey.note)
	var search_text := StarSearch.text(state.get("star_search"))
	if error.is_empty() and not search_text.is_empty():
		primary_status.text += "\n" + search_text
		primary_status.tooltip_text += "\n" + search_text + "\n" + str(state.star_search.note)
	var derelict_text := DerelictView.text(state.get("derelict"))
	if error.is_empty() and not derelict_text.is_empty():
		var motion_text := DerelictMotion.hud_text(state)
		if motion_text.is_empty():
			primary_status.text += "\n" + derelict_text
		else:
			# Keep approach cues near the top, ahead of optional survey/rumor lines.
			var lines := primary_status.text.split("\n")
			lines.insert(1, motion_text)
			primary_status.text = "\n".join(lines)
		primary_status.tooltip_text += "\n" + derelict_text + "\n" + str(state.derelict.note)
		if not motion_text.is_empty(): primary_status.tooltip_text += "\n" + motion_text + "\n" + str(state.derelict_motion.note)
		if not state.derelict.history.is_empty(): primary_status.tooltip_text += "\n" + state.derelict.history
	var recovery_text := DerelictRecovery.text(state.get("derelict_recovery"))
	if error.is_empty() and not recovery_text.is_empty():
		primary_status.text += "\n" + recovery_text
		primary_status.tooltip_text += "\n" + recovery_text + "\n" + str(state.derelict_recovery.note)
	var personal: Dictionary = state.get("surface_marker", {})
	if error.is_empty() and personal.get("present", false):
		var marker_text := ""
		if show_personal_marker and personal.current_body:
			marker_text = "MARK · %.1f m · straight-line distance" % personal.distance_metres
		elif not personal.current_body:
			marker_text = "MARK retained · %s / %s · another body" % [personal.system_name, SurfaceMarker.body_name(personal)]
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
		primary_status.tooltip_text += "\n" + contact_text + "\nCONTACT is a direction annotation; supported local trips use separate drive controls."
	var docking_aid: Dictionary = state.get("port_docking", {})
	if error.is_empty() and not docking_aid.is_empty() and str(docking_aid.get("note", "Docking off")) != "Docking off":
		primary_status.text += "\n" + str(docking_aid.note)
		if docking_aid.get("active", false): primary_status.text += " · %.0f s left" % docking_aid.remaining_seconds
	if error.is_empty() and not surface_walking() and player_input != null:
		var steering_hint := "Free look · release right mouse to steer" if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) else "Mouse steers · right mouse looks · %s centers" % player_input.binding_label("recenter", "key")
		primary_status.tooltip_text += "\n" + steering_hint
		var compact_steering := "RMB looks · release to steer" if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) else "Mouse steers · RMB looks"
		if survey_text.is_empty():
			primary_status.text += "\n" + (compact_steering if state.get("orbital_survey", {}).get("selected", false) else steering_hint)
	if cockpit_control_aimed():
		primary_status.text += "\n" + surface_interaction_binding() + " · " + ("Stow gear" if state.get("surface", {}).get("gear_deployed", false) else "Deploy gear · manual")
	context_source = null
	if surface_walking():
		context_source = landmark_inspect_button if not exposure.is_empty() and exposure.can_inspect and not state.surface_walk.return_access.get("within_reach", false) else surface_walk_button
	elif state.get("surface", {}).get("landed", false): context_source = surface_walk_button if landed_hatch_available() else liftoff_button
	elif state.get("attached", false): context_source = release_button
	elif docking_button != null and not docking_button.disabled and docking_button.visible: context_source = docking_button
	elif capture_button != null and not capture_button.disabled and capture_button.visible: context_source = capture_button
	elif approach_button != null and not approach_button.disabled and approach_button.visible: context_source = approach_button
	elif state.get("surface", {}).get("maneuver", "off") != "off": context_source = null
	elif landing_button != null and not landing_button.disabled and state.get("surface", {}).get("gear_deployed", false): context_source = landing_button
	elif jump_button != null and not jump_button.disabled and jump_button.visible: context_source = jump_button
	context_action.visible = not detailed and context_source != null
	if context_source != null:
		context_action.text = context_source.text
		if surface_walking() or landed_hatch_available(): context_action.text += " · " + surface_interaction_binding()
		elif player_input != null and player_input.last_device != "pad": context_action.text += " · left click"
		context_action.tooltip_text = context_source.tooltip_text
		context_action.disabled = context_source.disabled or paused or not focused or not error.is_empty()
	var commission: Variant = state.get("survey_commission")
	if valid_commission_progress(commission) and not commission.has("version"):
		# Let the extra wrapped status line use available space, while retaining
		# the existing lower action/Controls area and actual physical font size.
		var pixels: Vector2 = Vector2(get_window().size)
		if pixels.is_finite() and pixels.x > 0.0 and pixels.y > 0.0 and size.is_finite() and size.x > 0.0 and size.y > 0.0:
			var scale: float = maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
			var available: float = maxf(0.0, context_action.position.y - primary_status.position.y - 8.0 * scale)
			var base_height: float = Status.layout_bounds(size, pixels).size.y
			var content_height: float = float(primary_status.get_content_height()) if commission.active else base_height
			primary_status.size.y = minf(available, maxf(base_height, content_height))


func setup_cockpit_control() -> bool:
	if state.get("body_kind", "planet") == "minor": return true
	cockpit_control = CockpitControl.new()
	ship.add_child(cockpit_control)
	if not cockpit_control.bind_screens(staged_model.specification.screens):
		error = "The current cockpit gear control datum is unavailable"
		return false
	return true


func cockpit_control_present() -> bool:
	var boarding: Dictionary = state.get("boarding", {})
	return cockpit_control != null and cockpit and state.get("frame_id") == "2" and state.get("body_kind", "planet") in ["planet", "moon"] and (boarding.get("state") == "seated" or boarding.get("seated", false)) and not surface_walking()


func refresh_cockpit_control() -> void:
	if cockpit_control == null: return
	var active: bool = activated and focused and not paused and controls_armed and error.is_empty() and not journey_dialog_open and not recovery_notified and not mode_change_pending and save_dialog != null and not save_dialog.visible and player_input != null and player_input.enabled and not player_input.needs_neutral and player_input.waiting_action.is_empty()
	cockpit_control.present(state.get("surface", {}).get("gear_deployed", false), cockpit_control_present(), active, camera)
	if player_input != null: player_input.cockpit_interaction_enabled = cockpit_control.aimed


func clear_cockpit_control() -> void:
	cockpit_mouse_held = Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT)
	if cockpit_control != null: cockpit_control.clear()
	if player_input != null: player_input.cockpit_interaction_enabled = false


func cockpit_control_aimed() -> bool:
	return cockpit_control != null and cockpit_control.aimed


func request_cockpit_gear() -> void:
	refresh_cockpit_control()
	if not cockpit_control_aimed(): return
	if not player_input.cockpit_interaction_neutral() or (flight_mouse != null and flight_mouse.offset != Vector2.ZERO):
		report_action_status("Release flight controls before using the gear switch.")
		return
	var deployed: bool = not state.get("surface", {}).get("gear_deployed", false)
	if not bridge.set_freedom_landing_gear(deployed):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	report_action_status("Gear deployed · manual descent" if deployed else "Gear stowed")


func setup_controls() -> void:
	player_input = PlayerInput.new()
	player_input.initial_device = initial_controller_device
	player_input.persist = persist_controls
	player_input.settings_path = control_settings_path
	player_input.thrust_mode = true
	player_input.assistance_request_only = true
	player_input.surface_interaction_enabled = surface_walking()
	player_input.landed_hatch_enabled = landed_hatch_available()
	player_input.assist = state.assistance
	add_child(player_input)
	player_input.set_enabled(false)
	controls_menu = PauseMenu.new()
	controls_menu.controls = player_input
	controls_menu.saved_flight = true
	controls_menu.observations_query = Callable(bridge, "get_freedom_observations")
	controls_menu.exploration_notes_query = func(): return preload("res://scripts/native/exploration_notes.gd").read_bridge(bridge)
	controls_menu.exploration_marker_command = mark_recorded_sample_site
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
	controls_menu.origin_repair_button = controls_menu.add_button(survey_column, "Origin craft repair", open_origin_repair)
	controls_menu.origin_repair_view.action_requested.connect(origin_repair_command)
	controls_menu.commissions_button = controls_menu.add_button(survey_column, "Survey commissions", open_survey_commissions)
	controls_menu.commissions_view.action_requested.connect(survey_commission_command)
	controls_menu.commissions_view.paid_open_requested.connect(open_paid_operations)
	controls_menu.commissions_view.paid_action_requested.connect(paid_operations_command)
	controls_menu.commissions_view.sale_open_requested.connect(open_specimen_sale)
	controls_menu.commissions_view.sale_action_requested.connect(specimen_sale_command)
	controls_menu.commissions_view.recovery_sale_open_requested.connect(open_derelict_recovery_sale)
	controls_menu.commissions_view.recovery_sale_action_requested.connect(derelict_recovery_sale_command)
	setup_frame_diagnostics(survey_column)
	setup_origin_service_controls(survey_column)
	survey_start_button = controls_menu.add_button(survey_column, "Start orbital terrain survey", orbital_survey_command)
	survey_cancel_button = controls_menu.add_button(survey_column, "Stop orbital survey", orbital_survey_command.bind(true))
	star_search_start_button = controls_menu.add_button(survey_column, "Search nearby stars", star_search_command)
	star_search_cancel_button = controls_menu.add_button(survey_column, "Stop star search", star_search_command.bind(true))
	refresh_star_search()
	derelict_scan_button = controls_menu.add_button(survey_column, "Scan local space", derelict_command)
	derelict_inspect_button = controls_menu.add_button(survey_column, "Inspect derelict exterior", derelict_command.bind(true))
	derelict_motion_note = Label.new()
	derelict_motion_note.mouse_filter = Control.MOUSE_FILTER_IGNORE
	derelict_motion_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	survey_column.add_child(derelict_motion_note)
	derelict_recovery_button = controls_menu.add_button(survey_column, "Retrieve sealed canister · 30 seconds", derelict_recovery_command)
	derelict_recovery_cancel_button = controls_menu.add_button(survey_column, "Cancel canister retrieval", derelict_recovery_command.bind(true))
	refresh_derelict_controls()
	surface_conditions_button = controls_menu.add_button(survey_column, "Surface conditions", show_surface_conditions)
	controls_menu.add_button(survey_column, "Flight instruments & navigation", show_instruments)
	home_tracking_button = controls_menu.add_button(survey_column, "Track Origin Station", select_hud_tracking.bind("home"))
	port_tracking_button = controls_menu.add_button(survey_column, "Track selected docking port", select_hud_tracking.bind("port"))
	clear_tracking_button = controls_menu.add_button(survey_column, "Stop HUD tracking", select_hud_tracking.bind("off"))
	controls_menu.add_button(survey_column, "Select nearby moon jump", select_moon_jump)
	controls_menu.add_button(survey_column, "Select asteroid inspection jump", select_minor_jump)
	setup_planet_route_controls(survey_column)
	surface_marker_button = controls_menu.add_button(survey_column, "Mark this spot", func(): surface_marker_command("replace_freedom_surface_marker" if state.get("surface_marker", {}).get("present", false) else "mark_freedom_surface_marker"))
	clear_surface_marker_button = controls_menu.add_button(survey_column, "Clear marker", func(): surface_marker_command("clear_freedom_surface_marker"))
	show_surface_marker_button = controls_menu.add_button(survey_column, "Show personal marker: Off", toggle_surface_marker)
	landmark_enable_button = controls_menu.add_button(survey_column, "Enable local landmarks", enable_surface_landmarks)
	landmark_inspect_button = controls_menu.add_button(survey_column, "Inspect exposure", inspect_surface_landmark)
	setup_field_sample_controls(survey_column)
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
	player_input.safety_pause.connect(func(reason: String):
		reset_quiet_travel()
		pause_controls(reason))
	player_input.bindings_changed.connect(func():
		reset_quiet_travel()
		pause_controls("Controls changed. Release mapped controls before resuming."))
	player_input.assist_requested.connect(request_assistance)
	player_input.camera_requested.connect(change_camera)
	player_input.recenter_requested.connect(recenter_camera)
	player_input.surface_interaction_requested.connect(request_surface_interaction)
	controls_menu.resumed.connect(toggle_pause)
	controls_menu.save_requested.connect(open_save_dialog)
	controls_menu.catalog_save_requested.connect(func(save_as: bool):
		if paused and focused and not journey_dialog_open and not save_dialog.visible and not origin_repair_open():
			reset_quiet_travel()
			catalog_save_requested.emit(save_as))
	controls_menu.load_requested.connect(func():
		if paused and focused and not journey_dialog_open and not save_dialog.visible and not origin_repair_open(): load_requested.emit())
	controls_menu.title_requested.connect(func():
		if paused and focused and not journey_dialog_open and not save_dialog.visible and not origin_repair_open(): title_requested.emit())
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
	if commissions_open() or origin_repair_open(): return
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
	refresh_field_samples()
	controls_menu.show_exposure_reference("LOCAL EXPOSURE\n%s\n\n%s\n\n%s\nCollect explicitly while within reach on supported ground.\nThis is a geological appearance record, not a material analysis, resource deposit or landing certificate. Your personal marker is unchanged." % [site.label, site.description, notice], landmark_inspect_button, exposure_collection_context(state, str(site.id)), collect_exposure_sample)
	refresh_exposure_collection()


func exposure_collection_context(value: Dictionary, site_id: String) -> Dictionary:
	# Bind this snapshot to the actual loaded individual and outside actor, not
	# whichever exposure happens to be nearest after a Continue or owner change.
	var context: Dictionary = {"site_id": site_id, "bridge": bridge.get_instance_id()}
	for field in ["universe_seed", "system_id", "body_kind", "body_id", "planet_id", "craft_id", "frame_id", "tick", "checksum", "surface_walk"]:
		context[field] = value.get(field)
	return context.duplicate(true)


func exposure_collection_controls_available() -> bool:
	return activated and paused and focused and not journey_dialog_open and not recovery_notified and not mode_change_pending and error.is_empty() and save_dialog != null and not save_dialog.visible and controls_menu != null and controls_menu.panel.visible and not controls_menu.menu_margin.visible and player_input != null and player_input.waiting_action.is_empty() and controls_menu.surface_reference_view.visible and not controls_menu.settings_view.visible and not controls_menu.basics.visible and (controls_menu.observations_view == null or not controls_menu.observations_view.visible) and not commissions_open() and not origin_repair_open()


func refresh_exposure_collection() -> void:
	var reader: Control = controls_menu.surface_reference_view
	if reader.collection_context.is_empty(): return
	var site_id: String = str(reader.collection_context.site_id)
	var fresh: Dictionary = bridge.get_freedom_flight_state()
	if exposure_collection_context(fresh, site_id) != reader.collection_context or fresh.get("recovery_pending", false) or fresh.get("impact", {}).get("pending_loss", false):
		reader.refresh_collection(false, "This exposure snapshot belongs to an earlier state. Close it and inspect again.")
		return
	var availability: Dictionary = bridge.get_freedom_field_sample_availability(site_id)
	reader.refresh_collection(availability.get("available", false), str(availability.get("reason", "Collection unavailable.")) if not availability.get("available", false) else "Collect one hand specimen; stay paused and retain its source in field notes.")


func collect_exposure_sample() -> void:
	if not exposure_collection_controls_available() or exposure_collection_frame == Engine.get_process_frames(): return
	var reader: Control = controls_menu.surface_reference_view
	if not reader.collection_ready() or not surface_interaction_neutral(): return
	# Recheck the pinned owner and C++ reach/pouch/inspection rules immediately
	# before the existing transactional command. A covered control never acts.
	refresh_exposure_collection()
	if reader.collect_button.disabled:
		reader.show_collection_result()
		return
	exposure_collection_frame = Engine.get_process_frames()
	var site_id: String = str(reader.collection_context.site_id)
	var collected: bool = bridge.collect_freedom_field_sample(site_id)
	var reason: String = "Specimen collected. Its source is recorded in your field notes." if collected else str(bridge.get_last_error())
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	refresh_field_samples()
	refresh_exposure_collection()
	reader.show_collection_result("Specimen collected." if collected else reason)
	report_action_status(reason)
	reader.back_button.grab_focus()


func setup_field_sample_controls(column: Node) -> void:
	sample_collect_button = controls_menu.add_button(column, "Collect hand specimen", collect_field_sample)
	sample_pouch_button = controls_menu.add_button(column, "Sample pouch · 0 / 4", show_field_samples)
	for index in 4:
		sample_discard_buttons.append(controls_menu.add_button(column, "Discard specimen", discard_field_sample.bind(index)))
	refresh_field_samples()


static func valid_planet_destinations(value: Variant, system_id := "") -> bool:
	if not value is Dictionary or value.size() != 4 or typeof(value.get("version")) != TYPE_INT or value.version != 1: return false
	if not value.get("selected") is bool or not value.get("rows") is Array or value.rows.size() > 6: return false
	if not value.get("reason") is String or value.reason.length() > 1024: return false
	if not value.selected: return value.rows.is_empty()
	var seen := {}
	var current := 0
	for row in value.rows:
		if not row is Dictionary or row.size() != 7: return false
		for field in ["system_id", "planet_id"]:
			if not MoonState.decimal(row.get(field)): return false
		if not system_id.is_empty() and row.system_id != system_id: return false
		if not row.get("name") is String or row.name.is_empty() or row.name.length() > 128: return false
		if not row.get("reason") is String or row.reason.length() > 1024: return false
		for field in ["current", "visited", "selectable"]:
			if not row.get(field) is bool: return false
		var key: String = row.system_id + "/" + row.planet_id
		if seen.has(key) or (row.current and row.selectable): return false
		seen[key] = true
		if row.current: current += 1
	return current == 1


func setup_planet_route_controls(column: Node) -> void:
	planet_route_label = Label.new()
	planet_route_label.text = "Planetary routes · charted locations"
	planet_route_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(planet_route_label)
	for index in 6:
		planet_route_buttons.append(controls_menu.add_button(column, "Planet target", select_planet_destination.bind(index)))
	planet_jump_button = controls_menu.add_button(column, "Begin selected jump", request_planet_jump)
	refresh_planet_routes()


func refresh_planet_routes() -> void:
	if planet_route_label == null: return
	planet_route_packet = {}
	if bridge != null and bridge.has_method("get_freedom_planet_destinations"):
		var packet: Variant = bridge.get_freedom_planet_destinations()
		if valid_planet_destinations(packet, str(state.get("system_id", ""))): planet_route_packet = packet
	var shown: bool = planet_route_packet.get("selected", false)
	planet_route_label.visible = shown
	var rows: Array = planet_route_packet.get("rows", [])
	for index in planet_route_buttons.size():
		var button := planet_route_buttons[index]
		button.visible = shown and index < rows.size()
		if not button.visible: continue
		var row: Dictionary = rows[index]
		button.text = ("Current reference · " if row.current else "Select planet · ") + row.name + (" · visited" if row.visited else " · charted")
		button.disabled = not row.selectable or surface_walking()
		button.tooltip_text = row.reason if button.disabled else "Select this charted planet. Selection spends nothing; a jump uses one charge after spooling. Descent and landing remain manual."
	var jump: Dictionary = state.get("jump", {})
	var phase: String = jump.get("phase", "idle")
	planet_jump_button.visible = shown and jump.get("route_kind") == "planet" and not str(jump.get("selected", "")).is_empty()
	planet_route_label.text = "Planetary routes · charted locations"
	if jump.get("destination_name") is String and not jump.destination_name.is_empty():
		planet_route_label.text += "\nSelected · " + jump.destination_name
	planet_jump_button.text = "Cancel jump spool · no charge spent" if phase == "spool" else "Begin selected jump"
	if phase == "idle" and jump.get("recovery") is Dictionary and not jump.recovery.ready:
		planet_jump_button.text = "Jump early · 1 charge + arrival uncertainty"
	planet_jump_button.disabled = surface_walking() or phase == "transit" or (phase == "idle" and not jump.get("available", false))
	planet_jump_button.tooltip_text = "Committed transit cannot be canceled." if phase == "transit" else str(jump.get("refusal", "")) if planet_jump_button.disabled else "Spooling advances after explicit Resume; one charge commits after three seconds."
	if phase == "idle" and jump.get("recovery") is Dictionary and not jump.recovery.ready and not planet_jump_button.disabled:
		planet_jump_button.tooltip_text += " Early arrival has up to %.0f m extra spread; spooling reduces this bound." % jump.recovery.extra_radius_metres
	if phase == "idle" and jump.get("recovery", {}).get("version") == 2 and not planet_jump_button.disabled:
		planet_jump_button.tooltip_text += " Next arrival recovery ≤ %.1f s if committed now; spooling cools the drive." % jump.recovery.next_recovery_seconds


func select_planet_destination(index: int) -> void:
	if not marker_controls_available() or mode_change_pending or surface_walking(): return
	var rows: Array = planet_route_packet.get("rows", [])
	if index < 0 or index >= rows.size() or not rows[index].selectable: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release flight controls before selecting a planet.")
		return
	var row: Dictionary = rows[index]
	if not bridge.select_freedom_planet_jump(row.system_id, row.planet_id):
		report_action_status(str(bridge.get_last_error()))
		refresh_planet_routes()
		return
	state = bridge.get_freedom_flight_state()
	refresh_return_charge_check()
	refresh_known_chart_map()
	update_view(0.0)
	refresh_planet_routes()
	report_action_status("Planet target selected · %s. Begin selected jump when ready; one charge commits after spooling." % row.name)
	planet_jump_button.grab_focus()


func request_planet_jump() -> void:
	if not marker_controls_available() or mode_change_pending: return
	if state.get("jump", {}).get("route_kind") != "planet": return
	jump_requested()
	refresh_planet_routes()


func refresh_field_samples() -> void:
	if sample_pouch_button == null: return
	sample_packet = {}
	sample_collection_id = ""
	if bridge != null and bridge.has_method("get_freedom_field_samples"):
		sample_packet = bridge.get_freedom_field_samples()
	var carried: Array = sample_packet.get("carried", [])
	sample_pouch_button.text = "Sample pouch · %d / %d" % [carried.size(), sample_packet.get("capacity", 4)]
	sample_pouch_button.disabled = sample_packet.is_empty()
	sample_pouch_button.tooltip_text = "Review carried specimens and retained collection notes. Discard frees a slot; standard craft replacement loses carried specimens and keeps the notes."
	for index in sample_discard_buttons.size():
		var button: Button = sample_discard_buttons[index]
		button.visible = index < carried.size()
		if index < carried.size():
			button.text = "Discard %d · %s · %s" % [index + 1, carried[index].label, carried[index].source_name]
			button.tooltip_text = "Discard this carried specimen; keep its first collection note."
	var site := SurfaceLandmarks.nearest_nearby(state)
	sample_collect_button.visible = surface_walking() and not site.is_empty()
	sample_collect_button.disabled = true
	sample_collect_button.text = "Collect hand specimen"
	if site.is_empty(): return
	sample_collection_id = str(site.id)
	if bridge == null or not bridge.has_method("get_freedom_field_sample_availability"): return
	var availability: Dictionary = bridge.get_freedom_field_sample_availability(sample_collection_id)
	sample_collect_button.disabled = not availability.get("available", false)
	sample_collect_button.tooltip_text = str(availability.get("reason", "Inspect the exposure first.")) if sample_collect_button.disabled else "Collect one specimen from this inspected exposure. Collection keeps the game paused."
	if carried.size() >= int(sample_packet.get("capacity", 4)):
		sample_collect_button.text = "Collect specimen · pouch full"


func collect_field_sample() -> void:
	if not marker_controls_available() or sample_collection_id.is_empty(): return
	if not bridge.collect_freedom_field_sample(sample_collection_id):
		report_action_status(str(bridge.get_last_error()))
		refresh_field_samples()
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	refresh_field_samples()
	report_action_status("Specimen collected. Its source is recorded in your field notes.")
	sample_pouch_button.grab_focus()


func discard_field_sample(index: int) -> void:
	if not marker_controls_available(): return
	var carried: Array = sample_packet.get("carried", [])
	if index < 0 or index >= carried.size(): return
	# Use the displayed typed identity, never a name-based body lookup.
	var specimen: Dictionary = carried[index]
	if not bridge.discard_freedom_field_sample(specimen.system_id, specimen.body_kind, specimen.body_id, specimen.site_id):
		report_action_status(str(bridge.get_last_error()))
		refresh_field_samples()
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	refresh_field_samples()
	report_action_status("Specimen discarded. Its collection note is retained.")
	sample_pouch_button.grab_focus()


func show_field_samples() -> void:
	if not marker_controls_available(): return
	refresh_field_samples()
	var carried: Array = sample_packet.get("carried", [])
	var journal: Array = sample_packet.get("journal", [])
	var text := "SAMPLE POUCH · %d / %d\n" % [carried.size(), sample_packet.get("capacity", 4)]
	if carried.is_empty(): text += "No specimens carried.\n"
	for index in carried.size():
		text += "%d. %s · %s\n" % [index + 1, carried[index].label, carried[index].source_name]
	text += "\nFIELD NOTES · %d sites\n" % journal.size()
	for index in journal.size():
		text += "%d. %s · %s\n" % [index + 1, journal[index].label, journal[index].source_name]
	text += "\nInspect an exposure, then collect within 8 metres on supported ground. Discard from controls to free a slot. First collection notes survive discarded, sold or lost specimens."
	var buyer: Variant = bridge.get_freedom_specimen_sale() if bridge.has_method("get_freedom_specimen_sale") else {}
	if SpecimenSale.valid_packet(buyer) and buyer.supported:
		text += " At Origin, the paused Survey commissions office offers a provisional buyer for actual carried specimens; notes alone cannot be sold. No material analysis is provided."
	else:
		text += " Specimens have no sale value or material analysis in this journey."
	controls_menu.show_surface_reference(text, sample_pouch_button, "FIELD SAMPLES")


func mark_recorded_sample_site(target: Dictionary, replace: bool) -> Dictionary:
	# This command belongs to the active archive, never to a covered control.
	if not paused or not focused or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or player_input == null or not player_input.waiting_action.is_empty() or controls_menu == null or not controls_menu.panel.visible or controls_menu.observations_view == null or not controls_menu.observations_view.visible or controls_menu.settings_view.visible or controls_menu.basics.visible or controls_menu.surface_reference_view.visible:
		return {"ok": false, "reason": "Return to the focused, paused archive before marking a site."}
	var result: Dictionary = preload("res://scripts/native/exploration_notes.gd").mark_bridge(bridge, target, replace)
	if result.ok:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
	return result


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


func landed_hatch_available() -> bool:
	# Presentation eligibility never replaces the C++ entry/floor assessment.
	# Historical craft without the seated native assembly gain no exit action.
	return state.get("body_kind", "planet") == "planet" and state.get("frame_id") == "2" and state.get("surface", {}).get("landed", false) and state.get("surface", {}).get("maneuver", "off") == "off" and not state.get("attached", false) and not surface_walking() and state.get("boarding", {}).get("state") == "seated" and state.get("jump", {}).get("phase", "") == "idle" and not state.get("impact", {}).get("pending_loss", false) and state.get("resources", {}).get("selected", false) and state.get("chart", {}).get("selected", false)


func request_surface_interaction() -> void:
	if cockpit_control_aimed():
		request_cockpit_gear()
		return
	if not activated or not focused or paused or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or player_input == null or not player_input.waiting_action.is_empty(): return
	if not surface_walking():
		if not landed_hatch_available(): return
		# E is also a flight strafe binding. Exempt only that physical key;
		# every other key, remap barrier and independent pad demand stays checked.
		if not controls_armed or not player_input.landed_hatch_neutral() or not surface_interaction_neutral():
			report_action_status("Release flight and look controls to use the hatch.")
			return
		if not bridge.begin_freedom_surface_walk():
			report_action_status(str(bridge.get_last_error()))
			return
		state = bridge.get_freedom_flight_state()
		look_offset = Vector2.ZERO
		pause_controls("On the surface. Release controls, then resume to walk.")
		update_view(0.0)
		return
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


func setup_frame_diagnostics(column: Node) -> void:
	frame_diagnostics = FrameDiagnostics.new()
	add_child(frame_diagnostics)
	frame_diagnostics.set_enabled(initial_frame_diagnostics)
	frame_diagnostics_button = controls_menu.add_button(column, "", toggle_frame_diagnostics)
	refresh_frame_diagnostics_button()


func refresh_frame_diagnostics_button() -> void:
	frame_diagnostics_button.text = "Frame diagnostics: " + ("On" if frame_diagnostics.enabled else "Off")
	frame_diagnostics_button.tooltip_text = "Optional local frame/clock readings while playing. No automatic tuning, recording or upload. Resume explicitly."


func toggle_frame_diagnostics() -> void:
	if not marker_controls_available() or mode_change_pending: return
	frame_diagnostics.set_enabled(not frame_diagnostics.enabled)
	refresh_frame_diagnostics_button()
	report_action_status("Frame diagnostics selected. Resume explicitly to continue.")


func sample_frame_diagnostics(delta: float) -> void:
	if frame_diagnostics == null or not frame_diagnostics.enabled: return
	var targets: Array = []
	if far_viewport != null: targets.append(far_viewport.size)
	if near_viewport != null: targets.append(near_viewport.size)
	frame_diagnostics.observe(delta, state, targets, activated and focused and not paused and not journey_dialog_open and not recovery_notified and not mode_change_pending and error.is_empty() and save_dialog != null and not save_dialog.visible)


func marker_controls_available() -> bool:
	return paused and focused and controls_menu.panel.visible and player_input.waiting_action.is_empty() and not journey_dialog_open and not save_dialog.visible and not recovery_notified and error.is_empty() and not controls_menu.settings_view.visible and not controls_menu.basics.visible and not controls_menu.surface_reference_view.visible and (controls_menu.observations_view == null or not controls_menu.observations_view.visible) and (controls_menu.commissions_view == null or not controls_menu.commissions_view.visible) and not origin_repair_open()


func origin_repair_open() -> bool:
	return controls_menu != null and is_instance_valid(controls_menu.origin_repair_view) and controls_menu.origin_repair_view.visible

func repair_controls_available() -> bool:
	return activated and paused and focused and not journey_dialog_open and not recovery_notified and not mode_change_pending and error.is_empty() and save_dialog != null and not save_dialog.visible and controls_menu != null and controls_menu.panel.visible and player_input != null and player_input.waiting_action.is_empty() and not controls_menu.settings_view.visible and not controls_menu.basics.visible and not controls_menu.surface_reference_view.visible and (controls_menu.observations_view == null or not controls_menu.observations_view.visible) and not commissions_open() and state.get("boarding", {}).get("state", "") not in ["boarding", "disembarking"]

func open_origin_repair() -> void:
	if origin_repair_open() or not repair_controls_available() or not controls_menu.menu_margin.visible: return
	observe_neutral_controls()
	if not controls_armed: return
	var value: Variant = bridge.get_freedom_origin_repair()
	if not OriginRepair.valid_packet(value) or value.tick != state.tick:
		report_action_status("Origin repair quote is unavailable.")
		return
	controls_menu.entry_focus_frames = 0
	controls_menu.menu_margin.hide()
	controls_menu.origin_repair_view.open(value)

func origin_repair_command(action: String) -> void:
	if action != "repair" or not origin_repair_open() or not repair_controls_available() or repair_action_frame == Engine.get_process_frames(): return
	observe_neutral_controls()
	if not controls_armed: return
	var reader: Control = controls_menu.origin_repair_view
	if not reader.repair_button.is_visible_in_tree() or reader.repair_button.disabled: return
	var fresh: Variant = bridge.get_freedom_origin_repair()
	if not OriginRepair.valid_packet(fresh):
		reader.refresh({})
		return
	if fresh != reader.packet:
		reader.refresh(fresh)
		reader.back.grab_focus()
		return
	if not fresh.can_repair: return
	repair_action_frame = Engine.get_process_frames()
	var repaired: bool = bridge.repair_freedom_origin(fresh.token)
	var failure_reason: String = "" if repaired else str(bridge.get_last_error())
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	var after: Variant = bridge.get_freedom_origin_repair()
	reader.refresh(after if OriginRepair.valid_packet(after) else {})
	report_action_status("Craft repaired at Origin. Remain paused; Save to retain the service." if repaired else failure_reason)
	reader.back.grab_focus()

func commissions_open() -> bool:
	return controls_menu != null and is_instance_valid(controls_menu.commissions_view) and controls_menu.commissions_view.visible


func open_survey_commissions() -> void:
	if not marker_controls_available() or mode_change_pending or not controls_menu.menu_margin.visible: return
	observe_neutral_controls()
	if not controls_armed: return
	controls_menu.commissions_view.requested_target.clear()
	var packet: Variant = controls_menu.commissions_view.query(bridge)
	if not SurveyCommissions.valid_packet(packet) or packet.tick != state.tick:
		report_action_status("Survey commission terms are unavailable.")
		return
	controls_menu.entry_focus_frames = 0
	controls_menu.menu_margin.hide()
	controls_menu.commissions_view.open(packet)
	controls_menu.commissions_view.refresh_offers(bridge.get_freedom_survey_commission_offers())
	refresh_paid_operations()
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()


func survey_commission_command(action: String) -> void:
	if origin_repair_open(): return
	if commissions_open() and controls_menu.commissions_view.service_child_open(): return
	if not paused or not focused or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or controls_menu == null or not controls_menu.panel.visible or controls_menu.commissions_view == null or not controls_menu.commissions_view.visible or not player_input.waiting_action.is_empty() or controls_menu.settings_view.visible or controls_menu.basics.visible or controls_menu.surface_reference_view.visible or (controls_menu.observations_view != null and controls_menu.observations_view.visible): return
	if action not in ["accept", "submit", "abandon", "choose_orbital", "choose_geological", "choose_destination_0", "choose_destination_1"] or commission_action_frame == Engine.get_process_frames(): return
	observe_neutral_controls()
	if not controls_armed: return
	var reader: Control = controls_menu.commissions_view
	var packet: Variant = controls_menu.commissions_view.query(bridge)
	if not SurveyCommissions.valid_packet(packet) or packet.tick != state.tick:
		reader.refresh({})
		return
	if packet != reader.packet:
		reader.refresh(packet)
		reader.back.grab_focus()
		return
	if action == "accept" and packet.get("kind", 1) == 1:
		var current_offers: Variant = bridge.get_freedom_survey_commission_offers()
		if not SurveyCommissions.valid_offers(current_offers) or current_offers.tick != packet.tick or current_offers != controls_menu.commissions_view.offers_packet:
			controls_menu.commissions_view.refresh_offers(current_offers)
			controls_menu.commissions_view.back.grab_focus()
			return
	if action in ["choose_destination_0", "choose_destination_1"]:
		if packet.sequence != 0 or controls_menu.commissions_view.requested_kind != 1: return
		var offers: Variant = bridge.get_freedom_survey_commission_offers()
		if not SurveyCommissions.valid_offers(offers) or offers.tick != packet.tick or offers != controls_menu.commissions_view.offers_packet:
			controls_menu.commissions_view.refresh_offers(offers)
			controls_menu.commissions_view.back.grab_focus()
			return
		commission_action_frame = Engine.get_process_frames()
		controls_menu.commissions_view.choose_target(0 if action == "choose_destination_0" else 1, bridge)
		return
	if action in ["choose_orbital", "choose_geological"]:
		if packet.sequence != 0: return
		commission_action_frame = Engine.get_process_frames()
		controls_menu.commissions_view.requested_target.clear()
		var offer: Variant = bridge.get_freedom_survey_commissions(1 if action == "choose_orbital" else 2)
		reader.refresh(offer if SurveyCommissions.valid_packet(offer) else {})
		reader.back.grab_focus()
		return
	var action_button: Button = {"accept": reader.accept_button, "submit": reader.submit_button, "abandon": reader.abandon_button}[action]
	if not action_button.visible or action_button.disabled: return
	if not packet.at_origin or not packet.get("can_" + action, false): return
	commission_action_frame = Engine.get_process_frames()
	var accepted: bool
	if action == "accept":
		accepted = bridge.accept_freedom_geological_survey_commission(packet.offer_until, str(packet.offer_sequence)) if packet.get("kind", 1) == 2 else bridge.accept_freedom_survey_commission_target(packet.offer_until, str(packet.offer_sequence), packet.system_id, packet.planet_id)
	elif action == "submit" and packet.get("kind", 1) == 2:
		accepted = bridge.submit_freedom_geological_survey_commission(packet.submission_token)
	else:
		accepted = bridge.call("submit_freedom_survey_commission" if action == "submit" else "abandon_freedom_survey_commission", str(packet.sequence))
	var updated: Variant = controls_menu.commissions_view.query(bridge)
	reader.refresh(updated if SurveyCommissions.valid_packet(updated) else {})
	controls_menu.commissions_view.refresh_offers(bridge.get_freedom_survey_commission_offers())
	refresh_paid_operations()
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	if not accepted: report_action_status(str(bridge.get_last_error()))
	else:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
	reader.back.grab_focus()


func refresh_paid_service_label() -> Dictionary:
	var value: Variant = bridge.get_freedom_paid_operations() if bridge.has_method("get_freedom_paid_operations") else {}
	var valid := PaidOperations.valid_packet(value)
	if service_button != null:
		service_button.text = "Fuel service · review paid quote" if valid and value.selected else "Replenish flight fuel and jump charges · free" if not bridge.has_method("get_freedom_paid_operations") or valid else "Fuel service · quote unavailable"
	return value if valid else {}


func refresh_paid_operations() -> void:
	var value := refresh_paid_service_label()
	controls_menu.commissions_view.refresh_paid(value if value.get("tick") == state.get("tick") else {})


func paid_operations_controls_available() -> bool:
	if origin_repair_open() or not commissions_open() or not activated or not paused or not focused or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible or not controls_menu.panel.visible or not player_input.waiting_action.is_empty() or controls_menu.settings_view.visible or controls_menu.basics.visible or controls_menu.surface_reference_view.visible or (controls_menu.observations_view != null and controls_menu.observations_view.visible): return false
	observe_neutral_controls()
	return controls_armed


func open_paid_operations() -> void:
	if not paid_operations_controls_available() or controls_menu.commissions_view.service_child_open(): return
	refresh_paid_operations()
	if controls_menu.commissions_view.paid_packet.is_empty():
		report_action_status("Paid operating terms are unavailable.")
		return
	controls_menu.commissions_view.open_paid(controls_menu.commissions_view.paid_packet)


func paid_operations_command(action: String) -> void:
	if not paid_operations_controls_available() or not controls_menu.commissions_view.paid_open() or controls_menu.commissions_view.sale_open() or controls_menu.commissions_view.recovery_sale_open() or commission_action_frame == Engine.get_process_frames(): return
	if action not in ["enable", "purchase", "retainer"]: return
	var office: Control = controls_menu.commissions_view
	var reader: Control = office.paid_view
	var button: Button = {"enable": reader.enable_button, "purchase": reader.purchase_button, "retainer": reader.retainer_button}[action]
	if not button.visible or button.disabled: return
	var reviewed: Dictionary = reader.packet
	refresh_paid_operations()
	var fresh: Dictionary = office.paid_packet
	if fresh.is_empty() or fresh != reviewed:
		reader.refresh(fresh)
		reader.back.grab_focus()
		report_action_status("Paid terms or fuel quote changed. Review the refreshed page before confirming.")
		return
	if not fresh.at_origin or not fresh.get({"enable": "can_enable", "purchase": "can_purchase", "retainer": "can_retainer"}[action], false): return
	commission_action_frame = Engine.get_process_frames()
	var command: String = {"enable": "enable_freedom_paid_operations", "purchase": "purchase_freedom_paid_fuel", "retainer": "request_freedom_paid_retainer"}[action]
	var accepted: bool = bridge.call(command, reviewed.token)
	var after: Variant = controls_menu.commissions_view.query(bridge)
	office.refresh(after if SurveyCommissions.valid_packet(after) else {})
	controls_menu.commissions_view.refresh_offers(bridge.get_freedom_survey_commission_offers())
	refresh_paid_operations()
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	reader.refresh(office.paid_packet)
	reader.back.grab_focus()
	if accepted:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
	report_action_status("Paid operating action recorded. The journey remains paused." if accepted else "Paid operating action refused: " + str(bridge.get_last_error()))


func refresh_specimen_sale() -> void:
	var value: Variant = bridge.get_freedom_specimen_sale() if bridge.has_method("get_freedom_specimen_sale") else {}
	controls_menu.commissions_view.refresh_sale(value if SpecimenSale.valid_packet(value) and value.tick == state.get("tick") else {})


func open_specimen_sale() -> void:
	if not paid_operations_controls_available() or controls_menu.commissions_view.service_child_open(): return
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	if controls_menu.commissions_view.sale_packet.is_empty():
		report_action_status("Specimen buyer terms are unavailable.")
		return
	controls_menu.commissions_view.open_sale(controls_menu.commissions_view.sale_packet)


func specimen_sale_command(action: String = "sell") -> void:
	if action != "sell" or not paid_operations_controls_available() or not controls_menu.commissions_view.sale_open() or controls_menu.commissions_view.paid_open() or controls_menu.commissions_view.recovery_sale_open() or commission_action_frame == Engine.get_process_frames(): return
	var office: Control = controls_menu.commissions_view
	var reader: Control = office.sale_view
	if not reader.sell_button.visible or reader.sell_button.disabled: return
	var reviewed: Dictionary = reader.packet
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	var fresh: Dictionary = office.sale_packet
	if fresh.is_empty() or fresh != reviewed:
		reader.refresh(fresh)
		reader.back.grab_focus()
		report_action_status("Specimen quote changed. Review the refreshed page before confirming.")
		return
	if not fresh.at_origin or not fresh.can_sell: return
	commission_action_frame = Engine.get_process_frames()
	var accepted: bool = bridge.sell_freedom_specimens(reviewed.token)
	refresh_paid_operations()
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	reader.refresh(office.sale_packet)
	reader.back.grab_focus()
	if accepted:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
		refresh_field_samples()
	report_action_status("Specimens sold; first field notes retained. The journey remains paused." if accepted else "Specimen sale refused: " + str(bridge.get_last_error()))



func refresh_derelict_recovery_sale() -> void:
	var value: Variant = bridge.get_freedom_derelict_recovery_sale() if bridge.has_method("get_freedom_derelict_recovery_sale") else {}
	controls_menu.commissions_view.refresh_recovery_sale(value if RecoverySale.valid_packet(value) and value.tick == state.get("tick") else {})


func open_derelict_recovery_sale() -> void:
	if not paid_operations_controls_available() or controls_menu.commissions_view.service_child_open(): return
	refresh_derelict_recovery_sale()
	if controls_menu.commissions_view.recovery_sale_packet.is_empty():
		report_action_status("Canister buyer terms are unavailable.")
		return
	controls_menu.commissions_view.open_recovery_sale(controls_menu.commissions_view.recovery_sale_packet)


func derelict_recovery_sale_command(action: String = "sell") -> void:
	if action != "sell" or not paid_operations_controls_available() or not controls_menu.commissions_view.recovery_sale_open() or controls_menu.commissions_view.paid_open() or controls_menu.commissions_view.sale_open() or commission_action_frame == Engine.get_process_frames(): return
	var office: Control = controls_menu.commissions_view
	var reader: Control = office.recovery_sale_view
	if not reader.sell_button.visible or reader.sell_button.disabled: return
	var reviewed: Dictionary = reader.packet
	refresh_derelict_recovery_sale()
	var fresh: Dictionary = office.recovery_sale_packet
	if fresh.is_empty() or fresh != reviewed:
		reader.refresh(fresh)
		reader.back.grab_focus()
		report_action_status("Canister quote changed. Review the refreshed page before confirming.")
		return
	if not fresh.at_origin or not fresh.can_sell: return
	commission_action_frame = Engine.get_process_frames()
	var accepted: bool = bridge.sell_freedom_derelict_recovery(reviewed.token)
	refresh_paid_operations()
	refresh_specimen_sale()
	refresh_derelict_recovery_sale()
	reader.refresh(office.recovery_sale_packet)
	reader.back.grab_focus()
	if accepted:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
		refresh_field_samples()
	report_action_status("Canister sold; recovery note retained. The journey remains paused." if accepted else "Canister sale refused: " + str(bridge.get_last_error()))


func orbital_survey_command(cancel := false) -> void:
	if not marker_controls_available() or mode_change_pending or not OrbitalSurvey.valid(state.get("orbital_survey")): return
	if cancel:
		if not state.orbital_survey.active: return
	else:
		observe_neutral_controls()
		if not controls_armed:
			report_action_status("Release movement controls before starting an orbital survey.")
			return
		if not state.orbital_survey.can_begin: return
	if not bridge.call("cancel_freedom_orbital_survey" if cancel else "begin_freedom_orbital_survey"):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	report_action_status("Survey stopped." if cancel else "Survey started · Resume to scan.")
	controls_menu.resume_button.grab_focus()


func star_search_command(cancel := false) -> void:
	if not marker_controls_available() or mode_change_pending or not StarSearch.valid(state.get("star_search")): return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before changing the star search.")
		return
	if cancel:
		if not state.star_search.active: return
	elif not state.star_search.can_begin: return
	if not bridge.call("cancel_freedom_star_search" if cancel else "begin_freedom_star_search"):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	report_action_status("Star search stopped; unfinished progress discarded." if cancel else "Star search started · Resume to acquire a fix.")
	controls_menu.resume_button.grab_focus()


func derelict_command(inspect := false) -> void:
	if not marker_controls_available() or mode_change_pending or not DerelictView.valid_packet(state.get("derelict")): return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before scanning or inspecting.")
		return
	if not (state.derelict.can_inspect if inspect else state.derelict.can_scan): return
	if not bridge.call("inspect_freedom_derelict" if inspect else "scan_freedom_derelict"):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	report_action_status(str(state.derelict.note) + ("\n" + str(state.derelict.history) if inspect and not state.derelict.history.is_empty() else ""))
	controls_menu.resume_button.grab_focus()


func derelict_recovery_command(cancel := false) -> void:
	if not marker_controls_available() or mode_change_pending or commission_action_frame == Engine.get_process_frames(): return
	if not DerelictRecovery.valid_packet(state.get("derelict_recovery")): return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before changing canister retrieval.")
		return
	var fresh: Variant = bridge.get_freedom_derelict_recovery()
	if not DerelictRecovery.valid_packet(fresh) or fresh != state.derelict_recovery:
		state = bridge.get_freedom_flight_state()
		update_view(0.0)
		report_action_status("Retrieval conditions changed. Review them before confirming.")
		return
	if not (fresh.active if cancel else fresh.can_begin): return
	commission_action_frame = Engine.get_process_frames()
	if not bridge.call("cancel_freedom_derelict_recovery" if cancel else "begin_freedom_derelict_recovery"):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	update_view(0.0)
	if not error.is_empty(): return
	report_action_status("Retrieval canceled; source unclaimed." if cancel else "Retrieval started · Resume and hold stand-off/matched speed for 30 seconds. No automatic approach or transfer animation.")
	controls_menu.resume_button.grab_focus()


func refresh_derelict_controls() -> void:
	if derelict_motion_note != null:
		derelict_motion_note.text = DerelictMotion.text(state)
		derelict_motion_note.visible = not derelict_motion_note.text.is_empty()
		derelict_motion_note.tooltip_text = str(state.get("derelict_motion", {}).get("note", ""))
	if derelict_scan_button == null: return
	var packet: Variant = state.get("derelict")
	var available: bool = DerelictView.valid_packet(packet) and packet.selected
	var ordinary: bool = state.get("body_kind", "planet") == "planet"
	derelict_scan_button.visible = available and ordinary
	derelict_scan_button.disabled = not available or not packet.can_scan
	derelict_scan_button.tooltip_text = "Scan the local planet frame in ordinary space above the atmosphere. Contact range: 100,000 km. A scan may find no signal; it does not reveal remote locations or advance ship time."
	derelict_inspect_button.visible = available and packet.known
	derelict_inspect_button.disabled = not available or not packet.can_inspect
	derelict_inspect_button.tooltip_text = "Exterior inspection only: hold a 100–500 m stand-off with relative speed at most 20 m/s. Approach manually. No boarding or hull collision in this prototype."
	if available:
		derelict_scan_button.tooltip_text += "\n" + packet.note
		derelict_inspect_button.tooltip_text += "\n" + packet.note
		if not packet.history.is_empty(): derelict_inspect_button.tooltip_text += "\n" + packet.history
	var recovery: Variant = state.get("derelict_recovery")
	var selected: bool = DerelictRecovery.valid_packet(recovery) and recovery.selected
	if derelict_recovery_button != null:
		derelict_recovery_button.visible = selected and recovery.known and ordinary and not recovery.active and recovery.status == "unclaimed"
		derelict_recovery_button.disabled = not selected or not recovery.can_begin
		derelict_recovery_cancel_button.visible = selected and recovery.active
		derelict_recovery_cancel_button.disabled = not selected or not recovery.active
		if selected:
			derelict_recovery_button.tooltip_text = recovery.note
			derelict_recovery_cancel_button.tooltip_text = "Unfinished progress is discarded; the source remains unclaimed. Pause and Save preserve a valid attempt."
	if derelict_marker != null:
		derelict_marker.visible = available and packet.local and not surface_walking()
		if derelict_marker.visible: derelict_marker.update_cue(camera, packet.position, false)


func refresh_star_search() -> void:
	if star_search_start_button == null: return
	var packet: Variant = state.get("star_search")
	var available: bool = StarSearch.valid(packet) and packet.selected
	var ordinary: bool = state.get("body_kind", "planet") == "planet"
	star_search_start_button.visible = available and ordinary and not packet.active
	star_search_cancel_button.visible = available and packet.active
	star_search_start_button.disabled = not available or not packet.can_begin
	star_search_cancel_button.disabled = not available or not packet.active
	if available:
		star_search_start_button.tooltip_text = "Fly above the atmosphere for 10 simulation seconds to acquire a stellar navigation fix. Pause holds the clock; unsuitable flight holds progress. Observe the planets after arrival.\n" + packet.note
		star_search_cancel_button.tooltip_text = StarSearch.text(packet) + "\nStop, jump commitment or craft replacement discards unfinished work. Pause and Save preserve it."


func refresh_orbital_survey() -> void:
	if survey_start_button == null: return
	var packet: Variant = state.get("orbital_survey")
	var available := OrbitalSurvey.valid(packet)
	survey_start_button.visible = available and not packet.active
	survey_cancel_button.visible = available and packet.active
	survey_start_button.disabled = not available or not packet.can_begin
	survey_start_button.tooltip_text = "30 seconds of committed flight in a stable bound orbit, above the atmosphere and within two body radii. Pause and Save freeze acquisition; a nonqualifying orbit holds progress. Accepted jumps or craft replacement discard unfinished work. A terrain model, never landing clearance." if available else "Orbital surveys unavailable."
	if available:
		var progress := OrbitalSurvey.text(packet)
		survey_start_button.tooltip_text += "\n" + packet.note
		survey_cancel_button.tooltip_text = progress + "\nStop, accepted jumps or craft replacement discard unfinished work. Pause and Save preserve it; a nonqualifying orbit holds progress."


func select_hud_tracking(mode: String) -> void:
	if not marker_controls_available() or mode not in ["home", "port", "off"]: return
	var target := hud_tracking_target(state, station_geometry, mode)
	if mode != "off" and target.is_empty():
		report_action_status("That navigation target is unavailable in the current local frame.")
		return
	hud_tracking_mode = mode
	update_view(0.0)
	report_action_status("HUD tracking stopped." if mode == "off" else "Tracking %s on the HUD. Range is from the craft reference point; use collar offsets for docking." % target.name)


func refresh_hud_tracking() -> void:
	if home_tracking_button == null: return
	var local: bool = state.get("station_available", true) and not surface_walking()
	home_tracking_button.visible = not surface_walking()
	port_tracking_button.visible = not surface_walking() and state.get("frame_id", "1") == "2"
	clear_tracking_button.visible = not surface_walking()
	home_tracking_button.disabled = not local
	port_tracking_button.disabled = not local or hud_tracking_target(state, station_geometry, "port").is_empty()
	clear_tracking_button.disabled = hud_tracking_mode == "off"
	home_tracking_button.text = "Tracking Origin Station" if hud_tracking_mode == "home" else "Track Origin Station"
	var port_name := "D%d" % state.get("target_port", 1)
	port_tracking_button.text = ("Tracking " if hud_tracking_mode == "port" else "Track ") + "Origin " + port_name


func select_distant_contact(index: int) -> void:
	if not marker_controls_available() or not DistantContacts.valid(state): return
	var contacts: Array = state.get("distant_contacts", {}).get("contacts", [])
	if index < 0 or index >= contacts.size(): return
	selected_contact_id = contacts[index].id
	update_view(0.0)
	# Selection is presentation-only, including under held movement controls.
	controls_menu.show_surface_reference("DISTANT OPTICAL CONTACT\n%s\n\n%s\n\nCONTACT is a direction annotation, not an arrival target. This optical annotation is not a flight target. The explicit local-drive controls support the origin Moon and asteroid inspection routes; other contacts remain distant observations." % [contacts[index].label, contacts[index].reason], contact_buttons[index])


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
			button.tooltip_text = "Distant observation only · " + (contacts[index].reason if contacts[index].id == selected_contact_id else "Inspect optical availability. Tracking selects no arrival target; local trips use separate drive controls.")
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
		show_surface_marker_button.tooltip_text = "%s / %s · recorded ship time %.1f s\nDirection and straight-line distance only; no route or landing clearance." % [marker.system_name, SurfaceMarker.body_name(marker), marker.tick.to_float() / 120.0] if present else ""
	if not show_personal_marker: return
	if surface_walking():
		ship_marker.label_text = "MARK"
		ship_marker.update_cue(camera, marker.position, false)
	else:
		home_marker.visible = true
		home_marker.label_text = "MARK"
		home_marker.display_name = ""
		home_marker.detail_text = ""
		home_marker.update_cue(camera, marker.position, false)
		if home_cue != null: home_cue.visible = false


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
	hint.text = "%s main · %s retro · %s view · Esc / Start controls\nMouse steers · Hold right mouse to look · %s centers steering/head\nFlight controls request physical thrust/torque." % [player_input.binding_label("forward", family), player_input.binding_label("backward", family), player_input.binding_label("camera", family), player_input.binding_label("recenter", family)]


func request_assistance(enabled: bool) -> void:
	if commissions_open() or origin_repair_open(): return
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
	if commissions_open() or origin_repair_open(): return
	if not paused or not focused or not error.is_empty() or journey_dialog_open or save_dialog.visible or mode_change_pending:
		return
	if not bridge.set_freedom_hold(enabled):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	controls_menu.sync_saved_state(state, false)
	report_action_status("Orbit-hold request selected. Resume explicitly to apply correction." if enabled else "Orbit-hold request disabled. Resume explicitly to coast.")


func open_save_dialog() -> void:
	if commissions_open() or origin_repair_open(): return
	reset_quiet_travel()
	pause_controls("Save As preserves the last committed state.", false)
	controls_menu.hide_menu()
	save_dialog.popup_centered_ratio(0.75)


func pause_controls(reason: String, show_menu := true) -> void:
	if not paused: reset_quiet_travel()
	clear_cockpit_control()
	clear_flight_mouse()
	mouse_look.set_active(false)
	if state.get("port_docking", {}).get("active", false):
		bridge.cancel_freedom_port_docking()
		state = bridge.get_freedom_flight_state()
	if state.get("port_rendezvous", {}).get("active", false):
		bridge.cancel_freedom_port_rendezvous()
		state = bridge.get_freedom_flight_state()
	if state.get("surface", {}).get("maneuver", "off") != "off":
		bridge.cancel_freedom_surface_maneuver()
		state = bridge.get_freedom_flight_state()
	if state.get("port_approach", {}).get("active", false):
		bridge.cancel_freedom_port_approach()
		state = bridge.get_freedom_flight_state()
	paused = true
	if frame_diagnostics != null: frame_diagnostics.observe(0.0, {}, [], false)
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
		refresh_field_samples()
		refresh_planet_routes()
	if save_status != null and error.is_empty():
		save_status.text = reason
	update_compact_hud()


func change_camera() -> void:
	if origin_repair_open(): return
	if error.is_empty() and focused and not paused and not save_dialog.visible and state.frame_id == "2" and not surface_walking():
		cockpit = not cockpit
		clear_cockpit_control()
		clear_flight_mouse()
		look_offset = Vector2.ZERO
		update_view(0.0)


func recenter_camera() -> void:
	if origin_repair_open(): return
	if error.is_empty() and focused and not paused and not save_dialog.visible:
		clear_flight_mouse()
		look_offset = Vector2(-float(state.surface_walk.heading), 0.0) if surface_walking() else Vector2.ZERO
		update_view(0.0)


func unboard_requested() -> void:
	if commissions_open() or origin_repair_open(): return
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
	if commissions_open() or origin_repair_open(): return
	if not error.is_empty() or not focused or save_dialog.visible: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before selecting local travel.")
		return
	if not bridge.select_freedom_moon_jump():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	refresh_return_charge_check()
	refresh_known_chart_map()
	report_action_status("Nearby moon selected. One charge commits after spooling." + (" Last charge: none will remain for Home." if int(state.get("resources", {}).get("jump_charges", 0)) == 1 else ""))
	update_view(0.0)


func select_minor_jump() -> void:
	if commissions_open() or origin_repair_open(): return
	if not error.is_empty() or not focused or save_dialog.visible: return
	observe_neutral_controls()
	if not controls_armed:
		report_action_status("Release movement controls before selecting local travel.")
		return
	if not bridge.select_freedom_minor_jump():
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	refresh_return_charge_check()
	refresh_known_chart_map()
	report_action_status("Asteroid inspection selected. One charge commits after spooling." + (" Last charge: none will remain for Home." if int(state.get("resources", {}).get("jump_charges", 0)) == 1 else ""))
	update_view(0.0)


func select_jump_destination(ordinal: int) -> void:
	if commissions_open() or origin_repair_open(): return
	if not error.is_empty() or not focused or save_dialog.visible: return
	var rows: Array = state.get("chart", {}).get("rows", [])
	if ordinal < 0 or ordinal >= rows.size(): return
	if not bridge.select_freedom_jump(rows[ordinal].system_id):
		report_action_status(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	refresh_charted_arrivals()
	refresh_return_charge_check()
	refresh_known_chart_map()
	update_view(0.0)


func quiet_travel_controls_available() -> bool:
	if not activated or not paused or not focused or not instruments_open or surface_walking(): return false
	if hud_scroll == null or not hud_scroll.visible or journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty(): return false
	if save_dialog == null or save_dialog.visible or player_input == null or not player_input.waiting_action.is_empty() or controls_menu == null or controls_menu.panel.visible: return false
	if controls_menu.settings_view.visible or controls_menu.basics.visible or controls_menu.surface_reference_view.visible: return false
	if controls_menu.observations_view != null and controls_menu.observations_view.visible: return false
	if (controls_menu.commissions_view != null and controls_menu.commissions_view.visible) or origin_repair_open(): return false
	return true


func sync_quiet_travel_ui() -> void:
	if quiet_travel_select == null: return
	var valid := QuietTravel.valid_packet(quiet_travel_packet)
	var shown := paused and instruments_open and not surface_walking()
	quiet_travel_select.visible = shown
	quiet_travel_status.visible = shown
	quiet_travel_select.disabled = not valid or not quiet_travel_controls_available()
	for index in quiet_travel_select.item_count:
		var multiplier := quiet_travel_select.get_item_id(index)
		quiet_travel_select.set_item_disabled(index, multiplier != 1 and (not valid or not quiet_travel_packet.available))
	if valid: quiet_travel_select.select(quiet_travel_select.get_item_index(quiet_travel_packet.multiplier))
	quiet_travel_status.text = str(quiet_travel_packet.note) if valid else "Quiet travel unavailable."
	quiet_travel_status.tooltip_text = quiet_travel_select.tooltip_text


func refresh_quiet_travel() -> Dictionary:
	var value: Variant = bridge.get_freedom_quiet_travel() if bridge != null and is_instance_valid(bridge) and bridge.has_method("get_freedom_quiet_travel") else {}
	quiet_travel_packet = value.duplicate(true) if QuietTravel.valid_packet(value) else {}
	sync_quiet_travel_ui()
	return quiet_travel_packet.duplicate(true)


func reset_quiet_travel(refresh: bool = true) -> void:
	if bridge == null or not is_instance_valid(bridge) or not bridge.has_method("set_freedom_quiet_travel"): return
	bridge.set_freedom_quiet_travel(1)
	if not refresh: return
	var value := refresh_quiet_travel()
	if QuietTravel.valid_packet(value): state["quiet_travel"] = value


func quiet_travel_popup_input(event: InputEvent) -> void:
	var pad := event is InputEventJoypadButton or event is InputEventJoypadMotion
	if not focused or (pad and (player_input == null or event.device != player_input.device or not player_input.available_pads.get(event.device, false))) or (event is InputEventKey and event.echo):
		quiet_travel_select.get_popup().set_input_as_handled()


func quiet_travel_command(multiplier: int) -> void:
	if multiplier not in [1, 2, 4] or not quiet_travel_controls_available() or quiet_travel_action_frame == Engine.get_process_frames():
		sync_quiet_travel_ui()
		return
	var displayed := quiet_travel_packet.duplicate(true)
	var fresh := refresh_quiet_travel()
	if not QuietTravel.valid_packet(displayed) or fresh != displayed:
		report_action_status("Quiet travel changed. Review the current selection.")
		return
	observe_neutral_controls()
	if not controls_armed:
		sync_quiet_travel_ui()
		report_action_status("Release flight controls before changing quiet travel.")
		return
	quiet_travel_action_frame = Engine.get_process_frames()
	var accepted: bool = bridge.set_freedom_quiet_travel(multiplier)
	var refusal := str(bridge.get_last_error()) if not accepted else ""
	state = bridge.get_freedom_flight_state()
	refresh_quiet_travel()
	update_view(0.0)
	report_action_status(str(quiet_travel_packet.get("note", "Quiet travel unavailable.")) if accepted else refusal)


func charted_arrivals_controls_available() -> bool:
	if not activated or not paused or not focused or not instruments_open or surface_walking(): return false
	if hud_scroll == null or not hud_scroll.visible or chart_button == null or not chart_button.button_pressed: return false
	if journey_dialog_open or recovery_notified or mode_change_pending or not error.is_empty() or save_dialog == null or save_dialog.visible: return false
	if player_input == null or not player_input.waiting_action.is_empty() or controls_menu == null or controls_menu.panel.visible: return false
	if controls_menu.settings_view.visible or controls_menu.basics.visible or controls_menu.surface_reference_view.visible: return false
	if controls_menu.observations_view != null and controls_menu.observations_view.visible: return false
	if (controls_menu.commissions_view != null and controls_menu.commissions_view.visible) or origin_repair_open(): return false
	return state.get("jump", {}).get("phase", "idle") == "idle" and not state.get("impact", {}).get("pending_loss", false)


func sync_charted_arrivals_ui() -> void:
	if charted_arrival_button == null or charted_arrival_status == null: return
	var valid := ChartedArrivals.valid_packet(charted_arrival_packet)
	var shown := paused and instruments_open and chart_button.visible and chart_button.button_pressed and not surface_walking()
	charted_arrival_button.visible = shown
	charted_arrival_status.visible = shown
	charted_arrival_button.set_pressed_no_signal(valid and charted_arrival_packet.enabled)
	charted_arrival_button.disabled = not valid or not charted_arrival_packet.supported or not charted_arrivals_controls_available()
	charted_arrival_status.text = str(charted_arrival_packet.note) if valid else "Arrival option unavailable."


func update_chart_text(chart: Dictionary, acquired_chart: bool) -> void:
	chart_status.text = "Navigation chart · known system locations" if acquired_chart else "Starting chart · granted positions"
	if chart.get("local_sensors", false):
		chart_status.text += "\nShip sensors · %d recorded findings" % chart.observed_facts
	var ordinals := {}
	if known_chart_map != null and KnownChartMap.valid_packet(known_chart_map.packet) and known_chart_map.packet.available:
		for i in known_chart_map.packet.nodes.size(): ordinals[known_chart_map.packet.nodes[i].system_id] = i + 1
	for row: Dictionary in chart.get("rows", []):
		var detail: String = " · current" if row.current else " · %.1f light-hours · %s" % [float(row.distance_light_seconds) / 3600.0, "1 charge available" if row.affordable else "No jump charges"]
		if not row.current and not row.get("selectable", false): detail += "\n" + str(row.get("disabled_reason", "Destination unavailable"))
		var ordinal: String = str(ordinals[row.system_id]) + ". " if ordinals.has(row.system_id) else ""
		chart_status.text += "\n%s%s · %s%s" % [ordinal, row.name, row.confidence.to_upper(), detail]


func sync_known_chart_map() -> void:
	if known_chart_map == null or known_chart_map_legend == null: return
	var shown: bool = paused and instruments_open and chart_button.visible and chart_button.button_pressed and not surface_walking()
	known_chart_map.visible = shown and KnownChartMap.valid_packet(known_chart_map.packet) and known_chart_map.packet.available
	known_chart_map_legend.visible = shown


func refresh_known_chart_map() -> void:
	if known_chart_map == null or known_chart_map_legend == null: return
	var shown: bool = paused and instruments_open and chart_button.visible and chart_button.button_pressed and not surface_walking()
	if shown:
		var value: Variant = bridge.get_freedom_known_chart_map() if bridge.has_method("get_freedom_known_chart_map") else {}
		known_chart_map.configure(value)
		known_chart_map_legend.text = str(value.note) if KnownChartMap.valid_packet(value) else "Known-system diagram unavailable."
		update_chart_text(state.get("chart", {}), state.get("star_search", {}).get("selected", false))
	sync_known_chart_map()


func refresh_return_charge_check() -> void:
	if return_charge_status == null: return
	return_charge_status.visible = paused and instruments_open and chart_button.visible and not surface_walking()
	var value: Variant = bridge.get_freedom_return_charge_check() if bridge.has_method("get_freedom_return_charge_check") else {}
	return_charge_status.text = ReturnChargeCheck.text(value)


func refresh_charted_arrivals() -> void:
	if charted_arrival_button == null: return
	var value: Variant = bridge.get_freedom_charted_arrivals() if bridge.has_method("get_freedom_charted_arrivals") else {}
	charted_arrival_packet = value.duplicate(true) if ChartedArrivals.valid_packet(value) else {}
	sync_charted_arrivals_ui()


func set_charted_arrivals(enabled: bool) -> void:
	if not charted_arrivals_controls_available() or charted_arrival_action_frame == Engine.get_process_frames() or not ChartedArrivals.valid_packet(charted_arrival_packet) or not charted_arrival_packet.supported:
		sync_charted_arrivals_ui()
		return
	observe_neutral_controls()
	if not controls_armed:
		sync_charted_arrivals_ui()
		report_action_status("Release flight controls before changing arrival selection.")
		return
	charted_arrival_action_frame = Engine.get_process_frames()
	var accepted: bool = bridge.set_freedom_charted_arrivals(enabled)
	var refusal: String = str(bridge.get_last_error()) if not accepted else ""
	state = bridge.get_freedom_flight_state()
	refresh_charted_arrivals()
	update_view(0.0)
	if accepted:
		report_action_status("Closer charted arrivals selected. " + str(charted_arrival_packet.get("note", "")) if enabled else "Wide arrivals selected.")
	else:
		report_action_status(refusal)


func jump_requested() -> void:
	if commissions_open() or origin_repair_open(): return
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
	refresh_return_charge_check()
	refresh_known_chart_map()
	report_action_status("Spool canceled · no jump charge spent" if cancel else "Spooling · Resume flight explicitly." if paused else "Spooling · one charge commits after 3 seconds")
	update_view(0.0)


func port_command(command: String, ordinal: int = 0) -> void:
	if commissions_open() or origin_repair_open(): return
	if not error.is_empty() or not focused or save_dialog.visible:
		hide_exhaust()
		return
	if command == "replenish_freedom_resources" and bridge.has_method("get_freedom_paid_operations"):
		var paid := refresh_paid_service_label()
		if paid.is_empty():
			report_action_status("Fuel quote unavailable. Review paid operations in the commission office.")
			return
		if paid.selected:
			if journey_dialog_open or recovery_notified or mode_change_pending or not player_input.waiting_action.is_empty(): return
			if not paused: pause_controls("Paid fuel service requires reviewing the current quote while paused.")
			if not commissions_open(): open_survey_commissions()
			if commissions_open(): open_paid_operations()
			return
	if command in ["begin_freedom_port_docking", "begin_freedom_port_approach", "begin_freedom_port_rendezvous", "request_freedom_landing", "liftoff_freedom_surface", "begin_freedom_surface_walk", "return_from_freedom_surface_walk"]:
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
		refresh_return_charge_check()
		refresh_known_chart_map()
		report_action_status("Flight reserve and three jump charges replenished · free station service")
	elif state.attached:
		report_action_status("Attached to D%d. Release before firing propulsion." % state.target_port)
	elif command == "release_freedom_port":
		var family := "pad" if player_input.last_device == "pad" else "key"
		report_action_status("Released D%d. %s withdraws below the port; %s brakes. Clear the 12 m column before forward thrust." % [state.target_port, player_input.binding_label("fall", family), player_input.binding_label("rise", family)])
	elif command.ends_with("port_docking"):
		clear_flight_mouse()
		report_action_status(state.port_docking.note + (" · Resume flight explicitly." if paused and state.port_docking.active else ""))
	elif command.ends_with("port_approach"):
		report_action_status(state.port_approach.note + (" · Resume flight explicitly." if paused and state.port_approach.active else ""))
	elif command.ends_with("port_rendezvous"):
		report_action_status(state.port_rendezvous.note + (" · Uses flight fuel; allow several minutes." if state.port_rendezvous.active else "") + (" Resume flight explicitly." if paused and state.port_rendezvous.active else ""))
	elif canceled_aid:
		report_action_status(surface_aid_cue.text())
	elif command in ["request_freedom_landing", "liftoff_freedom_surface"] and state.surface.maneuver != "off":
		report_action_status(("Landing aid requested." if command == "request_freedom_landing" else "Liftoff aid requested.") + (" Resume flight explicitly." if paused else "") + reference_warning)
	elif command in ["request_freedom_landing", "stow_freedom_landing_gear", "liftoff_freedom_surface", "cancel_freedom_surface_maneuver"]:
		report_action_status(state.surface.note + (" · Resume flight explicitly." if paused and state.surface.maneuver != "off" else "") + reference_warning)
	else:
		report_action_status("Port selected. Capture requires physical alignment and low closure.")


func enable_impacts() -> void:
	if commissions_open() or origin_repair_open(): return
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
	# The camera vector is float32; a neutral tick must preserve the saved
	# float64 heading. Apply only the change from its rounded display baseline.
	var heading := float(state.surface_walk.heading)
	var baseline := Vector2(wrapf(Vector2(-heading, 0.0).x, -PI, PI), 0.0).x
	if look_offset.x != baseline:
		heading = wrapf(heading - wrapf(look_offset.x - baseline, -PI, PI), -PI, PI)
	# Recentered yaw has one canonical save representation, including signed zero.
	if heading == 0.0: heading = 0.0
	return PackedFloat64Array([clampf(forward, -1.0, 1.0), clampf(right, -1.0, 1.0), heading])


func clear_flight_mouse() -> void:
	if flight_mouse != null:
		flight_mouse.clear()
		flight_mouse.hide()


func mouse_steering_available() -> bool:
	return activated and focused and not paused and controls_armed and player_input != null and player_input.enabled and not player_input.needs_neutral and player_input.waiting_action.is_empty() and error.is_empty() and not journey_dialog_open and not recovery_notified and not mode_change_pending and save_dialog != null and not save_dialog.visible and not surface_walking() and not state.get("attached", false) and not state.get("surface", {}).get("landed", false) and state.get("jump", {}).get("phase", "idle") == "idle" and not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) and not player_input.looking


func sync_mouse_look() -> void:
	var looking := Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT)
	if looking != mouse_free_look:
		clear_flight_mouse()
		if mouse_free_look and not surface_walking(): look_offset = Vector2.ZERO
		mouse_free_look = looking
	var active := activated and focused and not paused and error.is_empty() and not journey_dialog_open and not recovery_notified and not mode_change_pending and save_dialog != null and not save_dialog.visible
	mouse_look.set_active(active)
	if flight_mouse != null:
		flight_mouse.visible = mouse_steering_available()
		if not flight_mouse.visible: flight_mouse.clear()


func _exit_tree() -> void:
	reset_quiet_travel(false)
	clear_cockpit_control()
	clear_flight_mouse()
	mouse_look.set_active(false)


func _input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT and not event.pressed: cockpit_mouse_held = false
	if paused and instruments_open and not journey_dialog_open and save_dialog != null and not save_dialog.visible and player_input != null:
		var pad := event is InputEventJoypadButton or event is InputEventJoypadMotion
		if not focused or (pad and event.device != player_input.device) or (event is InputEventKey and event.echo):
			get_viewport().set_input_as_handled()
			return
	if not focused or paused or journey_dialog_open or recovery_notified or not error.is_empty() or save_dialog == null or save_dialog.visible: return
	if player_input != null and player_input.pilot_event(event): reset_quiet_travel(false)
	if player_input != null and event.device == player_input.device:
		if (event is InputEventJoypadButton and event.pressed) or (event is InputEventJoypadMotion and absf(event.axis_value) > float(player_input.settings.deadzone)):
			clear_flight_mouse()
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and mouse_look.active:
		refresh_cockpit_control()
		if cockpit_control_aimed():
			if not cockpit_mouse_held:
				cockpit_mouse_held = true
				request_cockpit_gear()
		elif surface_walking(): request_surface_interaction()
		elif context_action != null and context_action.visible and not context_action.disabled:
			context_action.pressed.emit()
		get_viewport().set_input_as_handled()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		sync_mouse_look()
	elif event is InputEventMouseMotion and mouse_look.active:
		if surface_walking() or Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
			look_offset += MouseLook.motion_delta(event.relative, player_input.settings)
			look_offset.x = wrapf(look_offset.x, -PI, PI) if surface_walking() else clampf(look_offset.x, -1.2 if cockpit else -PI, 1.2 if cockpit else PI)
			look_offset.y = clampf(look_offset.y, -1.3 if surface_walking() else -0.8 if cockpit else -1.2, 1.3 if surface_walking() else 0.8 if cockpit else 1.2)
		elif mouse_steering_available():
			flight_mouse.motion(event.relative, size, player_input.settings)
			if flight_mouse.demands(player_input.settings) != Vector2.ZERO: reset_quiet_travel(false)
			get_viewport().set_input_as_handled()


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		reset_quiet_travel()
		focused = false
		if player_input != null:
			player_input.focused = false
		pause_controls("Focus lost. Release controls, then resume explicitly.")
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
		if player_input != null:
			player_input.focused = true


func _unhandled_key_input(event: InputEvent) -> void:
	if origin_repair_open(): return
	if not event is InputEventKey or not event.pressed or event.echo or save_dialog == null or save_dialog.visible:
		return
	# Esc/Start and remapped camera input belong to PlayerInput only.
	if event.physical_keycode == KEY_F3 and error.is_empty() and focused and state.frame_id == "2" and not surface_walking():
		# Historical alias may inspect the camera while paused; no world action.
		cockpit = not cockpit
		clear_cockpit_control()
		clear_flight_mouse()
		look_offset = Vector2.ZERO
		update_view(0.0)


func hide_exhaust() -> void:
	if exhaust != null:
		exhaust.update_applied({}, 0.0, true)


func flight_demand(axes: PackedFloat64Array) -> PackedFloat64Array:
	# Planet and companion views apply the same mouse intent before the existing
	# actuator mapping; presentation never supplies a separate flight force law.
	var flight_axes := axes.duplicate()
	if mouse_steering_available() and flight_mouse != null:
		var steering: Vector2 = flight_mouse.demands(player_input.settings)
		flight_axes[2] = clampf(flight_axes[2] + steering.x, -1.0, 1.0)
		flight_axes[3] = clampf(flight_axes[3] + steering.y, -1.0, 1.0)
	else: clear_flight_mouse()
	return actuator_fractions(flight_axes)


func pause_on_error(message: String) -> void:
	error = message
	if impact_overlay != null: impact_overlay.visible = false
	pause_controls(message)
	if save_status != null:
		save_status.text = "Flight paused: " + error


func _process(delta: float) -> void:
	sample_frame_diagnostics(delta)
	if not activated or recovery_notified: return
	sync_mouse_look()
	if bridge == null or camera == null or terrain == null or not error.is_empty():
		hide_exhaust()
		return
	if not focused:
		hide_exhaust()
		return
	if not bridge.get_freedom_walk_state().is_empty():
		reset_quiet_travel()
		hide_exhaust()
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	var resolved: Dictionary = player_input.sample()
	controls_armed = not player_input.needs_neutral
	var demand := flight_demand(resolved.thrust_axes)
	if not paused and not player_input.pilot_controls_neutral():
		reset_quiet_travel(false)
	if state.get("attached", false) or state.get("surface", {}).get("landed", false):
		demand.fill(0.0)
	var accepted: bool = bridge.advance_freedom_surface_walk(delta, surface_controls(minf(delta, 0.1)), paused or save_dialog.visible) if surface_walking() else bridge.advance_freedom_flight(delta, demand, paused or save_dialog.visible)
	if not accepted:
		pause_on_error(str(bridge.get_last_error()))
		return
	state = bridge.get_freedom_flight_state()
	if state.get("body_kind") in ["moon", "minor"]:
		reset_quiet_travel()
		hide_exhaust()
		if not mode_change_pending:
			mode_change_pending = true
			journey_mode_changed.emit()
		return
	if not valid_state(state):
		pause_on_error("C++ flight view became unavailable: " + str(bridge.get_last_error()))
		return
	if state.get("impact", {}).get("pending_loss", false):
		reset_quiet_travel()
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


static func home_navigation_target(value: Dictionary, geometry: Variant, force_port := false) -> Dictionary:
	var centre: Variant = value.get("station_position")
	if not centre is Vector3 or not centre.is_finite(): return {}
	var result := {"position": centre, "label": "HOME"}
	if not value.get("station_available", true) or (not force_port and (value.get("attached", false) or centre.length() >= 1000.0)): return result
	var ordinal: Variant = value.get("target_port")
	var frame: Variant = value.get("station_basis")
	var station_id: Variant = value.get("station_id")
	if not ordinal is int or ordinal < 1 or ordinal > 2 or not station_id is String or not frame is Basis or not frame.is_finite(): return result
	var determinant: float = frame.determinant()
	if not is_finite(determinant) or absf(determinant) < 0.000001 or not StationPresentation.valid_geometry(geometry, station_id): return result
	# Registered collar coordinates already include the authored station datum.
	# This is the same transform as the station's physical port markers; the
	# imported asset offset and the craft's attitude do not belong here.
	var point: Vector3 = centre + frame * StationPresentation.vector(geometry.ports[ordinal - 1].position_metres)
	if point.is_finite():
		result.position = point
		result.label = "D%d" % ordinal
	return result


static func hud_tracking_target(value: Dictionary, geometry: Variant, mode: String) -> Dictionary:
	var walking: Variant = value.get("surface_walk", {})
	if mode not in ["home", "port"] or not value.get("station_available", true) or not walking is Dictionary or not walking.is_empty(): return {}
	var target := home_navigation_target(value, geometry, mode == "port")
	if target.is_empty() or (mode == "port" and target.label == "HOME"): return {}
	var distance: float = target.position.length()
	if not is_finite(distance) or distance < 0: return {}
	target.name = "Origin Station" if target.label == "HOME" else "Origin · " + target.label
	target.range_metres = distance
	return target


func update_view(delta: float, defer_exhaust: bool = false) -> void:
	if not valid_state(state) and error.is_empty():
		pause_on_error("Saved flight observation is invalid")
	if not error.is_empty():
		hide_exhaust()
		if derelict_view != null: derelict_view.hide()
		if derelict_marker != null: derelict_marker.hide()
		return
	if not refresh_service_craft(state, delta):
		pause_on_error("The service Wayfarer presentation is unavailable")
		return
	refresh_origin_service_ui(delta)
	var condition: Dictionary = state.get("impact", {"selected": false})
	if not impact_overlay.apply(condition, state.craft_id, state.frame_id, state.frame_version, cockpit, surface_walking()):
		pause_on_error("Saved craft condition is unavailable")
		return
	impact_status.visible = condition.get("selected", false) and not surface_walking()
	impact_status.text = ImpactView.condition_text(condition)
	refresh_impact_action()
	var owner_key := str(state.system_id) + ":" + str(state.planet_id)
	if owner_key != rendered_world:
		clear_flight_mouse()
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
	for button in [local_moon_button, local_minor_button]:
		if button != null:
			button.disabled = state.attached or surface.get("landed", false) or surface_walking() or state.get("jump", {}).get("phase", "idle") != "idle"
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
		player_input.landed_hatch_enabled = landed_hatch_available()
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
	if state.has("survey_commission"):
		resource_status.text += "\nSurvey earnings: %s prototype credits" % state.survey_commission.earnings
	var chart: Dictionary = state.get("chart", {})
	var acquired_chart: bool = state.get("star_search", {}).get("selected", false)
	chart_button.text = "Navigation chart" if acquired_chart else "Starting chart"
	chart_button.visible = chart.get("selected", false)
	chart_status.visible = chart_button.visible and chart_button.button_pressed
	sync_known_chart_map()
	if chart_button.visible:
		update_chart_text(chart, acquired_chart)
	else:
		chart_status.visible = false
	var jump: Dictionary = state.get("jump", {})
	var phase: String = jump.get("phase", "idle")
	sync_charted_arrivals_ui()
	return_charge_status.visible = paused and instruments_open and chart_button.visible and not surface_walking()
	quiet_travel_packet = state.get("quiet_travel", {}).duplicate(true) if QuietTravel.valid_packet(state.get("quiet_travel")) else {}
	sync_quiet_travel_ui()
	assist_button.disabled = phase != "idle" or surface_walking()
	var rows: Array = chart.get("rows", [])
	for i in jump_destinations.size():
		var button: Button = jump_destinations[i]
		button.visible = i < rows.size() and chart_button.button_pressed
		if i < rows.size():
			button.text = "Select " + str(rows[i].name)
			button.disabled = not rows[i].get("selectable", false)
			button.tooltip_text = str(rows[i].get("disabled_reason", "Destination unavailable")) if button.disabled else "Select this destination; spooling commits the charge."
	jump_button.visible = chart.get("selected", false)
	jump_button.text = "Cancel spool · no jump charge" if phase == "spool" else "In transit" if phase == "transit" else "Spool jump · 1 charge"
	jump_button.disabled = phase == "transit" or (phase == "idle" and not jump.get("available", false))
	jump_button.tooltip_text = str(jump.get("refusal", "Select a destination first"))
	jump_status.text = "Jump drive · %s" % phase.capitalize()
	if phase != "idle": jump_status.text += " · %.1f s" % jump.get("remaining_seconds", 0.0)
	if jump.get("destination_name") is String and not jump.destination_name.is_empty():
		if jump.get("route_kind") == "foreign_minor":
			jump_status.text += "\nJump target · " + jump.destination_name + "\nStand-off arrival; close inspection only."
		else:
			jump_status.text += ("\nJump target · " + jump.destination_name + "\nFree-flight arrival; approach and landing remain manual." if jump.get("route_kind") == "foreign_moon" else "\nPlanet target · " + jump.destination_name + "\nHigh space arrival; descent and landing remain manual.")
	if jump.has("distance_light_hours"):
		jump_status.text += "\n%.1f light-hours · %s arrival envelope %.0f m\nHeading %.1f° · drift %.1f%%" % [jump.distance_light_hours, str(jump.quality), jump.envelope_radius_metres, jump.heading_error_degrees, jump.drift_percent]
	elif phase == "idle" and not jump.has("destination_name"): jump_status.text += "\nSelect a chart destination"
	if phase == "idle" and not jump.get("available", false): jump_status.text += "\n" + str(jump.get("refusal", ""))
	if jump.get("recovery") is Dictionary:
		var recovery: Dictionary = jump.recovery
		jump_status.text += "\n" + Status.drive_recovery_text(recovery, phase, true)
		if phase == "idle" and not recovery.ready: jump_button.text = "Jump early · 1 charge + arrival uncertainty"
		if phase == "idle" and recovery.version == 2 and not jump_button.disabled:
			jump_button.tooltip_text += " Next arrival recovery ≤ %.1f s if committed now; spooling cools the drive." % recovery.next_recovery_seconds
	orbital_forecast.text = orbit_text(state)
	var target: Vector3 = state.station_position
	var home_target := hud_tracking_target(state, station_geometry, hud_tracking_mode)
	home_marker.label_text = home_target.get("label", "HOME")
	home_marker.display_name = str(home_target.get("name", ""))
	home_marker.detail_text = HomeNavigation.range_text(float(home_target.get("range_metres", -1.0)))
	home_marker.visible = not home_target.is_empty()
	home_cue.visible = state.get("station_available", true)
	if home_marker.visible:
		home_marker.update_cue(camera, home_target.position, state.attached)
	else:
		home_marker.cue = {}
	refresh_hud_tracking()
	var navigation: Dictionary = home_marker.cue
	var direction := " · behind you" if navigation.get("behind", false) else (" · follow %s marker" % home_marker.label_text if not navigation.get("in_view", false) else " · ahead")
	if navigation.is_empty(): direction = " · direction unavailable"
	var occluded := HomeNavigation.globe_occludes(camera.position, target, Vector3(0, -state.planet_radius - state.altitude, 0), state.planet_radius)
	home_cue.text = "Origin Station · %.1f km%s%s" % [target.length() / 1000.0, " · attached" if state.attached else direction, "\nBeyond horizon · direction only, not a safe route" if occluded and not state.attached else ""]
	if not state.attached and not home_target.is_empty() and HomeNavigation.globe_occludes(camera.position, home_target.position, Vector3(0, -state.planet_radius - state.altitude, 0), state.planet_radius):
		home_marker.detail_text += "\nBeyond horizon"
	var bearing_description := "the selected port collar" if home_marker.label_text != "HOME" else "the station centre"
	home_cue.tooltip_text = "Station %s · %s marks %s. Range is to the station centre. Bearing only; use live port readings for alignment and capture." % [state.station_id, home_marker.label_text, bearing_description]
	var near_port: bool = state.get("station_available", true) and (state.attached or target.length() < 1000.0)
	if rendezvous_button != null:
		var rendezvous: Dictionary = state.get("port_rendezvous", {})
		rendezvous_button.visible = state.get("station_available", true) and not state.attached and not surface_walking() and not rendezvous.is_empty()
		rendezvous_button.text = "Cancel station rendezvous · %.0f s left" % rendezvous.get("remaining_seconds", 0.0) if rendezvous.get("active", false) else "Rendezvous with Origin Station"
		rendezvous_button.disabled = not rendezvous.get("active", false) and not rendezvous.get("available", false)
		rendezvous_button.tooltip_text = "Real thrusters and fuel; allow several minutes. Manual input cancels. Near-port aid takes over; Capture stays your action." if rendezvous.get("active", false) or rendezvous.get("available", false) else str(rendezvous.get("refusal", "Select D1 or D2 in paused flight controls."))
	if dock_status != null:
		var changed: bool = dock_status.visible != near_port
		for item in [port_row, dock_status, docking_button, approach_button, capture_button, release_button]: item.visible = near_port
		if changed: call_deferred("layout_hud")
	if dock_status != null:
		var assessment: Dictionary = state.docking
		dock_status.text = "Select a port for approach"
		if state.attached:
			dock_status.text = "Attached to D%d · station co-motion" % state.target_port
		elif not assessment.is_empty():
			var offset: PackedFloat64Array = assessment.offset_body_metres
			dock_status.text = "D%d · %s\nCollar %.3f m · attitude %.2f°\nInward %.3f m/s · lateral %.3f m/s\nPort offset: right %.2f · up %.2f · back %.2f m" % [state.target_port, assessment.reason, assessment.separation, rad_to_deg(assessment.alignment_radians), assessment.inward_speed, assessment.lateral_speed, offset[0], offset[1], offset[2]]
		var docking_aid: Dictionary = state.get("port_docking", {})
		docking_button.visible = near_port and not docking_aid.is_empty()
		docking_button.text = "Cancel docking · %s" % docking_aid.get("phase", "") if docking_aid.get("active", false) else "Dock with thrusters"
		docking_button.disabled = not docking_aid.get("active", false) and not docking_aid.get("available", false)
		docking_button.tooltip_text = "Align, close and brake with real thrusters, then latch. Manual input or pause cancels." if docking_aid.get("active", false) or docking_aid.get("available", false) else str(docking_aid.get("refusal", "Select a port first."))
		if not docking_aid.is_empty() and str(docking_aid.get("note", "Docking off")) != "Docking off":
			dock_status.text += "\n" + str(docking_aid.get("note", ""))
			if docking_aid.get("active", false): dock_status.text += " · %.1f s left" % docking_aid.remaining_seconds
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
		for index in port_buttons.size():
			var button := port_buttons[index]
			var reserved: bool = state.get("origin_service", {}).get("reserved_port", 0) == index + 1
			button.disabled = reserved or state.attached or not state.get("station_available", true) or phase != "idle"
			button.tooltip_text = "Reserved for Origin fuel service, including while it is away." if reserved else ""
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
	refresh_orbital_survey()
	refresh_star_search()
	refresh_derelict_controls()
	if not refresh_derelict(state):
		pause_on_error("The derelict Wayfarer presentation is unavailable")
		return
	refresh_distant_contacts()
	refresh_cockpit_control()
	update_compact_hud()
