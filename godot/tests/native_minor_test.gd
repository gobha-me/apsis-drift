extends SceneTree
const FrameDiagnosticsChecks = preload("res://tests/frame_diagnostics_checks.gd")
## Actual C++ inspection route, typed staging and ordinary occupied-craft UI.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const MinorView = preload("res://scripts/native/native_minor_view.gd")
const MinorState = preload("res://scripts/native/native_minor_state.gd")
const MinorGeometry = preload("res://scripts/native/native_minor_geometry.gd")
const RouteReuse = preload("res://tests/native_local_route_reuse.gd")
const PortRendezvous = preload("res://tests/native_port_rendezvous.gd")
const PHASES = ["arrival", "approach", "withdraw", "home"]
var failures := 0

class GeometryPacket extends RefCounted:
	var packet: Dictionary
	func get_freedom_minor_geometry(_candidate: String) -> Dictionary: return packet

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error(message)
	return value

func _initialize() -> void:
	call_deferred("run")

func save_bytes(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Actual minor Save failed: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func shell_for(owner: Variant, path: String, assets: String) -> Control:
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Minor Continue staging failed: " + shell.error):
		shell.free()
		return null
	shell.current_view.set_process(false)
	return shell

func check_admission(owner: Variant, view: Control, output: String) -> void:
	var state: Dictionary = owner.get_freedom_flight_state()
	var frozen := save_bytes(owner, output.path_join("admission-before.json"))
	check(not frozen.is_empty() and MinorState.valid(state) and not state.has("planet_id") and state.body_kind == "minor", "Actual source is not a typed minor-only observation")
	check(not state.surface.landed and not state.surface.gear_deployed and state.surface_walk.is_empty() and state.surface.gear_clearance == {"available": false}, "Inspection proxy invented surface/gear/walking authority")
	check(not state.surface_marker.current_body and not state.surface_marker.can_mark and state.exclusion_radius > state.body_radius and state.stand_off_gap > 0.0, "Minor packet lost C++ stand-off or invented local marker authority")
	check(owner.get_freedom_walk_state().is_empty() and owner.get_freedom_station_geometry().is_empty(), "Minor route exposed a phantom station/walking owner")
	for contact in state.get("distant_contacts", {}).get("contacts", []):
		check(str(contact.get("id", contact.get("body_id", ""))) != state.body_id, "Current physical minor was duplicated as a distant optical contact")
	for damage in ["planet_alias", "moon_alias", "body_version", "frame", "center", "basis", "eye", "force", "assistance", "tint", "gap", "landed", "walker", "gear"]:
		var bad := state.duplicate(true)
		match damage:
			"planet_alias": bad.planet_id = state.body_id
			"moon_alias": bad.body_kind = "moon"
			"body_version": bad.body_version = 99
			"frame": bad.coordinate_frame = "planet_relative_inertial"
			"center": bad.minor_center = Vector3(NAN, 0, 0)
			"basis": bad.minor_fixed_basis = Basis(Vector3.ZERO, Vector3.UP, Vector3.BACK)
			"eye": bad.boarding.eye_craft = PackedFloat64Array([INF, 0, 0])
			"force": bad.positive_force_body = PackedFloat64Array([0, NAN, 0])
			"assistance": bad.erase("assistance")
			"tint": bad.minor_tint = Color(0, 0, NAN, 1)
			"gap": bad.stand_off_gap = -1.0
			"landed": bad.surface.landed = true
			"walker": bad.surface_walk = {"version": 1}
			"gear": bad.surface.gear_deployed = true
		var pending := {"candidate_id": "1", "mode": "freedom_flight", "craft_binding": owner.get_freedom_craft_binding(), "walk_state": {}, "flight_state": bad, "station_state": {}, "station_geometry": {}, "streaming_ready": true}
		check(not MinorState.valid(bad) and not Shell.valid_pending(pending), "Malformed minor state passed typed staging: " + damage)
	var packet: Dictionary = owner.get_freedom_minor_geometry("")
	if not check(MinorGeometry.valid_packet(packet, state), "Actual source proxy packet failed admission"): return
	check(packet.vertices.size() == 2145 and packet.indices.size() == 11904, "Actual proxy differs from bounded C++32x64 source mesh")
	for damage in ["owner", "tick", "checksum", "extra", "vertices", "normal", "index", "oversize"]:
		var bad := packet.duplicate(true)
		match damage:
			"owner": bad.body_id = "0" if state.body_id != "0" else "1"
			"tick": bad.tick = "0" if state.tick != "0" else "1"
			"checksum": bad.source_checksum = "0" if state.checksum != "0" else "1"
			"extra": bad.extra = true
			"vertices":
				bad.vertices = packet.vertices.duplicate()
				bad.vertices[0] = Vector3(INF, 0, 0)
			"normal":
				bad.normals = packet.normals.duplicate()
				bad.normals[0] = Vector3.ZERO
			"index":
				bad.indices = packet.indices.duplicate()
				bad.indices[0] = packet.vertices.size()
			"oversize":
				bad.vertices = packet.vertices.duplicate()
				bad.vertices.append(Vector3.ZERO)
		var provider := GeometryPacket.new()
		provider.packet = bad
		var rejected := MinorGeometry.new()
		check(not rejected.stage(provider, {"candidate_id": "1"}, state) and rejected.nodes.is_empty() and rejected.get_child_count() == 0, "Malformed proxy allocated scene/mesh residency: " + damage)
		rejected.free()
	var mesh: ArrayMesh = view.terrain.nodes[0].mesh
	check(view.terrain.nodes.size() == 1 and mesh.surface_get_array_len(0) == 2145 and mesh.surface_get_array_index_len(0) == 11904, "Ordinary MinorView lost its single actual physical proxy")
	view.update_view(0.0)
	check(view.terrain.nodes[0].transform == Transform3D(state.minor_fixed_basis, state.minor_center), "MinorView replaced the C++ current-tick mesh transform")
	check(view.camera.get_world_3d() == view.terrain_camera.get_world_3d() and view.camera.transform == view.terrain_camera.transform, "Minor close/distant cameras lost shared world/registration")
	check(view.error.is_empty() and "Inspection only" in view.primary_status.text and not view.context_action.visible and not view.landing_button.visible, "MinorView lost inspection disclosure or exposed landing UI")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(not owner.advance_freedom_flight(NAN, neutral, false), "Nonfinite native elapsed time was accepted")
	var invalid := neutral.duplicate()
	invalid[0] = INF
	check(not owner.advance_freedom_flight(1.0 / 120.0, invalid, false) and not owner.advance_freedom_flight(1.0 / 120.0, PackedFloat64Array([0.0]), false), "Malformed native demand was accepted")
	check(owner.get_freedom_minor_geometry("0").is_empty(), "Foreign candidate reused current physical geometry")
	check(not owner.request_freedom_landing() and not owner.begin_freedom_surface_walk(), "Minor inspection route admitted landing or walking")
	for repeat in 3:
		view.update_view(0.0)
		check(owner.get_freedom_flight_state() == state, "Pure minor observation/mesh refresh advanced or changed state")
	check(save_bytes(owner, output.path_join("admission-after.json")) == frozen, "Reads/malformed packets/refused source commands changed the complete minor Save")

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func pad_binding(provider: Node, down: bool) -> void:
	var binding: Dictionary = provider.bindings.rise.pad
	var event: InputEvent
	if binding.kind == "axis":
		var motion := InputEventJoypadMotion.new()
		motion.axis = int(binding.code)
		motion.axis_value = float(binding.sign) if down else 0.0
		event = motion
	else:
		var button := InputEventJoypadButton.new()
		button.button_index = int(binding.code)
		button.pressed = down
		event = button
	event.device = provider.device
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check_controls(phases: String, assets: String, output: String) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, phases.path_join("arrival.json"), assets)
	if shell == null: return
	var view: Control = shell.current_view
	# Synthetic headless focus/input proves logical gates, not hardware acceptance.
	view.focused = true
	view.player_input.focused = true
	view.player_input.persist = false
	var frozen := save_bytes(owner, output.path_join("controls-before.json"))
	view._process(0.125)
	check(view.paused and save_bytes(owner, output.path_join("paused.json")) == frozen, "Paused minor view advanced clock/fuel/physics")
	view.controls_menu.show_settings()
	view._process(0.125)
	view.toggle_pause()
	check(view.paused and not view.controls_menu.settings_view.visible and save_bytes(owner, output.path_join("modal.json")) == frozen, "Nested Settings resumed or advanced minor flight")
	var code: int = int(view.player_input.bindings.rise.key.code)
	key(code, true)
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Held mapped keyboard input bypassed minor resume neutral gate")
	key(code, false)
	view.player_input.device = 41
	view.player_input.available_pads[41] = true
	view.player_input.install()
	pad_binding(view.player_input, true)
	view.toggle_pause()
	check(view.paused and not view.controls_armed, "Held selected-controller input bypassed minor resume neutral gate")
	pad_binding(view.player_input, false)
	check(view.paused, "Releasing controls automatically resumed minor flight")
	view.observe_neutral_controls()
	view.toggle_pause()
	check(not view.paused and view.controls_armed, "Explicit neutral minor resume failed")
	view.toggle_pause()
	check(view.paused and save_bytes(owner, output.path_join("controls-after.json")) == frozen, "Input ownership/pause transitions changed full minor Save without a simulation tick")
	var before: Dictionary = owner.get_freedom_flight_state()
	view.controls_menu.assistance_requested.emit(not before.assistance)
	var changed: Dictionary = owner.get_freedom_flight_state()
	check(changed.assistance != before.assistance and changed.tick == before.tick and changed.resources == before.resources, "Minor assistance UI lost actual owner or changed time/resources")
	view.request_assistance(before.assistance)
	check(save_bytes(owner, output.path_join("assistance-restored.json")) == frozen, "Restoring minor assistance changed unrelated complete Save state")
	shell.free()
	await process_frame

func replay(owner: Variant, segment: Dictionary) -> bool:
	var demand := PackedFloat64Array()
	for name in ["positive_translation", "negative_translation", "positive_rotation", "negative_rotation"]:
		if not check(segment.get(name) is Array and segment[name].size() == 3, "Fixture segment has malformed actual controls"): return false
		for value in segment[name]: demand.append(float(value))
	var ticks: int = int(segment.get("ticks", 0))
	if not check(ticks > 0 and ticks <= (24000 if segment.to == "approach" else 12000), "Fixture segment exceeds bounded route"): return false
	for tick in ticks:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, demand, false), "Actual inspection control tick refused: " + str(owner.get_last_error())): return false
	return true

func check_blocked_chart(owner: Variant, view: Control, output: String) -> void:
	var initial_failures := failures
	var state: Dictionary = owner.get_freedom_flight_state()
	var before_path := output.path_join("blocked-chart-before.json")
	var before := save_bytes(owner, before_path)
	var was_open: bool = view.chart_button.button_pressed
	view.chart_button.button_pressed = true
	view.update_view(0.0)
	var evidence := {"version": 1, "completed": false, "tick": state.tick, "source_sha256": FileAccess.get_sha256(before_path), "scope": "Actual committed local home return; disabled chart reason visible as text and tooltip, without a simulation tick or saved-state change"}
	var found := false
	for i in state.chart.rows.size():
		var row: Dictionary = state.chart.rows[i]
		if row.current or row.confidence not in ["resolved", "visited"]: continue
		var button: Button = view.jump_destinations[i]
		var reason: String = row.get("disabled_reason", "")
		check(view.chart_status.visible and button.visible and button.disabled and not row.selectable and not reason.is_empty() and reason in view.chart_status.text and reason in button.tooltip_text, "Known neighbor refusal is unavailable to the actual chart/controller text path")
		evidence.merge({"system_id": row.system_id, "confidence": row.confidence, "disabled_reason": reason, "chart_text": view.chart_status.text, "tooltip": button.tooltip_text})
		found = true
		break
	check(found, "Actual returned home chart lacks its known noncurrent neighbor")
	view.chart_button.button_pressed = was_open
	view.update_view(0.0)
	var after_path := output.path_join("blocked-chart-after.json")
	check(save_bytes(owner, after_path) == before and owner.get_freedom_flight_state() == state, "Opening/reading/restoring disabled chart changed the full saved voyage or clock")
	evidence.after_sha256 = FileAccess.get_sha256(after_path)
	evidence.completed = failures == initial_failures
	var file := FileAccess.open(output.path_join("blocked-chart.json"), FileAccess.WRITE)
	if check(file != null, "Blocked chart evidence unavailable"):
		file.store_string(JSON.stringify(evidence, "\t") + "\n")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	var phases := args[0]
	var assets := args[1]
	var output := args[2]
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Minor native output unavailable"): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Actual native minor bridge unavailable"): quit(1); return
	var report: Variant = JSON.parse_string(FileAccess.get_file_as_string(phases.path_join("report.json")))
	if not check(report is Dictionary and report.get("version") == 1 and report.get("seed") == "42" and report.get("approach_limited") == true and report.get("segments") is Array and report.segments.size() == 2, "Actual bounded C++ minor fixture report unavailable"): quit(1); return
	var originals := {}
	for label in PHASES: originals[label] = FileAccess.get_file_as_bytes(phases.path_join(label + ".json"))
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := shell_for(owner, phases.path_join("arrival.json"), assets)
	if shell == null: quit(1); return
	var view: Control = shell.current_view
	if not check(view is MinorView and view.paused and view.staged_model.valid_current_pose(), "Actual Continue failed ordinary typed MinorView staging"):
		shell.free(); quit(1); return
	await FrameDiagnosticsChecks.run(self, view, owner, output.path_join("arrival-native.json"), check)
	check_admission(owner, view, output)
	for index in 2:
		var segment: Dictionary = report.segments[index]
		if not check(segment.get("from") == ("arrival" if index == 0 else "approach") and segment.get("to") == ("approach" if index == 0 else "withdraw"), "Actual fixture reordered its control segments"): break
		var before: Dictionary = owner.get_freedom_flight_state()
		if not replay(owner, segment): break
		var after: Dictionary = owner.get_freedom_flight_state()
		view.state = after
		view.update_view(0.0)
		check(MinorState.valid(after) and int(after.tick) - int(before.tick) == int(segment.ticks) and after.resources.fraction < before.resources.fraction, "Actual thrust failed minor clock/fuel accounting")
		check(save_bytes(owner, output.path_join(segment.to + "-native.json")) == originals[segment.to], "Godot-applied controls differ from complete C++ phase: " + str(segment.to))
		if index == 0:
			check(after.altitude < before.altitude and after.stand_off_gap > 0.0 and not after.inspection_note.is_empty() and "Stand-off limit" in view.primary_status.text, "Actual inward flight lost stand-off response or actionable UI note")
		else:
			check(after.altitude > before.altitude and after.altitude >= 2000.0, "Actual withdrawal did not increase source-body separation")
	view.select_home()
	view.observe_neutral_controls()
	view.jump_requested()
	var spool: Dictionary = owner.get_freedom_flight_state()
	check(spool.jump.phase == "spool" and spool.resources.jump_charges == 2, "Minor home UI spent a charge before commitment or refused spool")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for tick in 600:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual charged home-return tick refused"): break
	var home: Dictionary = owner.get_freedom_flight_state()
	check(home.get("body_kind") != "minor" and home.get("station_available", false) and home.jump.phase == "idle" and home.resources.jump_charges == 1, "Charged minor return lost actual home/station/one-charge bill")
	check(save_bytes(owner, output.path_join("home-native.json")) == originals.home, "Actual native charged return differs from complete C++ home phase")
	view._process(0.0)
	await process_frame
	await process_frame
	check(shell.current_view is FlightView and not shell.current_view is MinorView and shell.current_view.paused and shell.current_view.error.is_empty(), "Minor-to-home handoff retained wrong consumer or resumed automatically")
	shell.current_view.set_process(false)
	shell.free()
	await process_frame
	for label in PHASES:
		var peer: Variant = ClassDB.instantiate("FreedomBridge")
		var restored := shell_for(peer, phases.path_join(label + ".json"), assets)
		if restored == null: continue
		check(restored.current_view.paused and (restored.current_view is MinorView) == (label != "home") and save_bytes(peer, output.path_join(label + "-continued.json")) == originals[label], "Complete phase Continue selected wrong view or changed bytes: " + label)
		check(str(peer.get_freedom_flight_state().tick) == str(report.phases[label].tick), "Actual phase Continue lost authoritative tick: " + label)
		if label == "home":
			check_blocked_chart(peer, restored.current_view, output)
			await PortRendezvous.run(self, peer, restored.current_view, phases, assets, output.path_join("rendezvous-ui"))
		restored.free()
		await process_frame
	await check_controls(phases, assets, output)
	var reused: Variant = ClassDB.instantiate("FreedomBridge")
	var reuse_shell := shell_for(reused, phases.path_join("reuse-captured.json"), assets)
	if reuse_shell != null:
		failures += await RouteReuse.run(self, reuse_shell, reused, assets, output.path_join("route-reuse"), "moon")
		reuse_shell.free()
		await process_frame
	for label in PHASES: check(FileAccess.get_file_as_bytes(phases.path_join(label + ".json")) == originals[label], "Minor integration test changed source fixture: " + label)
	print("Native minor: %d failures" % failures)
	quit(1 if failures else 0)
