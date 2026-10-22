extends SceneTree
## Actual bridge commands and physics from a labelled saved-space approach.
## This does not qualify an uninterrupted manual journey or GPU presentation.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const Derelict = preload("res://scripts/native/native_derelict_view.gd")
const ViewChecks = preload("res://tests/native_derelict_view_test.gd")
var failures := 0

func check(value: bool, message: String) -> bool:
	if not value:
		push_error("Native derelict: " + message)
		failures += 1
	return value

func _initialize() -> void:
	call_deferred("run")

func checkpoint(owner: Variant, output: String, source: String) -> void:
	check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == FileAccess.get_file_as_bytes(source), "Complete Save differs from actual C++ phase: " + source.get_file())

func key(code: Key, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	await process_frame

func peer_from(path: String) -> Variant:
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(path), "Independent Continue staging refused"): return null
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(not pending.is_empty() and peer.commit_pending_freedom_start(pending.candidate_id), "Independent Continue commit refused"): return null
	return peer

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	var phases: String = args[0]
	var assets: String = args[1]
	var output: String = args[2]
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge") and DirAccess.make_dir_recursive_absolute(output) == OK, "Bridge or output unavailable"):
		quit(1); return
	var original_hashes := {}
	for phase in ["station", "approach", "contact", "near", "inspection"]:
		var source: String = phases.path_join(phase + ".json")
		original_hashes[source] = FileAccess.get_sha256(source)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	if not check(shell.select_start(owner, {"mode": "continue", "value": phases.path_join("station.json"), "assets": assets}), "Selected station Continue refused: " + shell.error):
		shell.free(); quit(1); return
	shell.current_view.set_process(false)
	var station: Dictionary = owner.get_freedom_flight_state()
	check(Derelict.valid_packet(station.derelict) and station.derelict.selected and not station.derelict.known and not station.derelict.can_scan and not station.derelict.can_inspect, "Station leaked a contact or enabled space actions")
	check(not owner.scan_freedom_derelict() and not owner.inspect_freedom_derelict() and owner.get_freedom_flight_state() == station, "Station action refusal changed state")
	checkpoint(owner, output.path_join("station.json"), phases.path_join("station.json"))
	if not check(shell.select_start(owner, {"mode": "continue", "value": phases.path_join("approach.json"), "assets": assets}), "Labelled saved-space approach staging refused: " + shell.error):
		shell.free(); quit(1); return
	var view: Control = shell.current_view
	if not check(view is FlightView and view.paused and view.activated and view.staged_model.valid_current_pose(), "Approach lost real seated craft or explicit pause"):
		shell.free(); quit(1); return
	view.set_process(false)
	view.focused = true
	view.player_input.focused = true
	view.player_input.device = -1
	view.player_input.persist = false
	view.controls_menu.show_menu()
	view.observe_neutral_controls()
	var before: Dictionary = owner.get_freedom_flight_state()
	check(Derelict.valid_packet(before.derelict) and before.derelict.can_scan and not before.derelict.known and before.derelict.id.is_empty() and before.derelict.history.is_empty() and before.derelict.position == Vector3.ZERO and before.derelict.basis == Basis.IDENTITY and before.derelict.distance_metres == 0.0 and before.derelict.relative_speed_metres_per_second == 0.0, "Unknown approach leaked hull identity, motion or history")
	check(view.derelict_scan_button.visible and not view.derelict_scan_button.disabled and not view.derelict_inspect_button.visible and not view.derelict_marker.visible and view.derelict_view == null, "Unknown approach lost generic scan or revealed a hull")
	view.focused = false
	view.derelict_scan_button.pressed.emit()
	check(owner.get_freedom_flight_state() == before, "Unfocused scan changed state")
	view.focused = true
	view.journey_dialog_open = true
	view.derelict_scan_button.pressed.emit()
	check(owner.get_freedom_flight_state() == before, "Modal scan changed state")
	view.journey_dialog_open = false
	await key(KEY_W, true)
	view.derelict_scan_button.pressed.emit()
	check(owner.get_freedom_flight_state() == before and not view.controls_armed, "Held movement allowed scan")
	await key(KEY_W, false)
	view.observe_neutral_controls()
	view.derelict_scan_button.pressed.emit()
	var contact: Dictionary = owner.get_freedom_flight_state()
	check(view.error.is_empty() and view.paused and contact.derelict.known and contact.derelict.local and not contact.derelict.inspected and not contact.derelict.can_inspect and contact.tick == before.tick and contact.resources == before.resources, "Actual scan failed to earn contact without time/fuel changes")
	check(view.derelict_view != null and view.derelict_marker.visible and view.derelict_inspect_button.visible and view.derelict_inspect_button.disabled and "100–500 m" in view.derelict_inspect_button.tooltip_text and "No boarding or hull collision" in view.derelict_inspect_button.tooltip_text, "Contact lost marker, range refusal or prototype boundary")
	checkpoint(owner, output.path_join("contact.json"), phases.path_join("contact.json"))
	ViewChecks.run(view.scene, assets, view.service_binding, contact.derelict, check)
	checkpoint(owner, output.path_join("render-neutral.json"), phases.path_join("contact.json"))
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(0.1, neutral, true) and owner.get_freedom_flight_state() == contact, "Paused derelict coast advanced")
	# Exactly the fixture's real coasting ticks; no pose/velocity edits in Godot.
	for tick in 1200:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Committed approach tick refused: " + str(owner.get_last_error())): break
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	var near: Dictionary = owner.get_freedom_flight_state()
	check(view.error.is_empty() and view.paused and near.tick.to_int() == contact.tick.to_int() + 1200 and near.derelict.can_inspect and near.derelict.distance_metres >= 100.0 and near.derelict.distance_metres <= 500.0 and near.derelict.relative_speed_metres_per_second <= 20.0 and not view.derelict_inspect_button.disabled, "Real coast did not reach the allowed exterior stand-off")
	checkpoint(owner, output.path_join("near.json"), phases.path_join("near.json"))
	view.derelict_inspect_button.pressed.emit()
	var inspected: Dictionary = owner.get_freedom_flight_state()
	check(view.error.is_empty() and inspected.derelict.inspected and not inspected.derelict.history.is_empty() and inspected.tick == near.tick and inspected.resources == near.resources and view.paused, "Inspection failed to retain exterior history without spending time/fuel")
	check(inspected.derelict.history in view.derelict_inspect_button.tooltip_text and inspected.derelict.history in view.primary_status.tooltip_text, "Earned exterior history is absent from Controls/HUD")
	var saved := output.path_join("inspection.json")
	checkpoint(owner, saved, phases.path_join("inspection.json"))
	var peer: Variant = peer_from(saved)
	if peer != null:
		checkpoint(peer, output.path_join("continued.json"), saved)
		var continued: Dictionary = peer.get_freedom_flight_state()
		for field in ["tick", "craft_id", "system_id", "planet_id", "position_metres", "velocity_metres_per_second", "orientation_wxyz", "derelict"]:
			check(continued[field] == inspected[field], "Independent Continue changed authoritative owner: " + field)
	view.derelict_inspect_button.pressed.emit()
	checkpoint(owner, output.path_join("repeat.json"), saved)
	if check(shell.select_start(owner, {"mode": "continue", "value": saved, "assets": assets}), "Inspected scene Continue refused: " + shell.error):
		view = shell.current_view
		view.set_process(false)
		check(view.paused and view.derelict_marker.visible and view.derelict_view != null and view.state.derelict.history == inspected.derelict.history, "Scene Continue lost earned hull, history or pause")
		checkpoint(owner, output.path_join("scene-continued.json"), saved)
	for source in original_hashes:
		check(FileAccess.get_sha256(source) == original_hashes[source], "Godot rewrote a C++ source phase")
	shell.free()
	print("Native derelict: %d failures; labelled saved-space approach, actual scan/coast/inspection/Continue; no GPU or manual-route acceptance" % failures)
	quit(1 if failures else 0)
