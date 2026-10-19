extends SceneTree
## Real charged local travel and typed lunar UI against a continuous C++ route.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const MoonView = preload("res://scripts/native/native_moon_view.gd")
const MoonState = preload("res://scripts/native/native_moon_state.gd")
const MoonTerrain = preload("res://scripts/native/native_moon_terrain.gd")
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

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

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

func run() -> void:
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
	await check_cover(view, owner)
	check_assistance(view, owner, output)
	await check_home(shell, owner, assets, output)
	shell.free()
	await process_frame
	for label in PHASES:
		var restored: Variant = ClassDB.instantiate("FreedomBridge")
		var phase_shell := shell_for(restored, phases.path_join(label + ".json"), assets)
		if phase_shell == null: continue
		check(phase_shell.current_view is MoonView and MoonState.valid(restored.get_freedom_flight_state()) and save_bytes(restored, output.path_join(label + "-continued.json")) == originals[label], "Complete Moon phase Save/Continue changed: " + label)
		phase_shell.free()
		await process_frame
	await check_outside(phases, assets, output)
	for label in PHASES: check(FileAccess.get_file_as_bytes(phases.path_join(label + ".json")) == originals[label], "Native Moon test rewrote C++ source phase: " + label)
	print("Native Moon: %d failures" % failures)
	quit(1 if failures else 0)
