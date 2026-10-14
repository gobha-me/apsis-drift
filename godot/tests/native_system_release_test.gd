extends SceneTree
## Complete policy-selected station release, saved system flight and real capture.
## Headless native integration; no hardware or rendered-image acceptance.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
var failures := 0

func check(value: bool, message: String) -> bool:
	if not value:
		push_error(message)
		failures += 1
	return value

func _initialize() -> void:
	call_deferred("run")

func save_matches(owner: Variant, source: String, output: String) -> void:
	check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == FileAccess.get_file_as_bytes(source), "Complete system voyage differs: " + source.get_file())

func compare_projection(actual: Dictionary, expected: Dictionary, live_step := false) -> void:
	for key in ["universe_seed", "system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "tick", "checksum", "coordinate_frame", "frame_policy", "observation_frame", "canonical_position_metres", "canonical_velocity_metres_per_second", "position_metres", "velocity_metres_per_second", "orientation_wxyz", "angular_velocity_body", "body_basis", "station_position", "station_basis", "latitude", "longitude", "altitude", "planet_radius", "atmosphere_edge", "air_density", "star_direction", "lighting", "chart"]:
		check(actual.has(key) and expected.has(key) and actual[key] == expected[key], "System voyage projection differs: " + key)
	# Continue restores the ledger, while the last applied tick is transient.
	var actual_resources: Dictionary = actual.resources.duplicate()
	var expected_resources: Dictionary = expected.resources.duplicate()
	check(expected_resources.last_tick_quanta == "0" and expected_resources.equivalent_newtons == 0.0, "Continue invented a last applied resource tick")
	if live_step:
		check(actual_resources.last_tick_quanta.to_int() > 0 and actual_resources.equivalent_newtons > 0.0, "Live braking tick lost its applied resource reading")
	else:
		check(actual_resources.last_tick_quanta == "0" and actual_resources.equivalent_newtons == 0.0, "Capture retained a transient applied resource tick")
	for key in ["last_tick_quanta", "equivalent_newtons"]:
		actual_resources.erase(key)
		expected_resources.erase(key)
	check(actual_resources == expected_resources, "System voyage changed persistent resource readings")
	check(NativeEnvironment.valid(actual), "System voyage lost shared environment owner/time")

func check_gear_handoff(shell: Control, owner: Variant, assets: String, output: String) -> void:
	check(owner.set_freedom_assistance(false), "Manual gear integration selection refused")
	for stage in [{"name": "gear-deployed", "command": "request_freedom_landing", "frame": "planet_relative_inertial", "gear": true}, {"name": "gear-stowed", "command": "stow_freedom_landing_gear", "frame": "system_inertial", "gear": false}]:
		var view: Control = shell.current_view
		var model: int = view.staged_model.get_instance_id()
		var before: Dictionary = owner.get_freedom_flight_state()
		view.port_command(stage.command)
		var accepted: Dictionary = owner.get_freedom_flight_state()
		check(view.error.is_empty() and view.paused and view.staged_model.get_instance_id() == model and view.staged_model.valid_current_pose() and accepted.coordinate_frame == stage.frame and accepted.surface.gear_deployed == stage.gear and not accepted.surface.landed and accepted.surface.maneuver == "off", "Actual manual gear action lost frame, model, gear or pause: " + stage.name)
		var path := output.path_join(stage.name + ".json")
		check(owner.save_freedom_as(path), "Complete gear Save As refused: " + stage.name)
		var encoded := FileAccess.get_file_as_bytes(path)
		if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Gear staged Continue refused: " + shell.error): return
		view = shell.current_view
		view.set_process(false)
		var restored: Dictionary = owner.get_freedom_flight_state()
		check(view is FlightView and view.paused and view.activated and view.staged_model.valid_current_pose() and restored.coordinate_frame == stage.frame and restored.surface.gear_deployed == stage.gear and not restored.surface.landed, "Gear Continue lost selected frame/readiness: " + stage.name)
		for key in ["tick", "orientation_wxyz", "angular_velocity_body", "universe_seed", "system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "frame_policy", "assistance"]:
			check(before[key] == accepted[key] and restored[key] == accepted[key], "Gear handoff/Continue changed physical identity or motion: " + key)
		check(owner.save_freedom_as(output.path_join(stage.name + "-continued.json")) and FileAccess.get_file_as_bytes(output.path_join(stage.name + "-continued.json")) == encoded and FileAccess.get_file_as_bytes(path) == encoded, "Gear Continue changed complete save bytes/source: " + stage.name)

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge") and DirAccess.make_dir_recursive_absolute(args[2]) == OK, "System release bridge/output unavailable"):
		quit(1); return
	var manifest: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[0].path_join("manifest.json")))
	if not check(manifest is Dictionary and manifest.get("schema") == "apsis.native-system-release-fixtures/1", "System release fixture manifest unavailable"):
		quit(1); return
	var hashes := {}
	for checkpoint in manifest.checkpoints:
		var path: String = args[0].path_join(checkpoint.file)
		hashes[path] = FileAccess.get_sha256(path)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(not owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Uninitialized system flight accepted controls")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	if not check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("attached.json"), "assets": args[1]}), "Selected attached starter failed: " + shell.error):
		shell.free(); quit(1); return
	var view: Control = shell.current_view
	if not check(view is FlightView and view.paused and view.activated, "Selected attached view unavailable/unpaused"):
		shell.free(); quit(1); return
	# This is the actual completed seated hardware pose, not installation rest.
	if not check(view.staged_model.valid_current_pose(), "Selected seated model pose is invalid"):
		shell.free(); quit(1); return
	view.set_process(false)
	var model: int = view.staged_model.get_instance_id()
	var attached: Dictionary = owner.get_freedom_flight_state()
	check(attached.attached and attached.coordinate_frame == "planet_relative_inertial" and attached.frame_policy == 2 and attached.frame_id == "2", "Selected attached canonical frame confused with craft recipe")
	save_matches(owner, args[0].path_join("attached.json"), args[2].path_join("attached.json"))
	view.port_command("release_freedom_port")
	var released: Dictionary = owner.get_freedom_flight_state()
	check(view.error.is_empty() and view.paused and not released.attached and released.tick == attached.tick and released.coordinate_frame == "system_inertial" and released.observation_frame == "planet_relative_inertial" and view.staged_model.get_instance_id() == model, "Real undock lost canonical frame, time, pause or retained model")
	check(released.orientation_wxyz == attached.orientation_wxyz and released.angular_velocity_body == attached.angular_velocity_body and released.canonical_position_metres != released.position_metres, "System release reset attitude/spin or disguised absolute vector as local")
	save_matches(owner, args[0].path_join("released.json"), args[2].path_join("released.json"))
	var before: Dictionary = owner.get_freedom_flight_state()
	var invalid := neutral.duplicate()
	invalid[0] = NAN
	check(not owner.advance_freedom_flight(1.0 / 120.0, invalid, true) and owner.get_freedom_flight_state() == before, "Paused system flight ignored malformed input")
	check(not owner.advance_freedom_flight(1.0 / 120.0, PackedFloat64Array([0.0]), false) and owner.get_freedom_flight_state() == before, "Short system control buffer changed voyage")
	for delta in [0.0, 0.001, 0.1]: view.update_view(delta)
	check(view.error.is_empty() and owner.get_freedom_flight_state() == before and view.staged_model.get_instance_id() == model, "Presentation refresh changed system authority or model")
	save_matches(owner, args[0].path_join("released.json"), args[2].path_join("refreshed.json"))
	if not check(shell.select_start(owner, {"mode": "continue", "value": args[2].path_join("released.json"), "assets": args[1]}), "Complete system Continue failed: " + shell.error):
		shell.free(); quit(1); return
	view = shell.current_view
	view.set_process(false)
	model = view.staged_model.get_instance_id()
	check(view.paused and owner.get_freedom_flight_state() == released, "System Continue changed complete native projection")
	for segment in manifest.steps:
		var demand := PackedFloat64Array()
		for key in ["positive_translation", "negative_translation", "positive_rotation", "negative_rotation"]:
			for component in segment[key]: demand.append(float(component))
		for tick in int(segment.ticks):
			var previous: Dictionary = owner.get_freedom_flight_state()
			check(owner.advance_freedom_flight(1.0 / 120.0, demand, false), "Actual held system-flight tick refused")
			var accepted: Dictionary = owner.get_freedom_flight_state()
			check(accepted.resources.last_tick_quanta.to_int() == previous.resources.quantity_quanta.to_int() - accepted.resources.quantity_quanta.to_int() and not accepted.resources.propulsion_refused, "Applied resource debit differs from the actual ledger change")
			view.state = accepted
			view.update_view(0.0)
			check(view.error.is_empty() and view.staged_model.get_instance_id() == model and view.state.tick == accepted.tick and view.ship.basis == accepted.body_basis, "System tick replaced model or retained stale body/time")
			check(view.sky_material.get_shader_parameter("sun_direction") == accepted.star_direction and view.light.basis.z.distance_to(accepted.star_direction) < 0.000002 and view.local_light.basis == view.light.basis, "System tick retained stale or split sky/terrain/body lighting")
	var oracle: Variant = ClassDB.instantiate("FreedomBridge")
	# Complete starter voyages require the same asset-readiness transaction as
	# player Continue; the legacy bare-flight initializer must refuse them.
	var oracle_shell := Shell.new()
	oracle_shell.presentation_only = true
	oracle_shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(oracle_shell)
	if not check(oracle_shell.select_start(oracle, {"mode": "continue", "value": args[0].path_join("stepped.json"), "assets": args[1]}), "Independent stepped staged Continue refused: " + oracle_shell.error):
		oracle_shell.free(); shell.free(); quit(1); return
	oracle_shell.current_view.set_process(false)
	compare_projection(owner.get_freedom_flight_state(), oracle.get_freedom_flight_state(), true)
	save_matches(owner, args[0].path_join("stepped.json"), args[2].path_join("stepped.json"))
	check_gear_handoff(oracle_shell, oracle, args[1], args[2])
	# Inspect only the oracle copy; preserve the live capture/export trace.
	var approach_view: Control = oracle_shell.current_view
	var approach_source: Dictionary = oracle.get_freedom_flight_state()
	var approach_model: int = approach_view.staged_model.get_instance_id()
	check(approach_source.coordinate_frame == "system_inertial" and approach_source.port_approach.available and approach_view.approach_button.visible and not approach_view.approach_button.disabled and oracle.get_freedom_flight_state() == approach_source, "System approach query/button unavailable or mutating")
	save_matches(oracle, args[2].path_join("gear-stowed-continued.json"), args[2].path_join("approach-query.json"))
	approach_view.approach_button.pressed.emit()
	var approach_begun: Dictionary = oracle.get_freedom_flight_state()
	check(approach_view.error.is_empty() and approach_view.paused and approach_view.staged_model.get_instance_id() == approach_model and approach_begun.coordinate_frame == "planet_relative_inertial" and approach_begun.port_approach.active and not approach_begun.attached, "Actual approach button lost relative handoff, arming, model or pause")
	for key in ["tick", "orientation_wxyz", "angular_velocity_body", "universe_seed", "system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "frame_policy"]:
		check(approach_source[key] == approach_begun[key], "Approach action changed motion or selected identity: " + key)
	var approach_path: String = args[2].path_join("approach-begun.json")
	check(oracle.save_freedom_as(approach_path), "Complete armed approach Save refused")
	approach_view.approach_button.pressed.emit()
	check(not oracle.get_freedom_flight_state().port_approach.active and oracle.get_freedom_flight_state().coordinate_frame == "planet_relative_inertial", "Actual Cancel invented a system return or retained aid")
	save_matches(oracle, approach_path, args[2].path_join("approach-canceled.json"))
	# Restart only the oracle from the unchanged canonical system checkpoint.
	if not check(oracle_shell.select_start(oracle, {"mode": "continue", "value": args[0].path_join("stepped.json"), "assets": args[1]}), "Jump oracle staged Continue refused: " + oracle_shell.error):
		oracle_shell.free(); shell.free(); quit(1); return
	var jump_view: Control = oracle_shell.current_view
	jump_view.set_process(false)
	var jump_source: Dictionary = oracle.get_freedom_flight_state()
	var selected_known := false
	for ordinal in jump_source.chart.rows.size():
		if jump_source.chart.rows[ordinal].system_id == jump_source.jump.selected: jump_view.select_jump_destination(ordinal); selected_known = true; break
	check(selected_known and str(oracle.get_last_error()).is_empty() and jump_source.coordinate_frame == "system_inertial" and oracle.get_freedom_flight_state() == jump_source and jump_source.jump.available and jump_view.jump_button.visible and not jump_view.jump_button.disabled, "Known selection/query moved system body or disabled available jump")
	save_matches(oracle, args[0].path_join("stepped.json"), args[2].path_join("jump-query.json"))
	for phase in ["spool", "idle"]:
		var jump_model: int = jump_view.staged_model.get_instance_id()
		jump_view.jump_button.pressed.emit()
		var accepted_jump: Dictionary = oracle.get_freedom_flight_state()
		check(jump_view.error.is_empty() and jump_view.paused and jump_view.staged_model.get_instance_id() == jump_model and accepted_jump.coordinate_frame == "planet_relative_inertial" and accepted_jump.jump.phase == phase and accepted_jump.resources.jump_charges == jump_source.resources.jump_charges, "Actual jump begin/cancel changed model, frame, phase, pause or charge")
		for key in ["tick", "orientation_wxyz", "angular_velocity_body", "universe_seed", "system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "frame_policy"]: check(accepted_jump[key] == jump_source[key], "Jump begin/cancel changed motion or identity: " + key)
		var jump_path: String = args[2].path_join("jump-" + phase + ".json")
		check(oracle.save_freedom_as(jump_path), "Complete jump phase Save refused")
		var jump_bytes := FileAccess.get_file_as_bytes(jump_path)
		if not check(oracle_shell.select_start(oracle, {"mode": "continue", "value": jump_path, "assets": args[1]}), "Jump phase staged Continue refused: " + oracle_shell.error): oracle_shell.free(); shell.free(); quit(1); return
		jump_view = oracle_shell.current_view
		jump_view.set_process(false)
		check(jump_view.paused and jump_view.activated and oracle.get_freedom_flight_state().jump.phase == phase and oracle.save_freedom_as(args[2].path_join("jump-" + phase + "-continued.json")) and FileAccess.get_file_as_bytes(args[2].path_join("jump-" + phase + "-continued.json")) == jump_bytes and FileAccess.get_file_as_bytes(jump_path) == jump_bytes, "Jump staged Continue changed complete phase/source bytes")
	var capture_tick: String = str(owner.get_freedom_flight_state().tick)
	view.port_command("capture_freedom_port")
	var captured: Dictionary = owner.get_freedom_flight_state()
	check(view.error.is_empty() and view.paused and captured.attached and captured.coordinate_frame == "planet_relative_inertial" and captured.tick == capture_tick and view.staged_model.get_instance_id() == model, "Real capture lost its constrained relative state/model")
	if not check(oracle_shell.select_start(oracle, {"mode": "continue", "value": args[0].path_join("captured.json"), "assets": args[1]}), "Independent captured staged Continue refused: " + oracle_shell.error):
		oracle_shell.free(); shell.free(); quit(1); return
	oracle_shell.current_view.set_process(false)
	compare_projection(captured, oracle.get_freedom_flight_state())
	save_matches(owner, args[0].path_join("captured.json"), args[2].path_join("captured.json"))
	for path in hashes: check(FileAccess.get_sha256(path) == hashes[path], "System flight rewrote an original fixture")
	oracle_shell.free()
	shell.free()
	print("Native system release: %d failures" % failures)
	quit(0 if failures == 0 else 1)
