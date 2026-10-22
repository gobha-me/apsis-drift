extends SceneTree
## Real charged local travel and typed lunar UI against a continuous C++ route.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const MoonView = preload("res://scripts/native/native_moon_view.gd")
const MoonState = preload("res://scripts/native/native_moon_state.gd")
const MoonTerrain = preload("res://scripts/native/native_moon_terrain.gd")
const CockpitDisplays = preload("res://scripts/native/native_cockpit_displays.gd")
const Navigation = preload("res://scripts/native/home_navigation.gd")
const MoonExposures = preload("res://tests/native_moon_exposures.gd")
const OriginServiceChecks = preload("res://tests/native_origin_service_checks.gd")
const FrameDiagnosticsChecks = preload("res://tests/frame_diagnostics_checks.gd")
const CompanionMouseChecks = preload("res://tests/native_companion_mouse_checks.gd")
const CockpitGearChecks = preload("res://tests/native_cockpit_gear_checks.gd")
const CompanionLayoutChecks = preload("res://tests/native_companion_layout_checks.gd")
const PHASES = ["arrival", "approach", "touchdown-source", "landed", "outside", "returned", "released", "ascent"]
var failures := 0

class RefusedBackground extends RefCounted:
	var owner: Variant
	func get_freedom_walk_state() -> Dictionary: return owner.get_freedom_walk_state()
	func get_freedom_flight_state() -> Dictionary: return owner.get_freedom_flight_state()
	func get_freedom_craft_binding() -> Dictionary: return owner.get_freedom_craft_binding()
	func get_freedom_station_geometry() -> Dictionary: return owner.get_freedom_station_geometry()
	func get_freedom_background_sky(_candidate := "") -> Dictionary: return {}

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error(message)
	return value

func _initialize() -> void:
	call_deferred("run")

func key(code: int, down: bool) -> InputEventKey:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	return event

func hatch_pad(view: Control, device: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = JOY_BUTTON_A
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	# Headless GUI focus may consume the event before ordinary dispatch.
	# Still exercise the actual provider's selected-device and context gates.
	if down and not view.surface_walking(): view.player_input._input(event)

func save_bytes(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Actual Moon Save failed: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func shell_for(owner: Variant, path: String, assets: String) -> Control:
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Moon Continue staging failed: " + shell.error):
		shell.free()
		return null
	shell.current_view.set_process(false)
	return shell

func check_state_refusals(state: Dictionary) -> void:
	for damage in ["boarding", "pose", "eye", "short_eye", "nonfinite_eye", "assistance", "force", "planet_alias", "body_version"]:
		var bad := state.duplicate(true)
		match damage:
			"boarding": bad.boarding = {}
			"pose": bad.boarding.erase("pose")
			"eye": bad.boarding.erase("eye_craft")
			"short_eye": bad.boarding.eye_craft = PackedFloat64Array([0, 0])
			"nonfinite_eye": bad.boarding.eye_craft = PackedFloat64Array([NAN, 0, 0])
			"assistance": bad.erase("assistance")
			"force": bad.positive_force_body = PackedFloat64Array([0, INF, 0])
			"planet_alias": bad.planet_id = state.body_id
			"body_version": bad.body_version = 999
		check(not MoonState.valid(bad), "Malformed Moon packet accepted before staging: " + damage)

func check_clearance_refusals(state: Dictionary, binding: Dictionary) -> void:
	# Damage only copies of the genuine near-pad packet. These pure admission
	# checks run before any staged model/viewport allocation or bridge command.
	for damage in ["missing", "null", "unavailable_extra", "unavailable_type", "available_short", "version", "version_type", "terrain_version", "body_version", "frame_version", "body_id", "system_id", "craft_id", "frame_id", "tick", "source_checksum", "min_nan", "min_inf", "max_nan", "max_inf", "reversed", "min_type", "max_type", "extra"]:
		var bad := state.duplicate(true)
		var clearance: Dictionary = bad.surface.gear_clearance
		match damage:
			"missing": bad.surface.erase("gear_clearance")
			"null": bad.surface.gear_clearance = null
			"unavailable_extra": bad.surface.gear_clearance = {"available": false, "minimum_gap_metres": 0.0}
			"unavailable_type": bad.surface.gear_clearance = {"available": 0}
			"available_short": bad.surface.gear_clearance = {"available": true}
			"version": clearance.version = 2
			"version_type": clearance.version = 1.0
			"terrain_version": clearance.terrain_version = 2
			"body_version": clearance.body_version = state.body_version + 1
			"frame_version": clearance.frame_version = state.frame_version + 1
			"body_id", "system_id", "craft_id", "frame_id", "tick": clearance[damage] = "0" if state[damage] != "0" else "1"
			"source_checksum": clearance.source_checksum = "0" if state.checksum != "0" else "1"
			"min_nan": clearance.minimum_gap_metres = NAN
			"min_inf": clearance.minimum_gap_metres = INF
			"max_nan": clearance.maximum_gap_metres = NAN
			"max_inf": clearance.maximum_gap_metres = -INF
			"reversed": clearance.minimum_gap_metres = clearance.maximum_gap_metres + 1.0
			"min_type": clearance.minimum_gap_metres = 0
			"max_type": clearance.maximum_gap_metres = "0.0"
			"extra": clearance.extra = true
		var pending := {"candidate_id": "1", "mode": "freedom_flight", "craft_binding": binding, "walk_state": {}, "flight_state": bad, "station_state": {}, "station_geometry": {}, "streaming_ready": true}
		check(not MoonState.valid(bad) and not Shell.valid_pending(pending) and not CockpitDisplays.valid_state(bad), "Malformed Moon gear clearance passed a presentation admission gate: " + damage)

func check_gear_clearance(view: Control, owner: Variant, output: String, label: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	if not check(before.surface.get("gear_clearance") is Dictionary, "Actual Moon phase has no explicit gear-clearance packet: " + label): return
	var camera_before: Transform3D = view.camera.transform
	var look_before: Vector2 = view.look_offset
	var cockpit_before: bool = view.cockpit
	var clearance: Dictionary = before.surface.gear_clearance
	var frozen := save_bytes(owner, output.path_join(label + "-clearance-before.json"))
	if not check(not frozen.is_empty(), "Moon clearance baseline Save unavailable: " + label): return
	if label == "touchdown-source":
		if not check(clearance.get("available") == true and before.surface.gear_deployed and not before.surface.landed and before.surface_walk.is_empty() and before.jump.phase == "idle", "Actual durable near-pad source did not expose its seated free-Moon clearance"): return
		check(clearance.size() == 13 and clearance.version == 1 and clearance.terrain_version == 1, "Actual Moon gear clearance lost its closed recipe/version shape")
		for field in ["body_id", "body_version", "system_id", "craft_id", "frame_id", "frame_version", "tick"]:
			check(clearance[field] == before[field], "Actual Moon clearance differs from its live owner/clock: " + field)
		check(clearance.source_checksum == before.checksum, "Actual Moon clearance is not bound to current canonical source physics")
		check(clearance.minimum_gap_metres is float and clearance.maximum_gap_metres is float and is_finite(clearance.minimum_gap_metres) and is_finite(clearance.maximum_gap_metres) and clearance.minimum_gap_metres <= clearance.maximum_gap_metres, "Actual normal-plane gear gap is nonfinite or reversed")
		var span := "%.3f…%.3f m" % [clearance.minimum_gap_metres, clearance.maximum_gap_metres]
		view.update_view(0.0)
		check(view.error.is_empty() and "Gear clearance" in view.primary_status.text and span in view.primary_status.text, "Near-pad Moon HUD omitted actual min/max gear gaps")
		check(view.cockpit_displays.refresh(before, 0.0, true), "Actual Moon cockpit clearance refresh refused")
		var instruments := ""
		for page in view.cockpit_displays.presented_rows: instruments += "\n".join(page) + "\n"
		check("Gear clearance" in instruments and span in instruments, "Moon cockpit instrument omitted the actual gear-gap span")
		check_clearance_refusals(before, owner.get_freedom_craft_binding())
	elif label in ["arrival", "landed", "outside"]:
		check(clearance == {"available": false}, "Moon gear-off/supported/outside phase invented or retained a clearance witness: " + label)
	for repeat in 3:
		check(owner.get_freedom_flight_state() == before, "Reading Moon clearance changed live source state: " + label)
		view.update_view(0.0)
		check(view.cockpit_displays.refresh(before, 0.0, true), "Quiet Moon clearance cockpit refresh failed: " + label)
	check(view.paused and view.error.is_empty() and view.camera.transform == camera_before and view.look_offset == look_before and view.cockpit == cockpit_before and owner.get_freedom_flight_state() == before and save_bytes(owner, output.path_join(label + "-clearance-after.json")) == frozen, "Moon clearance projection/readouts changed pause, camera/look, clock, resources, physics or complete Save: " + label)

func check_manual_moon_menu(view: Control, owner: Variant, output: String, label: String) -> void:
	var original: Dictionary = owner.get_freedom_flight_state()
	var original_bytes := save_bytes(owner, output.path_join(label + "-manual-menu-original.json"))
	if not check(view.paused and view.controls_menu.panel.visible and original.body_kind == "moon" and original.surface_walk.is_empty() and original.jump.phase == "idle" and not original_bytes.is_empty(), "Manual Moon menu lacks its genuine paused seated phase: " + label): return
	var menu: CanvasLayer = view.controls_menu
	var landing: Button = menu.saved_port_buttons["Land / deploy gear"]
	var liftoff: Button = menu.saved_port_buttons["Liftoff with thrusters"]
	for assisted in [false, true]:
		menu.assistance_requested.emit(assisted)
		var live: Dictionary = owner.get_freedom_flight_state()
		if not check(live.assistance == assisted and live.tick == original.tick and live.resources == original.resources and view.error.is_empty(), "Actual Moon menu assistance selection changed time/resources or refused: " + label): return
		var prefix := output.path_join(label + "-manual-menu-" + ("assisted" if assisted else "unassisted"))
		var frozen := save_bytes(owner, prefix + "-before.json")
		for repeat in 2:
			menu.sync_saved_state(live, false)
			check(landing.text == view.landing_button.text and landing.text == "Deploy gear · manual descent" and liftoff.text == view.liftoff_button.text and liftoff.text == "Release support · use up thrusters", "Paused Moon actions promise automatic landing/liftoff or differ from the actual HUD: " + label)
			check("manual thrusters" in landing.tooltip_text and "does not land" in landing.tooltip_text and "apply up thrusters" in liftoff.tooltip_text and "no automatic" in liftoff.tooltip_text, "Paused Moon actions omit their manual-thrust instructions: " + label)
			check(landing.disabled == live.surface.landed and liftoff.disabled == (not live.surface.landed) and menu.saved_assist_button.button_pressed == assisted, "Moon label refresh changed action admission or assistance: " + label)
		# Pure presentation fallback, not a claim of a flown planetary state.
		# Reuse this same menu to detect stale Moon text/tooltips on its next owner.
		var planetary := live.duplicate(true)
		planetary.erase("body_kind")
		menu.sync_saved_state(planetary, false)
		check(landing.text == ("Land with thrusters" if assisted else "Deploy gear · manual landing") and liftoff.text == "Liftoff with thrusters" and landing.tooltip_text.is_empty() and liftoff.tooltip_text.is_empty(), "Shared menu failed to restore its unchanged planetary action text/tooltips: " + label)
		check(landing.disabled == live.surface.landed and liftoff.disabled == (not live.surface.landed), "Planetary presentation fallback changed existing action admission: " + label)
		menu.sync_saved_state(live, false)
		check(landing.text == "Deploy gear · manual descent" and liftoff.text == "Release support · use up thrusters" and not landing.tooltip_text.is_empty() and not liftoff.tooltip_text.is_empty(), "Moon action instructions were not restored after planetary presentation fallback: " + label)
		check(view.paused and view.controls_menu.panel.visible and owner.get_freedom_flight_state() == live and save_bytes(owner, prefix + "-after.json") == frozen, "Reading shared Moon/planet menu changed the actual Moon state or complete Save: " + label)
	menu.assistance_requested.emit(original.assistance)
	check(owner.get_freedom_flight_state().assistance == original.assistance and save_bytes(owner, output.path_join(label + "-manual-menu-restored.json")) == original_bytes, "Moon menu checks failed to restore the genuine phase's original complete Save: " + label)

func check_rollback(shell: Control, owner: Variant) -> void:
	var previous: Control = shell.current_view
	var model: Node3D = previous.staged_model
	var parent: Node = model.get_parent()
	var skin := model.find_child("HopperStructure", true, false) as MeshInstance3D
	if not check(skin != null, "Actual selected skin missing for handoff rollback"): return
	var material: Material = skin.get_surface_override_material(model.exterior_surface)
	var proxy := RefusedBackground.new()
	proxy.owner = owner
	shell.bridge = proxy
	var before: Dictionary = owner.get_freedom_flight_state()
	shell.switch_journey_view()
	check(shell.current_view == previous and model.get_parent() == parent and model.valid_current_pose(), "Failed lunar stage lost the borrowed Wayfarer")
	check(skin.get_surface_override_material(model.exterior_surface) == material, "Failed lunar stage changed the accepted skin override")
	check(not shell.error.is_empty() and owner.get_freedom_flight_state() == before, "Failed lunar stage mutated the committed world")
	shell.bridge = owner
	shell.error = ""
	previous.error = ""

func charged_jump(shell: Control, owner: Variant, assets: String) -> Dictionary:
	if not check(shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": assets}), "Actual New Game42 failed: " + shell.error): return {}
	shell.current_view.set_process(false)
	for tick in 1194:
		if not check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, -1, 0])), "Actual station approach refused"): return {}
	if not check(owner.begin_freedom_boarding(), "Actual Wayfarer boarding refused"): return {}
	for tick in 1440:
		if not check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, 0, 0])), "Actual boarding tick refused"): return {}
	if not check(owner.release_freedom_port(), "Actual port release refused"): return {}
	var demand := PackedFloat64Array()
	demand.resize(12)
	demand[4] = 0.25
	for tick in 720:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, demand, false), "Actual withdrawal refused"): return {}
	if not check(owner.select_freedom_moon_jump() and owner.begin_freedom_jump(), "Actual charged Moon jump refused"): return {}
	var spool: Dictionary = owner.get_freedom_flight_state()
	check(spool.jump.phase == "spool" and spool.resources.jump_charges == 3, "Moon spool spent a charge before commitment")
	demand.fill(0.0)
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, demand, false), "Actual local Moon travel tick refused"): return {}
	var arrival: Dictionary = owner.get_freedom_flight_state()
	check(MoonState.valid(arrival) and arrival.jump.phase == "idle" and arrival.resources.jump_charges == 2, "Charged jump did not install the actual Moon/one-charge bill")
	check(owner.get_freedom_station_geometry().is_empty() and not owner.select_freedom_port(1), "Moon exposed a phantom home station")
	return arrival

func check_cover(view: Control, owner: Variant) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	if not check(owner.request_freedom_moon_stream(), "Actual Moon stream request refused"): return
	var packet: Dictionary = {}
	var deadline := Time.get_ticks_msec() + 30000
	while packet.is_empty() and Time.get_ticks_msec() < deadline:
		packet = owner.poll_freedom_moon_stream()
		if packet.is_empty(): await create_timer(0.01).timeout
	if not check(packet.get("body_id") == before.body_id and packet.get("tiles") is Array, "Actual owned Moon stream did not complete"): return
	check(packet.tiles.size() >= 6 and packet.tiles.size() <= 384, "Actual Moon cover exceeds its bounded tile budget")
	for damage in ["owner", "duplicate", "vertices", "indices", "index_range", "color", "anchor"]:
		var bad := packet.duplicate(true)
		match damage:
			"owner": bad.body_id = "0" if before.body_id != "0" else "1"
			"duplicate": bad.tiles[1].id = bad.tiles[0].id
			"vertices": bad.tiles[0].vertices = PackedVector3Array([Vector3.ZERO])
			"indices": bad.tiles[0].indices = PackedInt32Array()
			"index_range":
				var indices: PackedInt32Array = bad.tiles[0].indices.duplicate()
				indices[0] = 1221
				bad.tiles[0].indices = indices
			"color":
				var colors: PackedColorArray = bad.tiles[0].colors.duplicate()
				colors[0] = Color(NAN, 0, 0, 1)
				bad.tiles[0].colors = colors
			"anchor": bad.tiles[0].anchor = PackedFloat64Array([INF, 0, 0])
		var rejected := MoonTerrain.new()
		rejected.expected_body_id = before.body_id
		check(not rejected.accept_cover(bad) and rejected.nodes.is_empty() and rejected.staged.is_empty() and rejected.get_child_count() == 0, "Malformed Moon cover allocated/changed residency: " + damage)
		rejected.free()
	# The terrain consumer owns and clears the accepted tile array on completion.
	var expected_count: int = packet.tiles.size()
	var first_id: String = packet.tiles[0].id
	if not check(view.terrain.accept_cover(packet), "Actual C++ Moon cover failed typed upload gate"): return
	for index in expected_count:
		view.terrain.tick(0.0, view.camera.position, true)
		if not check(view.terrain.error.is_empty(), "Actual Moon upload failed: " + view.terrain.error): return
	if not check(view.terrain.is_ready and view.terrain.resident_ids.size() == expected_count, "Actual Moon cover never became resident"): return
	var mesh: ArrayMesh = view.terrain.nodes[first_id].mesh
	check(mesh.surface_get_array_len(0) == 1221 and mesh.surface_get_array_index_len(0) == 7680, "Actual uploaded Moon mesh differs from C++32-interval tile")
	view.update_view(0.0)
	check(view.error.is_empty(), "Resident Moon camera depth refresh failed: " + view.error)
	check(view.near_viewport != view.far_viewport and view.near_viewport.find_world_3d() == view.far_viewport.find_world_3d() and view.camera.get_world_3d() == view.terrain_camera.get_world_3d(), "Moon depth passes lost their shared world or viewport isolation")
	check(view.camera.get_viewport() == view.near_viewport and view.terrain_camera.get_viewport() == view.far_viewport and view.near_viewport.transparent_bg and view.camera.cull_mask == 3 and view.terrain_camera.cull_mask == 1, "Moon close craft/terrain composition lost its viewport or layer ownership")
	check(view.camera.transform == view.terrain_camera.transform and view.camera.fov == view.terrain_camera.fov, "Resident Moon close/distant camera registration differs")
	for depth_camera in [view.camera, view.terrain_camera]:
		if check(is_finite(depth_camera.near) and is_finite(depth_camera.far) and depth_camera.near > 0.0 and depth_camera.far > depth_camera.near, "Resident Moon camera has invalid clip planes"):
			check(depth_camera.far / depth_camera.near <= 500001.0, "Resident Moon camera exceeds the bounded depth ratio")
	check(view.camera.far > view.terrain_camera.near, "Resident Moon close/distant depth bands have no rounding overlap")
	var join: Vector3 = view.camera.transform * Vector3(0, 0, -view.terrain_camera.near * 0.999)
	check(view.camera.is_position_in_frustum(join), "Actual Moon close frustum clips before the distant near plane")
	check(owner.get_freedom_flight_state() == before, "Paused lunar streaming advanced authoritative state")

func check_assistance(view: Control, owner: Variant, output: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	view.controls_menu.assistance_requested.emit(not before.assistance)
	var changed: Dictionary = owner.get_freedom_flight_state()
	check(changed.assistance != before.assistance and changed.tick == before.tick and changed.resources == before.resources and view.error.is_empty(), "Actual Moon assistance menu lost its HUD/control owner or changed time/resources")
	view.player_input.assist_requested.emit(before.assistance)
	check(owner.get_freedom_flight_state().assistance == before.assistance and view.assist_button.button_pressed == before.assistance, "Moon assistance input failed to restore/sync")
	check(not save_bytes(owner, output.path_join("assistance.json")).is_empty(), "Moon assistance Save is unavailable")
	check(owner.set_freedom_assistance(false), "Manual Moon force-channel selection refused")
	var gross := PackedFloat64Array()
	gross.resize(12)
	gross.fill(0.05)
	check(owner.advance_freedom_flight(1.0 / 120.0, gross, false), "Actual lunar all-channel actuator tick refused")
	var force: Dictionary = owner.get_freedom_flight_state()
	for name in ["positive_force_body", "negative_force_body", "positive_torque_body", "negative_torque_body"]:
		check(force.get(name) is PackedFloat64Array and force[name].size() == 3, "Missing actual Moon gross channel: " + name)
		for component in force[name]: check(is_finite(component) and component > 0.0, "Actual Moon channel came from zero legacy planetary actuation: " + name)
	check(view.exhaust.update_applied(force, 1.0 / 120.0, false) and view.exhaust.intensity > 0 and view.exhaust.lift_intensity > 0 and view.exhaust.withdrawal_intensity > 0, "Moon applied force did not drive all three actual exhaust channels")

func check_home(shell: Control, owner: Variant, assets: String, output: String) -> void:
	var view: Control = shell.current_view
	view.state = owner.get_freedom_flight_state()
	var charges: int = view.state.resources.jump_charges
	view.select_home()
	view.jump_requested()
	if not check(owner.get_freedom_flight_state().jump.phase == "spool" and view.error.is_empty(), "Actual Moon home-selection UI did not start charged return"): return
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual Moon-to-home travel tick refused"): return
		if tick == 360:
			var transit: Dictionary = owner.get_freedom_flight_state()
			check(MoonState.valid(transit) and transit.jump.phase == "transit" and transit.surface.gear_clearance == {"available": false}, "Actual committed Moon transit exposed a terrain gear-clearance witness")
	var home: Dictionary = owner.get_freedom_flight_state()
	if not check(home.get("body_kind") != "moon" and home.get("station_available", false) and home.jump.phase == "idle" and home.resources.jump_charges == charges - 1, "Charged return lost the real home planet/station/one-charge bill"): return
	view._process(0.0)
	await process_frame
	await process_frame
	var home_view: Control = shell.current_view
	if not check(home_view is FlightView and not home_view is MoonView and home_view.paused and home_view.state.station_available and home_view.error.is_empty(), "Moon-to-home deferred shell handoff retained the lunar consumer"): return
	home_view.set_process(false)
	var path := output.path_join("home-return.json")
	var bytes := save_bytes(owner, path)
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	var restored := shell_for(peer, path, assets)
	if restored == null: return
	check(restored.current_view is FlightView and not restored.current_view is MoonView and restored.current_view.paused and restored.current_view.state.station_available, "Home-return Continue selected a lunar or unpaused view")
	check(save_bytes(peer, output.path_join("home-continued.json")) == bytes, "Complete home-return Save/Continue changed authoritative state")
	restored.free()
	await process_frame

func check_outside(phases: String, assets: String, output: String) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, phases.path_join("outside.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	if not check(view is MoonView and view.surface_walking() and view.paused and is_equal_approx(view.state.surface_walk.heading, 0.7), "Actual nonzero-heading Moon walker Continue failed"):
		shell.free()
		return
	var frozen := save_bytes(owner, output.path_join("outside-before.json"))
	view._process(0.0)
	view._process(0.125)
	check(save_bytes(owner, output.path_join("outside-paused.json")) == frozen, "Paused Moon Continue advanced the surface actor")
	view.controls_menu.show_settings()
	view.request_surface_interaction()
	check(view.controls_menu.settings_view.visible and save_bytes(owner, output.path_join("outside-settings.json")) == frozen, "Nested Moon Settings permitted a hatch mutation")
	view.controls_menu.close_settings()
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Actual Moon walker could not explicitly resume after neutral"):
		shell.free()
		return
	view._process(1.0 / 120.0)
	check(is_equal_approx(view.state.surface_walk.heading, 0.7), "First resumed Moon tick reset the saved walking heading")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	var peer_shell := shell_for(peer, phases.path_join("outside.json"), assets)
	if peer_shell == null:
		shell.free()
		return
	check(peer.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, 0.7]), false), "Independent Moon neutral-continuation tick refused")
	check(save_bytes(owner, output.path_join("outside-resumed.json")) == save_bytes(peer, output.path_join("outside-peer.json")), "Actual Moon UI neutral continuation differs from complete direct next Save")
	peer_shell.free()
	await process_frame
	CompanionMouseChecks.outside(view, owner, output, check)
	var before := save_bytes(owner, output.path_join("outside-held-before.json"))
	key(KEY_W, true)
	view.request_surface_interaction()
	check(view.surface_walking() and save_bytes(owner, output.path_join("outside-held-after.json")) == before, "Held Moon walking key bypassed hatch neutral gate")
	key(KEY_W, false)
	key(KEY_E, true)
	check(Input.is_physical_key_pressed(KEY_E), "Actual surface interaction key was not physically held")
	if view.surface_walking(): view.request_surface_interaction()
	key(KEY_E, false)
	check(not view.surface_walking() and view.paused and view.error.is_empty(), "Actual E-held lunar hatch action was blocked by flight-only E mapping")
	shell.free()
	await process_frame

func check_walk_feedback(view: Control, owner: Variant, output: String) -> void:
	# Reuse the actual returned-phase contextual Leave above. Its fresh C++
	# ground entry faces the craft; no actor/terrain/heading fixture is rewritten.
	if not check(view.paused and view.surface_walking() and is_zero_approx(view.state.surface_walk.heading) and view.state.surface_walk.get("movement_blockage") == null, "Fresh actual Moon departure lacks neutral walking feedback"): return
	var initial: Dictionary = owner.get_freedom_flight_state()
	for value in [null, "terrain_unresolved", "water", "slope", "step", "craft_obstruction"]:
		var candidate := initial.duplicate(true)
		candidate.surface_walk.movement_blockage = value
		check(MoonState.valid(candidate), "Known optional Moon walking result refused: " + str(value))
	for value in [false, 1, 1.0, {}, [], "supported", "unknown"]:
		var candidate := initial.duplicate(true)
		candidate.surface_walk.movement_blockage = value
		check(not MoonState.valid(candidate), "Malformed Moon walking result accepted: " + str(value))
	var missing := initial.duplicate(true)
	missing.surface_walk.erase("movement_blockage")
	check(not MoonState.valid(missing) and owner.get_freedom_flight_state() == initial, "Missing Moon walking feedback accepted or admission mutated the actual source")
	view.player_input.persist = false
	view.player_input.device = -1
	view.player_input.install()
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Moon hull-feedback walk could not resume explicitly"): return
	key(KEY_W, true)
	for frame in 8: view._process(0.125)
	key(KEY_W, false)
	if not check(view.error.is_empty() and not view.paused and view.state.surface_walk.get("movement_blockage") == "craft_obstruction" and "Last step blocked" in view.primary_status.text and "Wayfarer hull" in view.primary_status.text and "On foot" in view.primary_status.text and not "Datum altitude" in view.primary_status.text, "Actual Moon walk into the ship lost its hull refusal or retained seated-craft instruments"): return
	check(int(view.state.tick) == int(initial.tick) + 120 and view.state.surface_walk.heading == initial.surface_walk.heading and view.state.resources == initial.resources, "Moon hull feedback changed the existing movement clock, heading or resources")
	# Compare the actual saved body-fixed foot, not the time-varying projected
	# eye relative to the rotating craft/tangent frame.
	var before_blocked: Dictionary = JSON.parse_string(save_bytes(owner, output.path_join("walk-blocked-before.json")).get_string_from_utf8())
	key(KEY_W, true)
	view._process(0.125)
	key(KEY_W, false)
	view.pause_controls("Inspecting actual blocked Moon step")
	var frozen: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join("walk-blocked.json")
	var blocked := save_bytes(owner, path)
	var blocked_document: Dictionary = JSON.parse_string(blocked.get_string_from_utf8())
	check(view.state.surface_walk.movement_blockage == "craft_obstruction" and blocked_document.actor.foot_position_metres == before_blocked.actor.foot_position_metres, "Moon hull obstruction allowed the actor to tunnel or lost its reason")
	view._process(0.125)
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([0, 0]), false) and owner.get_freedom_flight_state() == frozen and save_bytes(owner, output.path_join("walk-blocked-paused.json")) == blocked and "Wayfarer hull" in view.primary_status.text, "Moon pause/refused input changed blocked feedback or the complete Save")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(path), "Blocked Moon peer Continue staging refused"): return
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(not pending.is_empty() and peer.commit_pending_freedom_start(pending.candidate_id), "Blocked Moon peer Continue commit refused"): return
	check(peer.get_freedom_flight_state().surface_walk.get("movement_blockage") == null and save_bytes(peer, output.path_join("walk-blocked-continued.json")) == blocked, "Moon Continue restored transient blockage or changed saved state")
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Actual Moon retreat failed neutral Resume"): return
	key(KEY_S, true)
	view._process(0.125)
	key(KEY_S, false)
	var retreated := save_bytes(owner, output.path_join("walk-retreated.json"))
	var retreated_document: Dictionary = JSON.parse_string(retreated.get_string_from_utf8())
	check(view.error.is_empty() and view.state.surface_walk.get("movement_blockage") == null and not "Last step blocked" in view.primary_status.text and retreated_document.actor.foot_position_metres != blocked_document.actor.foot_position_metres, "Actual supported Moon retreat retained a stale hull warning or failed to move")
	check(peer.advance_freedom_surface_walk(0.125, PackedFloat64Array([-1, 0, 0]), false) and owner.get_freedom_flight_state() == peer.get_freedom_flight_state() and retreated == save_bytes(peer, output.path_join("walk-retreated-peer.json")), "Moon walking feedback changed the actual retreat or complete Save relative to direct C++ continuation")
	view.pause_controls("Moon feedback check complete")

func check_landed_hatch(view: Control, owner: Variant, output: String, label: String, controller: bool) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var prefix := output.path_join(label + "-context-hatch")
	var frozen := save_bytes(owner, prefix + "-before.json")
	if not check(view.paused and view.landed_hatch_available() and not view.surface_walking() and not frozen.is_empty(), "Actual supported seated Moon source lacks its contextual hatch: " + label): return
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(prefix + "-before.json"), "Direct landed Moon hatch peer staging refused"): return
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(peer.commit_pending_freedom_start(pending.candidate_id) and peer.begin_freedom_surface_walk(), "Direct actual Moon departure refused"): return
	var expected := save_bytes(peer, prefix + "-direct.json")
	# Every refusal observes the same real landed source; only presentation gates
	# change. No synthetic recovery, actor placement, tick or saved state is used.
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-paused.json") == frozen, "Paused contextual Moon hatch changed the complete Save")
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Landed Moon hatch could not explicitly resume after neutral"): return
	check(view.player_input.landed_hatch_enabled and not view.player_input.surface_interaction_enabled, "Landed hatch context reused the on-foot movement mode")
	view.controls_armed = false
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-unarmed.json") == frozen, "Unarmed contextual Moon hatch changed the complete Save")
	view.observe_neutral_controls()
	key(KEY_W, true)
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-held.json") == frozen and not view.surface_walking(), "Held W bypassed landed Moon hatch neutrality")
	key(KEY_W, false)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.request_surface_interaction()
	check(view.paused and save_bytes(owner, prefix + "-focus.json") == frozen, "Unfocused contextual Moon hatch changed the complete Save")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Focus return auto-resumed or prevented explicit Moon hatch rearm"): return
	view.save_dialog.show()
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-save-modal.json") == frozen, "Save modal allowed contextual Moon departure")
	view.save_dialog.hide()
	view.journey_dialog_open = true
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-journey-modal.json") == frozen, "Journey modal allowed contextual Moon departure")
	view.journey_dialog_open = false
	view.recovery_notified = true
	view.request_surface_interaction()
	check(save_bytes(owner, prefix + "-recovery-gate.json") == frozen, "Recovery presentation gate allowed contextual Moon departure")
	view.recovery_notified = false
	view.refresh_control_hint()
	if controller:
		# Connection notifications plus real button events simulate software pad
		# ownership, as in the existing input cases; this is not hardware testing.
		Input.joy_connection_changed.emit(0, true)
		Input.joy_connection_changed.emit(7, true)
		view.observe_neutral_controls()
		if view.paused: view.toggle_pause()
		if not check(view.player_input.device == 0 and not view.paused and view.controls_armed, "Selected controller could not explicitly resume landed Moon context"): return
		hatch_pad(view, 7, true)
		hatch_pad(view, 7, false)
		check(view.player_input.device == 0 and not view.surface_walking() and save_bytes(owner, prefix + "-foreign-pad.json") == frozen, "Foreign pad stole selection or opened the Moon hatch")
		hatch_pad(view, 0, true)
		hatch_pad(view, 0, false)
		Input.joy_connection_changed.emit(7, false)
		Input.joy_connection_changed.emit(0, false)
	else:
		var event := key(KEY_E, true)
		check(Input.is_physical_key_pressed(KEY_E), "Contextual Moon departure did not hold actual physical E")
		if not view.surface_walking(): view.player_input._input(event)
		key(KEY_E, false)
	var actual: Dictionary = owner.get_freedom_flight_state()
	check(view.surface_walking() and view.paused and view.error.is_empty() and view.look_offset == Vector2(-float(actual.surface_walk.get("heading", 0.0)), 0.0), "Contextual Moon departure failed fresh outside/pause/look handoff: " + label)
	check(actual.tick == before.tick and actual.resources == before.resources and save_bytes(owner, prefix + "-after.json") == expected, "Contextual Moon hatch differs from the same zero-tick direct C++ departure: " + label)

func marker_state_only(state: Dictionary) -> Dictionary:
	var result := state.duplicate(true)
	result.erase("surface_marker")
	return result

func activate_marker(view: Control, button: Button, controller := false) -> void:
	var mode := button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(button)
	check(button.visible and not button.disabled and button.has_focus() and button.can_process(), "Lunar marker action lacks keyboard/controller focus")
	if controller:
		for pressed in [true, false]:
			var event := InputEventJoypadButton.new()
			event.device = view.player_input.device
			event.button_index = JOY_BUTTON_A
			event.pressed = pressed
			Input.parse_input_event(event)
			Input.flush_buffered_events()
			await process_frame
	else:
		key(KEY_ENTER, true)
		await process_frame
		key(KEY_ENTER, false)
	button.process_mode = mode
	button.release_focus()

func check_marker_refusals(state: Dictionary, binding: Dictionary) -> void:
	for field in ["version", "selected", "present", "current_body", "can_mark", "can_clear", "position", "distance_metres", "system_id", "body_kind", "body_version", "body_id", "tick", "system_name", "body_name"]:
		var bad := state.duplicate(true)
		bad.surface_marker[field] = null
		check(not MoonState.valid(bad), "Malformed typed lunar marker field accepted: " + field)
	for damage in ["alias", "owner", "system", "kind", "body_version", "version_type", "future", "position", "range", "infinite", "extra"]:
		var bad := state.duplicate(true)
		match damage:
			"alias": bad.surface_marker.planet_id = state.body_id
			"owner": bad.surface_marker.body_id = "0" if state.body_id != "0" else "1"
			"system": bad.surface_marker.system_id = "0" if state.system_id != "0" else "1"
			"kind": bad.surface_marker.body_kind = "planet"
			"body_version": bad.surface_marker.body_version = 999
			"version_type": bad.surface_marker.version = 2.0
			"future": bad.surface_marker.tick = "18446744073709551615"
			"position": bad.surface_marker.position = Vector3(NAN, 0, 0)
			"range": bad.surface_marker.distance_metres = -1.0
			"infinite": bad.surface_marker.distance_metres = INF
			"extra": bad.surface_marker.extra = true
		var pending := {"candidate_id": "1", "mode": "freedom_flight", "craft_binding": binding, "walk_state": {}, "flight_state": bad, "station_state": {}, "station_geometry": {}, "streaming_ready": true}
		check(not MoonState.valid(bad) and not FlightView.valid_surface_marker(bad) and not Shell.valid_pending(pending) and not CockpitDisplays.valid_state(bad), "Malformed lunar marker passed a presentation admission gate: " + damage)

func check_moon_marker(phases: String, assets: String, output: String) -> void:
	# Continue the physically reached outside source, not a manufactured actor.
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, phases.path_join("outside.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	var initial: Dictionary = owner.get_freedom_flight_state()
	var records: Dictionary = owner.get_freedom_observations()
	var frozen := save_bytes(owner, output.path_join("marker-unselected.json"))
	check(view.paused and view.surface_walking() and initial.surface_marker.can_mark and not initial.surface_marker.present and view.ship_marker.label_text == "RETURN", "Actual Moon walker lacks marker/return controls")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.surface_marker_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.controls_menu.show_settings()
	view.surface_marker_button.pressed.emit()
	view.controls_menu.close_settings()
	check(save_bytes(owner, output.path_join("marker-refused.json")) == frozen, "Unfocused/nested Moon marker callback changed full Save")
	await activate_marker(view, view.surface_marker_button)
	var marked: Dictionary = owner.get_freedom_flight_state()
	var point: Dictionary = marked.surface_marker.duplicate(true)
	check(point.version == 2 and point.present and point.current_body and point.body_kind == "moon" and point.body_id == marked.body_id and point.body_version == marked.body_version and point.tick == marked.tick and not point.has("planet_id") and point.distance_metres < 0.000001, "Moon mark did not retain its genuine body/current supported foot")
	check(point.body_name == "Unresolved moon" and marker_state_only(marked) == marker_state_only(initial) and owner.get_freedom_observations() == records, "Moon mark invented a name/knowledge or changed physics/resources/clock")
	check(not owner.mark_freedom_surface_marker() and owner.get_freedom_flight_state() == marked, "Repeated Moon Mark silently overwrote the point")
	check_marker_refusals(marked, owner.get_freedom_craft_binding())
	await activate_marker(view, view.show_surface_marker_button)
	check(view.show_personal_marker and view.ship_marker.label_text == "MARK" and not view.home_marker.visible, "Lunar MARK overlapped RETURN or invented HOME")
	var heading: float = marked.surface_walk.heading
	for tick in 120:
		if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 1, heading]), false), "Actual marked Moon walk refused"): shell.free(); return
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	var moved: Dictionary = view.state
	check(moved.surface_marker.tick == point.tick and moved.surface_marker.distance_metres > 0.5 and "straight-line distance" in view.primary_status.text and "Hatch" in view.primary_status.text, "Walking moved the saved Moon marker or lost hatch/range information")
	var expected := Navigation.project(view.camera, moved.surface_marker.position, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.ship_marker.cue == expected, "Lunar MARK differs from the C++ current-body projection")
	check(not save_bytes(owner, output.path_join("moon-marker-walked.json")).is_empty(), "Actual marked walking phase Save unavailable")
	view.player_input.device = 0
	view.player_input.available_pads[0] = true
	view.player_input.install()
	var replace_records: Dictionary = owner.get_freedom_observations()
	await activate_marker(view, view.surface_marker_button, true)
	view.player_input.available_pads.erase(0)
	view.player_input.device = -2
	view.player_input.install()
	var replaced: Dictionary = owner.get_freedom_flight_state()
	check(replaced.surface_marker.tick == replaced.tick and replaced.surface_marker.tick != point.tick and replaced.surface_marker.distance_metres < 0.000001 and marker_state_only(replaced) == marker_state_only(moved) and owner.get_freedom_observations() == replace_records, "Selected-pad Moon Replace changed more than the actual foot metadata")
	var path := output.path_join("moon-personal-marker.json")
	var saved := save_bytes(owner, path)
	for repeat in 3: view.update_view(0.0)
	check(save_bytes(owner, path + ".review.json") == saved, "Reviewing lunar marker changed the complete Save")
	check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Moon personal marker Continue refused")
	view = shell.current_view
	view.set_process(false)
	check(view.paused and not view.show_personal_marker and view.state.surface_marker == replaced.surface_marker and view.ship_marker.label_text == "RETURN" and save_bytes(owner, path + ".continued.json") == saved, "Continue moved the Moon marker or retained its display selection")
	for tick in 120:
		if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, -1, heading]), false), "Actual Moon return walk refused"): shell.free(); return
	check(owner.return_from_freedom_surface_walk(), "Marked Moon walker failed actual hatch return")
	view.state = owner.get_freedom_flight_state()
	view.pause_controls("Inspect marked Moon from cockpit")
	view.update_view(0.0)
	await activate_marker(view, view.show_surface_marker_button)
	check(view.home_marker.visible and view.home_marker.label_text == "MARK" and not view.ship_marker.visible and view.state.surface_marker.tick == replaced.surface_marker.tick, "Reboarding lost Moon marker or used a phantom HOME target")
	check(owner.liftoff_freedom_surface(), "Marked Moon release refused")
	var up := PackedFloat64Array()
	up.resize(12)
	up[1] = 0.1
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, up, false), "Marked Moon actual ascent refused"): shell.free(); return
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	check(view.state.surface_marker.current_body and view.state.surface_marker.tick == replaced.surface_marker.tick and not view.state.surface.landed, "Ascent lost the fixed lunar point")
	var low := save_bytes(owner, output.path_join("marker-low-ascent.json"))
	view.select_home()
	check("Departure volume is obstructed" in view.save_status.text and "Fly clear" in view.save_status.text and save_bytes(owner, output.path_join("marker-low-refused.json")) == low, "Actual low lunar home-selection UI lost actionable C++ clearance feedback or changed the full Save")
	# Fly beyond the existing global relief/hull envelope before selecting home.
	# This is a bounded authored climb using actual thrusters, not a pose edit.
	up[1] = 1.0
	var climb_ticks := 0
	while view.state.altitude < 5000.0 and climb_ticks < 7200:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, up, false), "Marked Moon clearance climb refused"): shell.free(); return
		view.state = owner.get_freedom_flight_state()
		climb_ticks += 1
	if not check(view.state.altitude >= 5000.0 and view.state.surface_marker.tick == replaced.surface_marker.tick and view.state.resources.fraction > 0.0, "Bounded actual ascent did not reach clear local departure with its retained marker"): shell.free(); return
	view.update_view(0.0)
	await check_home(shell, owner, assets, output.path_join("marker"))
	view = shell.current_view
	var home: Dictionary = owner.get_freedom_flight_state()
	check(not view is MoonView and home.surface_marker.version == 2 and home.surface_marker.present and not home.surface_marker.current_body and not home.surface_marker.has("position") and not home.surface_marker.has("distance_metres") and view.show_surface_marker_button.disabled and "another body" in view.primary_status.text, "Home return lost Moon metadata or invented a local bearing")
	var before_clear: Dictionary = home.duplicate(true)
	var home_records: Dictionary = owner.get_freedom_observations()
	await activate_marker(view, view.clear_surface_marker_button)
	var cleared: Dictionary = owner.get_freedom_flight_state()
	check(cleared.surface_marker.version == 2 and cleared.surface_marker.selected and not cleared.surface_marker.present and marker_state_only(cleared) == marker_state_only(before_clear) and owner.get_freedom_observations() == home_records, "Off-body Clear changed more than the selected Moon marker")
	shell.free()
	await process_frame

func check_foreign_outing(phases: String, assets: String, output: String) -> void:
	# The C++ sidecar contains real paid travel after one labelled Home-space
	# placement. Repeat its Moon/return commands through the actual native UI.
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, phases.path_join("foreign-parent.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	if not check(view is FlightView and not view is MoonView, "Foreign parent did not stage the actual planetary view"):
		shell.free(); return
	view.select_moon_jump()
	var selected: Dictionary = owner.get_freedom_flight_state()
	if not check(selected.jump.get("route_kind") == "foreign_moon" and selected.resources.jump_charges == 2 and view.error.is_empty(), "Actual planet UI refused its genuine foreign Moon selection"):
		shell.free(); return
	view.jump_requested()
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Foreign cancel-spool tick refused"):
		shell.free(); return
	view.state = owner.get_freedom_flight_state()
	view.jump_requested()
	check(owner.get_freedom_flight_state().jump.phase == "idle" and owner.get_freedom_flight_state().resources.jump_charges == 2, "Actual UI cancellation spent a foreign jump charge")
	view.jump_requested()
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual foreign Moon travel tick refused: " + str(owner.get_last_error())):
			shell.free(); return
	var arrival: Dictionary = owner.get_freedom_flight_state()
	check(MoonState.valid(arrival) and arrival.resources.jump_charges == 1 and arrival.jump.phase == "idle", "Foreign arrival lost the typed Moon or starter's final charge")
	check(save_bytes(owner, output.path_join("foreign-arrival-native.json")) == FileAccess.get_file_as_bytes(phases.path_join("foreign-arrival.json")), "Native UI foreign arrival differs from the independent C++ full Save")
	shell.switch_journey_view()
	view = shell.current_view
	if not check(view is MoonView and view.paused and view.error.is_empty() and shell.error.is_empty(), "Foreign Moon failed the actual borrowed-Wayfarer view handoff"):
		shell.free(); return
	view.set_process(false)
	view.select_home()
	var home_choice: Dictionary = owner.get_freedom_flight_state()
	check(home_choice.jump.get("destination_name") == "Origin Home" and "Origin Home" in view.moon_select_button.text, "Foreign Moon UI did not identify direct charted Origin Home")
	view.jump_requested()
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual foreign-to-Home tick refused: " + str(owner.get_last_error())):
			shell.free(); return
	check(save_bytes(owner, output.path_join("foreign-home-native.json")) == FileAccess.get_file_as_bytes(phases.path_join("foreign-home.json")), "Native direct Home return differs from independent C++ full Save")
	shell.switch_journey_view()
	view = shell.current_view
	var home: Dictionary = owner.get_freedom_flight_state()
	check(view is FlightView and not view is MoonView and view.paused and view.error.is_empty() and shell.error.is_empty() and home.resources.jump_charges == 0 and not home.attached, "Three-charge outing did not return to free-flight Home with the actual planetary controls")
	shell.free()
	await process_frame
	for label in ["foreign-landed", "foreign-outside", "foreign-marked"]:
		var ground_owner: Variant = ClassDB.instantiate("FreedomBridge")
		var source := phases.path_join(label + ".json")
		var ground_shell := shell_for(ground_owner, source, assets)
		if ground_shell == null: continue
		var ground_view: Control = ground_shell.current_view
		var ground: Dictionary = ground_owner.get_freedom_flight_state()
		var original := FileAccess.get_file_as_bytes(source)
		if check(ground_view is MoonView and ground_view.paused and MoonState.valid(ground) and save_bytes(ground_owner, output.path_join(label + "-native.json")) == original, "Foreign supported/walking Continue failed: " + label) and label == "foreign-marked":
			check(ground.surface_marker.version == 3 and ground.surface_marker.present and ground.surface_marker.current_body and ground_view.surface_walking(), "Foreign marked foot state lost marker3 or its actual local bearing")
			var samples: Dictionary = ground_owner.get_freedom_field_samples()
			if check(samples.carried.size() == 1 and samples.journal.size() == 1, "Foreign specimen/retained note projection is missing"):
				var checker := preload("res://tests/native_field_samples.gd").new()
				checker.output = output.path_join("foreign-samples")
				DirAccess.make_dir_recursive_absolute(checker.output)
				await checker.check_archive(self, ground_view, ground_owner, original, samples.journal[0], true, "foreign")
				var reference: Variant = ClassDB.instantiate("FreedomBridge")
				var note: Dictionary = samples.journal[0]
				if check(reference.stage_freedom_continue(source) and reference.commit_pending_freedom_start(reference.get_pending_freedom_start().candidate_id) and reference.mark_freedom_field_sample_site(note.system_id, note.body_kind, note.body_id, note.site_id, true), "Independent foreign marker replacement refused"):
					var expected := save_bytes(reference, output.path_join("foreign-replace-direct.json"))
					ground_view.controls_menu.show_observations()
					var reader: Control = ground_view.controls_menu.observations_view
					for page in reader.titles().size():
						if reader.mark_button.visible: break
						reader.next_button.pressed.emit()
					if check(reader.mark_button.visible and not reader.mark_button.disabled and reader.mark_button.text == "Replace marker", "Foreign archive omitted the explicit same-body replacement action"):
						await checker.activate_archive(self, ground_view, reader.mark_button, false)
						check(save_bytes(ground_owner, output.path_join("foreign-replace-native.json")) == expected and ground_owner.get_freedom_field_samples() == samples, "Foreign archive replacement differs from direct C++ or changes specimens/first notes")
					reader.back_button.pressed.emit()
				failures += checker.failures
		ground_shell.free()
		await process_frame

func run() -> void:
	CompanionLayoutChecks.check_inputs(check)
	if failures: quit(1); return
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	var phases := args[0]
	var assets := args[1]
	var output := args[2]
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Moon native output unavailable"): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Actual native Moon bridge unavailable"): quit(1); return
	var originals := {}
	for label in PHASES: originals[label] = FileAccess.get_file_as_bytes(phases.path_join(label + ".json"))
	await CompanionMouseChecks.run(self, phases.path_join("arrival.json"), assets, output.path_join("mouse"), "moon", check)
	await CockpitGearChecks.run(self, phases.path_join("arrival.json"), assets, output.path_join("cockpit-gear"), check)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var arrival := charged_jump(shell, owner, assets)
	if arrival.is_empty(): shell.free(); quit(1); return
	check_state_refusals(arrival)
	check_rollback(shell, owner)
	shell.switch_journey_view()
	var view: Control = shell.current_view
	if not check(view is MoonView and view.paused and view.staged_model.valid_current_pose(), "Actual local arrival failed typed MoonView handoff"):
		shell.free()
		quit(1)
		return
	view.set_process(false)
	var arrived := save_bytes(owner, output.path_join("arrival-native.json"))
	check(arrived == originals.arrival, "Native charged Moon jump differs from continuous C++ arrival Save")
	# Own the first stream batch before diagnostics drives a paused view tick;
	# that tick can otherwise consume the batch through the terrain presenter.
	await check_cover(view, owner)
	await FrameDiagnosticsChecks.run(self, view, owner, output.path_join("arrival-native.json"), check)
	check_assistance(view, owner, output)
	await check_home(shell, owner, assets, output)
	shell.free()
	await process_frame
	for label in PHASES:
		var restored: Variant = ClassDB.instantiate("FreedomBridge")
		var phase_shell := shell_for(restored, phases.path_join(label + ".json"), assets)
		if phase_shell == null: continue
		check(phase_shell.current_view is MoonView and MoonState.valid(restored.get_freedom_flight_state()) and save_bytes(restored, output.path_join(label + "-continued.json")) == originals[label], "Complete Moon phase Save/Continue changed: " + label)
		check_gear_clearance(phase_shell.current_view, restored, output, label)
		var phase_view: Control = phase_shell.current_view
		if label in ["arrival", "outside"]:
			await CompanionLayoutChecks.run(self, phase_view, restored, output, "moon-" + label + "-companion", check)
		if label in ["approach", "landed"]:
			check_manual_moon_menu(phase_view, restored, output, label)
		var observed: Dictionary = restored.get_freedom_flight_state()
		if phase_view.moon_jump_button.disabled:
			check(not phase_view.moon_jump_button.tooltip_text.is_empty() and phase_view.moon_jump_button.tooltip_text == str(observed.jump.refusal), "Disabled Moon jump control omitted its actual C++ reason: " + label)
			if not observed.surface.landed and observed.surface_walk.is_empty() and observed.jump.phase == "idle":
				check("Jump unavailable" in phase_view.primary_status.text and str(observed.jump.refusal) in phase_view.primary_status.text, "Compact Moon HUD omitted controller-readable jump refusal: " + label)
		if label in ["landed", "returned"]:
			check_landed_hatch(phase_shell.current_view, restored, output, label, label == "returned")
			if label == "returned": check_walk_feedback(phase_shell.current_view, restored, output)
		phase_shell.free()
		await process_frame
	await check_outside(phases, assets, output)
	DirAccess.make_dir_recursive_absolute(output.path_join("marker"))
	await check_moon_marker(phases, assets, output)
	failures += await MoonExposures.run(self, phases, assets, output.path_join("exposures"))
	failures += await OriginServiceChecks.run(self, phases, assets, output.path_join("service"), Callable(self, "activate_marker"))
	await check_foreign_outing(phases, assets, output)
	for label in PHASES: check(FileAccess.get_file_as_bytes(phases.path_join(label + ".json")) == originals[label], "Native Moon test rewrote C++ source phase: " + label)
	print("Native Moon: %d failures" % failures)
	quit(1 if failures else 0)
