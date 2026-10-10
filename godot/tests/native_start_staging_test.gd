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
	var initial_resources: Dictionary = bridge.get_freedom_flight_state().resources
	check(initial_resources.selected and initial_resources.quantity_quanta == "3032640000000000" and initial_resources.jump_charges == 3, "Staged New Game lost finite C++ resources")
	var initial_chart: Dictionary = bridge.get_freedom_flight_state().chart
	check(initial_chart.local_sensors and initial_chart.observed_facts == 0, "New Game silently invented ship observations")
	check(initial_chart.selected and initial_chart.rows.size() == 2 and initial_chart.rows[0].current and initial_chart.rows[0].confidence == "resolved" and initial_chart.rows[1].confidence == "resolved" and initial_chart.rows[1].affordable, "Starting chart leaked extra rows, invented visits or lost granted route")
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
	await check_load(shell, bridge, args)
	shell.free()
	print("Native start staging checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)


func check_boarding(shell: Control, bridge: Variant) -> void:
	var walk: Control = shell.current_view
	walk.set_process(false)
	check(walk.light.basis.z.is_equal_approx(bridge.get_freedom_flight_state().star_direction), "Staged station light diverged from authoritative star")
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
	var seat_light: Basis = walk.light.basis
	shell.switch_journey_view()
	var flight: Control = shell.current_view
	check(flight is FlightView and flight.staged_model == model and flight.cockpit, "Seat did not hand the same model to cockpit flight")
	if not flight is FlightView: return
	flight.set_process(false)
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
	var continued_owner: Variant = ClassDB.instantiate("FreedomBridge")
	var continued_shell := Shell.new()
	continued_shell.presentation_only = true
	root.add_child(continued_shell)
	check(continued_shell.select_start(continued_owner, {"mode": "continue", "value": save_path, "assets": shell.assets_root}), "Seated Continue could not stage: " + continued_shell.error)
	if continued_shell.current_view != null:
		check(continued_shell.current_view is FlightView and continued_shell.current_view.cockpit and continued_shell.current_view.paused and continued_shell.current_view.staged_model.valid_current_pose(), "Seated Continue lost pose, cockpit or neutral pause")
	check(continued_owner.get_freedom_flight_state().resources == bridge.get_freedom_flight_state().resources, "Staged Continue changed exact resource readings")
	check(continued_owner.get_freedom_flight_state().chart == bridge.get_freedom_flight_state().chart, "Continue changed granted chart knowledge or invented visits")
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
	var return_light: Basis = flight.light.basis
	shell.switch_journey_view()
	walk = shell.current_view
	check(walk is WalkView and walk.staged_model == model, "Unboard did not restore walking on the same model")
	if not walk is WalkView: return
	check(walk.light.basis.is_equal_approx(return_light) and walk.light.basis.z.is_equal_approx(bridge.get_freedom_flight_state().star_direction), "Disembarking changed stellar direction at the view handoff")
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
