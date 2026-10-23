extends SceneTree
## Real C++ survey saves; no orbital placement or acquisition in presentation.
const Survey = preload("res://scripts/native/native_orbital_survey.gd")
const Observations = preload("res://scripts/native/recorded_observations.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const Shell = preload("res://scripts/native/native_start_shell.gd")
var failures := 0

func check(ok: bool, reason: String) -> bool:
	if not ok:
		failures += 1
		push_error(reason)
	return ok

func _initialize() -> void:
	check_inputs()
	call_deferred("run")

func check_inputs() -> void:
	var packet := {"selected": false, "active": false, "can_begin": true, "completed": false, "note": "Ready", "body_name": "Known planet", "acquired_ticks": 0, "required_ticks": 3600, "result": {}}
	check(Survey.valid(packet) and Survey.text(packet).is_empty(), "First survey opt-in or quiet idle presentation refused")
	for change in [{"extra": true}, {"selected": 1}, {"active": null}, {"can_begin": "yes"}, {"completed": 0}, {"note": null}, {"body_name": 1}, {"note": "x".repeat(513)}, {"acquired_ticks": NAN}, {"acquired_ticks": INF}, {"acquired_ticks": -1}, {"acquired_ticks": 3601}, {"required_ticks": 3600.0}, {"required_ticks": 3599}, {"result": null}, {"result": {"character": "rugged", "water_basis_points": 0}}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		check(not Survey.valid(bad) and Survey.text(bad).is_empty(), "Malformed survey packet reached the HUD: " + str(change.keys()))
	for key in packet:
		var bad := packet.duplicate(true)
		bad.erase(key)
		check(not Survey.valid(bad), "Missing survey field accepted: " + key)
	packet.selected = true
	packet.active = true
	packet.can_begin = false
	packet.acquired_ticks = 120
	packet.note = "Held: return to stable bound orbit"
	check(Survey.valid(packet) and "1.0 / 30 s" in Survey.text(packet) and "Held" in Survey.text(packet), "Held survey lost actual partial progress or reason")
	packet.note = "Survey held: return to stable bound orbit"
	check("held" in Survey.compact_text(packet) and "1.0 / 30 s" in Survey.compact_text(packet), "Compact survey lost C++ held status or progress")
	packet.active = false
	packet.completed = true
	packet.acquired_ticks = 3600
	packet.can_begin = false
	packet.result = {"character": "rugged", "water_basis_points": 1234}
	check(Survey.valid(packet) and "12.3%" in Survey.text(packet) and "not landing clearance" in Survey.text(packet), "Survey result lost terrain/water qualification")
	for result in [{}, {"character": "", "water_basis_points": 0}, {"character": "x".repeat(65), "water_basis_points": 0}, {"character": "rock", "water_basis_points": NAN}, {"character": "rock", "water_basis_points": -1}, {"character": "rock", "water_basis_points": 10001}, {"character": "rock", "water_basis_points": 0, "extra": true}]:
		var bad := packet.duplicate(true)
		bad.result = result
		check(not Survey.valid(bad), "Malformed completed survey result accepted")
	var provenance := {"started_tick": "10", "completed_tick": "3610", "craft_id": "2", "acquired_ticks": 3600}
	check(Survey.valid_provenance(provenance), "Canonical completed survey provenance refused")
	for change in [{"extra": true}, {"started_tick": "01"}, {"completed_tick": "9"}, {"craft_id": "18446744073709551616"}, {"craft_id": "-2"}, {"acquired_ticks": 3600.0}, {"acquired_ticks": 3599}, {"started_tick": null}]:
		var bad := provenance.duplicate(true)
		bad.merge(change, true)
		check(not Survey.valid_provenance(bad), "Malformed orbital provenance accepted")
	var subject := {"name": "Known planet", "kind": "planet", "system_id": "1", "subject_id": "2", "facts": []}
	var readings := {"selected": true, "tick": "3610", "subjects": [subject]}
	check(Observations.valid_readings(readings), "Legacy observation row now requires survey metadata")
	subject.orbital_survey = provenance
	check(Observations.valid_readings(readings) and "30 s acquired" in Observations.subject_text(subject) and "craft 2" in Observations.subject_text(subject), "Reader lost optional survey provenance")
	subject.orbital_survey = null
	check(not Observations.valid_readings(readings), "Null survey provenance accepted by reader")

func save_bytes(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Survey complete Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func restore(path: String) -> Variant:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(owner.stage_freedom_continue(path), "Survey backend Continue staging refused"): return null
	var pending: Dictionary = owner.get_pending_freedom_start()
	if not check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Survey backend Continue commit refused"): return null
	return owner

func shell_for(owner: Variant, path: String, assets: String) -> Control:
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Survey Shell Continue refused: " + shell.error):
		shell.free()
		return null
	shell.current_view.set_process(false)
	shell.current_view.focused = true
	shell.current_view.player_input.focused = true
	shell.current_view.player_input.persist = false
	return shell

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check_compact_layout(view: Control, owner: Variant, output: String, label: String) -> void:
	var state: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join(label + "-layout.json")
	var before := save_bytes(owner, path)
	var previous_size := root.size
	view.controls_menu.hide_menu()
	root.size = Vector2i(640, 450)
	for frame in 3: await process_frame
	view.layout_hud()
	view.update_compact_hud()
	for frame in 2: await process_frame
	var status: RichTextLabel = view.primary_status
	check(view.paused and status.is_visible_in_tree() and Rect2(Vector2.ZERO, view.size).encloses(status.get_rect()), "Compact survey HUD escaped its 640x450 view: " + label)
	check(status.get_content_height() > 0 and status.get_content_height() <= status.size.y, "Compact survey HUD text overflowed at 640x450: %s · content %d / rect %.1f · width %.1f · text %s" % [label, status.get_content_height(), status.size.y, status.size.x, status.text])
	if state.orbital_survey.active or state.orbital_survey.completed:
		check(Survey.compact_text(state.orbital_survey, not view.observation_cue.text().is_empty()) in status.text, "Important survey progress was lost from compact HUD: " + label)
	if state.orbital_survey.active or state.orbital_survey.completed:
		check(Survey.text(state.orbital_survey) in status.tooltip_text and (not state.orbital_survey.completed or "not landing clearance" in status.tooltip_text), "Compact survey omitted full qualified details from tooltip: " + label)
	root.size = previous_size
	for frame in 2: await process_frame
	view.layout_hud()
	view.controls_menu.show_menu()
	check(owner.get_freedom_flight_state() == state and save_bytes(owner, path) == before and view.paused, "Survey layout changed clock, acquisition or complete Save: " + label)

func run() -> void:
	if failures: quit(1); return
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path(): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Survey bridge unavailable"): quit(1); return
	var output := args[0].get_base_dir().path_join("orbital-survey-native")
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Survey output unavailable"): quit(1); return
	var originals := {}
	for phase in ["survey-start", "survey-near", "survey-complete"]:
		originals[phase] = FileAccess.get_file_as_bytes(args[0].path_join(phase + ".json"))
		check(not originals[phase].is_empty(), "Missing actual C++ survey fixture: " + phase)
	if failures: quit(1); return
	root.size = Vector2i(1280, 720)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, args[0].path_join("survey-start.json"), args[1])
	if shell == null: quit(1); return
	var view: Control = shell.current_view
	var start: Dictionary = owner.get_freedom_flight_state()
	if not check(view.paused and FlightView.valid_state(start) and Survey.valid(start.get("orbital_survey")) and not start.orbital_survey.selected and start.orbital_survey.can_begin and view.survey_start_button.visible and not view.survey_start_button.disabled, "Actual stable orbit lacks explicit survey opt-in"):
		shell.free(); quit(1); return
	var before := save_bytes(owner, output.path_join("before.json"))
	var code: int = int(view.player_input.bindings.forward.key.code)
	key(code, true)
	view.survey_start_button.pressed.emit()
	check(not owner.get_freedom_flight_state().orbital_survey.active and save_bytes(owner, output.path_join("held-refused.json")) == before, "Held movement began survey or changed complete Save")
	key(code, false)
	view.observe_neutral_controls()
	view.focused = false
	view.survey_start_button.pressed.emit()
	check(save_bytes(owner, output.path_join("focus-refused.json")) == before, "Unfocused survey command changed Save")
	view.focused = true
	view.journey_dialog_open = true
	view.survey_start_button.pressed.emit()
	check(save_bytes(owner, output.path_join("modal-refused.json")) == before, "Modal survey command changed Save")
	view.journey_dialog_open = false
	view.survey_start_button.pressed.emit()
	var begun: Dictionary = owner.get_freedom_flight_state()
	var begun_bytes := save_bytes(owner, output.path_join("begun.json"))
	check(view.paused and begun.orbital_survey.active and begun.orbital_survey.selected and begun.orbital_survey.acquired_ticks == 0 and begun.tick == start.tick and begun.checksum == start.checksum and begun.resources == start.resources, "Start failed zero-time opt-in or implicitly resumed")
	await check_compact_layout(view, owner, output, "started")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(0.1, neutral, true) and save_bytes(owner, output.path_join("paused.json")) == begun_bytes, "Paused survey advanced acquisition or Save clock")
	var peer: Variant = restore(output.path_join("begun.json"))
	if peer != null:
		check(peer.get_freedom_flight_state().orbital_survey == begun.orbital_survey and save_bytes(peer, output.path_join("continued.json")) == begun_bytes, "Continue changed unfinished survey or full Save")
	peer = null
	view.survey_cancel_button.pressed.emit()
	var canceled: Dictionary = owner.get_freedom_flight_state()
	check(view.paused and not canceled.orbital_survey.active and canceled.orbital_survey.acquired_ticks == 0 and canceled.tick == start.tick and canceled.checksum == start.checksum and canceled.resources == start.resources, "Stop advanced flight or retained unfinished acquisition")
	await check_compact_layout(view, owner, output, "stopped")
	save_bytes(owner, output.path_join("canceled.json"))
	shell.free()
	owner = null
	await process_frame

	owner = ClassDB.instantiate("FreedomBridge")
	shell = shell_for(owner, args[0].path_join("survey-near.json"), args[1])
	if shell == null: quit(1); return
	view = shell.current_view
	var near: Dictionary = owner.get_freedom_flight_state()
	check(view.paused and near.orbital_survey.active and near.orbital_survey.acquired_ticks == 3599 and "30 s" in view.primary_status.text and save_bytes(owner, output.path_join("near-continued.json")) == originals["survey-near"], "Near-complete survey lost exact Continue or readable HUD")
	await check_compact_layout(view, owner, output, "active")
	view.observe_neutral_controls()
	view.toggle_pause()
	check(not view.paused, "Neutral explicit Resume refused survey flight")
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual completing survey flight tick refused")
	view.state = owner.get_freedom_flight_state()
	# Mirror the ordinary committed-flight presenter hook using the actual
	# before/after facts; completion must not stack a duplicate Records cue.
	view.observation_cue.observe(view.state, true, 1.0 / 120.0)
	view.update_view(0.0)
	var completed: Dictionary = view.state
	check(completed.orbital_survey.completed and not completed.orbital_survey.active and completed.orbital_survey.acquired_ticks == 3600 and int(completed.tick) == int(near.tick) + 1 and save_bytes(owner, output.path_join("complete.json")) == originals["survey-complete"], "One actual survey tick diverged from direct C++ complete Save")
	check("Survey complete" in view.primary_status.text and "not landing clearance" in view.primary_status.tooltip_text, "Completed survey is absent or presented as clearance")
	if not view.observation_cue.text().is_empty():
		check("New records" in view.primary_status.text and not view.observation_cue.text() in view.primary_status.text and view.observation_cue.text() in view.primary_status.tooltip_text, "Survey completion duplicated or discarded its actual observation notice")
	view.pause_controls("Survey proof paused")
	await check_compact_layout(view, owner, output, "completed")
	var records: Dictionary = owner.get_freedom_observations()
	check(Observations.valid_readings(records), "Actual survey observation records refused")
	var found := false
	for row: Dictionary in records.get("subjects", []):
		if not row.has("orbital_survey"): continue
		found = true
		check(Survey.valid_provenance(row.orbital_survey) and row.orbital_survey.completed_tick == completed.tick and row.orbital_survey.craft_id == completed.craft_id, "Completed terrain model lost real craft/tick provenance")
		check("Orbital terrain survey" in Observations.subject_text(row), "Recorded survey provenance is unreadable")
	check(found, "Completed survey has no retained observation provenance")
	view.controls_menu.show_observations()
	check(view.paused and view.controls_menu.observations_view.visible and view.controls_menu.observations_view.readings == records and save_bytes(owner, output.path_join("reader.json")) == originals["survey-complete"], "Actual paused reader altered Save or omitted survey records")
	peer = restore(output.path_join("complete.json"))
	if peer != null:
		check(peer.get_freedom_flight_state().orbital_survey == completed.orbital_survey and peer.get_freedom_observations() == records and save_bytes(peer, output.path_join("complete-continued.json")) == originals["survey-complete"], "Completed survey/result provenance did not survive Continue")
	peer = null
	for phase in originals:
		check(FileAccess.get_file_as_bytes(args[0].path_join(phase + ".json")) == originals[phase], "Survey UI rewrote actual C++ fixture: " + phase)
	var report := {"completed": failures == 0, "scope": "Actual paused survey commands, one committed near-completion tick, full Save/Continue and recorded provenance; no autonomous orbit", "start_tick": start.tick, "near_tick": near.tick, "completed_tick": completed.tick, "acquired_ticks": completed.orbital_survey.acquired_ticks}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "Survey report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	shell.free()
	owner = null
	await process_frame
	var commissions := preload("res://tests/survey_commissions_test.gd").new()
	failures += await commissions.run(self, args[0], args[1], output.path_join("commissions"))
	print("Orbital survey checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)
