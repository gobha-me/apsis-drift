extends SceneTree
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
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
func _initialize() -> void:
	call_deferred("run")
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
	var shell := LateDamagedShell.new()
	shell.presentation_only = true
	root.add_child(shell)
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
	var initial_resources: Dictionary = bridge.get_freedom_flight_state().resources
	check(initial_resources.selected and initial_resources.quantity_quanta == "3032640000000000" and initial_resources.jump_charges == 3, "Staged New Game lost finite C++ resources")
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
	check_boarding(shell, bridge)
	shell.free()
	print("Native start staging checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)


func check_boarding(shell: Control, bridge: Variant) -> void:
	var walk: Control = shell.current_view
	walk.set_process(false)
	var original: Dictionary = bridge.get_freedom_walk_state()
	check(not bridge.begin_freedom_boarding() and bridge.get_freedom_walk_state() == original, "Distant boarding changed the walker")
	for tick in 2960:
		if not bridge.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, -1.0, 0.0])):
			check(false, "Supported approach to D1 refused")
			return
	walk.update_view()
	var entry: Dictionary = bridge.get_freedom_walk_state()
	var model: Node3D = walk.staged_model
	var roots: Array = model.replacement_nodes.duplicate()
	walk.board_requested()
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
	# Present at 30 Hz while C++ still consumes every authoritative 120-Hz tick.
	for frame in 360:
		if not walk.advance_requested(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])):
			check(false, "Boarding advance refused")
			return
	var seat_camera: Transform3D = walk.camera.transform
	shell.switch_journey_view()
	var flight: Control = shell.current_view
	check(flight is FlightView and flight.staged_model == model and flight.cockpit, "Seat did not hand the same model to cockpit flight")
	if not flight is FlightView: return
	flight.set_process(false)
	check(flight.camera.transform.origin.distance_to(seat_camera.origin) < 0.002 and flight.camera.transform.basis.is_equal_approx(seat_camera.basis), "Seated camera jumped at view handoff")
	check(model.valid_current_pose() and model.replacement_nodes == roots and model.get_child_count() == 1, "Boarding replaced or reparented the hardware roster")
	check(flight.state.resources.quantity_quanta == "3032640000000000" and "90.0 min at full main" in flight.resource_status.text and flight.service_button.focus_mode == Control.FOCUS_ALL and flight.service_button.visible, "Walking/boarding burned fuel or cockpit lost accessible resource service")
	check(not flight.unboard_button.disabled and flight.unboard_button.visible and flight.unboard_menu_button.visible and flight.unboard_menu_button.focus_mode == Control.FOCUS_ALL and not flight.release_button.disabled and flight.exhaust.valid_bound_skin(), "Seated UI lost unboard/release/exhaust")
	var save_path: String = OS.get_cmdline_user_args()[1].get_base_dir().path_join("playable-seated.json")
	check(bridge.save_freedom_as(save_path), "Seated Save As refused")
	var continued_owner: Variant = ClassDB.instantiate("FreedomBridge")
	var continued_shell := Shell.new()
	continued_shell.presentation_only = true
	root.add_child(continued_shell)
	check(continued_shell.select_start(continued_owner, {"mode": "continue", "value": save_path, "assets": shell.assets_root}), "Seated Continue could not stage: " + continued_shell.error)
	if continued_shell.current_view != null:
		check(continued_shell.current_view is FlightView and continued_shell.current_view.cockpit and continued_shell.current_view.paused and continued_shell.current_view.staged_model.valid_current_pose(), "Seated Continue lost pose, cockpit or neutral pause")
	check(continued_owner.get_freedom_flight_state().resources == bridge.get_freedom_flight_state().resources, "Staged Continue changed exact resource readings")
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
	shell.switch_journey_view()
	walk = shell.current_view
	check(walk is WalkView and walk.staged_model == model, "Unboard did not restore walking on the same model")
	if not walk is WalkView: return
	walk.set_process(false)
	if walk.paused: walk.resume_requested()
	for frame in 360:
		if not walk.advance_requested(1.0 / 30.0, PackedFloat64Array([0.0, 0.0, 0.0])):
			check(false, "Unboarding advance refused")
			return
	var restored: Dictionary = bridge.get_freedom_walk_state()
	check(restored.foot_position_metres == entry.foot_position_metres and restored.heading_radians == entry.heading_radians, "Unboard did not restore the exact entry actor")
	check(bridge.get_freedom_boarding_state().state == "station" and model.valid_current_pose(), "Unboard lost completed station/hardware state")
	var before: PackedFloat64Array = restored.eye_position_metres
	check(walk.advance_requested(0.1, PackedFloat64Array([0.0, 1.0, 0.0])) and bridge.get_freedom_walk_state().eye_position_metres != before, "Completed unboard pinned the ordinary walking eye")
