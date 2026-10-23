extends RefCounted
## Invoked only by the existing orbital_survey case. C++ owns all fixture state.
const Office = preload("res://scripts/native/survey_commissions.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const Shell = preload("res://scripts/native/native_start_shell.gd")
var failures := 0
var witnesses: Array = []

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error("Commission: " + message)
	return value

func save(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "complete Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func restore(path: String) -> Variant:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(owner.stage_freedom_continue(path), "Continue staging refused: " + path): return null
	var pending: Dictionary = owner.get_pending_freedom_start()
	if not check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Continue commit refused"): return null
	return owner

func host(tree: SceneTree, path: String, assets: String) -> Control:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "actual Shell Continue refused: " + shell.error):
		shell.free()
		return null
	var view: Control = shell.current_view
	view.set_process(false)
	view.focused = true
	if view is WalkView:
		view.preferences_provider.focused = true
		view.preferences_provider.persist = false
	else:
		view.player_input.focused = true
		view.player_input.persist = false
	return shell

func reader(view: Control) -> Control:
	return view.commissions_view if view is WalkView else view.controls_menu.commissions_view

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func packet_controls(packet: Dictionary) -> void:
	check(Office.clock_text("0") == "0:00:00" and Office.clock_text("432000") == "1:00:00", "ship-time display omits elapsed hour/minute units")
	check(Office.remaining_text("120", "119") == "less than 1 s" and Office.remaining_text("120", "120") == "expired", "deadline display loses the last subsecond or equality expiry")
	check(Office.valid_packet(packet), "actual closed office packet refused")
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check(not Office.valid_packet(missing), "missing closed field accepted: " + field)
	for change in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"supported": 1}, {"selected": null}, {"at_origin": "yes"}, {"can_accept": 1}, {"can_submit": null}, {"can_abandon": 0}, {"completed": 1}, {"expired": null}, {"sequence": -1}, {"sequence": 4294967296}, {"offer_sequence": 1.0}, {"acquired_ticks": NAN}, {"acquired_ticks": INF}, {"acquired_ticks": -1}, {"acquired_ticks": 3601}, {"required_ticks": 3600.0}, {"required_ticks": 3599}, {"reward_credits": 99}, {"reward_credits": 100.0}, {"tick": 0}, {"tick": "01"}, {"offer_until": "18446744073709551616"}, {"accepted_tick": "-1"}, {"deadline_tick": "1.0"}, {"earnings": NAN}, {"earnings": "1"}, {"earnings": "429496729600"}, {"system_id": []}, {"planet_id": null}, {"body_name": 1}, {"note": ""}, {"note": "x".repeat(513)}, {"last_payment": false}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		check(not Office.valid_packet(bad), "malformed office packet accepted: " + str(change.keys()))
	var progress := {"active": true, "completed": false, "expired": false, "acquired_ticks": 3599, "earnings": "0"}
	check(FlightView.valid_commission_progress(progress), "bounded partial progress refused")
	for change in [{"extra": true}, {"active": 1}, {"completed": null}, {"expired": "no"}, {"acquired_ticks": 3599.0}, {"acquired_ticks": NAN}, {"acquired_ticks": -1}, {"acquired_ticks": 3601}, {"acquired_ticks": 3600}, {"earnings": 0}, {"earnings": "01"}, {"earnings": "18446744073709551616"}]:
		var bad := progress.duplicate(true)
		bad.merge(change, true)
		check(not FlightView.valid_commission_progress(bad), "malformed cheap progress accepted: " + str(change.keys()))
	for field in progress:
		var bad := progress.duplicate(true)
		bad.erase(field)
		check(not FlightView.valid_commission_progress(bad), "missing progress field accepted: " + field)

func malformed_commands(owner: Variant, packet: Dictionary, output: String) -> void:
	var before := save(owner, output.path_join("arguments-before.json"))
	for value in [null, 1, 1.0, NAN, INF, "", "01", "-1", "1.0", "4294967296", [], {}]:
		check(not owner.accept_freedom_survey_commission(packet.offer_until, value), "wrong sequence Variant accepted")
		check(not owner.submit_freedom_survey_commission(value), "wrong submit Variant accepted")
		check(not owner.abandon_freedom_survey_commission(value), "wrong abandon Variant accepted")
	for value in [null, 1, 1.0, NAN, INF, "", "01", "-1", "1.0", "18446744073709551616", [], {}]:
		check(not owner.accept_freedom_survey_commission(value, str(packet.offer_sequence)), "wrong offer Variant accepted")
	check(not owner.accept_freedom_survey_commission(str(int(packet.offer_until) - 1), str(packet.offer_sequence)), "stale offer accepted")
	check(not owner.accept_freedom_survey_commission(packet.offer_until, str(packet.offer_sequence + 1)), "stale sequence accepted")
	check(save(owner, output.path_join("arguments-after.json")) == before, "refused arguments changed complete Save")

func refused_ui(view: Control, owner: Variant, action: String, output: String, label: String) -> void:
	var before := save(owner, output.path_join(label + "-before.json"))
	var page: Control = reader(view)
	var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
	view.focused = false
	provider.focused = false
	view.survey_commission_command(action)
	view.focused = true
	provider.focused = true
	view.mode_change_pending = true
	view.survey_commission_command(action)
	view.mode_change_pending = false
	view.journey_dialog_open = true
	view.survey_commission_command(action)
	view.journey_dialog_open = false
	provider.waiting_action = "forward"
	view.survey_commission_command(action)
	provider.waiting_action = ""
	view.save_dialog.show()
	view.survey_commission_command(action)
	view.save_dialog.hide()
	var settings: Control = view.settings_view if view is WalkView else view.controls_menu.settings_view
	settings.show()
	view.survey_commission_command(action)
	settings.hide()
	var held: int = int(provider.foot_key_codes()[0]) if view is WalkView else int(provider.bindings.forward.key.code)
	key(held, true)
	view.survey_commission_command(action)
	key(held, false)
	view.observe_neutral_controls()
	var button: Button = page.accept_button if action == "accept" else page.submit_button
	button.grab_focus()
	var foreign := InputEventJoypadButton.new()
	foreign.device = 99
	foreign.button_index = JOY_BUTTON_A
	foreign.pressed = true
	if view is WalkView: view._input(foreign)
	else: view.controls_menu._input(foreign)
	check(page.visible and view.paused and save(owner, output.path_join(label + "-after.json")) == before, "focus/held/modal/remap/foreign-pad action changed Save: " + label)
	# Alter only the displayed token, never an authoritative save or packet owner.
	page.packet.offer_until = str(int(page.packet.offer_until) + 1)
	view.survey_commission_command(action)
	check(page.packet == owner.get_freedom_survey_commissions() and save(owner, output.path_join(label + "-stale.json")) == before, "stale displayed terms were committed instead of refreshed")

func run(tree: SceneTree, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "witness directory unavailable"): return failures
	var originals := {}
	for phase in ["commission-station", "commission-accepted", "commission-near", "commission-complete", "commission-returned", "commission-paid", "commission-repeat", "commission-expired"]:
		originals[phase] = FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json"))
		check(not originals[phase].is_empty(), "missing actual scenario fixture: " + phase)
	if failures: return failures
	var backend: Variant = restore(fixtures.path_join("commission-station.json"))
	if backend == null: return failures
	var packet: Dictionary = backend.get_freedom_survey_commissions()
	packet_controls(packet)
	malformed_commands(backend, packet, output)
	backend = null
	if failures: return failures

	var shell := host(tree, fixtures.path_join("commission-station.json"), assets)
	if shell == null: return failures
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	if not check(view is WalkView and view.paused, "station fixture did not open the real walking host"):
		shell.free(); return failures
	view.focused = false
	view.open_survey_commissions()
	check(not reader(view).visible, "unfocused station opened office")
	view.focused = true
	view.journey_dialog_open = true
	view.open_survey_commissions()
	check(not reader(view).visible, "journey dialog opened office underneath")
	view.journey_dialog_open = false
	key(int(view.preferences_provider.foot_key_codes()[0]), true)
	view.open_survey_commissions()
	check(not reader(view).visible, "held walking opened station office")
	key(int(view.preferences_provider.foot_key_codes()[0]), false)
	view.observe_neutral_controls()
	view.open_survey_commissions()
	var page: Control = reader(view)
	check(page.visible and page.packet == packet and not page.accept_button.disabled and page.submit_button.disabled and view.paused, "station reader failed neutral paused offer")
	check(save(owner, output.path_join("station-reader.json")) == originals["commission-station"], "opening office advanced time or selected policy")
	refused_ui(view, owner, "accept", output, "station-refused")
	page.refresh({})
	check(page.packet.is_empty() and page.accept_button.disabled and page.submit_button.disabled and page.abandon_button.disabled, "invalid packet enabled office actions")
	page.accept_button.pressed.emit()
	check(save(owner, output.path_join("invalid-reader-refused.json")) == originals["commission-station"], "invalid reader changed complete Save")
	page.refresh(owner.get_freedom_survey_commissions())
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(view.paused and page.visible and page.packet.acquired_ticks == 0 and page.packet.sequence == 1 and save(owner, output.path_join("accepted-ui.json")) == originals["commission-accepted"], "explicit station Accept differs from direct C++ full Save")
	var accepted := save(owner, output.path_join("accepted-before-repeat.json"))
	page.accept_button.pressed.emit()
	check(save(owner, output.path_join("accepted-duplicate.json")) == accepted, "duplicate queued Accept changed account")
	page.back.pressed.emit()
	check(not page.visible and view.paused and not view.controls_armed, "Back resumed walking or retained office")
	await tree.process_frame
	view.observe_neutral_controls()
	view.open_survey_commissions()
	var oracle: Variant = restore(fixtures.path_join("commission-accepted.json"))
	if oracle != null:
		check(oracle.abandon_freedom_survey_commission("1"), "independent backend abandonment refused")
		page.abandon_button.pressed.emit()
		check(view.paused and not page.packet.can_abandon and save(owner, output.path_join("abandoned-ui.json")) == save(oracle, output.path_join("abandoned-oracle.json")), "explicit Abandon differs from complete C++ oracle")
	oracle = null
	witnesses.append({"host": "station", "accepted_cpp_bytes_equal": FileAccess.get_file_as_bytes(output.path_join("accepted-ui.json")) == originals["commission-accepted"], "remains_paused": view.paused})
	shell.free(); owner = null
	await tree.process_frame

	shell = host(tree, fixtures.path_join("commission-near.json"), assets)
	if shell == null: return failures
	view = shell.current_view; owner = shell.bridge
	if not check(view is FlightView and view.paused, "near-complete fixture did not open flight host"):
		shell.free(); return failures
	view.open_survey_commissions()
	page = reader(view)
	check(page.visible and page.packet.acquired_ticks == 3599 and not page.packet.completed and page.submit_button.disabled and page.accept_button.disabled and "29.9" in page.details.text, "3599-interval reader falsely completed or omitted actual progress")
	check(save(owner, output.path_join("near-reader.json")) == originals["commission-near"], "near reader acquired work while paused")
	view.survey_commission_command("submit")
	view.survey_commission_command("abandon")
	check(save(owner, output.path_join("remote-refused.json")) == originals["commission-near"], "remote office action changed account")
	page.back.pressed.emit()
	check(view.paused and not page.visible, "flight Back auto-resumed")
	view.observe_neutral_controls()
	view.toggle_pause()
	check(not view.paused, "explicit neutral Resume refused measurement flight")
	var neutral := PackedFloat64Array(); neutral.resize(12)
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "actual completing measurement tick refused")
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	view.pause_controls("Commission completion proof paused")
	view.open_survey_commissions()
	page = reader(view)
	check(page.visible and page.packet.completed and page.packet.acquired_ticks == 3600 and page.packet.earnings == "0" and view.state.survey_commission.completed and save(owner, output.path_join("complete-ui.json")) == originals["commission-complete"], "one genuine completing tick differs from C++ fixture or pays remotely")
	oracle = restore(output.path_join("complete-ui.json"))
	if oracle != null:
		check(oracle.get_freedom_survey_commissions() == page.packet and save(oracle, output.path_join("complete-continued.json")) == originals["commission-complete"], "completion Continue lost report or wallet")
	oracle = null
	witnesses.append({"host": "flight", "near_ticks": 3599, "completed_ticks": 3600, "completed_cpp_bytes_equal": FileAccess.get_file_as_bytes(output.path_join("complete-ui.json")) == originals["commission-complete"]})
	shell.free(); owner = null
	await tree.process_frame

	shell = host(tree, fixtures.path_join("commission-returned.json"), assets)
	if shell == null: return failures
	view = shell.current_view; owner = shell.bridge
	if not check(view is FlightView and view.paused, "returned fixture did not open attached seated flight host"):
		shell.free(); return failures
	view.open_survey_commissions()
	page = reader(view)
	check(page.visible and page.packet.at_origin and page.packet.can_submit and not page.submit_button.disabled and page.packet.earnings == "0", "returned reader did not expose explicit earned submission")
	refused_ui(view, owner, "submit", output, "returned-refused")
	await tree.process_frame
	page.submit_button.pressed.emit()
	check(view.paused and page.visible and page.packet.earnings == "100" and page.packet.sequence == 0 and not page.packet.last_payment.is_empty() and save(owner, output.path_join("paid-ui.json")) == originals["commission-paid"], "Submit differs from direct C++ full Save or lacks payment receipt")
	var paid := save(owner, output.path_join("paid-before-duplicate.json"))
	check(not owner.submit_freedom_survey_commission("1"), "duplicate backend submission paid again")
	page.submit_button.pressed.emit()
	check(save(owner, output.path_join("paid-duplicate.json")) == paid, "duplicate UI/backend payout changed wallet")
	oracle = restore(output.path_join("paid-ui.json"))
	if oracle != null:
		check(oracle.get_freedom_survey_commissions() == page.packet and save(oracle, output.path_join("paid-continued.json")) == paid, "paid Continue lost wallet/report")
	oracle = null
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(view.paused and page.packet.sequence == 2 and page.packet.acquired_ticks == 0 and not page.packet.completed and page.packet.earnings == "100" and save(owner, output.path_join("repeat-ui.json")) == originals["commission-repeat"], "repeat acceptance reused old evidence or differs from C++ complete Save")
	page.back.pressed.emit()
	check(view.paused and not page.visible, "return office Back resumed flight")
	witnesses.append({"host": "returned-flight", "paid_cpp_bytes_equal": FileAccess.get_file_as_bytes(output.path_join("paid-ui.json")) == originals["commission-paid"], "earnings": "100", "repeat_requires_new_intervals": page.packet.sequence == 2 and page.packet.acquired_ticks == 0 and not page.packet.completed})
	shell.free(); owner = null
	await tree.process_frame
	backend = restore(fixtures.path_join("commission-expired.json"))
	if backend != null:
		var expired: Dictionary = backend.get_freedom_survey_commissions()
		check(Office.valid_packet(expired) and expired.completed and expired.expired and not expired.can_submit and not backend.submit_freedom_survey_commission(str(expired.sequence)) and save(backend, output.path_join("expired-refused.json")) == originals["commission-expired"], "expired completed report paid or changed source")
	backend = null
	for phase in originals:
		check(FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json")) == originals[phase], "presentation rewrote C++ source fixture: " + phase)
	var report := {"schema": "apsis.survey-commissions-native/1", "completed": failures == 0, "failures": failures, "witnesses": witnesses, "scope": "Actual station/flight Shell Continue, paused office commands, malformed/held/modal/device refusal, full C++ Save parity and one committed completion interval. Source scenario uses explicitly initialized home-space, remote orbit and home-port poses with actual charged jumps; not uninterrupted flown/manual/hardware/performance acceptance."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
