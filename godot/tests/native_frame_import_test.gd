extends SceneTree
## Actual system-inertial import and independently exported canonical C++ saves.
## Headless scene integration does not qualify visible pixels or hardware.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
var failures := 0

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failures += 1
	return value

func _initialize() -> void:
	call_deferred("run")

func save_matches(owner: Variant, expected: String, output: String) -> bool:
	return check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == FileAccess.get_file_as_bytes(expected), "Complete canonical Save As differs: " + expected.get_file())

func check_projection(actual: Dictionary, expected: Dictionary) -> void:
	# Compare independently opened canonical state, including complete body/star
	# readouts, rather than subtracting astronomical vectors in presentation.
	for key in ["universe_seed", "system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "tick", "checksum", "position_metres", "velocity_metres_per_second", "orientation_wxyz", "angular_velocity_body", "body_basis", "station_position", "station_basis", "latitude", "longitude", "altitude", "planet_radius", "atmosphere_edge", "air_density", "star_direction", "lighting"]:
		check(actual.has(key) and expected.has(key) and actual[key] == expected[key], "Imported canonical projection differs: " + key)
	check(NativeEnvironment.valid(actual), "Imported body/star projection has invalid ownership or clock")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge") and DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Native import bridge/output unavailable"):
		quit(1)
		return
	var source := args[0].path_join("system-import.json")
	var before := args[0].path_join("canonical-before.json")
	var after := args[0].path_join("canonical-after.json")
	var malformed := args[0].path_join("malformed-import.json")
	var source_hashes := {}
	for path in [source, before, after, malformed]:
		if not check(FileAccess.file_exists(path), "Missing authoritative import fixture: " + path.get_file()):
			quit(1)
			return
		source_hashes[path] = FileAccess.get_sha256(path)
	var source_document: Variant = JSON.parse_string(FileAccess.get_file_as_string(source))
	var canonical_document: Variant = JSON.parse_string(FileAccess.get_file_as_string(before))
	if not check(source_document is Dictionary and source_document.get("format_version") == 35 and source_document.state.location == "system_flight" and canonical_document is Dictionary and canonical_document.get("format_version") == 31 and canonical_document.state.location == "planetary_flight", "Exporter did not provide distinct system-import and canonical planetary formats"):
		quit(1)
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	# Refuse the invalid physical owner before constructing any presentation.
	check(not owner.stage_freedom_continue(malformed) and not str(owner.get_last_error()).is_empty() and owner.get_pending_freedom_start().is_empty() and owner.get_freedom_flight_state().is_empty(), "Malformed import acquired an active or pending world")
	var oracle: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(oracle.initialize_freedom_continue(before), "Independent canonical before fixture refused"):
		quit(1)
		return
	var expected: Dictionary = oracle.get_freedom_flight_state()
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": args[1]}), "System import did not stage through ordinary Continue: " + shell.error):
		shell.free()
		quit(1)
		return
	var view: Control = shell.current_view
	if not check(view is FlightView and view.activated and view.paused and view.error.is_empty() and view.staged_model.valid_installed() and view.exhaust.valid_bound_skin(), "Imported ordinary flight presenter is incomplete or unpaused"):
		shell.free()
		quit(1)
		return
	view.set_process(false)
	var imported: Dictionary = owner.get_freedom_flight_state()
	check(imported == expected, "Complete imported projection differs from independently opened canonical before")
	check_projection(imported, expected)
	check(imported.tick == "25" and imported.terrain_policy == 2 and imported.orientation_wxyz != PackedFloat64Array([1.0, 0.0, 0.0, 0.0]) and imported.angular_velocity_body != PackedFloat64Array([0.0, 0.0, 0.0]), "Fixture lost saved policy, time, full orientation or body spin")
	check(not imported.resources.selected and not imported.chart.selected and imported.impact == {"selected": false} and owner.get_freedom_walk_state().is_empty() and shell.recovery_overlay == null, "Bare physical import invented resource, knowledge, impact, walker or recovery state")
	var before_copy := args[2].path_join("canonical-before-copy.json")
	save_matches(owner, before, before_copy)
	check(FileAccess.get_sha256(source) == source_hashes[source], "Canonical Save As rewrote original system source")
	# Presentation cadence/look never transforms or advances the imported save.
	for delta in [0.0, 0.001, 0.1]:
		view.update_view(delta)
	check(owner.get_freedom_flight_state() == imported, "Presentation refresh changed imported authoritative state")
	save_matches(owner, before, args[2].path_join("after-presentation.json"))
	var neutral := PackedFloat64Array([0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0])
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "One actual neutral C++ tick refused")
	var stepped: Dictionary = owner.get_freedom_flight_state()
	check(stepped.tick == "26" and stepped.checksum != imported.checksum, "Neutral import step failed to advance exactly one authoritative tick")
	var after_copy := args[2].path_join("canonical-after-copy.json")
	save_matches(owner, after, after_copy)
	check(oracle.initialize_freedom_continue(after), "Independent canonical after fixture refused")
	check_projection(stepped, oracle.get_freedom_flight_state())
	if not check(shell.select_start(owner, {"mode": "continue", "value": after_copy, "assets": args[1]}), "Canonical saved import could not resume: " + shell.error):
		shell.free()
		quit(1)
		return
	view = shell.current_view
	view.set_process(false)
	var resumed: Dictionary = owner.get_freedom_flight_state()
	check(view is FlightView and view.paused and owner.get_pending_freedom_start().is_empty(), "Canonical import resume lost paused ordinary flight")
	check_projection(resumed, oracle.get_freedom_flight_state())
	save_matches(owner, after, args[2].path_join("resumed-copy.json"))
	var stream: Dictionary = owner.get_stream_identity()
	check(not shell.select_start(owner, {"mode": "continue", "value": malformed, "assets": args[1]}) and not shell.error.is_empty(), "Malformed import replaced an accepted canonical game")
	check(shell.current_view == view and view.paused and owner.get_freedom_flight_state() == resumed and owner.get_stream_identity() == stream and owner.get_pending_freedom_start().is_empty(), "Refused import changed accepted view, world or terrain generation")
	save_matches(owner, after, args[2].path_join("after-refusal.json"))
	for path in source_hashes:
		check(FileAccess.get_sha256(path) == source_hashes[path], "Native consumer changed an authoritative import fixture: " + path.get_file())
	check(FileAccess.get_file_as_bytes(after_copy) == FileAccess.get_file_as_bytes(after), "Refused replacement modified resumed source bytes")
	var report := FileAccess.open(args[2].path_join("frame-import-report.json"), FileAccess.WRITE)
	if check(report != null, "Import qualification report unavailable"):
		report.store_string(JSON.stringify({"schema_version": 1, "pass": failures == 0, "scope": "headless actual Continue/presentation/canonical Save As/one C++120Hzneutral tick/resume/refusal; no visible-pixel or hardware claim", "source_sha256": source_hashes, "canonical_before_sha256": FileAccess.get_sha256(before_copy), "canonical_after_sha256": FileAccess.get_sha256(after_copy), "before_tick": imported.tick, "after_tick": stepped.tick, "bridge_sha256": FileAccess.get_sha256("res://bin/libapsis_freedom_bridge.so")}, "\t") + "\n")
		report.close()
	shell.free()
	shell = null
	view = null
	owner = null
	oracle = null
	report = null
	await process_frame
	print("Native system-frame import: %d failures" % failures)
	quit(1 if failures else 0)
