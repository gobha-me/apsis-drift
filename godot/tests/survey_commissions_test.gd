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
	var progress := {"active": true, "completed": false, "expired": false, "acquired_ticks": 3599, "earnings": "0", "remaining_ticks": 1}
	check(FlightView.valid_commission_progress(progress), "bounded partial progress refused")
	check("29.9 / 30 s" in FlightView.commission_progress_text(progress) and "less than 1 s" in FlightView.commission_progress_text(progress), "3599 intervals or last pre-deadline tick round up")
	var complete := progress.duplicate()
	complete.merge({"completed": true, "acquired_ticks": 3600, "remaining_ticks": 120}, true)
	check("report ready" in FlightView.commission_progress_text(complete) and "0:00:01 left" in FlightView.commission_progress_text(complete), "completed report loses remaining submission time")
	complete.merge({"expired": true, "remaining_ticks": 0}, true)
	check(FlightView.valid_commission_progress(complete) and FlightView.commission_progress_text(complete) == "Commission · deadline passed", "deadline equality does not expire a completed report")
	var inactive := {"active": false, "completed": false, "expired": false, "acquired_ticks": 0, "earnings": "100", "remaining_ticks": 0}
	check(FlightView.valid_commission_progress(inactive) and FlightView.commission_progress_text(inactive).is_empty(), "inactive paid account invents active countdown")
	for bad_remaining in [null, false, 1.0, NAN, INF, -1, 1728001, 0]:
		var bad := progress.duplicate(true)
		bad.remaining_ticks = bad_remaining
		check(not FlightView.valid_commission_progress(bad), "malformed or inconsistent remaining ticks accepted")

	for change in [{"extra": true}, {"active": 1}, {"completed": null}, {"expired": "no"}, {"acquired_ticks": 3599.0}, {"acquired_ticks": NAN}, {"acquired_ticks": -1}, {"acquired_ticks": 3601}, {"acquired_ticks": 3600}, {"earnings": 0}, {"earnings": "01"}, {"earnings": "18446744073709551616"}]:
		var bad := progress.duplicate(true)
		bad.merge(change, true)
		check(not FlightView.valid_commission_progress(bad), "malformed cheap progress accepted: " + str(change.keys()))
	for field in progress:
		var bad := progress.duplicate(true)
		bad.erase(field)
		check(not FlightView.valid_commission_progress(bad), "missing progress field accepted: " + field)

func geological_packet_controls(owner: Variant, output: String) -> void:
	var before := save(owner, output.path_join("geological-query-before.json"))
	var offer: Dictionary = owner.get_freedom_survey_commissions(2)
	check(Office.valid_packet(offer) and offer.version == 2 and offer.kind == 2 and offer.size() == 27 and offer.required_ticks == 0 and offer.acquired_ticks == 0 and offer.specimen.is_empty() and offer.submission_token.is_empty(), "actual geological offer lacks closed physical-work schema")
	for field in offer:
		var bad := offer.duplicate(true)
		bad.erase(field)
		check(not Office.valid_packet(bad), "missing geological field accepted: " + field)
	for change in [{"extra": true}, {"version": 1}, {"version": 2.0}, {"kind": 1}, {"kind": 2.0}, {"kind": true}, {"specimen": null}, {"specimen": "x".repeat(513)}, {"submission_token": []}, {"submission_token": "x".repeat(2049)}, {"acquired_ticks": 1}, {"required_ticks": 3600}, {"completed": true}, {"specimen": "old sample"}]:
		var bad := offer.duplicate(true)
		bad.merge(change, true)
		check(not Office.valid_packet(bad), "malformed geological packet accepted: " + str(change.keys()))
	# Reader-only shape fixture, not a physical collection or authoritative Save.
	var ready := offer.duplicate(true)
	ready.merge({"selected": true, "sequence": 1, "accepted_tick": offer.tick, "deadline_tick": str(int(offer.tick) + 1728000), "completed": true, "specimen": "Pouch slot 1: Pale specimen", "submission_token": "reader-shape-only", "can_accept": false, "can_submit": true, "can_abandon": true}, true)
	check(Office.valid_packet(ready), "physical-ready packet requires fake orbital intervals")
	ready.submission_token = ""
	check(not Office.valid_packet(ready), "delivery enables without reviewed physical proof token")
	var progress := {"version": 2, "kind": 2, "active": true, "ready": false, "expired": false, "earnings": "0", "remaining_ticks": 120}
	check(FlightView.valid_commission_progress(progress) and "collect a fresh specimen" in FlightView.commission_progress_text(progress), "geological HUD invents orbital measurement")
	progress.ready = true
	check(FlightView.valid_commission_progress(progress) and "fresh specimen carried" in FlightView.commission_progress_text(progress), "geological HUD hides actual physical readiness")
	for change in [{"acquired_ticks": 3600}, {"completed": true}, {"version": 2.0}, {"kind": 1}, {"active": false}, {"ready": 1}, {"remaining_ticks": 0}, {"earnings": "1"}]:
		var bad := progress.duplicate(true)
		bad.merge(change, true)
		check(not FlightView.valid_commission_progress(bad), "malformed physical HUD accepted: " + str(change.keys()))
	for value in [null, true, false, "1", "2", 1.0, 2.0, NAN, INF, 0, 3, [], {}]:
		check(owner.get_freedom_survey_commissions(value).is_empty(), "non-typed or unsupported office kind accepted")
	for value in [null, true, 1, 1.0, NAN, INF, "", "x".repeat(2049), "é", [], {}]:
		check(not owner.submit_freedom_geological_survey_commission(value), "malformed geological confirmation accepted")
	check(save(owner, output.path_join("geological-query-after.json")) == before, "geological reads or malformed confirmations mutated complete Save")

func progress_matches_office(owner: Variant, packet: Dictionary) -> bool:
	var state: Dictionary = owner.get_freedom_flight_state()
	var progress: Variant = state.get("survey_commission")
	if not FlightView.valid_commission_progress(progress): return false
	# Retained seed42 fixture clocks fit signed int; this is not a uint64 parser.
	var remaining := maxi(0, int(packet.deadline_tick) - int(packet.tick)) if packet.sequence != 0 else 0
	if packet.get("kind", 1) == 2 and packet.sequence != 0:
		return progress.get("version") == 2 and progress.ready == packet.completed and progress.expired == packet.expired and progress.remaining_ticks == remaining
	return progress.remaining_ticks == remaining and progress.active == (packet.sequence != 0) and progress.completed == packet.completed and progress.expired == packet.expired and progress.acquired_ticks == packet.acquired_ticks

func offer_packet_controls(owner: Variant, output: String) -> void:
	var before: PackedByteArray = save(owner, output.path_join("offers-query-before.json"))
	var offers: Dictionary = owner.get_freedom_survey_commission_offers()
	if not check(Office.valid_offers(offers), "actual closed destination offers packet refused"): return
	for field in offers:
		var bad: Dictionary = offers.duplicate(true)
		bad.erase(field)
		check(not Office.valid_offers(bad), "missing offers field accepted: " + str(field))
	for change in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"tick": "01"}, {"note": ""}, {"note": "x".repeat(1025)}, {"offers": {}}, {"offers": [1]}]:
		var bad: Dictionary = offers.duplicate(true)
		bad.merge(change, true)
		check(not Office.valid_offers(bad), "malformed destination offers accepted: " + str(change.keys()))
	if not offers.offers.is_empty():
		var row: Dictionary = offers.offers[0]
		var quote: Dictionary = owner.get_freedom_survey_commissions_for_target(row.system_id, row.planet_id)
		check(Office.valid_packet(quote) and quote.size() == 24 and quote.system_id == row.system_id and quote.planet_id == row.planet_id and quote.body_name == row.body_name, "targeted quote differs from its admitted destination")
		for field in row:
			var bad: Dictionary = offers.duplicate(true)
			bad.offers[0].erase(field)
			check(not Office.valid_offers(bad), "missing destination row field accepted: " + str(field))
		for change in [{"extra": 0}, {"system_id": 1}, {"system_id": "0"}, {"planet_id": "01"}, {"planet_id": "18446744073709551616"}, {"system_name": ""}, {"body_name": false}, {"route": {} }]:
			var bad: Dictionary = offers.duplicate(true)
			bad.offers[0].merge(change, true)
			check(not Office.valid_offers(bad), "malformed destination row accepted: " + str(change.keys()))
		var repeated: Dictionary = offers.duplicate(true)
		repeated.offers = [row, row]
		check(not Office.valid_offers(repeated), "duplicate destination identity accepted")
		for value in [null, true, 1, 1.0, NAN, INF, "", "0", "01", "-1", "18446744073709551616", [], {}]:
			check(owner.get_freedom_survey_commissions_for_target(value, row.planet_id).is_empty() and owner.get_freedom_survey_commissions_for_target(row.system_id, value).is_empty(), "noncanonical destination Variant exposed a quote")
			check(not owner.accept_freedom_survey_commission_target(quote.offer_until, str(quote.offer_sequence), value, row.planet_id) and not owner.accept_freedom_survey_commission_target(quote.offer_until, str(quote.offer_sequence), row.system_id, value), "noncanonical destination Variant accepted work")
	check(save(owner, output.path_join("offers-query-after.json")) == before, "offer reads/malformed targeted commands changed the complete Save")

func malformed_commands(owner: Variant, packet: Dictionary, output: String) -> void:
	var before := save(owner, output.path_join("arguments-before.json"))
	for value in [null, 1, 1.0, NAN, INF, "", "01", "-1", "1.0", "4294967296", [], {}]:
		check(not owner.accept_freedom_survey_commission(packet.offer_until, value), "wrong sequence Variant accepted")
		check(not owner.accept_freedom_geological_survey_commission(packet.offer_until, value), "wrong geological sequence Variant accepted")
		check(not owner.submit_freedom_survey_commission(value), "wrong submit Variant accepted")
		check(not owner.abandon_freedom_survey_commission(value), "wrong abandon Variant accepted")
	for value in [null, 1, 1.0, NAN, INF, "", "01", "-1", "1.0", "18446744073709551616", [], {}]:
		check(not owner.accept_freedom_survey_commission(value, str(packet.offer_sequence)), "wrong offer Variant accepted")
		check(not owner.accept_freedom_geological_survey_commission(value, str(packet.offer_sequence)), "wrong geological offer Variant accepted")
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

func run_geological(tree: SceneTree, fixtures: String, assets: String, output: String) -> void:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "geological witness directory unavailable"): return
	var originals := {}
	for phase in ["geo-offer", "geo-accepted", "geo-collected", "geo-origin", "geo-paid-origin", "geo-delivered", "geo-lost"]:
		originals[phase] = FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json"))
		check(not originals[phase].is_empty(), "missing actual geological source: " + phase)
	if failures: return
	var backend: Variant = restore(fixtures.path_join("geo-offer.json"))
	if backend == null: return
	var offer: Dictionary = backend.get_freedom_survey_commissions(2)
	check(Office.valid_packet(offer) and offer.can_accept and offer.kind == 2, "actual geological source lacks explicit offer")
	backend = null
	var station_shell := host(tree, fixtures.path_join("geo-offer.json"), assets)
	if station_shell == null: return
	var station: Control = station_shell.current_view
	var station_owner: Variant = station_shell.bridge
	if not check(station is WalkView and station.paused, "geological offer lacks actual station walking host"):
		station_shell.free()
		return
	station.open_survey_commissions()
	var station_page: Control = reader(station)
	await tree.process_frame
	station_page.geological_button.pressed.emit()
	check(station_page.packet == offer and save(station_owner, output.path_join("offer-ui.json")) == originals["geo-offer"], "geological station choice mutated source or differs from actual offer")
	await tree.process_frame
	station_page.accept_button.pressed.emit()
	var active: Dictionary = station_owner.get_freedom_survey_commissions()
	check(active.get("kind") == 2 and not active.completed and not station_owner.accept_freedom_survey_commission(offer.offer_until, str(offer.offer_sequence)) and save(station_owner, output.path_join("accepted-ui.json")) == originals["geo-accepted"], "explicit station geological Accept differs from C++ full Save or permits orbital replacement")
	station_page.geological_button.pressed.emit()
	check(save(station_owner, output.path_join("accepted-duplicate.json")) == originals["geo-accepted"], "queued geological acceptance changed active terms")
	station_shell.free()
	station_owner = null
	await tree.process_frame
	backend = restore(fixtures.path_join("geo-collected.json"))
	if backend != null:
		var collected: Dictionary = backend.get_freedom_survey_commissions(2)
		check(Office.valid_packet(collected) and collected.completed and not collected.at_origin and not collected.can_submit and progress_matches_office(backend, collected), "actual fresh collection lacks truthful remote physical HUD")
		check(not backend.submit_freedom_geological_survey_commission(collected.submission_token) and save(backend, output.path_join("remote-refused.json")) == originals["geo-collected"], "fresh carried sample remotely paid")
	backend = restore(fixtures.path_join("geo-lost.json"))
	if backend != null:
		var lost: Dictionary = backend.get_freedom_survey_commissions(2)
		check(Office.valid_packet(lost) and lost.sequence != 0 and not lost.completed and lost.submission_token.is_empty() and not backend.submit_freedom_geological_survey_commission("old-physical-item") and save(backend, output.path_join("lost-refused.json")) == originals["geo-lost"], "replacement reconstructs lost goods from discovery notes")
	backend = null
	var shell := host(tree, fixtures.path_join("geo-paid-origin.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	if not check(view is FlightView and view.paused, "geological return fixture lacks actual seated flight host"):
		shell.free()
		return
	view.open_survey_commissions()
	var page: Control = reader(view)
	check(page.visible and Office.valid_packet(page.packet) and page.packet.kind == 2 and page.packet.completed and page.packet.can_submit and not page.submit_button.disabled and page.packet.required_ticks == 0 and page.packet.acquired_ticks == 0 and page.packet.specimen in page.details.text and "consumed" in page.details.text and progress_matches_office(owner, page.packet), "physical delivery reader omits actual item/consumption or invents orbital work")
	check("fresh specimen carried" in view.primary_status.text and "deliver the named specimen" in view.primary_status.tooltip_text, "compact physical-work HUD omits its ready state or named delivery tooltip")
	check(save(owner, output.path_join("reader-ui.json")) == originals["geo-paid-origin"], "paused geological office read changed full Save")
	var token: String = page.packet.submission_token
	check(not owner.submit_freedom_survey_commission(str(page.packet.sequence)), "orbital submission accepted physical work")
	view.focused = false
	view.survey_commission_command("submit")
	view.focused = true
	view.journey_dialog_open = true
	view.survey_commission_command("submit")
	view.journey_dialog_open = false
	view.mode_change_pending = true
	view.survey_commission_command("submit")
	view.mode_change_pending = false
	view.save_dialog.popup_centered()
	view.survey_commission_command("submit")
	view.save_dialog.hide()
	key(int(view.player_input.bindings.forward.key.code), true)
	view.survey_commission_command("submit")
	key(int(view.player_input.bindings.forward.key.code), false)
	view.observe_neutral_controls()
	check(save(owner, output.path_join("modal-refused.json")) == originals["geo-paid-origin"], "held/modal geological action changed full Save")
	page.packet.submission_token += ":stale"
	view.survey_commission_command("submit")
	check(page.packet == owner.get_freedom_survey_commissions(2) and save(owner, output.path_join("stale-refused.json")) == originals["geo-paid-origin"], "stale item confirmation committed instead of refreshing")
	await tree.process_frame
	page.submit_button.pressed.emit()
	check(view.paused and page.visible and page.packet.sequence == 0 and page.packet.earnings == "100" and owner.get_freedom_paid_operations().cash == "280" and save(owner, output.path_join("delivered-ui.json")) == originals["geo-delivered"], "explicit named delivery differs from C++ item removal/payment Save")
	check(owner.get_freedom_specimen_sale().earned_credits == "0", "commission also credited ordinary 40-credit specimen sale")
	check(not owner.submit_freedom_geological_survey_commission(token), "duplicate physical delivery paid again")
	page.submit_button.pressed.emit()
	check(save(owner, output.path_join("duplicate-refused.json")) == originals["geo-delivered"], "queued duplicate delivery changed Save")
	backend = restore(output.path_join("delivered-ui.json"))
	if backend != null:
		check(backend.get_freedom_survey_commissions(2) == page.packet and save(backend, output.path_join("delivered-continued.json")) == originals["geo-delivered"], "delivery Continue lost receipt/income or recreated goods")
	backend = null
	shell.free()
	owner = null
	await tree.process_frame
	for phase in originals:
		check(FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json")) == originals[phase], "geological presentation rewrote producer source: " + phase)
	var report := {"schema": "apsis.geological-commissions-native/1", "completed": failures == 0, "failures": failures, "physical_proof": true, "reward_credits": 100, "paid_cash": "280", "scope": "Actual public acceptance, real initialized-supported-ground inspection/collection source, explicit same-clock Origin attachment, paused named delivery and whole C++ Save parity. Loss uses actual replacement; no uninterrupted flown collection/return, manual pacing, hardware or profit claim."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "geological report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")

func destination_index(page: Control, quote: Dictionary) -> int:
	for index in page.offers_packet.get("offers", []).size():
		var row: Dictionary = page.offers_packet.offers[index]
		if row.system_id == quote.system_id and row.planet_id == quote.planet_id: return index
	return -1

func activate_destination(tree: SceneTree, page: Control, button: Button, pad_device := -1) -> void:
	button.grab_focus()
	page.scroll.ensure_control_visible(button)
	await tree.process_frame
	check(button.has_focus() and button.is_visible_in_tree() and not button.disabled, "destination action cannot receive keyboard/controller focus")
	for down in [true, false]:
		if pad_device < 0: key(KEY_ENTER, down)
		else:
			var event := InputEventJoypadButton.new()
			event.device = pad_device
			event.button_index = JOY_BUTTON_A
			event.pressed = down
			Input.parse_input_event(event)
			Input.flush_buffered_events()
		await tree.process_frame

func run_destinations(tree: SceneTree, fixtures: String, assets: String, output: String) -> void:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "destination witness directory unavailable"): return
	var originals: Dictionary = {}
	for name in ["station", "station-accepted", "seated", "zero-charge", "accepted", "supported", "partial", "ready", "origin", "paid", "expired", "renewed"]:
		originals[name] = FileAccess.get_file_as_bytes(fixtures.path_join(name + ".json"))
		check(not originals[name].is_empty(), "destination C++ phase missing: " + name)
	if failures: return
	var backend: Variant = restore(fixtures.path_join("station-accepted.json"))
	if backend == null: return
	var target: Dictionary = backend.get_freedom_survey_commissions()
	backend = null
	var shell: Control = host(tree, fixtures.path_join("station.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	view.open_survey_commissions()
	var page: Control = reader(view)
	var index: int = destination_index(page, target)
	if not check(view is WalkView and page.visible and page.offers_packet.offers.size() == 2 and index >= 0, "actual station did not expose the two admitted destinations"):
		shell.free(); return
	var initial_quote: Dictionary = page.packet.duplicate(true)
	refused_ui(view, owner, "choose_destination_" + str(index), output, "choice-guarded")
	await activate_destination(tree, page, page.destination_buttons[index])
	check(page.packet == owner.get_freedom_survey_commissions_for_target(target.system_id, target.planet_id) and page.packet != initial_quote and target.body_name in page.details.text and page.offers_packet.offers[index].system_name in page.details.text and save(owner, output.path_join("station-choice.json")) == originals.station, "keyboard choice did not display its own full target quote/route purely")
	var window: Vector2i = tree.root.size
	check(tree.root.content_scale_size == Vector2i(1920, 1080) and tree.root.content_scale_mode == Window.CONTENT_SCALE_MODE_CANVAS_ITEMS, "destination layout lost actual project stretch")
	for pixels in [Vector2i(640, 450), Vector2i(1280, 720)]:
		tree.root.size = pixels
		for frame in 4: await tree.process_frame
		page.accept_button.grab_focus()
		page.scroll.ensure_control_visible(page.accept_button)
		for frame in 2: await tree.process_frame
		var bounds := Rect2(Vector2.ZERO, Vector2(pixels)).grow(1)
		var button_rect: Rect2 = tree.root.get_final_transform() * page.accept_button.get_global_rect()
		check(bounds.encloses(button_rect) and button_rect.size.y >= 44, "destination confirmation is unreadable/offscreen at " + str(pixels))
	tree.root.size = window
	for frame in 3: await tree.process_frame
	view.open_paid_operations()
	check(page.paid_open(), "existing paid reader became unreachable while browsing a destination")
	page.close_paid()
	check(page.packet.system_id == target.system_id and page.packet.planet_id == target.planet_id and target.body_name in page.details.text, "child-service return replaced the chosen quote with Nearby")
	page.back.pressed.emit()
	check(save(owner, output.path_join("station-back.json")) == originals.station and view.paused, "destination Back changed full Save or resumed")
	view.open_survey_commissions()
	page = reader(view)
	check(page.requested_target.is_empty() and page.packet == owner.get_freedom_survey_commissions(page.requested_kind) and page.packet == initial_quote and save(owner, output.path_join("station-reopened.json")) == originals.station, "top-level reopen retained a transient destination proposal or changed the complete Save")
	await activate_destination(tree, page, page.destination_buttons[index])
	check(page.packet.system_id == target.system_id and page.packet.planet_id == target.planet_id, "explicit reselection after reopen did not restore the reviewed target")
	await activate_destination(tree, page, page.accept_button)
	check(save(owner, output.path_join("station-accepted-ui.json")) == originals["station-accepted"] and page.packet.sequence != 0 and page.requested_target.is_empty(), "station target acceptance differs from complete C++ oracle")
	for button: Button in page.destination_buttons: check(button.disabled, "active destination remains changeable")
	check(page.orbital_button.disabled and page.geological_button.disabled, "active kind remains changeable")
	view.survey_commission_command("choose_geological")
	view.survey_commission_command("choose_destination_" + str(1 - index))
	check(save(owner, output.path_join("active-locked.json")) == originals["station-accepted"], "active destination/kind switch changed the complete owner")
	shell.free(); owner = null
	await tree.process_frame

	shell = host(tree, fixtures.path_join("zero-charge.json"), assets)
	if shell == null: return
	view = shell.current_view; owner = shell.bridge
	view.open_survey_commissions(); page = reader(view)
	index = destination_index(page, target)
	if not check(view is FlightView and index >= 0 and page.offers_packet.offers[index].route.jump_charges == 0 and page.offers_packet.offers[index].route.charge_shortfall == 2, "initialized zero-charge source lost its actual shortfall"):
		shell.free(); return
	var old_device: int = view.player_input.device
	var old_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 2; view.player_input.available_pads[2] = true; view.player_input.install()
	page.destination_buttons[index].grab_focus()
	for down in [true, false]:
		var foreign := InputEventJoypadButton.new()
		foreign.device = 3; foreign.button_index = JOY_BUTTON_A; foreign.pressed = down
		Input.parse_input_event(foreign); Input.flush_buffered_events()
		await tree.process_frame
	check(page.requested_target.is_empty() and save(owner, output.path_join("foreign-pad.json")) == originals["zero-charge"], "foreign pad selected or accepted a destination")
	await activate_destination(tree, page, page.destination_buttons[index], 2)
	view.player_input.device = old_device; view.player_input.available_pads = old_pads; view.player_input.install()
	check(page.packet.system_id == target.system_id and page.packet.planet_id == target.planet_id and page.packet.can_accept and not page.accept_button.disabled and "SHORT 2" in page.details.text, "zero charges became an acceptance gate or selected quote was lost")
	var reviewed: Dictionary = page.packet.duplicate(true)
	page.packet.system_id = "1"
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(page.packet == reviewed and save(owner, output.path_join("stale-quote-refused.json")) == originals["zero-charge"], "stale target confirmation accepted instead of refreshing complete terms")
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(save(owner, output.path_join("zero-accepted-ui.json")) == originals.accepted, "zero-charge flight acceptance differs from actual C++ result")
	page.back.pressed.emit()
	view.service_button.pressed.emit()
	check(save(owner, output.path_join("supported-ui.json")) == originals.supported, "existing free replenishment became unavailable after zero-charge acceptance")
	shell.free(); owner = null
	await tree.process_frame

	for phase in ["partial", "ready", "seated"]:
		backend = restore(fixtures.path_join(phase + ".json"))
		if backend == null: return
		var quote: Dictionary = backend.get_freedom_survey_commissions()
		check(Office.valid_packet(quote) and save(backend, output.path_join(phase + "-continued.json")) == originals[phase], "destination phase Continue/read changed full Save: " + phase)
		if phase != "seated": check(quote.system_id == target.system_id and quote.planet_id == target.planet_id and quote.acquired_ticks == (600 if phase == "partial" else 3600), "target acquisition or literal accepted terms changed")
		backend = null

	shell = host(tree, fixtures.path_join("expired.json"), assets)
	if shell == null: return
	view = shell.current_view; owner = shell.bridge
	view.open_survey_commissions(); page = reader(view)
	var expired: Dictionary = owner.get_freedom_survey_commissions()
	check(page.packet == expired and expired.sequence != 0 and expired.expired and expired.can_abandon and expired.can_accept and owner.get_freedom_survey_commissions_for_target(expired.system_id, expired.planet_id) == expired, "expired matching read replaced accepted terms with a fresh proposal")
	for button: Button in page.destination_buttons: check(button.disabled, "expired destination became changeable")
	view.survey_commission_command("choose_geological")
	check(save(owner, output.path_join("expired-locked.json")) == originals.expired, "expired kind change bypassed explicit abandonment")
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(save(owner, output.path_join("renewed-ui.json")) == originals.renewed, "same-target renewal differs from complete C++ oracle")
	shell.free(); owner = null
	await tree.process_frame

	shell = host(tree, fixtures.path_join("origin.json"), assets)
	if shell == null: return
	view = shell.current_view; owner = shell.bridge
	view.open_survey_commissions(); page = reader(view)
	check(page.packet.completed and page.packet.can_submit and page.packet.system_id == target.system_id and page.packet.planet_id == target.planet_id, "returned report lost actual selected target/submission")
	await tree.process_frame
	page.submit_button.pressed.emit()
	check(save(owner, output.path_join("paid-ui.json")) == originals.paid, "destination report payment differs from actual C++ oracle")
	page.submit_button.pressed.emit()
	check(save(owner, output.path_join("duplicate-paid.json")) == originals.paid, "destination report paid twice")
	shell.free(); owner = null
	await tree.process_frame
	for phase in originals: check(FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json")) == originals[phase], "destination presentation rewrote producer source: " + phase)
	var report: Dictionary = {"schema": "apsis.commission-destinations-native/1", "completed": failures == 0, "failures": failures, "cpp_phases": originals.size(), "scope": "Actual station and seated hosts; strict target-bound quote/confirmation, keyboard/selected-pad choice, full C++ acceptance/renewal/service/payout Save parity. Sources label initialized zero-charge, orbit, deadline and return-port boundaries; not an uninterrupted Waypoint earning trip, manual/hardware or viability proof."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "destination report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")

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
	geological_packet_controls(backend, output)
	malformed_commands(backend, packet, output)
	offer_packet_controls(backend, output)
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
	page.geological_button.grab_focus()
	key(KEY_ENTER, true)
	key(KEY_ENTER, false)
	check(page.packet == owner.get_freedom_survey_commissions(2) and page.packet.kind == 2 and "Discovery notes" in page.details.text and save(owner, output.path_join("geological-choice-ui.json")) == originals["commission-station"], "station kind selection is not read-only or hides consumption terms")
	page.orbital_button.pressed.emit()
	check(page.requested_kind == 2, "same-frame queued kind choice bypassed action barrier")
	await tree.process_frame
	page.orbital_button.pressed.emit()
	check(page.packet == packet and save(owner, output.path_join("orbital-choice-ui.json")) == originals["commission-station"], "return to orbital offer mutated full Save")
	await tree.process_frame
	page.accept_button.pressed.emit()
	check(view.paused and page.visible and page.packet.acquired_ticks == 0 and page.packet.sequence == 1 and save(owner, output.path_join("accepted-ui.json")) == originals["commission-accepted"], "explicit station Accept differs from direct C++ full Save")
	check(page.orbital_button.disabled and page.geological_button.disabled, "active work permits kind replacement without abandonment")
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
	check(progress_matches_office(owner, page.packet) and "29.9 / 30 s" in view.primary_status.text, "actual near HUD countdown differs from C++ office")
	check(save(owner, output.path_join("near-reader.json")) == originals["commission-near"], "near reader acquired work while paused")
	view.survey_commission_command("choose_geological")
	check(page.packet.get("kind", 1) == 1 and page.geological_button.disabled and save(owner, output.path_join("active-kind-refused.json")) == originals["commission-near"], "active orbital work was replaced by a geological selection")
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
	check(progress_matches_office(owner, page.packet) and "report ready" in view.primary_status.text, "actual completed HUD lost submission countdown")
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
	check(progress_matches_office(owner, page.packet), "actual paid inactive account retains countdown")
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
		check(progress_matches_office(backend, expired), "actual exact-deadline fixture has nonzero remaining ticks")
		check(Office.valid_packet(expired) and expired.completed and expired.expired and not expired.can_submit and not backend.submit_freedom_survey_commission(str(expired.sequence)) and save(backend, output.path_join("expired-refused.json")) == originals["commission-expired"], "expired completed report paid or changed source")
	backend = null
	for phase in originals:
		check(FileAccess.get_file_as_bytes(fixtures.path_join(phase + ".json")) == originals[phase], "presentation rewrote C++ source fixture: " + phase)
	if not failures: await run_destinations(tree, fixtures.path_join("commission-destinations"), assets, output.get_base_dir().path_join("commission-destinations"))
	if not failures: await run_geological(tree, fixtures.path_join("geological-commissions"), assets, output.get_base_dir().path_join("geological-commissions"))
	var report := {"schema": "apsis.survey-commissions-native/1", "completed": failures == 0, "failures": failures, "witnesses": witnesses, "scope": "Actual station/flight Shell Continue, paused office commands, malformed/held/modal/device refusal, full C++ Save parity and one committed completion interval. Source scenario uses explicitly initialized home-space, remote orbit and home-port poses with actual charged jumps; not uninterrupted flown/manual/hardware/performance acceptance."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
