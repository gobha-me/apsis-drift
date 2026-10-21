extends RefCounted
## Shared checks at the existing genuine inspected-foot checkpoints only.
const EnvironmentState = preload("res://scripts/native/native_environment.gd")
var failures := 0
var output := ""

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error("Field samples: " + message)
	return value

func save_bytes(owner: Variant, name: String) -> PackedByteArray:
	var path := output.path_join("field-samples-" + name + ".json")
	if not check(owner.save_freedom_as(path), "Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

static func saved_knowledge(bytes: PackedByteArray) -> Dictionary:
	var value: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	for depth in 16:
		if not value is Dictionary: return {}
		if value.get("knowledge") is Dictionary: return value.knowledge
		value = value.get("voyage", value.get("base"))
	return {}

static func uninspected(owner: Variant, site_id: String, destination: String) -> bool:
	var state: Dictionary = owner.get_freedom_flight_state()
	var path := destination.path_join("field-samples-uninspected.json")
	if not owner.save_freedom_as(path): return false
	var bytes := FileAccess.get_file_as_bytes(path)
	var availability: Dictionary = owner.get_freedom_field_sample_availability(site_id)
	var packet: Dictionary = owner.get_freedom_field_samples()
	return packet == {"selected": false, "capacity": 4, "carried": [], "journal": []} and not saved_knowledge(bytes).has("field_samples") and not availability.get("available", true) and not str(availability.get("reason", "")).is_empty() and not owner.collect_freedom_field_sample(site_id) and owner.get_freedom_flight_state() == state and owner.save_freedom_as(path + ".after.json") and FileAccess.get_file_as_bytes(path + ".after.json") == bytes

func valid_packet(packet: Dictionary, state: Dictionary, site_id: String) -> bool:
	if packet.size() != 4 or packet.get("selected") != true or packet.get("capacity") != 4: return false
	if not packet.get("carried") is Array or not packet.get("journal") is Array: return false
	if packet.carried.size() != 1 or packet.journal.size() != 1: return false
	var expected_body: String = state.get("body_id", state.get("planet_id", ""))
	var expected_kind := "moon" if state.get("body_kind") == "moon" else "planet"
	for row: Variant in [packet.carried[0], packet.journal[0]]:
		if not row is Dictionary or row.size() != 9: return false
		for key in ["system_id", "body_id", "site_id", "craft_id", "tick"]:
			if not EnvironmentState.decimal(row.get(key)): return false
		if row.system_id != state.system_id or row.body_kind != expected_kind or row.body_id != expected_body or row.site_id != site_id or row.craft_id != state.craft_id: return false
		if typeof(row.get("kind")) != TYPE_INT or row.kind < 0 or row.kind > 2: return false
		if not row.get("label") is String or row.label.is_empty() or not row.get("source_name") is String or row.source_name.is_empty(): return false
	return true

static func run(tree: SceneTree, shell: Control, owner: Variant, assets: String, destination: String, site_id: String, activate: Callable) -> int:
	var helper: RefCounted = load("res://tests/native_field_samples.gd").new()
	helper.output = destination
	await helper.exercise(tree, shell, owner, assets, site_id, activate)
	return int(helper.failures)

func exercise(tree: SceneTree, shell: Control, owner: Variant, assets: String, site_id: String, activate: Callable) -> void:
	var view: Control = shell.current_view
	view.set_process(false)
	var initial: Dictionary = owner.get_freedom_flight_state()
	var observations: Dictionary = owner.get_freedom_observations()
	var before := save_bytes(owner, "before")
	var absent: Dictionary = owner.get_freedom_field_samples()
	if not check(view.paused and view.surface_walking() and absent == {"selected": false, "capacity": 4, "carried": [], "journal": []} and not saved_knowledge(before).has("field_samples"), "Inspected foot lost historical absent pouch state"): return
	view.update_view(0.0)
	if not check(not view.sample_collect_button.disabled and view.sample_collect_button.visible and owner.get_freedom_field_sample_availability(site_id).get("available", false), "Actual inspected supported site lacks its Collect action"): return
	# Empty reads are real menu actions and must not adopt the optional state.
	await activate.call(view, view.sample_pouch_button, false)
	var reader: Control = view.controls_menu.surface_reference_view
	if not check(reader.visible and reader.heading.text == "FIELD SAMPLES" and reader.back_button.has_focus() and "No specimens carried" in reader.body.text and save_bytes(owner, "empty-reader") == before, "Empty pouch reader changed saved state or lost readable focus"): return
	view.sample_collect_button.pressed.emit()
	check(save_bytes(owner, "reader-guard") == before, "Reference modal admitted background collection")
	reader.back_button.pressed.emit()
	check(view.paused and not reader.visible and view.sample_pouch_button.has_focus(), "Pouch Back resumed or lost the invoking action")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.sample_collect_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(save_bytes(owner, "focus-guard") == before, "Unfocused collection changed the complete voyage")
	view.controls_menu.show_settings()
	view.sample_collect_button.pressed.emit()
	view.controls_menu.close_settings()
	check(save_bytes(owner, "settings-guard") == before, "Settings modal admitted background collection")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	var baseline := output.path_join("field-samples-before.json")
	if not check(peer.stage_freedom_continue(baseline) and peer.commit_pending_freedom_start(peer.get_pending_freedom_start().candidate_id) and peer.collect_freedom_field_sample(site_id), "Independent direct collection reference refused"): return
	var expected_path := output.path_join("field-samples-direct.json")
	if not check(peer.save_freedom_as(expected_path), "Direct specimen Save refused"): return
	await activate.call(view, view.sample_collect_button, false)
	var collected: Dictionary = owner.get_freedom_field_samples()
	var first := save_bytes(owner, "collected")
	if not check(valid_packet(collected, initial, site_id) and collected.carried[0].tick == initial.tick and collected.journal == collected.carried and first == FileAccess.get_file_as_bytes(expected_path), "Controls collection differs from authentic typed direct C++ result"): return
	check(owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations and view.paused and view.sample_collect_button.disabled, "Collection changed clock/actor/craft/resources/facts or left duplicate collection enabled")
	var notes: Array = collected.journal.duplicate(true)
	var first_metadata: Dictionary = saved_knowledge(first).field_samples
	check(not owner.get_freedom_field_sample_availability(site_id).get("available", true) and not owner.collect_freedom_field_sample(site_id) and save_bytes(owner, "duplicate") == first, "Duplicate specimen was admitted or changed full saved bytes")
	await activate.call(view, view.sample_pouch_button, false)
	check(reader.visible and reader.heading.text == "FIELD SAMPLES" and collected.carried[0].label in reader.body.text and collected.carried[0].source_name in reader.body.text and reader.back_button.has_focus() and save_bytes(owner, "reader") == first, "Populated pouch reader changed the voyage or omitted truthful source text")
	reader.back_button.pressed.emit()
	check(view.paused and view.sample_pouch_button.has_focus(), "Populated reader Back lost pause/focus")
	# Use the existing selected-controller GUI path for the real Discard action.
	var old_device: int = view.player_input.device
	var old_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 19
	view.player_input.available_pads[19] = true
	view.player_input.install()
	await activate.call(view, view.sample_discard_buttons[0], true)
	view.player_input.available_pads = old_pads
	view.player_input.device = old_device
	view.player_input.install()
	var discarded: Dictionary = owner.get_freedom_field_samples()
	var discarded_bytes := save_bytes(owner, "discarded")
	if not check(discarded.selected and discarded.carried.is_empty() and discarded.journal == notes and saved_knowledge(discarded_bytes).field_samples.journal == first_metadata.journal and not view.sample_collect_button.disabled and owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations, "Controller Discard lost first notes, changed physics, or failed to free the slot"): return
	var source: Dictionary = collected.carried[0]
	if not check(peer.discard_freedom_field_sample(source.system_id, source.body_kind, source.body_id, source.site_id) and peer.save_freedom_as(expected_path) and FileAccess.get_file_as_bytes(expected_path) == discarded_bytes, "Controls Discard differs from the complete direct C++ result"): return
	await activate.call(view, view.sample_collect_button, false)
	var recollected: Dictionary = owner.get_freedom_field_samples()
	var final_bytes := save_bytes(owner, "recollected")
	if not check(valid_packet(recollected, initial, site_id) and recollected.journal == notes and saved_knowledge(final_bytes).field_samples.journal == first_metadata.journal and owner.get_freedom_flight_state() == initial, "Recollection rewrote first notes or changed the actual foot/clock"): return
	if not check(peer.collect_freedom_field_sample(site_id) and peer.save_freedom_as(expected_path) and FileAccess.get_file_as_bytes(expected_path) == final_bytes, "Controls recollection differs from the complete direct C++ result"): return
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("field-samples-recollected.json"), "assets": assets}), "Carried specimen Continue refused"): return
	view = shell.current_view
	view.set_process(false)
	await tree.process_frame
	check(view.paused and view.surface_walking() and owner.get_freedom_field_samples() == recollected and owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations and save_bytes(owner, "continued") == final_bytes, "Continue changed actual specimens/first notes/actor/state or complete Save bytes")
