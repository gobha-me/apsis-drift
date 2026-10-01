extends SceneTree
## Real save-selection boundary; no scene, GPU, or synthetic flight start.

var failures := 0


func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func coordinates(value: Variant) -> bool:
	if not value is PackedFloat64Array or value.size() != 3:
		return false
	for component in value:
		if not is_finite(component):
			return false
	return true


func reject_seed(bridge: Variant, seed: String, expected: Dictionary) -> void:
	check(not bridge.initialize_freedom_new_game(seed), "Invalid seed accepted: " + seed)
	check(not str(bridge.get_last_error()).is_empty(), "Seed refusal lacks diagnostic")
	check(bridge.get_freedom_start() == expected, "Seed refusal changed native start")


func reject_path(bridge: Variant, path: String, expected: Dictionary) -> void:
	check(not bridge.initialize_freedom_continue(path), "Invalid save accepted: " + path)
	check(not str(bridge.get_last_error()).is_empty(), "Save refusal lacks diagnostic")
	check(bridge.get_freedom_start() == expected, "Save refusal changed native start")


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 6:
		push_error("Expected station/career/corrupt saves, snapshot and flight save")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the live C++ bridge first")
		quit(1)
		return
	var source_bytes: Array[PackedByteArray] = []
	for path in args.slice(0, 4):
		source_bytes.append(FileAccess.get_file_as_bytes(path))
	var flight_bytes := FileAccess.get_file_as_bytes(args[5])
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.get_freedom_start().is_empty(), "Uninitialized bridge exposed a start")
	var extreme: Variant = ClassDB.instantiate("FreedomBridge")
	check(extreme.initialize_freedom_new_game("18446744073709551615"), "Maximum uint64 seed refused")
	check(extreme.get_freedom_walk_state().universe_seed == "18446744073709551615", "Maximum seed lost precision through Godot")
	check(bridge.initialize_freedom_new_game("42"), "Freedom New Game refused: " + str(bridge.get_last_error()))
	var new_actor: Dictionary = bridge.get_freedom_walk_state()
	check(not new_actor.is_empty() and new_actor.universe_seed == "42" and new_actor.tick == "0" and not new_actor.continued, "Ordinary New Game lost its station actor")
	check(bridge.get_freedom_start().is_empty() and not bridge.get_freedom_flight_state().is_empty(), "New actor was confused with the historical frozen station shell")
	check(bridge.initialize_freedom_continue(args[0]), "Historical station17 Continue refused")
	var fresh: Dictionary = bridge.get_freedom_start()
	check(fresh.mode == "freedom" and fresh.universe_seed == "42" and fresh.tick == "0" and fresh.continued, "Historical station identity or clock changed")
	check(fresh.station_id != "0" and fresh.craft_id != "0" and fresh.system_id != "0" and fresh.home_planet_id != "0", "C++ identities were lost")
	check(fresh.discovery_count == 0 and fresh.world_delta_count == 0, "New Game invented mutable history")
	check(fresh.home_planet_radius_metres is float and fresh.home_planet_radius_metres >= 5000000.0 and fresh.home_planet_radius_metres <= 6500000.0, "Physical home radius was lost")
	for key in ["host_position_metres", "host_velocity_metres_per_second", "station_position_metres", "station_velocity_metres_per_second", "station_relative_position_metres", "station_relative_velocity_metres_per_second"]:
		check(coordinates(fresh[key]), "Invalid finite binary64 coordinates: " + key)
	var station_distance: float = Vector3(fresh.station_relative_position_metres[0], fresh.station_relative_position_metres[1], fresh.station_relative_position_metres[2]).length()
	check(station_distance - fresh.home_planet_radius_metres >= 400000.0 and station_distance - fresh.home_planet_radius_metres <= 600000.0, "Station-local host clearance differs from C++ orbit")
	var shell: Control = load("res://native_start_shell.gd").new()
	check(shell.valid_start(fresh), "Native shell rejected C++ station geometry")
	check(not extreme.get_freedom_walk_state().is_empty(), "Maximum-seed actor geometry is unavailable")
	var malformed := fresh.duplicate(true)
	malformed.home_planet_radius_metres = INF
	check(not shell.valid_start(malformed), "Native shell accepted nonfinite host radius")
	malformed = fresh.duplicate(true)
	malformed.station_relative_position_metres = PackedFloat64Array([fresh.home_planet_radius_metres * 0.5, 0.0, 0.0])
	check(not shell.valid_start(malformed), "Native shell accepted intersecting host/station geometry")
	malformed = fresh.duplicate(true)
	malformed.station_relative_position_metres = PackedFloat64Array([NAN, 0.0, 0.0])
	check(not shell.valid_start(malformed), "Native shell accepted nonfinite station position")
	malformed = fresh.duplicate(true)
	malformed.station_relative_position_metres = PackedFloat64Array([0.0, 0.0])
	check(not shell.valid_start(malformed), "Native shell accepted truncated station position")
	shell.free()
	for axis in 3:
		check(abs(fresh.station_position_metres[axis] - fresh.host_position_metres[axis] - fresh.station_relative_position_metres[axis]) <= 1.0, "Station position differs from C++ host composition")
		check(abs(fresh.station_velocity_metres_per_second[axis] - fresh.host_velocity_metres_per_second[axis] - fresh.station_relative_velocity_metres_per_second[axis]) <= 0.001001, "Station velocity differs from C++ host composition")
	check(is_finite(fresh.station_phase_radians), "Station phase is nonfinite")
	check(bridge.get_state().is_empty(), "Station bootstrap invented surface flight")
	check(bridge.initialize_freedom_continue(args[0]), "Tick-zero Continue refused: " + str(bridge.get_last_error()))
	var continued: Dictionary = bridge.get_freedom_start()
	check(continued.continued, "Continue lost selected-save origin")
	check(continued == fresh, "Repeated historical tick-zero Continue differs")
	check(bridge.initialize_freedom_continue(args[1]), "Progressed Continue refused: " + str(bridge.get_last_error()))
	var progressed: Dictionary = bridge.get_freedom_start()
	check(progressed.tick == "25" and progressed.cycle_tick == "25", "Saved clock was discarded")
	check(progressed.station_position_metres != fresh.station_position_metres, "Station ephemeris stayed at tick zero")
	check(progressed.station_phase_radians != fresh.station_phase_radians, "Station phase stayed at tick zero")
	check(progressed.discovery_count == 1 and progressed.world_delta_count == 1, "Saved mutable history was discarded")
	check(progressed.station_id == fresh.station_id and progressed.craft_id == fresh.craft_id, "Continue changed seeded identities")
	for bad_seed in ["", "01", "-1", "18446744073709551616", "nan", " 42", "42x"]:
		reject_seed(bridge, bad_seed, progressed)
	for bad_path in ["", "relative.json", args[2], args[3], args[0] + ".missing"]:
		reject_path(bridge, bad_path, progressed)
	check(bridge.initialize_freedom_continue(args[5]), "Supported saved flight refused")
	var flight: Dictionary = bridge.get_freedom_flight_state()
	check(flight.mode == "freedom_flight" and flight.tick == "25" and bridge.get_freedom_start().is_empty() and bridge.get_state().is_empty(), "Flight owner did not replace station without a study spawn")
	check(not bridge.initialize_freedom_continue(args[3]) and bridge.get_freedom_flight_state() == flight, "Corrupt Continue changed saved flight")
	check(bridge.initialize_freedom_continue(args[1]) and bridge.get_freedom_flight_state().is_empty(), "Station Continue failed to replace saved flight")
	reject_path(bridge, "/" + "x".repeat(4097), progressed)
	check(not bridge.initialize("{broken snapshot"), "Malformed study snapshot accepted")
	check(bridge.get_freedom_start() == progressed, "Rejected snapshot destroyed native station start")
	var error: String = bridge.get_last_error()
	check(bridge.get_freedom_start() == progressed and bridge.get_last_error() == error, "Read-only start query changed state or error")
	var snapshot := FileAccess.get_file_as_string(args[4])
	check(bridge.initialize(snapshot), "Snapshot fixture failed after native start")
	check(bridge.get_freedom_start().is_empty() and not bridge.get_state().is_empty(), "Snapshot did not replace native session")
	var snapshot_state: Dictionary = bridge.get_state()
	check(not bridge.initialize_freedom_continue(args[3]), "Corrupt save accepted after snapshot")
	check(bridge.get_state() == snapshot_state and bridge.get_freedom_start().is_empty(), "Refusal destroyed snapshot session")
	check(bridge.initialize_freedom_new_game("42"), "New Game failed after snapshot")
	check(bridge.get_state().is_empty() and bridge.get_freedom_start().is_empty() and bridge.get_freedom_walk_state() == new_actor, "Ordinary station actor did not replace fixture session")
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == source_bytes[i], "Start or refusal modified source save")
	check(FileAccess.get_file_as_bytes(args[5]) == flight_bytes, "Flight Continue modified source save")
	print("Freedom station bootstrap: %d failures" % failures)
	quit(0 if failures == 0 else 1)
