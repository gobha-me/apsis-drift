extends RefCounted
## Actual UI commands versus independently ticked C++ ordinary-planet sidecars.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const MouseChecks = preload("res://tests/native_companion_mouse_checks.gd")
var failures := 0

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error(message)
	return value

func save(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Planet travel Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func restore(path: String) -> Variant:
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(path), "Planet phase backend Continue staging refused"): return null
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(not pending.is_empty() and peer.commit_pending_freedom_start(pending.candidate_id), "Planet phase backend Continue commitment refused"): return null
	return peer

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func activate(tree: SceneTree, view: Control, button: Button, pad: bool) -> void:
	button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(button)
	await tree.process_frame
	check(button.has_focus() and button.is_visible_in_tree() and not button.disabled, "Planet action cannot receive ordinary keyboard/controller focus")
	for down in [true, false]:
		if pad:
			var event := InputEventJoypadButton.new()
			event.device = view.player_input.device
			event.button_index = JOY_BUTTON_A
			event.pressed = down
			Input.parse_input_event(event)
			Input.flush_buffered_events()
		else: key(KEY_ENTER, down)
		await tree.process_frame

func target_index(packet: Dictionary, target: String) -> int:
	for index in packet.rows.size():
		if packet.rows[index].planet_id == target: return index
	return -1

func packet_refusals(packet: Dictionary, system_id: String) -> void:
	check(FlightView.valid_planet_destinations(packet, system_id), "Actual charted planet packet failed closed admission")
	for damage in ["extra", "version", "version_type", "rows", "oversize", "selected", "reason", "foreign", "duplicate", "id", "name", "boolean", "row_extra", "current"]:
		var bad := packet.duplicate(true)
		match damage:
			"extra": bad.extra = true
			"version": bad.version = 2
			"version_type": bad.version = "1"
			"rows": bad.rows = null
			"oversize":
				while bad.rows.size() <= 6: bad.rows.append(packet.rows[0].duplicate(true))
			"selected": bad.selected = null
			"reason": bad.reason = "x".repeat(1025)
			"foreign": bad.rows[0].system_id = "1" if system_id != "1" else "2"
			"duplicate": bad.rows.append(bad.rows[0].duplicate(true))
			"id": bad.rows[0].planet_id = "01"
			"name": bad.rows[0].name = ""
			"boolean": bad.rows[0].selectable = 1
			"row_extra": bad.rows[0].physical_radius = 42.0
			"current":
				for row in bad.rows: row.current = false
		check(not FlightView.valid_planet_destinations(bad, system_id), "Malformed planet packet admitted before presentation: " + damage)
	check(FlightView.valid_planet_destinations({"version": 1, "selected": false, "rows": [], "reason": "Historical chart remains unselected."}, system_id), "Absent historical planetary chart lost its quiet closed form")

func phase(owner: Variant, expected: String, actual: String, records: Array) -> void:
	var source := FileAccess.get_file_as_bytes(expected)
	var bytes := save(owner, actual)
	check(not source.is_empty() and bytes == source, "Actual planet phase differs from independent C++ full Save: " + expected)
	var peer: Variant = restore(actual)
	if peer != null:
		var resumed: Dictionary = peer.get_freedom_flight_state()
		var current: Dictionary = owner.get_freedom_flight_state()
		for field in ["system_id", "planet_id", "tick", "checksum", "craft_id", "frame_id", "position_metres"]:
			check(resumed[field] == current[field], "Planet backend Continue changed actual " + field)
		check(save(peer, actual + ".continued.json") == bytes, "Planet backend Continue changed complete phase bytes")
	records.append({"file": actual.get_file(), "oracle": expected.get_file(), "sha256": FileAccess.get_sha256(actual), "tick": owner.get_freedom_flight_state().tick})

func mouse_after_handoff(view: Control, owner: Variant, output: String) -> void:
	var before_path := output.path_join("planet-ui-mouse-before.json")
	var before := save(owner, before_path)
	var state: Dictionary = owner.get_freedom_flight_state()
	if not MouseChecks.resume(view, Callable(self, "check")): return
	MouseChecks.motion(view, view.size * Vector2(0.02, -0.01))
	var demand: Vector2 = view.flight_mouse.demands(view.player_input.settings)
	if not check(demand.x != 0.0 and demand.y != 0.0 and owner.get_freedom_flight_state() == state, "Post-handoff planet mouse failed to request steering without mutating C++ state"): return
	var axes := PackedFloat64Array()
	axes.resize(12)
	# Independent public bridge channel order, not the production blend helper.
	axes[6 if demand.x > 0.0 else 9] = absf(demand.x)
	axes[10 if demand.y > 0.0 else 7] = absf(demand.y)
	var peer: Variant = restore(before_path)
	if peer == null: return
	view._process(1.0 / 120.0)
	check(peer.advance_freedom_flight(1.0 / 120.0, axes, false), "Post-handoff direct steering peer refused")
	var actual: Dictionary = owner.get_freedom_flight_state()
	check(actual == peer.get_freedom_flight_state() and int(actual.tick) == int(state.tick) + 1 and actual.resources.quantity_quanta != state.resources.quantity_quanta and actual.angular_velocity_body != state.angular_velocity_body, "Post-handoff mouse did not apply the same real torque/fuel/clock transaction")
	check(save(owner, output.path_join("planet-ui-mouse-after.json")) == save(peer, output.path_join("planet-ui-mouse-peer.json")) and not before.is_empty(), "Post-handoff mouse full Save differs from direct C++ peer")
	view.pause_controls("Planet travel test complete")

func run(tree: SceneTree, trace: String, assets: String, recovering: bool = false, strained: bool = false) -> int:
	var output := trace.get_base_dir()
	if recovering:
		output = output.path_join("strain-ui" if strained else "recovery-ui")
		DirAccess.make_dir_recursive_absolute(output)
	var prefix := trace + (".strain" if strained else ".recovery" if recovering else ".planet")
	var report: Variant = JSON.parse_string(FileAccess.get_file_as_string(prefix + ".report.json"))
	if not check(report is Dictionary and report.get("version") == 1 and report.get("completed") == true, "Independent C++ planet trace sidecars missing/incomplete"): return failures
	var station: Variant = restore(prefix + ".station.json")
	if station != null:
		var station_packet: Dictionary = station.get_freedom_planet_destinations()
		check(station_packet.selected and not station_packet.rows.is_empty(), "Explicit5 actual station NewGame lacks charted ordinary-planet identities")
		if not station_packet.rows.is_empty(): packet_refusals(station_packet, report.system_id)
	station = null
	var source := prefix + ".start.json"
	var source_bytes := FileAccess.get_file_as_bytes(source)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Planet actual source asset staging refused: " + shell.error):
		shell.free()
		return failures
	var view: Control = shell.current_view
	view.set_process(false)
	view.focused = true
	view.player_input.focused = true
	view.player_input.persist = false
	view.player_input.device = 0
	view.player_input.install()
	view.controls_menu.show_menu()
	for frame in 3: await tree.process_frame
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	view.refresh_planet_routes()
	var initial: Dictionary = owner.get_freedom_flight_state()
	check(initial.jump.has("recovery") == recovering, "Drive recovery selection was lost or silently added to historical voyage")
	if recovering:
		check(initial.jump.recovery.ready and initial.jump.recovery.remaining_seconds == 0.0 and initial.jump.recovery.extra_radius_metres == 0.0, "Selected fresh drive is not ready")
		check(initial.jump.recovery.version == (2 if strained else 1), "Continue changed selected drive version")
		if strained: check(initial.jump.recovery.next_recovery_seconds == 45.0, "Fresh strain drive lost baseline next recovery")
	var packet: Dictionary = owner.get_freedom_planet_destinations()
	check(view.paused and view.planet_route_packet == packet and save(owner, output.path_join("planet-ui-before.json")) == source_bytes, "Planet Continue changed complete source or hid the actual paused controls")
	var size_before: Vector2 = tree.root.size
	for dimensions in [Vector2i(640, 450), Vector2i(1280, 720)]:
		tree.root.size = dimensions
		view.size = Vector2(dimensions)
		view.layout_hud()
		view.update_view(0.0)
		owner.get_freedom_planet_destinations()
		owner.get_freedom_observations()
	check(owner.get_freedom_flight_state() == initial and save(owner, output.path_join("planet-ui-read-resize.json")) == source_bytes, "Planet read/resize changed full Save/clock/provenance")
	tree.root.size = Vector2i(size_before)
	view.size = size_before
	view.layout_hud()
	for bad in ["", "01", "-1", "1.5", "18446744073709551616"]:
		check(not owner.select_freedom_planet_jump(report.system_id, bad) and owner.get_freedom_flight_state() == initial, "Malformed planet destination changed source")
	check(not owner.select_freedom_planet_jump("1" if report.system_id != "1" else "2", report.target_planet_id) and owner.get_freedom_flight_state() == initial, "Foreign system destination changed current owner")
	for ordinal in [-1, packet.rows.size()]:
		view.select_planet_destination(ordinal)
		check(owner.get_freedom_flight_state() == initial, "Invalid planet UI ordinal changed selection")
	var target := target_index(packet, report.target_planet_id)
	if not check(target >= 0, "Actual chart lacks independent C++ ordinary-planet target"):
		shell.free()
		return failures
	var movement: int = int(view.player_input.bindings.forward.key.code)
	key(movement, true)
	view.planet_route_buttons[target].pressed.emit()
	check(owner.get_freedom_flight_state() == initial and not view.controls_armed, "Held controls bypassed planet neutral gate")
	key(movement, false)
	view.observe_neutral_controls()
	for gate in ["focus", "modal"]:
		view.focused = gate != "focus"
		view.journey_dialog_open = gate == "modal"
		view.planet_route_buttons[target].pressed.emit()
		check(owner.get_freedom_flight_state() == initial, "Planet target bypassed actual " + gate + " guard")
	view.focused = true
	view.journey_dialog_open = false
	check(save(owner, output.path_join("planet-ui-refused.json")) == source_bytes, "Held/modal/invalid destination refusals changed full Save")
	var records := []
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for leg in 2:
		view.refresh_planet_routes()
		packet = owner.get_freedom_planet_destinations()
		var target_id: String = report.target_planet_id if leg == 0 else report.home_planet_id
		target = target_index(packet, target_id)
		if not check(target >= 0, "Return chart lost its known genuine Home identity"): break
		var previously_visited: bool = packet.rows[target].visited
		var before_selection: Dictionary = owner.get_freedom_flight_state()
		var prior_bytes := save(owner, output.path_join("planet-ui-before-select-%d.json" % leg))
		await activate(tree, view, view.planet_route_buttons[target], leg == 1)
		var selected: Dictionary = owner.get_freedom_flight_state()
		check(view.paused and selected.tick == before_selection.tick and selected.resources == before_selection.resources and selected.position_metres == before_selection.position_metres and selected.jump.selected == target_id and selected.jump.route_kind == "planet", "Actual keyboard/controller target selection failed or advanced authoritative state")
		var selected_bytes := save(owner, output.path_join("planet-ui-selected-%d.json" % leg))
		var previous: Dictionary = JSON.parse_string(prior_bytes.get_string_from_utf8())
		var chosen: Dictionary = JSON.parse_string(selected_bytes.get_string_from_utf8())
		chosen.travel.composed.active = previous.travel.composed.active
		chosen.travel.composed.selected_body = previous.travel.composed.selected_body
		check(chosen == previous, "Planet target selection changed more than its explicit route/target metadata")
		if recovering and leg == 1:
			check("early" in view.planet_jump_button.text.to_lower() and "arrival" in view.planet_jump_button.tooltip_text.to_lower(), "Early planet jump lacks visible uncertainty warning")
			if strained: check("next arrival recovery" in view.planet_jump_button.tooltip_text.to_lower() and "next recovery" in view.primary_status.text.to_lower(), "Early planet jump hid its longer next-arrival cycle")
		await activate(tree, view, view.planet_jump_button, leg == 1)
		check(view.paused and owner.get_freedom_flight_state().jump.phase == "spool", "Actual keyboard/controller Begin did not arm paused3s spool")
		if leg == 0:
			view._process(0.125)
			check(owner.get_freedom_flight_state().tick == selected.tick and owner.get_freedom_flight_state().resources.jump_charges == selected.resources.jump_charges, "Paused planet spool consumed time/charge")
			await activate(tree, view, view.planet_jump_button, false)
			check(owner.get_freedom_flight_state().jump.phase == "idle" and save(owner, output.path_join("planet-ui-canceled.json")) == selected_bytes, "Uncommitted planet Cancel failed to restore exact selected Save")
			await activate(tree, view, view.planet_jump_button, false)
		phase(owner, prefix + ".leg%d.0.json" % leg, output.path_join("planet-ui-leg%d-0.json" % leg), records)
		view.observe_neutral_controls()
		view.toggle_pause()
		check(not view.paused and view.controls_armed, "Planet spool failed explicit neutral Resume")
		var before_id: String = owner.get_freedom_flight_state().planet_id
		var charges: int = owner.get_freedom_flight_state().resources.jump_charges
		for offset in range(1, 601):
			if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual planet jump tick refused: " + str(owner.get_last_error())): break
			if offset in [1, 360, 361, 600]:
				var state: Dictionary = owner.get_freedom_flight_state()
				check(FlightView.valid_state(state) and state.system_id == initial.system_id and state.resources.jump_charges == charges - (1 if offset >= 360 else 0), "Planet leg lost actual same-system identity/one-charge phase")
				check(not state.jump.has("point") and not state.jump.has("nominal_arrival") and not state.jump.has("reference_planet"), "Planet UI exposed unresolved arrival placement")
				if recovering:
					check(state.jump.recovery.pending_arrival == (offset >= 360 and offset < 600), "Recovery arrival state differs from actual commitment")
					if offset == 600: check(state.jump.recovery.remaining_seconds == (87.0 if strained and leg == 1 else 45.0) and state.jump.recovery.extra_radius_metres == 2000.0 and not state.jump.recovery.ready, "Actual arrival does not start full recovery")
				view.state = state
				view.update_view(0.0)
				phase(owner, prefix + ".leg%d.%d.json" % [leg, offset], output.path_join("planet-ui-leg%d-%d.json" % [leg, offset]), records)
		var arrived: Dictionary = owner.get_freedom_flight_state()
		check(view.error.is_empty() and arrived.planet_id == target_id and arrived.planet_id != before_id and arrived.jump.phase == "idle" and view.rendered_world == arrived.system_id + ":" + target_id, "Actual destination handoff retained the old terrain/body owner")
		view.pause_controls("Planet arrival controls")
		view.refresh_planet_routes()
		var arrival_packet: Dictionary = owner.get_freedom_planet_destinations()
		var current_index := target_index(arrival_packet, target_id)
		if check(current_index >= 0, "Arrival chart lost its actual body identity"):
			var row: Dictionary = arrival_packet.rows[current_index]
			# Existing planet presence records a landing, not an orbital reference change.
			check(row.current and row.visited == previously_visited and not row.selectable, "Orbital handoff fabricated or lost recorded planetary landing presence")
	check(records.size() == 10, "Planet phase proof lost one independently ticked boundary")
	mouse_after_handoff(view, owner, output)
	check(FileAccess.get_file_as_bytes(source) == source_bytes, "Planet checks modified source fixture")
	var result := {"version": 1, "completed": failures == 0, "scope": report.scope, "keyboard_outward": true, "controller_return": true, "phases": records, "source_sha256": FileAccess.get_sha256(source)}
	var file := FileAccess.open(output.path_join("planet-ui-report.json"), FileAccess.WRITE)
	if check(file != null, "Planet UI report unavailable"): file.store_string(JSON.stringify(result, "\t") + "\n")
	shell.free()
	await tree.process_frame
	return failures
