extends SceneTree
## Real C++ survey saves; no orbital placement or acquisition in presentation.
const Survey = preload("res://scripts/native/native_orbital_survey.gd")
const Observations = preload("res://scripts/native/recorded_observations.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const Shell = preload("res://scripts/native/native_start_shell.gd")
const ReturnCharge = preload("res://scripts/native/native_return_charge_check.gd")
var failures := 0
var return_charge_assertions := 0

func check(ok: bool, reason: String) -> bool:
	if not ok:
		failures += 1
		push_error(reason)
	return ok

func _initialize() -> void:
	check_inputs()
	call_deferred("run")

func check_inputs() -> void:
	check_return_charge_inputs()
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


func charge_check(ok: bool, reason: String) -> bool:
	return_charge_assertions += 1
	return check(ok, "Return-charge check: " + reason)

func charge_system_label(value: String) -> String:
	# Test-only join between the new decimal protocol and existing chart labels.
	# Each multiply is at most 10 * UINT32_MAX + 9, safely inside positive int64.
	if value.is_empty() or value.length() > 20 or value == "0" or (value.length() > 1 and value.begins_with("0")): return ""
	var high: int = 0
	var low: int = 0
	for index in value.length():
		var digit: int = value.unicode_at(index) - 48
		if digit < 0 or digit > 9: return ""
		var expanded: int = low * 10 + digit
		low = expanded & 0xffffffff
		high = high * 10 + (expanded >> 32)
		if high > 0xffffffff: return ""
	return "system-%08x%08x" % [high, low]

func check_return_charge_inputs() -> void:
	# Independent known formatting, including actual fixture IDs above signed64.
	for row in [["18446744073709551615", "system-ffffffffffffffff"], ["9223372036854775808", "system-8000000000000000"], ["677859337506523986", "system-09683d79dbc20b52"], ["2910174744474113395", "system-28630482e6b15573"], ["11569431468446217840", "system-a08ee09f81f67670"]]:
		charge_check(charge_system_label(row[0]) == row[1], "decimal ID/legacy label normalization failed: " + row[0])
	var packet := {"version": 1, "available": true, "tick": "18446744073709551615", "jump_charges": 3, "required_charges": 2, "charge_shortfall": 0, "flight_fraction": 0.75, "reserve": false, "operationally_empty": false, "legs": [{"source_id": "1", "destination_id": "2", "source_name": "Origin", "destination_name": "Neighbor"}, {"source_id": "2", "destination_id": "1", "source_name": "Neighbor", "destination_name": "Origin"}], "note": "Charge budget only; maneuver fuel and jump feasibility remain separate."}
	charge_check(ReturnCharge.valid_packet(packet), "valid closed11/two closed4 legs refused")
	var text: String = ReturnCharge.text(packet)
	charge_check("Origin → Neighbor → Origin" in text and "2 charges needed" in text and "3 carried" in text and "75.0%" in text and packet.note in text, "named route/actual quantities/provider qualification lost")
	var one := packet.duplicate(true)
	one.legs.resize(1)
	one.required_charges = 1
	charge_check(ReturnCharge.valid_packet(one), "one-leg budget refused")
	var bounds := packet.duplicate(true)
	bounds.note = "é".repeat(512)
	bounds.legs[0].destination_name = "é".repeat(128)
	bounds.legs[1].source_name = bounds.legs[0].destination_name
	bounds.legs[0].destination_id = "18446744073709551615"
	bounds.legs[1].source_id = bounds.legs[0].destination_id
	charge_check(ReturnCharge.valid_packet(bounds), "maximum canonical ID/UTF8 byte bounds refused")
	var shortfall := packet.duplicate(true)
	shortfall.jump_charges = 0
	shortfall.charge_shortfall = 2
	shortfall.flight_fraction = 0.0
	shortfall.reserve = true
	shortfall.operationally_empty = true
	charge_check(ReturnCharge.valid_packet(shortfall) and "SHORT 2" in ReturnCharge.text(shortfall) and "EMPTY" in ReturnCharge.text(shortfall), "shortfall or empty fuel warning lost")
	var unavailable := packet.duplicate(true)
	unavailable.available = false
	unavailable.required_charges = 0
	unavailable.charge_shortfall = 0
	unavailable.legs = []
	charge_check(ReturnCharge.valid_packet(unavailable) and ReturnCharge.text(unavailable) == unavailable.note, "unavailable reading invented applicable route quantities")
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		charge_check(not ReturnCharge.valid_packet(missing), "missing closed field accepted: " + str(field))
		var wrong := packet.duplicate(true)
		wrong[field] = null
		charge_check(not ReturnCharge.valid_packet(wrong), "null field accepted: " + str(field))
	for change in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"available": 1}, {"tick": 0}, {"tick": "01"}, {"tick": "-1"}, {"tick": "18446744073709551616"}, {"jump_charges": true}, {"jump_charges": 4}, {"jump_charges": -1}, {"required_charges": 2.0}, {"required_charges": 0}, {"required_charges": 3}, {"charge_shortfall": 1}, {"charge_shortfall": -1}, {"flight_fraction": 1}, {"flight_fraction": NAN}, {"flight_fraction": INF}, {"flight_fraction": -0.1}, {"flight_fraction": 1.1}, {"reserve": 1}, {"operationally_empty": 0}, {"legs": []}, {"legs": [null]}, {"note": ""}, {"note": "x".repeat(1025)}, {"note": "é".repeat(513)}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		charge_check(not ReturnCharge.valid_packet(bad) and "unavailable" in ReturnCharge.text(bad).to_lower(), "malformed packet reached budget text: " + str(change.keys()))
	for field in packet.legs[0]:
		var missing := packet.duplicate(true)
		missing.legs[0].erase(field)
		charge_check(not ReturnCharge.valid_packet(missing), "missing closed leg field accepted")
	for change in [{"extra": true}, {"source_id": "0"}, {"source_id": 1}, {"source_id": "01"}, {"destination_id": "1"}, {"destination_id": "18446744073709551616"}, {"source_name": ""}, {"destination_name": "x".repeat(257)}, {"source_name": "é".repeat(129)}]:
		var bad := packet.duplicate(true)
		bad.legs[0].merge(change, true)
		charge_check(not ReturnCharge.valid_packet(bad), "malformed closed leg accepted")
	for field in ["source_id", "source_name"]:
		var disconnected := packet.duplicate(true)
		disconnected.legs[1][field] = "3" if field == "source_id" else "Another neighbor"
		charge_check(not ReturnCharge.valid_packet(disconnected), "disconnected named legs accepted")
	var excess := packet.duplicate(true)
	excess.legs.append(packet.legs[0].duplicate(true))
	charge_check(not ReturnCharge.valid_packet(excess), "third leg accepted")
	unavailable.legs = packet.legs
	charge_check(not ReturnCharge.valid_packet(unavailable), "unavailable route leaked legs")
	for bad in [null, [], true, "packet", NAN]: charge_check(not ReturnCharge.valid_packet(bad), "non-dictionary accepted")

func charge_pad(device: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = JOY_BUTTON_A
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check_return_charge_readers(fixtures: String, assets: String, output: String) -> void:
	if not charge_check(DirAccess.make_dir_recursive_absolute(output) == OK, "proof directory unavailable"): return
	var source := fixtures.path_join("charted-arrivals")
	var source_hashes := {}
	var witnesses: Array = []
	var origin_id := ""
	for row in [{"phase": "seated-on", "required": 2}, {"phase": "origin-selected", "required": 1}, {"phase": "outbound-spool", "required": 0}, {"phase": "outbound-arrived", "required": 0}]:
		var path: String = source.path_join(row.phase + ".json")
		var original := FileAccess.get_file_as_bytes(path)
		source_hashes[row.phase + ".json"] = FileAccess.get_sha256(path)
		var owner: Variant = restore(path)
		if owner == null: return
		var state: Dictionary = owner.get_freedom_flight_state()
		var packet: Dictionary = owner.get_freedom_return_charge_check()
		if not charge_check(ReturnCharge.valid_packet(packet), "actual getter packet refused: " + row.phase): return
		charge_check(packet.available == (row.required > 0) and packet.required_charges == row.required and packet.tick == state.tick, "selected/phase ownership or actual clock mismatch: " + row.phase)
		charge_check(packet.jump_charges == state.resources.jump_charges and packet.flight_fraction == state.resources.fraction and packet.reserve == state.resources.reserve and packet.operationally_empty == state.resources.operationally_empty, "charge/fuel fields diverged from actual resource reading")
		if row.phase == "seated-on":
			origin_id = state.system_id
			charge_check(state.attached and not state.jump.available and packet.available, "docked charge check was incorrectly gated on physical departure readiness")
		if packet.available:
			charge_check(charge_system_label(packet.legs[0].source_id) == state.system_id and charge_system_label(packet.legs[0].destination_id) == state.jump.selected and charge_system_label(packet.legs[-1].destination_id) == origin_id, "budget does not follow actual current/selected/Origin identity")
			for leg: Dictionary in packet.legs:
				for chart_row: Dictionary in state.chart.rows:
					if chart_row.system_id == charge_system_label(leg.source_id): charge_check(chart_row.name == leg.source_name, "source name was not the known chart identity")
					if chart_row.system_id == charge_system_label(leg.destination_id): charge_check(chart_row.name == leg.destination_name, "destination name was not the known chart identity")
		for repeat in 3: charge_check(owner.get_freedom_return_charge_check() == packet, "repeated getter changed quote or clock")
		var saved := output.path_join(row.phase + "-read.json")
		charge_check(owner.get_freedom_flight_state() == state, "read-only getter changed current state: " + row.phase)
		charge_check(save_bytes(owner, saved) == original, "read-only getter changed complete Save: " + row.phase)
		witnesses.append({"file": saved.get_file(), "source": row.phase + ".json", "sha256": FileAccess.get_sha256(saved), "required_charges": packet.required_charges, "packet": packet})
		owner = null
	var shell_owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(shell_owner, source.path_join("seated-on.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	var original := FileAccess.get_file_as_bytes(source.path_join("seated-on.json"))
	var original_size := root.size
	var original_device: int = view.player_input.device
	var original_pads: Dictionary = view.player_input.available_pads.duplicate(true)
	view.player_input.available_pads[2] = true
	var navigation: Button
	for child in view.controls_menu.save_button.get_parent().get_children():
		if child is Button and child.text == "Flight instruments & navigation": navigation = child
	if not charge_check(navigation != null, "paused navigation entry missing"):
		shell.free(); return
	for row in [{"size": Vector2i(640, 450), "device": -1, "name": "keyboard-small"}, {"size": Vector2i(1280, 720), "device": 2, "name": "pad-normal"}]:
		root.size = row.size
		view.player_input.device = row.device
		view.player_input.install()
		for frame in 3: await process_frame
		view.observe_neutral_controls()
		navigation.grab_focus()
		if row.device < 0:
			key(KEY_SPACE, true)
			key(KEY_SPACE, false)
		else:
			charge_pad(2, true)
			charge_pad(2, false)
		for frame in 2: await process_frame
		var packet: Dictionary = shell_owner.get_freedom_return_charge_check()
		charge_check(view.paused and view.instruments_open and view.return_charge_status.is_visible_in_tree() and view.return_charge_status.text == ReturnCharge.text(packet), "actual keyboard/pad opening omitted charge budget: " + row.name)
		view.hud_scroll.ensure_control_visible(view.return_charge_status)
		for frame in 2: await process_frame
		var viewport := Rect2(view.hud_scroll.global_position, view.hud_scroll.size).grow(1.0)
		charge_check(viewport.encloses(view.return_charge_status.get_global_rect()) and view.return_charge_status.get_content_height() > 0 and view.return_charge_status.get_content_height() <= view.return_charge_status.size.y + 1.0, "budget text clipped at small/normal viewport: " + row.name)
		var open_path: String = output.path_join(row.name + "-open.json")
		charge_check(save_bytes(shell_owner, open_path) == original, "opening navigation changed complete Save")
		view.instruments_button.grab_focus()
		if row.device < 0:
			key(KEY_SPACE, true)
			key(KEY_SPACE, false)
		else:
			charge_pad(2, true)
			charge_pad(2, false)
		for frame in 2: await process_frame
		var back_path: String = output.path_join(row.name + "-back.json")
		charge_check(view.paused and not view.instruments_open and view.controls_menu.panel.visible and save_bytes(shell_owner, back_path) == original, "Back navigation resumed or changed Save")
		witnesses.append({"file": open_path.get_file(), "source": "seated-on.json", "sha256": FileAccess.get_sha256(open_path), "input": row.name})
		witnesses.append({"file": back_path.get_file(), "source": "seated-on.json", "sha256": FileAccess.get_sha256(back_path), "input": row.name})
	root.size = original_size
	view.player_input.device = original_device
	view.player_input.available_pads = original_pads
	view.player_input.install()
	view.show_instruments()
	view.chart_button.button_pressed = true
	view.update_view(0.0)
	var state: Dictionary = shell_owner.get_freedom_flight_state()
	var selected := -1
	for i in state.chart.rows.size():
		var row: Dictionary = state.chart.rows[i]
		if row.selectable and not row.current and row.system_id != state.jump.selected:
			selected = i
			break
	if charge_check(selected >= 0, "existing four-system chart lacks a second selectable route"):
		var peer: Variant = restore(source.path_join("seated-on.json"))
		if peer != null:
			charge_check(peer.select_freedom_jump(state.chart.rows[selected].system_id), "independent ordinary route selection refused")
			view.jump_destinations[selected].pressed.emit()
			var packet: Dictionary = shell_owner.get_freedom_return_charge_check()
			charge_check(ReturnCharge.valid_packet(packet) and packet.available and charge_system_label(packet.legs[0].destination_id) == state.chart.rows[selected].system_id, "actual system selection budget has a different destination")
			charge_check(view.return_charge_status.text == ReturnCharge.text(packet), "actual system selection left stale budget text")
			charge_check(shell_owner.get_freedom_flight_state().tick == state.tick and shell_owner.get_freedom_flight_state().resources == state.resources, "selection/budget reading consumed clock or resources")
			var path := output.path_join("selected-route.json")
			var oracle := output.path_join("selected-route-oracle.json")
			charge_check(save_bytes(shell_owner, path) == save_bytes(peer, oracle), "UI selection/getter differs from independent C++ selection complete Save")
			witnesses.append({"file": path.get_file(), "oracle": oracle.get_file(), "sha256": FileAccess.get_sha256(path), "destination_id": packet.legs[0].destination_id})
		peer = null
	for phase in source_hashes: charge_check(FileAccess.get_sha256(source.path_join(phase)) == source_hashes[phase], "reader rewrote existing source fixture")
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if charge_check(file != null, "proof report unavailable"):
		file.store_string(JSON.stringify({"schema": "apsis.return-charge-native/1", "completed": failures == 0, "failures": failures, "assertions": return_charge_assertions, "source_sha256": source_hashes, "witnesses": witnesses, "scope": "Read-only actual getter, docked two-leg/selected-Origin one-leg and spool/unselected refusal using existing C++ charted fixtures. One existing FlightView asset host, keyboard-small/selected-pad-normal opening and Back, full Save purity and independent ordinary system-selection parity. No new voyage, forecast, range/price/orbit/return-feasibility, manual hardware or GPU acceptance."}, "\t") + "\n")
	shell.free()
	shell_owner = null
	await process_frame

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
	if not failures: await check_return_charge_readers(args[0], args[1], output.path_join("return-charge-check"))
	var commissions := preload("res://tests/survey_commissions_test.gd").new()
	failures += await commissions.run(self, args[0], args[1], output.path_join("commissions"))
	var paid := preload("res://tests/paid_operations_test.gd").new()
	failures += await paid.run(self, args[0], args[1], output.path_join("paid-operations"))
	var buyer := preload("res://tests/specimen_sale_test.gd").new()
	failures += await buyer.run(self, args[0], args[1], output.path_join("specimen-sales"))
	var charted := preload("res://tests/charted_arrivals_test.gd").new()
	failures += await charted.run(self, args[0], args[1], output.path_join("charted-arrivals"))
	var quiet := preload("res://tests/quiet_travel_test.gd").new()
	failures += await quiet.run(self, args[0], args[1], output.path_join("quiet-travel"))
	print("Orbital survey checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)
