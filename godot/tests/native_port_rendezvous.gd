extends RefCounted
## Actual home/near snapshots exercise UI, not a replay of the long C++ route.
const FlightView = preload("res://scripts/native/native_flight_view.gd")

static func restore(test: Variant, path: String) -> Variant:
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not test.check(peer.stage_freedom_continue(path), "Rendezvous fixture Continue staging refused"): return null
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not test.check(not pending.is_empty() and peer.commit_pending_freedom_start(pending.candidate_id), "Rendezvous fixture Continue commit refused"): return null
	return peer

static func run(test: Variant, owner: Variant, view: Control, phases: String, assets: String, output: String) -> void:
	if not test.check(DirAccess.make_dir_recursive_absolute(output) == OK, "Rendezvous UI output unavailable"): return
	var initial_failures: int = test.failures
	view.focused = true
	view.player_input.focused = true
	view.player_input.persist = false
	view.port_command("select_freedom_port", 1)
	var state: Dictionary = owner.get_freedom_flight_state()
	if not test.check(view.paused and not state.attached and state.docking.separation > 39000.0 and state.port_rendezvous.available and not view.rendezvous_button.disabled, "Actual charged home return lacks its far guidance action"): return
	for damage in ["shape", "active", "remaining", "infinite", "range", "note"]:
		var bad := state.duplicate(true)
		match damage:
			"shape": bad.port_rendezvous.extra = true
			"active": bad.port_rendezvous.active = null
			"remaining": bad.port_rendezvous.remaining_seconds = "0"
			"infinite": bad.port_rendezvous.remaining_seconds = INF
			"range": bad.port_rendezvous.remaining_seconds = 2400.1
			"note": bad.port_rendezvous.note = "x".repeat(1025)
		test.check(not FlightView.valid_state(bad), "Malformed actual rendezvous observation passed admission: " + damage)
	var before: PackedByteArray = test.save_bytes(owner, output.path_join("home-selected.json"))
	var report := {"version": 1, "scope": "Actual home UI and reached near snapshot; full far route belongs to C++ coverage", "home_tick": state.tick, "completed": false}
	# Space is also UI Accept; releasing it can activate the focused Resume.
	# Probe mapped propulsion without issuing that separate menu command.
	var code: int = int(view.player_input.bindings.forward.key.code)
	test.key(code, true)
	view.rendezvous_button.pressed.emit()
	test.check(not owner.get_freedom_flight_state().port_rendezvous.active and not view.controls_armed and test.save_bytes(owner, output.path_join("held-refused.json")) == before, "Held mapped movement armed rendezvous or changed the complete Save")
	test.key(code, false)
	test.check(view.paused, "Releasing mapped propulsion implicitly resumed flight")
	view.observe_neutral_controls()
	view.focused = false
	view.rendezvous_button.pressed.emit()
	test.check(not owner.get_freedom_flight_state().port_rendezvous.active, "Unfocused far action armed guidance")
	view.focused = true
	view.rendezvous_button.pressed.emit()
	state = owner.get_freedom_flight_state()
	if not test.check(state.port_rendezvous.active and state.port_rendezvous.remaining_seconds == 2400.0 and view.paused and test.save_bytes(owner, output.path_join("armed.json")) == before, "Actual rendezvous button failed its zero-time, save-neutral arm"): return
	report.armed_seconds = state.port_rendezvous.remaining_seconds
	var peer: Variant = restore(test, output.path_join("armed.json"))
	if peer != null:
		test.check(not peer.get_freedom_flight_state().port_rendezvous.active and not peer.get_freedom_flight_state().port_approach.active and test.save_bytes(peer, output.path_join("armed-continued.json")) == before, "Actual Continue persisted transient guidance or changed complete bytes")
	view.rendezvous_button.pressed.emit()
	test.check(not owner.get_freedom_flight_state().port_rendezvous.active and test.save_bytes(owner, output.path_join("explicit-cancel.json")) == before, "Explicit rendezvous cancellation changed the saved journey")
	view.rendezvous_button.pressed.emit()
	view.toggle_pause()
	test.check(not view.paused and owner.get_freedom_flight_state().port_rendezvous.active, "Neutral Resume failed or silently canceled the armed aid")
	view.toggle_pause()
	test.check(view.paused and not owner.get_freedom_flight_state().port_rendezvous.active and test.save_bytes(owner, output.path_join("pause-cancel.json")) == before, "Pause retained guidance or consumed an unrequested simulation tick")
	view.observe_neutral_controls()
	view.rendezvous_button.pressed.emit()
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	test.check(view.paused and not owner.get_freedom_flight_state().port_rendezvous.active and test.save_bytes(owner, output.path_join("focus-cancel.json")) == before, "Focus loss retained far guidance or changed the complete Save")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	report.home_zero_tick = owner.get_freedom_flight_state().tick == report.home_tick

	# This pose was reached by the production far controller in the exporter.
	# Continue intentionally drops both transient aids, so UI arms the old near aid.
	var near_path := phases.path_join("rendezvous-near.json")
	var original := FileAccess.get_file_as_bytes(near_path)
	var near_owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell: Control = test.shell_for(near_owner, near_path, assets)
	if shell == null: return
	var near: Control = shell.current_view
	near.focused = true
	near.player_input.focused = true
	near.player_input.persist = false
	var reached: Dictionary = near_owner.get_freedom_flight_state()
	if test.check(not original.is_empty() and near.paused and not reached.attached and not reached.port_rendezvous.active and not reached.port_approach.active and reached.port_approach.available and reached.docking.separation <= 150.0 and not near.approach_button.disabled and test.save_bytes(near_owner, output.path_join("near-continued.json")) == original, "Reached near snapshot failed exact Continue or exposed the wrong guidance gate"):
		report.near_tick = reached.tick
		near.observe_neutral_controls()
		near.approach_button.pressed.emit()
		var armed: Dictionary = near_owner.get_freedom_flight_state()
		if test.check(armed.port_approach.active and armed.port_approach.remaining_seconds == 60.0 and not armed.port_rendezvous.active and test.save_bytes(near_owner, output.path_join("near-armed.json")) == original, "Existing near button failed same-tick handoff snapshot admission"):
			var neutral := PackedFloat64Array()
			neutral.resize(12)
			var ticks := 0
			while not armed.docking.ready and ticks < 7200:
				if not test.check(near_owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual near-controller tick refused"): break
				ticks += 1
				armed = near_owner.get_freedom_flight_state()
				if not test.check(not armed.attached and armed.resources.jump_charges == reached.resources.jump_charges and int(armed.tick) == int(reached.tick) + ticks, "Near guidance auto-captured, spent a charge or lost its real clock"): break
			if test.check(ticks < 7200 and armed.docking.ready and not armed.attached and armed.resources.fraction < reached.resources.fraction, "Actual reached near snapshot did not fly to the unchanged explicit capture gate"):
				near.state = armed
				near.update_view(0.0)
				near.capture_button.pressed.emit()
				var captured: Dictionary = near_owner.get_freedom_flight_state()
				test.check(captured.attached and captured.tick == armed.tick and captured.resources.quantity_quanta == armed.resources.quantity_quanta and captured.resources.jump_charges == armed.resources.jump_charges and not captured.port_approach.active and not captured.port_rendezvous.active, "Only explicit UI capture should attach without clock advance or free service")
				test.check(test.save_bytes(near_owner, output.path_join("near-captured.json")) == FileAccess.get_file_as_bytes(phases.path_join("reuse-captured.json")), "Native near controls and explicit UI capture differ from the C++ reached capture Save")
				report.near_ticks = ticks
				report.capture_tick = captured.tick
				report.capture_explicit = captured.attached
	test.check(FileAccess.get_file_as_bytes(near_path) == original, "Native rendezvous checks modified the reached source fixture")
	shell.free()
	await test.process_frame
	report.completed = test.failures == initial_failures
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if test.check(file != null, "Rendezvous UI report unavailable"):
		file.store_string(JSON.stringify(report, "\t") + "\n")
