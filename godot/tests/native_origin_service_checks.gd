extends RefCounted
## Actual selected service sidecars; no pose/actor/site/fuel changes in Godot.
## Node/force checks are headless presentation evidence, not raster acceptance.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const MoonView = preload("res://scripts/native/native_moon_view.gd")
const ServiceCraft = preload("res://scripts/native/native_service_craft_view.gd")
const LABELS = ["service-landed", "service-requested", "service-partial600"]
var failures := 0
var output := ""

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error("Origin service: " + message)
	return value

func saved(owner: Variant, label: String) -> PackedByteArray:
	var path := output.path_join(label + ".json")
	if not check(owner.save_freedom_as(path), "Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func peer_from(path: String) -> Variant:
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(path), "Independent real Continue staging refused"): return null
	if not check(peer.commit_pending_freedom_start(peer.get_pending_freedom_start().candidate_id), "Independent real Continue commit refused"): return null
	return peer

func shell_for(tree: SceneTree, owner: Variant, path: String, assets: String) -> Control:
	var shell := Shell.new()
	shell.presentation_only = true
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Actual selected service Continue failed: " + shell.error):
		shell.free()
		return null
	shell.current_view.set_process(false)
	return shell

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

static func player_part(state: Dictionary) -> Dictionary:
	var result := state.duplicate(true)
	result.erase("origin_service")
	return result

func reserved_berth(view: Control, state: Dictionary) -> void:
	var button: Button = view.controls_menu.saved_port_buttons.get("Target D2")
	check(state.get("origin_service", {}).get("reserved_port", 0) == 2 and button != null and button.disabled and "reserved" in button.tooltip_text.to_lower() and "away" in button.tooltip_text.to_lower() and not "occupied" in button.tooltip_text.to_lower(), "Service D2 must remain reserved with a truthful away tooltip, including at the Moon")

func packet_boundaries(packet: Dictionary) -> void:
	# Malformed copies stay inside this pure validator; never submit them to a
	# bridge, scene, renderer or saved actor. The baseline comes from real C++.
	var original := packet.duplicate(true)
	if not check(ServiceCraft.valid_packet(packet, 0.0) and ServiceCraft.valid_packet(packet, 60.0), "Real service packet must admit elapsed endpoints0/60"):
		return
	for elapsed in [-0.001, NAN, 60.001]:
		check(not ServiceCraft.valid_packet(packet, elapsed), "Service packet admitted invalid elapsed " + str(elapsed))
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check(not ServiceCraft.valid_packet(missing, 0.0), "Service packet admitted missing field " + str(field))
	var extra := packet.duplicate(true)
	extra["unexpected"] = true
	check(not ServiceCraft.valid_packet(extra, 0.0), "Service packet admitted an extra field")
	for version in [0, 2, 1.0, "1"]:
		var wrong := packet.duplicate(true)
		wrong.version = version
		check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted wrong version " + str(version))
	for identity in [0, "", "0", "01", "x", "18446744073709551616"]:
		var wrong := packet.duplicate(true)
		wrong.craft_id = identity
		check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted invalid craft identity " + str(identity))
	for position in [Vector3(NAN, 0.0, 0.0), Vector3(0.0, INF, 0.0), Vector3(2.1e15, 0.0, 0.0), Vector3(0.0, 0.0, -2.1e15)]:
		var wrong := packet.duplicate(true)
		wrong.position = position
		check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted nonfinite/out-of-range position")
	for basis in [Basis(Vector3(NAN, 0.0, 0.0), Vector3.UP, Vector3.BACK), Basis(Vector3(2.0, 0.0, 0.0), Vector3.UP, Vector3.BACK), Basis(Vector3.LEFT, Vector3.UP, Vector3.BACK)]:
		var wrong := packet.duplicate(true)
		wrong.basis = basis
		check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted nonfinite/scaled/reflected basis")
	for sign_name in ["positive", "negative"]:
		for suffix in ["_force_body", "_force_ratings"]:
			var field: String = sign_name + suffix
			for size in [2, 4]:
				var wrong := packet.duplicate(true)
				var values: PackedFloat64Array = wrong.propulsion[field].duplicate()
				values.resize(size)
				wrong.propulsion[field] = values
				check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted propulsion buffer size " + str(size) + ": " + field)
			for value in [NAN, INF, -1.0]:
				var wrong := packet.duplicate(true)
				var values: PackedFloat64Array = wrong.propulsion[field].duplicate()
				values[0] = value
				wrong.propulsion[field] = values
				check(not ServiceCraft.valid_packet(wrong, 0.0), "Service packet admitted invalid propulsion scalar: " + field)
		var over := packet.duplicate(true)
		var forces: PackedFloat64Array = over.propulsion[sign_name + "_force_body"].duplicate()
		forces[0] = over.propulsion[sign_name + "_force_ratings"][0] * 2.0
		over.propulsion[sign_name + "_force_body"] = forces
		check(not ServiceCraft.valid_packet(over, 0.0), "Service packet admitted over-rating " + sign_name + " force")
	check(packet == original, "Pure service packet refusal checks changed the actual C++ packet")

static func applied(packet: Dictionary) -> float:
	var total := 0.0
	for sign_name in ["positive", "negative"]:
		var forces: PackedFloat64Array = packet.propulsion[sign_name + "_force_body"]
		for force in forces: total += force
	return total

static func run(tree: SceneTree, phases: String, assets: String, destination: String, activate: Callable) -> int:
	var helper: RefCounted = load("res://tests/native_origin_service_checks.gd").new()
	helper.output = destination
	await helper.exercise(tree, phases, assets, activate)
	return int(helper.failures)

func selected_station_handoff(tree: SceneTree, assets: String) -> void:
	# Actual explicit opt-in New Game, registered1194 walking ticks and1440
	# boarding ticks. No saved pose/fuel mutation or additional rescue route.
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": assets, "origin_service": true}), "Selected-service station New Game refused: " + shell.error):
		shell.free(); return
	var walking: Control = shell.current_view
	walking.set_process(false)
	walking.pause_controls("Service actor handoff regression")
	var binding: Dictionary = owner.get_freedom_craft_binding()
	if not check(walking.service_binding == binding and walking.service_craft != null and walking.service_craft.model.selected_binding == binding, "Selected station service craft lost its authenticated binding"):
		shell.free(); return
	for frame in 298:
		if not check(owner.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, -1.0, 0.0])), "Selected-service actual station walk refused"):
			shell.free(); return
	if not check(owner.advance_freedom_walk(1.0 / 60.0, PackedFloat64Array([0.0, -1.0, 0.0])) and owner.begin_freedom_boarding(), "Selected-service actual boarding admission refused"):
		shell.free(); return
	for frame in 360:
		if not check(owner.advance_freedom_walk(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])), "Selected-service actual boarding tick refused"):
			shell.free(); return
	if not check(owner.get_freedom_boarding_state().state == "seated" and owner.get_freedom_walk_state().is_empty(), "Selected-service real boarding did not finish seated"):
		shell.free(); return
	var before := saved(owner, "selected-service-seated-before")
	# Stage the genuine current seated source without activating it. This
	# catches late service-model imports that would otherwise follow commit.
	var probe: Control = load("res://scripts/native/native_flight_view.gd").new()
	var model: Node3D = Shell.HopperPresentation.load_selected_asset(assets, binding)
	var staged: bool = model != null and probe.stage(owner, assets, {"flight_state": owner.get_freedom_flight_state(), "station_geometry": owner.get_freedom_station_geometry(), "craft_binding": binding}, model)
	check(staged and not probe.activated and probe.ready_to_commit() and probe.service_craft != null and probe.service_craft.craft_id == walking.service_craft.craft_id and probe.service_craft.model.selected_binding == binding, "Unactivated flight stage lacks its validated second service craft")
	if model != null and model.get_parent() == null: model.free()
	probe.free()
	check(saved(owner, "selected-service-staged") == before, "Staging the second service craft changed the actual complete voyage")
	var identity: String = walking.service_craft.craft_id
	var walking_id := walking.get_instance_id()
	shell.switch_journey_view()
	var seated: Control = shell.current_view
	seated.set_process(false)
	check(seated.get_instance_id() != walking_id and shell.error.is_empty() and seated.error.is_empty() and seated.service_binding == binding and seated.service_craft != null and seated.service_craft.craft_id == identity and seated.service_craft.model.selected_binding == binding and saved(owner, "selected-service-seated-after") == before, "Selected-service station-to-seat handoff lost its actor/binding or changed the complete Save")
	shell.free()
	await tree.process_frame

func exercise(tree: SceneTree, phases: String, assets: String, activate: Callable) -> void:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Output directory unavailable"): return
	var originals := {}
	for label in LABELS:
		originals[label] = FileAccess.get_file_as_bytes(phases.path_join(label + ".json"))
		if not check(not originals[label].is_empty(), "Missing actual C++ service sidecar: " + label): return
	var old_size: Vector2i = tree.root.size
	tree.root.size = Vector2i(1280, 720)
	await tree.process_frame
	await tree.process_frame
	await selected_station_handoff(tree, assets)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(tree, owner, phases.path_join("service-landed.json"), assets)
	if shell == null: tree.root.size = old_size; return
	var view: Control = shell.current_view
	await tree.process_frame
	await tree.process_frame
	view.update_view(0.25)
	var before: Dictionary = owner.get_freedom_flight_state()
	var initial := saved(owner, "landed-before")
	var status: Dictionary = owner.get_freedom_origin_assistance_state()
	if not check(view is MoonView and view.paused and view.controls_menu.panel.visible and status.get("selected", false) and status.get("request_available", false) and not view.service_request.disabled and view.service_request.visible and initial == originals["service-landed"], "Actual supported fuel fixture lacks its paused request UI"):
		shell.free(); tree.root.size = old_size; return
	reserved_berth(view, before)
	packet_boundaries(before.get("origin_service", {}).get("craft", {}))
	for repeat in 3:
		owner.get_freedom_origin_assistance_state()
		view.update_view(0.0)
	check(saved(owner, "read-only") == initial and owner.get_freedom_flight_state() == before, "Availability/reading advanced either actor or changed supplies")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.service_request.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(saved(owner, "focus-refusal") == initial, "Unfocused request changed the complete Save")
	view.controls_menu.show_settings()
	view.service_request.pressed.emit()
	view.controls_menu.close_settings()
	check(saved(owner, "modal-refusal") == initial, "Settings modal admitted a background request")
	view.save_dialog.visible = true
	view.service_request.pressed.emit()
	view.save_dialog.hide()
	check(saved(owner, "save-dialog-refusal") == initial, "Save dialog admitted a background request")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	key(KEY_W, true)
	view.observe_neutral_controls()
	check(view.focused and Input.is_physical_key_pressed(KEY_W) and not view.controls_armed, "Held-demand refusal setup failed to hold actual flight input")
	view.service_request.pressed.emit()
	check(saved(owner, "held-refusal") == initial, "Held independent flight demand bypassed request neutrality")
	key(KEY_W, false)
	view.observe_neutral_controls()
	view.toggle_pause()
	check(not view.paused and view.controls_armed, "Neutral explicit Resume failed before active-mode guard")
	view.service_request.pressed.emit()
	check(saved(owner, "active-refusal") == initial, "Unpaused callback admitted a Controls-only request")
	view.pause_controls("Inspect actual service request")
	view.observe_neutral_controls()
	var peer: Variant = peer_from(phases.path_join("service-landed.json"))
	if peer == null: shell.free(); tree.root.size = old_size; return
	check(peer.request_freedom_origin_assistance(), "Independent direct C++ request refused")
	var expected := saved(peer, "requested-direct")
	await activate.call(view, view.service_request, false)
	var requested := saved(owner, "requested-ui")
	check(requested == expected and requested == originals["service-requested"] and player_part(owner.get_freedom_flight_state()) == player_part(before) and view.paused and view.controls_menu.panel.visible and not view.service_cancel.disabled, "Controls request differs from real C++ command, changes player state/time, or resumes implicitly")
	check(peer.cancel_freedom_origin_assistance(), "Independent predeparture cancellation refused")
	var expected_cancel := saved(peer, "canceled-direct")
	var old_device: int = view.player_input.device
	var old_pads: Dictionary = view.player_input.available_pads.duplicate()
	view.player_input.device = 19
	view.player_input.available_pads[19] = true
	view.player_input.install()
	await activate.call(view, view.service_cancel, true)
	view.player_input.available_pads = old_pads
	view.player_input.device = old_device
	view.player_input.install()
	check(saved(owner, "canceled-ui") == expected_cancel and expected_cancel == initial and view.paused and owner.get_freedom_flight_state().tick == before.tick, "Selected-pad predeparture Cancel differs from C++, refills supplies or changes clock")
	shell.free()
	await tree.process_frame
	owner = ClassDB.instantiate("FreedomBridge")
	shell = shell_for(tree, owner, phases.path_join("service-partial600.json"), assets)
	if shell == null: tree.root.size = old_size; return
	view = shell.current_view
	await tree.process_frame
	await tree.process_frame
	view.update_view(0.25)
	var partial := saved(owner, "partial-continued")
	var partial_state: Dictionary = owner.get_freedom_flight_state()
	reserved_berth(view, partial_state)
	var craft: Dictionary = partial_state.get("origin_service", {}).get("craft", {})
	if not check(partial == originals["service-partial600"] and ServiceCraft.valid_packet(craft, 0.0) and craft.visible and view.service_craft != null and view.service_craft.visible and view.service_craft.craft_id == craft.craft_id and view.service_craft.model != view.staged_model and view.service_craft.transform == Transform3D(craft.basis, craft.position) and not view.service_cancel.disabled, "Partial Continue lost the real second craft/placement/independent identity or saved receipt"):
		shell.free(); tree.root.size = old_size; return
	# Continue correctly clears transient actuation. One real shared flight tick
	# regenerates the actual NPC hover force; no pose or thrust is fabricated.
	peer = peer_from(phases.path_join("service-partial600.json"))
	if peer == null: shell.free(); tree.root.size = old_size; return
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(peer.advance_freedom_flight(1.0 / 120.0, neutral, false), "Independent actual hover tick refused")
	view.observe_neutral_controls()
	view.toggle_pause()
	if not check(not view.paused and view.controls_armed, "Partial service could not explicitly Resume"): shell.free(); tree.root.size = old_size; return
	view._process(1.0 / 120.0)
	var live: Dictionary = owner.get_freedom_flight_state()
	craft = live.get("origin_service", {}).get("craft", {})
	check(saved(owner, "hover-ui") == saved(peer, "hover-direct") and ServiceCraft.valid_packet(craft, 1.0 / 120.0) and craft.visible and applied(craft) > 0.0 and view.service_craft.visible and view.service_craft.exhaust.lift_intensity > 0.0 and view.error.is_empty(), "Real resumed NPC hover differs from C++ or lacks actual applied-force exhaust/second craft")
	view.toggle_pause()
	view.update_view(0.0)
	check(view.paused and is_zero_approx(view.service_craft.exhaust.lift_intensity), "Pause failed to freeze the service presentation without inventing actuation")
	var live_bytes := saved(owner, "hover-save")
	var live_status: Dictionary = owner.get_freedom_origin_assistance_state()
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("hover-save.json"), "assets": assets}), "Actual partial-after-hover Continue refused"):
		shell.free(); tree.root.size = old_size; return
	view = shell.current_view
	view.set_process(false)
	await tree.process_frame
	await tree.process_frame
	view.update_view(0.25)
	check(view.paused and saved(owner, "hover-recontinued") == live_bytes and owner.get_freedom_origin_assistance_state() == live_status and view.service_craft.visible and applied(view.state.origin_service.craft) == 0.0, "Continue changed partial receipt/supplies/pose or retained transient NPC exhaust")
	check(peer.cancel_freedom_origin_assistance(), "Independent partial-delivery return request refused")
	await activate.call(view, view.service_cancel, false)
	check(view.paused and saved(owner, "partial-canceled-ui") == saved(peer, "partial-canceled-direct") and not view.state.origin_service.cancel_available, "Paused partial Cancel differs from C++, advances time, refunds cargo/charges or resumes implicitly")
	shell.free()
	await tree.process_frame
	for label in LABELS:
		check(FileAccess.get_file_as_bytes(phases.path_join(label + ".json")) == originals[label], "Native UI rewrote its C++ sidecar: " + label)
	tree.root.size = old_size
