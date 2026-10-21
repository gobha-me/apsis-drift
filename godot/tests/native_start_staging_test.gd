extends SceneTree
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const HomeNavigation = preload("res://scripts/native/home_navigation.gd")
const Shell = preload("res://scripts/native/native_start_shell.gd")
class LateDamagedShell extends Shell:
	var damage := ""
	var staged_reference: WeakRef
	func stage_view(owner: Variant, assets: String, pending: Dictionary) -> Control:
		var candidate := super.stage_view(owner, assets, pending)
		if candidate != null and not damage.is_empty():
			staged_reference = weakref(candidate)
			if damage == "last_root":
				candidate.staged_model.replacement_nodes[13].transform = Transform3D.IDENTITY
			else:
				candidate.staged_model.atlas_material.roughness = 0.123456
		return candidate
var failures := 0
func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1
func check_sunlight(view: Control) -> void:
	var projection: Dictionary = view.bridge.get_freedom_flight_state()
	var visibility := NativeEnvironment.sun_visibility(projection)
	check(visibility >= 0.0, "Actual view lost canonical shadow observation")
	if view is FlightView:
		check(view.light != view.local_light and view.light.light_cull_mask == 1 and view.local_light.light_cull_mask == 2, "Terrain/local sunlight layers overlap")
		check(view.light.light_energy == 1.0 and is_equal_approx(view.local_light.light_energy, visibility) and view.local_light.basis.is_equal_approx(view.light.basis) and view.local_light.light_color == projection.lighting.star_color, "Flight sunlight disagrees with current C++ projection")
	elif view is WalkView:
		check(is_equal_approx(view.light.light_energy, 1.5 * visibility) and view.light.light_color == projection.lighting.star_color, "Station sunlight disagrees with current C++ projection")
func _initialize() -> void:
	call_deferred("run")

func saved_knowledge_entries(path: String) -> Array:
	# Keep uint64 provenance exact: Godot's JSON number path uses doubles.
	# Quote only these integer-valued schema fields in the test's decoded copy.
	var text := FileAccess.get_file_as_string(path)
	var numbers := RegEx.new()
	for field in ["system", "identity", "source_id", "flight", "tick"]:
		numbers.compile('(\"' + field + '\"\\s*:\\s*)([0-9]+)(?=\\s*[,}\\]])')
		text = numbers.sub(text, '$1"$2"', true)
	var value: Variant = JSON.parse_string(text)
	for level in 16:
		if not value is Dictionary: break
		if value.get("knowledge") is Dictionary:
			return value.knowledge.get("entries", [])
		value = value.get("voyage", value.get("base"))
	check(false, "Recorded-observation oracle could not find the actual saved ledger")
	return []

func check_recorded_observations(bridge: Variant, path: String, fresh: bool, require_system_arrival := true) -> void:
	var before: Dictionary = bridge.get_freedom_flight_state()
	var actor: Dictionary = bridge.get_freedom_walk_state()
	if not bridge.save_freedom_as(path):
		check(false, "Recorded-observation source Save refused")
		return
	var source := FileAccess.get_file_as_bytes(path)
	var entries := saved_knowledge_entries(path)
	check(not bridge.advance_freedom_flight(-1.0, PackedFloat64Array(), false), "Invalid observation-diagnostic fixture advanced")
	var diagnostic: String = bridge.get_last_error()
	var readings: Dictionary = bridge.get_freedom_observations()
	check(not diagnostic.is_empty() and bridge.get_freedom_observations() == readings and bridge.get_last_error() == diagnostic, "Repeated lazy observation reads sampled or cleared the command diagnostic")
	if not readings.get("selected", false) or not readings.get("subjects") is Array:
		check(false, "Selected recorded observations unavailable")
		return
	check(readings.tick == before.tick, "Recorded observations used another authoritative clock")
	var kinds := ["system", "planet", "station"]
	var facts := ["identity_type", "location", "physical", "atmosphere", "hazards", "landing", "station_ports", "presence"]
	var confidence := ["contact", "probable", "resolved", "visited"]
	var sources := ["starting_chart", "local_ship", "physical_arrival"]
	var systems := {}
	for subject in readings.subjects:
		if subject.kind != "system": continue
		for row in before.chart.rows:
			if subject.name == row.name:
				check(subject.system_id == row.system_id, "Recorded system ownership differs from its granted chart")
				systems[subject.subject_id] = row.system_id
	var expected := {}
	var observation_transitions := 0
	var expected_subjects := {}
	for entry in entries:
		for transition in entry.transitions:
			if int(transition.source) != 0: observation_transitions += 1
		var system: String = systems.get(entry.system, "")
		check(not system.is_empty(), "Saved fact lost its known system owner")
		var key: String = kinds[int(entry.kind)] + "|" + system + "|" + entry.identity
		expected_subjects[key] = true
		expected[key + "|" + facts[int(entry.fact)]] = entry.transitions[-1]
	check(typeof(before.chart.get("observation_transitions")) == TYPE_INT and before.chart.observation_transitions == observation_transitions, "HUD counter differs from the complete saved non-starting transition history")
	var seen := {}
	var subject_keys := {}
	var local_facts := 0
	var arrival_facts := 0
	var redacted := 0
	var current_physical := false
	var current_presence := false
	for subject in readings.subjects:
		var key: String = subject.kind + "|" + subject.system_id + "|" + subject.subject_id
		check(expected_subjects.has(key) and not subject_keys.has(key), "Observation getter listed an unrecorded or duplicate subject")
		subject_keys[key] = true
		for fact in subject.facts:
			var fact_key: String = key + "|" + fact.fact
			if not expected.has(fact_key):
				check(false, "Observation getter exposed a fact absent from the saved ledger")
				continue
			var transition: Dictionary = expected[fact_key]
			check(not seen.has(fact_key) and fact.confidence == confidence[int(transition.level)] and fact.source == sources[int(transition.source)] and fact.source_id == transition.source_id and fact.tick == transition.tick and fact.craft_id == transition.flight, "Recorded fact lost exact confidence/provenance/owner")
			seen[fact_key] = true
			if fresh:
				check(fact.source == "starting_chart" and fact.confidence == "resolved" and fact.tick == "0" and fact.source_id == "1" and fact.craft_id == "0" and fact.fact in ["identity_type", "location", "station_ports"], "Fresh grants manufactured a local finding or visit")
			if fact.source == "local_ship": local_facts += 1
			if fact.source == "physical_arrival": arrival_facts += 1
			if fact.confidence in ["contact", "probable"]:
				check(fact.value is Dictionary and fact.value.is_empty(), "Unresolved fact leaked generated values")
				if fact.fact == "identity_type": check(subject.name == "Unresolved " + subject.kind, "Unresolved identity leaked a generated name")
				redacted += 1
			elif fact.fact == "identity_type":
				check(fact.value == {"name": subject.name}, "Resolved identity differs from its typed reading")
			elif fact.fact == "station_ports":
				check(subject.subject_id == before.station_id and fact.value == {"count": bridge.get_freedom_station_geometry().ports.size()}, "Granted port count differs from the owned station")
			elif fact.fact == "presence":
				check(fact.source == "physical_arrival" and fact.source_id == before.craft_id and fact.craft_id == before.craft_id and fact.value == {"arrival_tick": fact.tick}, "Physical arrival lost its exact craft or event tick")
				if subject.kind == "system" and subject.subject_id == before.system_id: current_presence = true
			elif fact.fact == "physical" and subject.kind == "planet" and subject.subject_id == before.planet_id:
				check(fact.source == "local_ship" and fact.source_id == before.craft_id and fact.craft_id == before.craft_id and fact.value.radius_kilometres * 1000.0 == before.planet_radius and fact.value.gravity_milli_g > 0, "Resolved current-planet physical reading differs from its owner")
				current_physical = true
	check(seen.size() == expected.size() and subject_keys.size() == expected_subjects.size(), "Recorded getter omitted saved subjects or facts")
	if not fresh:
		check(local_facts > 0 and arrival_facts > 0 and redacted > 0 and current_physical, "Active ship sensors did not exercise resolved, redacted and visited recorded facts")
		if require_system_arrival: check(current_presence, "Actual departure did not record the current system visit")
	check(bridge.get_freedom_flight_state() == before and bridge.get_freedom_walk_state() == actor, "Reading recorded observations changed the actual actor/world")
	check(bridge.save_freedom_as(path + ".after.json") and FileAccess.get_file_as_bytes(path + ".after.json") == source and FileAccess.get_file_as_bytes(path) == source, "Lazy observations changed complete saved knowledge or its source")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		check(false, "Native C++ bridge unavailable")
		quit(1)
		return
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	var unselected: Dictionary = bridge.get_freedom_observations()
	check(not unselected.get("selected", true) and unselected.get("subjects", [1]).is_empty(), "Uninitialized bridge invented recorded subjects")
	var shell := LateDamagedShell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	check(shell.select_start(bridge, {"mode": "continue", "value": args[1], "assets": args[0]}), "Selected journey could not stage through the ready-view seam")
	var old: Dictionary = bridge.get_freedom_walk_state()
	var old_flight: Dictionary = bridge.get_freedom_flight_state()
	var options := {"mode": "new_game", "value": "42", "assets": args[0] + "-missing"}
	check(not shell.select_start(bridge, options), "Missing assets committed pending starter")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight and bridge.get_pending_freedom_start().is_empty(), "Failed model staging changed active session")
	check(not shell.select_start(bridge, {"mode": "continue", "value": args[2], "assets": args[0]}), "Corrupt save staged")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight, "Save refusal changed active session")
	check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "assets": args[0], "validate_only": true}), "Source-only validation refused")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight, "Source-only validation committed")
	check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "assets": args[0]}), "Ready model/session transaction refused: " + shell.error)
	check(shell.current_view != null and shell.current_view.activated and shell.current_view.staged_model.valid_installed(), "Committed view was not complete")
	check_sunlight(shell.current_view)
	var initial_resources: Dictionary = bridge.get_freedom_flight_state().resources
	check(initial_resources.selected and initial_resources.quantity_quanta == "3032640000000000" and initial_resources.jump_charges == 3, "Staged New Game lost finite C++ resources")
	var initial_chart: Dictionary = bridge.get_freedom_flight_state().chart
	check(initial_chart.local_sensors and initial_chart.observed_facts == 0 and initial_chart.observation_transitions == 0, "New Game silently invented ship observations")
	check(initial_chart.selected and initial_chart.rows.size() == 2 and initial_chart.rows[0].current and initial_chart.rows[0].confidence == "resolved" and initial_chart.rows[1].confidence == "resolved" and initial_chart.rows[1].affordable, "Starting chart leaked extra rows, invented visits or lost granted route")
	check_recorded_observations(bridge, args[1].get_base_dir().path_join("recorded-starting-grants.json"), true)
	var current: Dictionary = bridge.get_freedom_walk_state()
	check(current.universe_seed == "42" and current.tick == "0" and bridge.get_pending_freedom_start().is_empty(), "Presentation changed new actor clock or left pending")
	var view: Control = shell.current_view
	check(not shell.select_start(bridge, options), "Second missing-assets stage accepted")
	check(shell.current_view == view and bridge.get_freedom_walk_state() == current, "Refused replacement lost previous complete model/session")
	for damage in ["last_root", "atlas"]:
		shell.damage = damage
		check(not shell.select_start(bridge, {"mode": "new_game", "value": "43", "assets": args[0]}), "Late staged " + damage + " committed")
		check(shell.current_view == view and bridge.get_freedom_walk_state() == current and bridge.get_pending_freedom_start().is_empty(), "Late refusal changed current view/session or retained pending")
		check(shell.staged_reference.get_ref() == null, "Refused detached candidate leaked")
	shell.damage = ""
	await check_boarding(shell, bridge)
	await check_load(shell, bridge, args)
	check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "network": true, "assets": args[0]}), "Four-system staged New Game refused: " + shell.error)
	var network_state: Dictionary = bridge.get_freedom_flight_state()
	check(shell.current_view is WalkView and network_state.chart.rows.size() == 4 and network_state.resources.jump_charges == 3, "Selected network lost actual station or finite drive")
	for row in network_state.chart.rows:
		check(row.confidence == "resolved" and row.known and row.valid, "Network chart invented visits or lost anchor grants")
	check(network_state.chart.observed_facts == 0 and network_state.chart.observation_transitions == 0, "Network initialization invented sensor observations")
	var network_save: String = args[1].get_base_dir().path_join("network-start.json")
	check(bridge.save_freedom_as(network_save), "Network start Save As refused")
	var network_hash := FileAccess.get_sha256(network_save)
	check(shell.select_start(bridge, {"mode": "continue", "value": network_save, "assets": args[0]}), "Network station Continue refused: " + shell.error)
	check(shell.current_view is WalkView and shell.current_view.paused and bridge.get_freedom_flight_state().chart == network_state.chart, "Network Continue lost paused actual station chart")
	check(bridge.save_freedom_as(network_save) and FileAccess.get_sha256(network_save) == network_hash, "Network Continue changed complete saved owner bytes")
	# Use actual station walking and completed boarding before admitting the
	# flight scene; only chart selection is under test here, with no jump bill.
	# Batch presentation calls at 30 Hz, retaining every authoritative 120 Hz tick.
	for frame in 298:
		check(bridge.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, -1.0, 0.0])), "Network station walking refused")
	check(bridge.advance_freedom_walk(1.0 / 60.0, PackedFloat64Array([0.0, -1.0, 0.0])), "Network final station walking ticks refused")
	check(bridge.begin_freedom_boarding(), "Network actual boarding refused")
	for frame in 360:
		check(bridge.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])), "Network boarding tick refused")
	check(bridge.get_freedom_boarding_state().get("seated", false), "Network pilot did not reach the seat")
	var boarded_save: String = args[1].get_base_dir().path_join("network-boarded.json")
	check(bridge.save_freedom_as(boarded_save), "Network seated save refused")
	check(shell.select_start(bridge, {"mode": "continue", "value": boarded_save, "assets": args[0]}), "Network seated scene staging refused: " + shell.error)
	if shell.current_view is FlightView:
		var network_flight: Control = shell.current_view
		network_flight.set_process(false)
		network_flight.controls_menu.hide_menu()
		network_flight.chart_button.button_pressed = true
		network_flight._process(0.0)
		check(network_flight.jump_destinations.size() == 4, "Flight scene lost network chart actions")
		var before_choice: Dictionary = bridge.get_freedom_flight_state()
		var before_facts: Dictionary = bridge.get_freedom_observations()
		for ordinal in [1, 2, 3]:
			check(not network_flight.jump_destinations[ordinal].disabled, "Known network destination action is disabled")
			network_flight.jump_destinations[ordinal].pressed.emit()
			var chosen: Dictionary = bridge.get_freedom_flight_state()
			check(chosen.jump.selected == chosen.chart.rows[ordinal].system_id and chosen.jump.phase == "idle", "Actual network chart button selected the wrong destination")
			check(chosen.tick == before_choice.tick and chosen.resources == before_choice.resources and bridge.get_freedom_observations() == before_facts, "Chart choice changed clock, resources or knowledge")
	else:
		check(false, "A seated network Continue did not create the actual flight view")
	for chart in 3:
		check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "network": chart > 0, "planetary": chart == 2, "drive_recovery": true, "assets": args[0]}), "Independent drive/chart staging refused: " + shell.error)
		var selected_drive: Dictionary = bridge.get_freedom_flight_state()
		check(shell.current_view is WalkView and selected_drive.tick == "0" and selected_drive.chart.rows.size() == (2 if chart == 0 else 4) and selected_drive.jump.recovery.ready and selected_drive.jump.recovery.remaining_seconds == 0.0, "Drive choice changed chart selection or failed ready station start")
		var drive_save: String = args[1].get_base_dir().path_join("recovery-start-%d.json" % chart)
		check(bridge.save_freedom_as(drive_save), "Selected drive station Save refused")
		var drive_hash := FileAccess.get_sha256(drive_save)
		check(shell.select_start(bridge, {"mode": "continue", "value": drive_save, "assets": args[0]}) and shell.current_view.paused, "Selected drive Continue failed actual station staging")
		check(bridge.save_freedom_as(drive_save) and FileAccess.get_sha256(drive_save) == drive_hash, "Selected drive Continue changed complete station Save")
	shell.free()
	print("Native start staging checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)


func joy_axis(axis: int, value: float, device: int) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = device
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func physical_key(code: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func boarding_reference(bridge: Variant, label: String) -> Variant:
	var path := ProjectSettings.globalize_path("user://" + label + "-look-source.json")
	if not bridge.save_freedom_as(path):
		check(false, label + ": look reference Save refused")
		return null
	var reference: Variant = ClassDB.instantiate("FreedomBridge")
	if not reference.stage_freedom_continue(path) or not reference.commit_pending_freedom_start(reference.get_pending_freedom_start().candidate_id):
		check(false, label + ": look reference Continue refused")
		return null
	return reference


func check_boarding_look(walk: Control, bridge: Variant, label: String) -> void:
	var actor: Dictionary = bridge.get_freedom_walk_state()
	var flight: Dictionary = bridge.get_freedom_flight_state()
	var heading: float = walk.requested_heading
	walk.sync_mouse_look()
	check(walk.boarding_transition() and walk.mouse_look.active, label + ": transition disabled ordinary mouse look")
	var motion := InputEventMouseMotion.new()
	motion.relative = Vector2(40, -20)
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	check(walk.boarding_look.x > 0.0 and walk.boarding_look.y < 0.0, label + ": mouse did not look around")
	var accepted: Vector2 = walk.boarding_look
	motion.relative = Vector2(NAN, INF)
	walk._input(motion)
	check(walk.boarding_look == accepted, label + ": invalid mouse motion contaminated the transition")
	# Exercise each input source away from the previous source's camera limit.
	walk.boarding_look = Vector2.ZERO
	accepted = walk.boarding_look
	var key: int = walk.preferences_provider.bindings.look_right.key.code
	physical_key(KEY_W, true)
	physical_key(key, true)
	var commands: PackedFloat64Array = walk.input_controls(0.1)
	physical_key(key, false)
	physical_key(KEY_W, false)
	check(walk.boarding_look.x > accepted.x, label + ": key look failed; before=%s after=%s key=%s paused=%s" % [accepted, walk.boarding_look, key, walk.paused])
	check(commands == PackedFloat64Array([0.0, 0.0, heading]), label + ": key look steered the route; commands=%s heading=%s" % [commands, heading])
	walk.boarding_look = Vector2.ZERO
	accepted = walk.boarding_look
	joy_axis(JOY_AXIS_RIGHT_X, 0.8, 9)
	walk.input_controls(0.1)
	check(walk.boarding_look == accepted, label + ": foreign stick moved the transition camera")
	joy_axis(JOY_AXIS_RIGHT_X, 0.0, 9)
	joy_axis(JOY_AXIS_RIGHT_X, 0.8, 19)
	joy_axis(JOY_AXIS_LEFT_Y, 0.8, 19)
	commands = walk.input_controls(0.1)
	joy_axis(JOY_AXIS_RIGHT_X, 0.0, 19)
	joy_axis(JOY_AXIS_LEFT_Y, 0.0, 19)
	check(walk.boarding_look.x > accepted.x, label + ": selected stick look failed; before=%s after=%s paused=%s" % [accepted, walk.boarding_look, walk.paused])
	check(commands == PackedFloat64Array([0.0, 0.0, heading]), label + ": stick look steered the route; commands=%s heading=%s" % [commands, heading])
	walk.update_view()
	check(walk.camera.position == actor.actor_eye_position and walk.requested_heading == heading and walk.pitch == 0.0, label + ": look displaced the authoritative eye/heading")
	accepted = walk.boarding_look
	for gate in ["pause", "focus", "modal"]:
		if gate == "focus": walk._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
		else: walk.pause_controls("Boarding look gate")
		if gate == "modal": walk.journey_dialog_open = true
		check(not walk.mouse_look.active, label + ": " + gate + " retained pointer capture")
		motion.relative = Vector2(20, 20)
		walk._input(motion)
		walk.input_controls(0.1)
		check(walk.boarding_look == accepted, label + ": " + gate + " allowed look")
		walk.journey_dialog_open = false
		if gate == "focus": walk._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
		walk.resume_requested()
		check(not walk.paused and walk.mouse_look.active, label + ": neutral Resume lost transition look")
	check(bridge.get_freedom_walk_state() == actor and bridge.get_freedom_flight_state() == flight, label + ": camera input changed C++ state/clock")
	for progress in [NAN, INF, -0.01, 1.01]:
		check(WalkView.boarding_look_weight(progress) == 0.0, label + ": invalid camera progress accepted")
	check(WalkView.boarding_look_weight(0.5) == 1.0 and is_equal_approx(WalkView.boarding_look_weight(23.0 / 24.0), 0.5) and WalkView.boarding_look_weight(1.0) == 0.0, label + ": camera did not settle at the route end")


func check_boarding_save(bridge: Variant, reference: Variant, label: String) -> void:
	if reference == null: return
	var actual := ProjectSettings.globalize_path("user://" + label + "-look-actual.json")
	var expected := ProjectSettings.globalize_path("user://" + label + "-look-reference.json")
	check(bridge.save_freedom_as(actual) and reference.save_freedom_as(expected) and FileAccess.get_file_as_bytes(actual) == FileAccess.get_file_as_bytes(expected), label + ": looking changed the complete route Save")


func check_distant_tracking(flight: Control, bridge: Variant, path: String) -> void:
	var before: Dictionary = bridge.get_freedom_flight_state()
	var contacts: Array = before.get("distant_contacts", {}).get("contacts", [])
	check(flight.paused and flight.controls_menu.panel.visible and contacts.size() == 2 and flight.contact_buttons.size() == 2, "Actual seated Controls lacks two selected optical contact actions")
	if contacts.size() != 2 or flight.contact_buttons.size() != 2: return
	check(bridge.save_freedom_as(path), "Optical tracking baseline Save refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	var home_before: Dictionary = flight.home_marker.cue.duplicate(true)
	var home_label: String = flight.home_marker.label_text
	var home_visible: bool = flight.home_marker.visible
	var ship_before: Dictionary = flight.ship_marker.cue.duplicate(true)
	var ship_label: String = flight.ship_marker.label_text
	var ship_visible: bool = flight.ship_marker.visible
	var personal_before: bool = flight.show_personal_marker
	for frame in 4: await process_frame
	for index in 2:
		var button: Button = flight.contact_buttons[index]
		button.grab_focus()
		flight.controls_menu.left_scroll.ensure_control_visible(button)
		check(button.is_visible_in_tree() and not button.disabled and button.focus_mode == Control.FOCUS_ALL and button.has_focus(), "Actual optical action cannot receive keyboard/controller focus")
		for pressed in [true, false]:
			var event: InputEvent
			if index == 0:
				var key := InputEventKey.new()
				key.keycode = KEY_ENTER
				key.physical_keycode = KEY_ENTER
				key.pressed = pressed
				event = key
			else:
				var pad := InputEventJoypadButton.new()
				pad.device = flight.player_input.device
				pad.button_index = JOY_BUTTON_A
				pad.pressed = pressed
				event = pad
			Input.parse_input_event(event)
			Input.flush_buffered_events()
			await process_frame
		check(flight.paused and flight.selected_contact_id == contacts[index].id and flight.controls_menu.surface_reference_view.visible and contacts[index].reason in flight.controls_menu.surface_reference_view.body.text, "Actual keyboard/controller optical action lost selection, explanation or pause")
		flight.controls_menu.surface_reference_view.back_button.pressed.emit()
		flight.update_view(0.0)
		check(not flight.controls_menu.surface_reference_view.visible and button.has_focus() and flight.selected_contact_id == contacts[index].id and contacts[index].status.replace("_", " ") in flight.primary_status.text and contacts[index].reason in flight.primary_status.text, "Optical Back/refresh lost focused selection or actual compact availability")
		if contacts[index].status in ["visible", "unresolved"]:
			var expected := HomeNavigation.project(flight.camera, flight.camera.global_position + contacts[index].direction * 1000.0, flight.contact_marker.size, HomeNavigation.INSET * flight.contact_marker.render_scale)
			check(flight.contact_marker.cue == expected, "CONTACT bearing diverged from the actual C++ optical direction")
		else:
			check(not flight.contact_marker.visible, "Occluded or close-unavailable actual contact retained a bearing annotation")
		check(flight.home_marker.cue == home_before and flight.home_marker.label_text == home_label and flight.home_marker.visible == home_visible and flight.ship_marker.cue == ship_before and flight.ship_marker.label_text == ship_label and flight.ship_marker.visible == ship_visible and flight.show_personal_marker == personal_before, "Optical selection replaced HOME/RETURN/MARK ownership")
		check(bridge.get_freedom_flight_state() == before, "Optical GUI action changed the actual C++ clock/checksum or observations")
	flight.contact_clear_button.grab_focus()
	flight.contact_clear_button.pressed.emit()
	check(flight.selected_contact_id.is_empty() and not flight.contact_marker.visible and not "CONTACT ·" in flight.primary_status.text and flight.paused, "Paused clear tracking left an optical annotation or resumed flight")
	check(bridge.get_freedom_flight_state() == before and bridge.save_freedom_as(path + ".after.json") and FileAccess.get_file_as_bytes(path + ".after.json") == bytes, "Actual keyboard/controller tracking changed complete C++ save bytes")


func check_boarding(shell: Control, bridge: Variant) -> void:
	var walk: Control = shell.current_view
	walk.set_process(false)
	# Software-selected second pad; physical hardware remains a separate check.
	walk.available_pads = {9: true, 19: true}
	walk.selected_pad = 19
	check(walk.light.basis.z.is_equal_approx(bridge.get_freedom_flight_state().star_direction), "Staged station light diverged from authoritative star")
	var original: Dictionary = bridge.get_freedom_walk_state()
	var availability: Dictionary = bridge.get_freedom_boarding_availability()
	check(WalkView.valid_boarding_availability(availability, original) and not availability.available and not walk.board_button.visible, "Far station actor received a boarding action")
	check(not bridge.begin_freedom_boarding() and bridge.get_freedom_walk_state() == original, "Distant boarding changed the walker")
	for tick in 2960:
		if not bridge.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, -1.0, 0.0])):
			check(false, "Supported approach to D1 refused")
			return
	walk.boarding_query_elapsed = 0.1
	walk.update_view()
	var entry: Dictionary = bridge.get_freedom_walk_state()
	check(walk.board_button.visible and not walk.board_button.disabled and walk.boarding_prompt.visible and "Climb aboard" in walk.boarding_prompt.text, "Nearby ladder lost its contextual action")
	var model: Node3D = walk.staged_model
	var roots: Array = model.replacement_nodes.duplicate()
	var board_key := InputEventKey.new()
	board_key.keycode = KEY_E
	board_key.physical_keycode = KEY_E
	board_key.pressed = true
	Input.parse_input_event(board_key)
	Input.flush_buffered_events()
	board_key = board_key.duplicate()
	board_key.pressed = false
	Input.parse_input_event(board_key)
	Input.flush_buffered_events()
	var malformed: Dictionary = bridge.get_freedom_boarding_state().pose.duplicate(true)
	malformed.craft_world_deltas.seat_lift[0] = NAN
	check(not model.set_pose(malformed) and model.valid_current_pose(), "Nonfinite runtime pose changed the current installed model")
	check(bridge.get_freedom_boarding_state().get("state") == "boarding", "Nearby Board action did not begin")
	check(not bridge.release_freedom_port(), "Boarding actor gained flight release authority")
	var boarding: Dictionary = bridge.get_freedom_boarding_state()
	var paused_tick: String = bridge.get_freedom_flight_state().tick
	walk._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	walk._process(1.0)
	check(bridge.get_freedom_boarding_state() == boarding and bridge.get_freedom_flight_state().tick == paused_tick, "Focus loss advanced boarding")
	walk._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	walk.resume_requested()
	var look_reference: Variant = boarding_reference(bridge, "boarding")
	check_boarding_look(walk, bridge, "boarding")
	# Present at 30 Hz while C++ still consumes every authoritative 120-Hz tick.
	for frame in 360:
		if look_reference != null: check(look_reference.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])), "No-look boarding reference refused")
		if not walk.advance_requested(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])):
			check(false, "Boarding advance refused")
			return
		if frame == 120:
			check(not walk.camera.basis.is_equal_approx(walk.state.station_basis * Basis(Vector3.UP, walk.state.heading_radians)) and walk.camera.position == walk.state.actor_eye_position, "Boarding look was reset or displaced the eye")
		if frame in [120, 330, 345, 359]: check_boarding_save(bridge, look_reference, "boarding")
	check(walk.boarding_look == Vector2.ZERO, "Seating retained a stale transition look")
	var seat_camera: Transform3D = walk.camera.transform
	var seat_light: Basis = walk.light.basis
	check(bridge.save_freedom_as(ProjectSettings.globalize_path("user://controller-before.json")), "Controller pre-board export failed")
	var before_board: PackedByteArray = FileAccess.get_file_as_bytes("user://controller-before.json")
	walk.frame_diagnostics.set_enabled(true)
	shell.switch_journey_view()
	var flight: Control = shell.current_view
	check(flight is FlightView and flight.staged_model == model and flight.cockpit, "Seat did not hand the same model to cockpit flight")
	if not flight is FlightView: return
	check(flight.frame_diagnostics.enabled and flight.frame_diagnostics.samples.is_empty(), "Boarding lost diagnostic choice or retained a stale frame sample epoch")
	check_sunlight(flight)
	flight.set_process(false)
	check(flight.player_input.device == 19, "Boarding replaced the station-selected controller")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.8, 19)
	flight.toggle_pause()
	check(flight.paused and not flight.controls_armed, "Held selected trigger bypassed the boarding neutral gate")
	joy_axis(JOY_AXIS_TRIGGER_RIGHT, 0.0, 19)
	for action in flight.player_input.ACTIONS:
		for event in InputMap.action_get_events("pilot_" + action):
			if event is InputEventJoypadButton or event is InputEventJoypadMotion:
				check(event.device == 19, "Boarding installed another controller in the flight action map")
	check(bridge.save_freedom_as(ProjectSettings.globalize_path("user://controller-after.json")) and FileAccess.get_file_as_bytes("user://controller-after.json") == before_board, "Controller boarding handoff changed exact world bytes")
	check(flight.light.basis.is_equal_approx(seat_light) and flight.light.basis.z.is_equal_approx(flight.state.star_direction), "Boarding changed stellar direction at the view handoff")
	check(flight.camera.transform.origin.distance_to(seat_camera.origin) < 0.002 and flight.camera.transform.basis.is_equal_approx(seat_camera.basis), "Seated camera jumped at view handoff")
	check(model.valid_current_pose() and model.replacement_nodes == roots and model.get_child_count() == 1, "Boarding replaced or reparented the hardware roster")
	check(flight.state.resources.quantity_quanta == "3032640000000000" and "90.0 min at full main" in flight.resource_status.text and flight.service_button.focus_mode == Control.FOCUS_ALL and flight.service_button.visible, "Walking/boarding burned fuel or cockpit lost accessible resource service")
	check(flight.chart_button.visible and flight.chart_button.focus_mode == Control.FOCUS_ALL and not flight.chart_status.visible and "Starting chart" in flight.chart_status.text and "Nearby system" in flight.chart_status.text, "Cockpit lost accessible, collapsed C++ chart readings")
	var chart_state_before: Dictionary = bridge.get_freedom_flight_state()
	flight.chart_button.button_pressed = true
	check(flight.chart_status.visible and bridge.get_freedom_flight_state() == chart_state_before, "Reading chart granted evidence or changed fuel/flight/time")
	flight.chart_button.button_pressed = false
	check(not flight.unboard_button.disabled and flight.unboard_button.visible and flight.unboard_menu_button.visible and flight.unboard_menu_button.focus_mode == Control.FOCUS_ALL and not flight.release_button.disabled and flight.exhaust.valid_bound_skin(), "Seated UI lost unboard/release/exhaust")
	var save_path: String = OS.get_cmdline_user_args()[1].get_base_dir().path_join("playable-seated.json")
	check(bridge.save_freedom_as(save_path), "Seated Save As refused")
	await check_distant_tracking(flight, bridge, save_path + ".contacts.json")
	var continued_owner: Variant = ClassDB.instantiate("FreedomBridge")
	var continued_shell := Shell.new()
	continued_shell.presentation_only = true
	root.add_child(continued_shell)
	check(continued_shell.select_start(continued_owner, {"mode": "continue", "value": save_path, "assets": shell.assets_root}), "Seated Continue could not stage: " + continued_shell.error)
	if continued_shell.current_view != null:
		check(continued_shell.current_view is FlightView and continued_shell.current_view.cockpit and continued_shell.current_view.paused and continued_shell.current_view.staged_model.valid_current_pose(), "Seated Continue lost pose, cockpit or neutral pause")
	check(continued_owner.get_freedom_flight_state().resources == bridge.get_freedom_flight_state().resources, "Staged Continue changed exact resource readings")
	check(continued_owner.get_freedom_flight_state().chart == bridge.get_freedom_flight_state().chart, "Continue changed granted chart knowledge or invented visits")
	check(continued_owner.get_freedom_observations() == bridge.get_freedom_observations(), "Seated Continue changed recorded facts or provenance")
	# Exercise the real active-frame hook on an independent seated continuation.
	# The main departure/return trace below retains its existing clock and inputs.
	var cue_view: Control = continued_shell.current_view
	if cue_view is FlightView:
		cue_view.set_process(false)
		check(cue_view.observation_cue.text().is_empty(), "Continue replayed existing findings")
		cue_view.toggle_pause()
		check(not cue_view.paused, "Neutral observation-cue fixture could not resume")
		# Stay within the existing per-frame catch-up cap to reach a sensor tick.
		for frame in 10: cue_view._process(0.1)
		check(cue_view.error.is_empty() and cue_view.state.chart.observation_transitions > 0 and cue_view.observation_cue.text() == "Ship observations updated\nControls → Recorded observations" and cue_view.observation_cue.text() in cue_view.primary_status.text, "Successful active sensor ticks did not reach the actual compact HUD")
		var cue_text: String = cue_view.observation_cue.text()
		var cue_save := save_path + ".cue.json"
		check(continued_owner.save_freedom_as(cue_save), "Observation cue snapshot refused")
		var cue_bytes := FileAccess.get_file_as_bytes(cue_save)
		check_recorded_observations(continued_owner, cue_save + ".records.json", false, false)
		cue_view.pause_controls("Command result has priority", false)
		cue_view._process(30.0)
		check(cue_view.observation_cue.text() == cue_text and cue_view.primary_status.text.split("\n")[1] == "Command result has priority", "Pause aged the cue or hid a command notice")
		cue_view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
		cue_view._process(30.0)
		check(cue_view.observation_cue.text() == cue_text, "Focus loss aged the observation cue")
		cue_view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
		check(continued_owner.save_freedom_as(cue_save + ".after.json") and FileAccess.get_file_as_bytes(cue_save + ".after.json") == cue_bytes, "Cue review/pause/focus changed complete saved state")
		check(continued_shell.select_start(continued_owner, {"mode": "continue", "value": cue_save, "assets": shell.assets_root}), "Observation-cue Continue refused")
		check(continued_shell.current_view.observation_cue.text().is_empty(), "Continue announced saved findings again")
	continued_shell.free()
	flight.port_command("release_freedom_port")
	check(not bridge.get_freedom_flight_state().attached and "12 m" in flight.save_status.text, "Release lost mapped withdrawal guidance")
	check(bridge.set_freedom_assistance(false), "Explicit manual coast refused")
	var commands := PackedFloat64Array()
	commands.resize(12)
	commands[4] = 0.25
	for frame in 360:
		check(bridge.advance_freedom_flight(1.0 / 60.0, commands, false), "Public departure refused")
	flight.state = bridge.get_freedom_flight_state()
	flight.update_view(0.0)
	check(flight.state.docking.separation > 80.0 and flight.state.negative_force_body[1] > 0.0, "Same craft never departed under withdrawal thrust")
	check(flight.state.resources.quantity_quanta.to_int() < 3032640000000000 and flight.state.resources.jump_charges == 3 and not flight.service_button.visible, "Actual departure did not burn flight fuel or exposed free-flight service")
	check(flight.state.chart.rows[0].confidence == "visited" and flight.state.chart.rows[1].confidence == "resolved", "Actual free flight failed to record only the physically visited system")
	check(flight.state.chart.observed_facts > 0 and "Ship sensors" in flight.chart_status.text, "Committed local evidence remained invisible in chart summary")
	check_recorded_observations(bridge, save_path.get_base_dir().path_join("recorded-departure.json"), false)
	check(flight.exhaust.update_applied(flight.state, 1.0 / 60.0, false) and flight.exhaust.withdrawal_intensity > 0.0, "Composed departure exhaust stayed dark")
	var approach_menu: Button = flight.controls_menu.saved_port_buttons["Approach port with thrusters"]
	check(not flight.approach_button.disabled and not approach_menu.disabled and approach_menu.focus_mode == Control.FOCUS_ALL, "Aligned return lost controller-accessible approach")
	approach_menu.pressed.emit()
	check(bridge.get_freedom_flight_state().port_approach.active, "Menu approach did not arm C++ command")
	flight._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	check(not bridge.get_freedom_flight_state().port_approach.active, "Focus loss retained approach thrust command")
	flight._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	flight.toggle_pause()
	flight.port_command("begin_freedom_port_approach")
	var departure: Dictionary = bridge.get_freedom_flight_state()
	var approach_save := save_path.get_base_dir().path_join("playable-approach.json")
	check(bridge.save_freedom_as(approach_save), "Approach Save As refused")
	var cadence: Variant = ClassDB.instantiate("FreedomBridge")
	var cadence_shell := Shell.new()
	cadence_shell.presentation_only = true
	root.add_child(cadence_shell)
	check(cadence_shell.select_start(cadence, {"mode": "continue", "value": approach_save, "assets": shell.assets_root}), "Approach Continue could not stage: " + cadence_shell.error)
	if cadence_shell.current_view == null:
		cadence_shell.free()
		return
	cadence_shell.current_view.set_process(false)
	check(not cadence.get_freedom_flight_state().port_approach.active, "Continue armed unsaved approach command")
	check(cadence.get_freedom_observations() == bridge.get_freedom_observations(), "Departure Continue changed recorded values or exact provenance")
	check(cadence.begin_freedom_port_approach(), "Resumed explicit approach refused")
	commands.fill(0.0)
	var invalid := commands.duplicate()
	invalid[3] = NAN
	check(not bridge.advance_freedom_flight(1.0 / 120.0, invalid, false) and bridge.get_freedom_flight_state() == departure, "Invalid command canceled approach or changed body")
	for frame in 450:
		check(bridge.advance_freedom_flight(1.0 / 30.0, commands, false), "30 Hz approach refused")
	for frame in 900:
		check(cadence.advance_freedom_flight(1.0 / 60.0, commands, false), "60 Hz approach refused")
	var from_continue: Dictionary = cadence.get_freedom_flight_state()
	var uninterrupted: Dictionary = bridge.get_freedom_flight_state()
	# The source-save selection is intentionally different; physical results
	# and transient command state must still agree exactly.
	from_continue.erase("continued")
	uninterrupted.erase("continued")
	check(from_continue == uninterrupted, "Render cadence changed actual approach or actuator state")
	cadence_shell.free()
	var returned := false
	for tick in 300:
		var current: Dictionary = bridge.get_freedom_flight_state()
		if current.docking.ready:
			returned = true
			break
		check(bridge.advance_freedom_flight(1.0 / 120.0, commands, false), "Final close approach refused")
	check(returned and not bridge.get_freedom_flight_state().attached and bridge.get_freedom_flight_state().port_approach.active, "Aid lost actual readiness, or auto-captured")
	flight.state = bridge.get_freedom_flight_state()
	flight.update_view(0.0)
	flight.port_command("capture_freedom_port")
	check(flight.state.attached and flight.exhaust.withdrawal_intensity == 0.0, "Composed capture lost attachment or retained propulsion")
	var service_tick: String = flight.state.tick
	flight.service_button.pressed.emit()
	check(flight.state.resources.quantity_quanta == "3032640000000000" and flight.state.resources.jump_charges == 3 and flight.state.tick == service_tick and "free station service" in flight.save_status.text, "Attached cockpit action failed exact free replenishment")
	flight.unboard_menu_button.pressed.emit()
	var return_light: Basis = flight.light.basis
	joy_axis(JOY_AXIS_LEFT_X, 0.8, 19)
	joy_axis(JOY_AXIS_LEFT_X, 0.8, 9)
	check(bridge.save_freedom_as(ProjectSettings.globalize_path("user://controller-before.json")), "Controller pre-unboard export failed")
	var before_unboard: PackedByteArray = FileAccess.get_file_as_bytes("user://controller-before.json")
	shell.switch_journey_view()
	walk = shell.current_view
	check(walk is WalkView and walk.staged_model == model, "Unboard did not restore walking on the same model")
	if not walk is WalkView: return
	check(walk.frame_diagnostics.enabled and walk.frame_diagnostics.samples.is_empty(), "Unboarding lost diagnostic choice or retained stale frame samples")
	check_sunlight(walk)
	check(walk.selected_pad == 19 and walk.available_pads.get(19, false), "Unboarding forgot the selected controller")
	check(walk.paused and not walk.controls_armed, "Held selected walking stick bypassed the unboarding neutral gate")
	joy_axis(JOY_AXIS_LEFT_X, 0.0, 19)
	check(walk.current_controls_neutral(), "Foreign stick prevented the selected controller from neutral rearming")
	joy_axis(JOY_AXIS_LEFT_X, 0.0, 9)
	check(bridge.save_freedom_as(ProjectSettings.globalize_path("user://controller-after.json")) and FileAccess.get_file_as_bytes("user://controller-after.json") == before_unboard, "Controller unboarding handoff changed exact world bytes")
	check(walk.light.basis.is_equal_approx(return_light) and walk.light.basis.z.is_equal_approx(bridge.get_freedom_flight_state().star_direction), "Disembarking changed stellar direction at the view handoff")
	walk.set_process(false)
	if walk.paused: walk.resume_requested()
	look_reference = boarding_reference(bridge, "disembarking")
	check_boarding_look(walk, bridge, "disembarking")
	for frame in 360:
		if look_reference != null: check(look_reference.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])), "No-look disembarking reference refused")
		if not walk.advance_requested(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])):
			check(false, "Unboarding advance refused")
			return
		if frame == 120:
			check(not walk.camera.basis.is_equal_approx(walk.state.station_basis * Basis(Vector3.UP, walk.state.heading_radians)) and walk.camera.position == walk.state.actor_eye_position, "Disembarking look was reset or displaced the eye")
		if frame in [120, 330, 345, 359]: check_boarding_save(bridge, look_reference, "disembarking")
	check(walk.boarding_look == Vector2.ZERO and walk.camera.basis.is_equal_approx(walk.state.station_basis * Basis(Vector3.UP, walk.state.heading_radians)), "Station return retained a stale camera offset")
	var restored: Dictionary = bridge.get_freedom_walk_state()
	check(restored.foot_position_metres == entry.foot_position_metres and restored.heading_radians == entry.heading_radians, "Unboard did not restore the exact entry actor")
	check(bridge.get_freedom_boarding_state().state == "station" and model.valid_current_pose(), "Unboard lost completed station/hardware state")
	var before: PackedFloat64Array = restored.eye_position_metres
	check(walk.advance_requested(0.1, PackedFloat64Array([0.0, 1.0, 0.0])) and bridge.get_freedom_walk_state().eye_position_metres != before, "Completed unboard pinned the ordinary walking eye")


func check_load(shell: Control, bridge: Variant, args: PackedStringArray) -> void:
	var directory := args[1].get_base_dir()
	var before_save := directory.path_join("before-load.json")
	var after_save := directory.path_join("after-load.json")
	var view: Control = shell.current_view
	view.pause_controls("Load regression paused.")
	check(bridge.save_freedom_as(before_save), "Pre-load save refused")
	# This regression exercises explicit file loading; catalog loading has its
	# own real-provider contract and does not open a filesystem chooser.
	check(shell.select_start(bridge, {"mode": "continue", "value": before_save, "assets": args[0]}), "Explicit file workflow could not stage")
	view = shell.current_view
	check(view is WalkView and view.selected_pad == 19 and view.available_pads.get(19, false), "Explicit source replacement forgot the selected walking controller")
	view.pause_controls("Explicit file Load regression paused.")
	var original_hash := FileAccess.get_sha256(before_save)
	var source_hash := FileAccess.get_sha256(args[1])
	var current: Dictionary = bridge.get_freedom_flight_state()
	# Refusal/cancel never stages a replacement or bypasses pause/neutral input.
	view.focused = false
	view.load_button.pressed.emit()
	check(shell.load_origin == null, "Unfocused Load acquired authority")
	view.focused = true
	view.load_button.pressed.emit()
	check(shell.load_origin == view and view.journey_dialog_open and view.process_mode == Node.PROCESS_MODE_DISABLED, "Station Load did not suspend its view")
	view.resume_requested()
	view._process(0.125)
	check(view.paused and bridge.get_freedom_flight_state() == current, "Modal Load resumed/advanced current journey")
	shell.load_dialog.get_cancel_button().pressed.emit()
	check(shell.load_origin == null and not view.journey_dialog_open and view.paused and root.gui_get_focus_owner() == view.load_button, "File cancel lost paused invoking selection")
	view.load_button.pressed.emit()
	shell.load_dialog.file_selected.emit(args[1])
	check(shell.load_confirmation.visible and shell.current_view == view and bridge.get_freedom_flight_state() == current, "File selection replaced journey before confirmation")
	var previous_window: Vector2i = root.size
	for pixels in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = pixels
		for frame in 2: await process_frame
		shell.layout_load_dialogs()
		for frame in 2: await process_frame
		var font_pixels: float = shell.load_confirmation.get_label().get_theme_font_size("font_size") * float(pixels.x) / view.size.x
		print("Load layout: pixels=%s logical=%s font=%s wrapped=%s dialog=%s" % [pixels, view.size, font_pixels, shell.load_confirmation.get_label().autowrap_mode, shell.load_confirmation.size])
		check(font_pixels >= 17.5 and font_pixels <= 20.5 and shell.load_confirmation.dialog_autowrap and shell.load_confirmation.get_label().autowrap_mode != TextServer.AUTOWRAP_OFF, "Load confirmation lost readable wrapped text")
		check(shell.load_confirmation.size.x <= view.size.x and shell.load_confirmation.size.y <= view.size.y, "Load confirmation escaped logical viewport")
	root.size = previous_window
	shell.layout_load_dialogs()
	for frame in 2: await process_frame
	shell.load_confirmation.get_cancel_button().pressed.emit()
	check(shell.current_view == view and view.paused, "Discard cancel lost journey")
	for path in ["relative.json", directory.path_join("missing.json"), args[2], directory.path_join("career.json")]:
		view.load_button.pressed.emit()
		shell.load_dialog.file_selected.emit(path)
		if shell.load_origin != null: shell.confirm_load()
		check(shell.load_origin == null and shell.current_view == view and view.paused and view.error.is_empty() and bridge.get_pending_freedom_start().is_empty(), "Failed load replaced/poisoned view")
		check(bridge.save_freedom_as(after_save) and FileAccess.get_sha256(after_save) == original_hash, "Refused load changed complete save bytes")
	view.load_button.pressed.emit()
	shell.load_dialog.file_selected.emit(args[1])
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	shell.confirm_load()
	check(shell.load_origin == null and shell.current_view == view and view.paused and bridge.save_freedom_as(after_save) and FileAccess.get_sha256(after_save) == original_hash, "Focus loss authorized replacement")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	# A valid source still cannot commit an incomplete native scene.
	shell.damage = "atlas"
	view.load_button.pressed.emit()
	shell.load_dialog.file_selected.emit(args[1])
	shell.confirm_load()
	check(shell.current_view == view and shell.load_origin == null and bridge.save_freedom_as(after_save) and FileAccess.get_sha256(after_save) == original_hash and bridge.get_pending_freedom_start().is_empty(), "Late asset refusal changed active journey")
	shell.damage = ""
	# Repeated successful path-based loads use the same bridge, complete target
	# saves, paused Continue and existing neutral rearming; no source rewriting.
	for target in [directory.path_join("flight-trace.json"), args[1], directory.path_join("wayfarer-flight.json")]:
		check(FileAccess.file_exists(target), "Load fixture is missing")
		view = shell.current_view
		view.pause_controls("Paused before replacement.")
		if view is WalkView: view.load_button.pressed.emit()
		else: view.controls_menu.load_button.pressed.emit()
		check(shell.load_origin == view, "Paused Load entry unavailable")
		shell.load_dialog.file_selected.emit(target)
		shell.confirm_load()
		view = shell.current_view
		view.set_process(false)
		check((view.selected_pad if view is WalkView else view.player_input.device) == 19, "Confirmed Load changed the selected controller")
		check(shell.bridge == bridge and view.paused and not view.journey_dialog_open and shell.load_origin == null and shell.error.is_empty(), "Successful load lost bridge, pause or modal cleanup")
		check(bridge.save_freedom_as(after_save) and JSON.parse_string(FileAccess.get_file_as_string(after_save)) == JSON.parse_string(FileAccess.get_file_as_string(target)), "Loaded target differs from complete source save")
		if view is FlightView:
			view.controls_menu.load_button.pressed.emit()
			shell.load_dialog.file_selected.emit(args[2])
			shell.confirm_load()
			view.controls_menu._process(0.0)
			check("Load refused:" in view.controls_menu.message.text and "Load refused:" in view.primary_status.tooltip_text, "Load refusal vanished on menu refresh")
		var frozen: Dictionary = bridge.get_freedom_flight_state()
		view._process(0.125)
		check(bridge.get_freedom_flight_state() == frozen, "Continue replacement advanced before explicit Resume")
	check(FileAccess.get_sha256(args[1]) == source_hash, "Load rewrote source bytes")
	for frame in 2: await process_frame
