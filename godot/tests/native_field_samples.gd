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

func check_archive(tree: SceneTree, view: Control, owner: Variant, expected: PackedByteArray, sample: Dictionary, carried: bool, label: String) -> void:
	if not check(view.controls_menu.observations_button != null, "Companion/planet controls omit the archive"): return
	var facts: Dictionary = owner.get_freedom_observations()
	view.controls_menu.show_observations()
	var reader: Control = view.controls_menu.observations_view
	if not check(reader.visible and view.paused and reader.readings == facts, "Archive lost pause or displaced actual recorded facts"): return
	var found := false
	for index in reader.titles().size():
		if sample.label in reader.body.text and str(sample.source_name) in reader.body.text:
			found = true
			break
		reader.next_button.pressed.emit()
	await tree.process_frame
	check(found and ("Specimen carried." if carried else "Specimen not carried.") in reader.body.text and ("ship time %.1f s" % (float(sample.tick) / 120.0)) in reader.body.text, "Actual retained note lost source, carrying status or first provenance")
	check(save_bytes(owner, "archive-" + label) == expected, "Reading exploration notes changed the complete voyage")
	reader.back_button.pressed.emit()
	check(view.paused and not reader.visible and view.controls_menu.observations_button.has_focus(), "Archive Back resumed flight or lost invoking focus")

func activate_archive(tree: SceneTree, view: Control, button: Button, controller: bool) -> void:
	# Reader controls are outside the main controls-menu scroll container.
	var mode := button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await tree.process_frame
	button.grab_focus()
	if check(button.visible and not button.disabled and button.has_focus(), "Archive marker action is not focusable"):
		for down in [true, false]:
			var event: InputEvent
			if controller:
				var pad := InputEventJoypadButton.new()
				pad.device = view.player_input.device
				pad.button_index = JOY_BUTTON_A
				pad.pressed = down
				event = pad
			else:
				var key := InputEventKey.new()
				key.keycode = KEY_ENTER
				key.physical_keycode = KEY_ENTER
				key.pressed = down
				event = key
			Input.parse_input_event(event)
			Input.flush_buffered_events()
			await tree.process_frame
	button.process_mode = mode

func sample_key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func inspect_reader(tree: SceneTree, view: Control, owner: Variant, before: PackedByteArray, site_id: String) -> Control:
	# Hold the actual opening interaction key across opening/release. The fixture
	# already recorded this exposure, so repeat inspection is complete-Save neutral.
	sample_key(KEY_E, true)
	view.inspect_surface_landmark()
	var reader: Control = view.controls_menu.surface_reference_view
	reader.process_mode = Node.PROCESS_MODE_ALWAYS
	reader.collect_button.pressed.emit()
	check(reader.visible and reader.collection_context.get("site_id") == site_id and reader.back_button.has_focus() and not reader.collect_button.disabled and save_bytes(owner, "exposure-open-held") == before, "Exposure opening gesture collected or lost its pinned site/Back focus")
	sample_key(KEY_E, false)
	await tree.process_frame
	check(reader.visible and save_bytes(owner, "exposure-open-released") == before, "Opening-key release collected or closed the exposure reader")
	return reader

func exposure_guards(tree: SceneTree, view: Control, owner: Variant, reader: Control, before: PackedByteArray) -> void:
	for gate in ["focus", "journey", "save", "mode", "remap", "movement", "look", "settings", "recovery"]:
		if gate == "focus": view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
		elif gate == "journey": view.journey_dialog_open = true
		elif gate == "save": view.save_dialog.show()
		elif gate == "mode": view.mode_change_pending = true
		elif gate == "remap": view.player_input.waiting_action = "forward"
		elif gate == "movement": sample_key(KEY_W, true)
		elif gate == "look": sample_key(KEY_RIGHT, true)
		elif gate == "settings": view.controls_menu.settings_view.show()
		elif gate == "recovery": view.recovery_notified = true
		reader.collect_button.pressed.emit()
		if gate == "focus": view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
		elif gate == "journey": view.journey_dialog_open = false
		elif gate == "save": view.save_dialog.hide()
		elif gate == "mode": view.mode_change_pending = false
		elif gate == "remap": view.player_input.waiting_action = ""
		elif gate == "movement": sample_key(KEY_W, false)
		elif gate == "look": sample_key(KEY_RIGHT, false)
		elif gate == "settings": view.controls_menu.settings_view.hide()
		elif gate == "recovery": view.recovery_notified = false
		check(save_bytes(owner, "exposure-guard-" + gate) == before, "Exposure reader bypassed " + gate)
	var old_device: int = view.player_input.device
	var old_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 2
	view.player_input.available_pads[2] = true
	view.player_input.install()
	reader.collect_button.grab_focus()
	for down in [true, false]:
		var foreign := InputEventJoypadButton.new()
		foreign.device = 3
		foreign.button_index = JOY_BUTTON_A
		foreign.pressed = down
		Input.parse_input_event(foreign)
		Input.flush_buffered_events()
		await tree.process_frame
	view.player_input.device = old_device
	view.player_input.available_pads = old_pads
	view.player_input.install()
	check(save_bytes(owner, "exposure-foreign-pad") == before, "Foreign pad collected the inspected specimen")
	# A genuine neutral foot tick invalidates the pinned snapshot. Restore by
	# normal Continue after proving the stale action cannot change that new Save.
	var state: Dictionary = owner.get_freedom_flight_state()
	check(owner.advance_freedom_surface_walk(1.0/120.0, PackedFloat64Array([0, 0, float(state.surface_walk.heading)]), false), "Neutral tick for stale exposure check refused")
	var advanced: PackedByteArray = save_bytes(owner, "exposure-advanced")
	reader.collect_button.pressed.emit()
	check(reader.collect_button.disabled and "earlier state" in reader.collect_button.tooltip_text and save_bytes(owner, "exposure-stale") == advanced, "Stale inspected-owner snapshot collected after a real tick")
	check(owner.stage_freedom_continue(output.path_join("field-samples-before.json")) and owner.commit_pending_freedom_start(owner.get_pending_freedom_start().candidate_id), "Restore actual baseline after stale-reader check refused")
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	view.refresh_exposure_collection()
	reader.back_button.grab_focus()
	check(save_bytes(owner, "exposure-restored") == before and not reader.collect_button.disabled, "Restoring the exact owner snapshot changed its complete Save")

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
	if not check(reader.visible and reader.heading.text == "FIELD SAMPLES" and not reader.collect_button.visible and reader.collection_context.is_empty() and reader.back_button.has_focus() and "No specimens carried" in reader.body.text and save_bytes(owner, "empty-reader") == before, "Empty pouch reader changed saved state or lost readable focus"): return
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
	reader = await inspect_reader(tree, view, owner, before, site_id)
	await exposure_guards(tree, view, owner, reader, before)
	await activate_archive(tree, view, reader.collect_button, false)
	reader.collect_button.pressed.emit()
	var collected: Dictionary = owner.get_freedom_field_samples()
	var first := save_bytes(owner, "collected")
	if not check(valid_packet(collected, initial, site_id) and collected.carried[0].tick == initial.tick and collected.journal == collected.carried and first == FileAccess.get_file_as_bytes(expected_path), "Exposure-reader keyboard collection differs from authentic typed direct C++ result"): return
	check(owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations and view.paused and view.sample_collect_button.disabled, "Collection changed clock/actor/craft/resources/facts or left duplicate collection enabled")
	check(reader.visible and reader.collect_button.disabled and reader.back_button.has_focus(), "Collected exposure did not stay paused with duplicate disabled and Back focus")
	reader.back_button.pressed.emit()
	check(not reader.visible and reader.collection_context.is_empty() and not reader.collect_button.visible and view.landmark_inspect_button.has_focus(), "Exposure Back retained its action or lost invoking focus")
	reader.process_mode = Node.PROCESS_MODE_INHERIT
	var notes: Array = collected.journal.duplicate(true)
	var first_metadata: Dictionary = saved_knowledge(first).field_samples
	await check_archive(tree, view, owner, first, notes[0], true, "collected")
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
	await check_archive(tree, view, owner, discarded_bytes, notes[0], false, "discarded")
	if not check(discarded.selected and discarded.carried.is_empty() and discarded.journal == notes and saved_knowledge(discarded_bytes).field_samples.journal == first_metadata.journal and not view.sample_collect_button.disabled and owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations, "Controller Discard lost first notes, changed physics, or failed to free the slot"): return
	var source: Dictionary = collected.carried[0]
	if not check(peer.discard_freedom_field_sample(source.system_id, source.body_kind, source.body_id, source.site_id) and peer.save_freedom_as(expected_path) and FileAccess.get_file_as_bytes(expected_path) == discarded_bytes, "Controls Discard differs from the complete direct C++ result"): return
	reader = await inspect_reader(tree, view, owner, discarded_bytes, site_id)
	var collect_device: int = view.player_input.device
	var collect_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 2
	view.player_input.available_pads[2] = true
	view.player_input.install()
	await activate_archive(tree, view, reader.collect_button, true)
	view.player_input.device = collect_device
	view.player_input.available_pads = collect_pads
	view.player_input.install()
	reader.back_button.pressed.emit()
	reader.process_mode = Node.PROCESS_MODE_INHERIT
	var recollected: Dictionary = owner.get_freedom_field_samples()
	var final_bytes := save_bytes(owner, "recollected")
	if not check(valid_packet(recollected, initial, site_id) and recollected.journal == notes and saved_knowledge(final_bytes).field_samples.journal == first_metadata.journal and owner.get_freedom_flight_state() == initial, "Recollection rewrote first notes or changed the actual foot/clock"): return
	if not check(peer.collect_freedom_field_sample(site_id) and peer.save_freedom_as(expected_path) and FileAccess.get_file_as_bytes(expected_path) == final_bytes, "Selected-pad exposure recollection differs from the complete direct C++ result"): return
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("field-samples-recollected.json"), "assets": assets}), "Carried specimen Continue refused"): return
	view = shell.current_view
	view.set_process(false)
	await tree.process_frame
	check(view.paused and view.surface_walking() and owner.get_freedom_field_samples() == recollected and owner.get_freedom_flight_state() == initial and owner.get_freedom_observations() == observations and save_bytes(owner, "continued") == final_bytes, "Continue changed actual specimens/first notes/actor/state or complete Save bytes")
	await check_archive(tree, view, owner, final_bytes, notes[0], true, "continued")

	# Revisit from a discarded specimen's retained note. Compare the complete
	# explicit UI result against an independent direct C++ command, then Continue.
	check(owner.discard_freedom_field_sample(source.system_id, source.body_kind, source.body_id, source.site_id), "Discard before recorded-site marking refused")
	if owner.get_freedom_flight_state().surface_marker.present:
		check(owner.clear_freedom_surface_marker(), "Clear existing marker before explicit Mark refused")
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	var mark_before := save_bytes(owner, "mark-before")
	check(peer.stage_freedom_continue(output.path_join("field-samples-mark-before.json")) and peer.commit_pending_freedom_start(peer.get_pending_freedom_start().candidate_id), "Independent marker reference Continue refused")
	view.controls_menu.show_observations()
	var archive: Control = view.controls_menu.observations_view
	for index in archive.titles().size():
		if archive.mark_button.visible: break
		archive.next_button.pressed.emit()
	if not check(archive.mark_button.visible and not archive.mark_button.disabled and archive.mark_button.text == "Mark site" and "Specimen not carried." in archive.body.text, "Discarded actual note did not offer explicit Mark site"): return
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	archive.mark_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.journey_dialog_open = true
	archive.mark_button.pressed.emit()
	view.journey_dialog_open = false
	check(save_bytes(owner, "mark-guard") == mark_before, "Unfocused/covered archive changed complete marker state")
	check(peer.mark_freedom_field_sample_site(source.system_id, source.body_kind, source.body_id, source.site_id, false) and peer.save_freedom_as(expected_path), "Direct retained-site Mark refused")
	await activate_archive(tree, view, archive.mark_button, false)
	var marked := save_bytes(owner, "marked")
	var marker: Dictionary = owner.get_freedom_flight_state().surface_marker
	if not check(marked == FileAccess.get_file_as_bytes(expected_path) and marker.present and marker.current_body and marker.distance_metres <= 8.0 and archive.mark_button.text == "Replace marker" and owner.get_freedom_field_samples().journal == notes and owner.get_freedom_field_samples().carried.is_empty() and owner.get_freedom_observations() == observations and view.paused, "Actual retained-site Mark differs from C++, loses bearing/notes, or changes other state"): return
	var marker_device: int = view.player_input.device
	var marker_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 19
	view.player_input.available_pads[19] = true
	view.player_input.install()
	await activate_archive(tree, view, archive.mark_button, true)
	view.player_input.available_pads = marker_pads
	view.player_input.device = marker_device
	view.player_input.install()
	check(save_bytes(owner, "mark-replaced") == marked, "Controller same-site Replace changed time, position or first notes")
	archive.back_button.pressed.emit()
	check(view.paused and view.controls_menu.observations_button.has_focus(), "Marker archive Back resumed or lost focus")
	# Restore a carried specimen through the ordinary collection command so the
	# continuing Moon return route still exercises its existing pouch contract.
	check(owner.collect_freedom_field_sample(site_id), "Recollection after marking refused")
	var marked_carried := save_bytes(owner, "marked-carried")
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("field-samples-marked-carried.json"), "assets": assets}), "Marked retained site Continue refused"): return
	view = shell.current_view
	view.set_process(false)
	await tree.process_frame
	check(view.paused and owner.get_freedom_field_samples() == recollected and owner.get_freedom_flight_state().surface_marker == marker and save_bytes(owner, "marked-continued") == marked_carried, "Marked site Continue lost marker, notes or complete bytes")
