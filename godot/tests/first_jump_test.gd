extends SceneTree
## Actual bridge travel compared with independently ticked C++ save oracles.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
var failures := 0

func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func checkpoint(owner: Variant, path: String, expected: String) -> void:
	check(owner.save_freedom_as(path), "Native phase Save As failed")
	check(JSON.parse_string(FileAccess.get_file_as_string(path)) == JSON.parse_string(FileAccess.get_file_as_string(expected)), "Native bridge differs from independently ticked C++ phase: " + expected)

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 5: quit(1); return
	var directory := args[0].get_base_dir()
	var trace := directory.path_join("first-jump.json")
	var output: Array = []
	var code := OS.execute(directory.path_join("apsis-drift-freedom-start-fixture"), PackedStringArray([trace, "42", "240", "travel-trace"]), output, true)
	check(code == 0, "Independent physical travel trace failed: " + str(output))
	if code != 0: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	check(shell.select_start(owner, {"mode": "continue", "value": trace, "assets": directory.path_join("native-assets")}), "Travel Continue cannot stage native model: " + shell.error)
	if shell.current_view == null: shell.free(); quit(1); return
	var view: Control = shell.current_view
	view.set_process(false)
	view.controls_menu.hide_menu()
	check(view is FlightView and view.staged_model.valid_current_pose(), "Travel Continue lost authored Wayfarer")
	var start: Dictionary = owner.get_freedom_flight_state()
	for bad in ["", "system-0000000000000000", "SYSTEM-0000000000000000", "system-00000000000000000"]:
		check(not owner.select_freedom_jump(bad) and owner.get_freedom_flight_state() == start, "Invalid destination changed native owner")
	check(not owner.begin_freedom_jump(), "Unselected native jump committed")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for leg in 2:
		view.state = owner.get_freedom_flight_state()
		var ordinal := 1 if leg == 0 else 0
		view.select_jump_destination(ordinal)
		view.jump_requested()
		var state: Dictionary = owner.get_freedom_flight_state()
		check(state.jump.phase == "spool" and state.jump.remaining_seconds == 3.0, "Production UI did not start real spool")
		check("Spooling" in view.primary_status.text and not view.hud_scroll.visible, "Compact HUD lost the actual spool phase")
		check(not state.jump.has("point") and not state.jump.has("nominal_arrival") and not state.jump.has("reference_planet"), "Native preview leaked unresolved target/hidden sample")
		checkpoint(owner, directory.path_join("phase.json"), trace + ".leg%d.0.json" % leg)
		var old_node := MeshInstance3D.new()
		view.terrain.add_child(old_node)
		view.terrain.nodes["old-cover"] = old_node
		view.terrain.anchors["old-cover"] = PackedFloat64Array([0, 0, 0])
		view.terrain.resident_ids.append("old-cover")
		for t in range(1, 601):
			check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual native travel tick refused")
			if t in [1, 360, 361, 600]:
				state = owner.get_freedom_flight_state()
				check(FlightView.valid_state(state), "Current-world native projection invalid")
				check(owner.get_freedom_boarding_state().state == "seated" and owner.get_freedom_boarding_state().seated, "Jump lost actual seated pilot presentation")
				view.state = state
				view.update_view(0.0)
				check(("In transit" in view.primary_status.text) == (state.jump.phase == "transit"), "Compact HUD disagrees with committed transit")
				checkpoint(owner, directory.path_join("phase.json"), trace + ".leg%d.%d.json" % [leg, t])
				var restored: Variant = ClassDB.instantiate("FreedomBridge")
				check(restored.stage_freedom_continue(directory.path_join("phase.json")), "Native phase Continue could not stage")
				var pending: Dictionary = restored.get_pending_freedom_start()
				check(not pending.is_empty() and restored.commit_pending_freedom_start(pending.candidate_id), "Native phase Continue could not commit")
				check(restored.get_freedom_flight_state().position_metres == state.position_metres and restored.get_freedom_flight_state().system_id == state.system_id, "Native resumed phase changed actual pose/world")
				check(restored.get_freedom_craft_binding().operating_model_sha256 == owner.get_freedom_craft_binding().operating_model_sha256, "Continue lost authored craft binding")
				check(restored.get_freedom_boarding_state() == owner.get_freedom_boarding_state(), "Continue changed actual pilot/entry/seat projection")
		state = owner.get_freedom_flight_state()
		check(state.jump.phase == "idle" and state.resources.jump_charges == 2-leg, "Native leg did not install arrival/one-charge bill")
		check(not view.terrain.nodes.has("old-cover") and not old_node.visible, "Arrival reused old GPU terrain cover")
		check(state.chart.rows[ordinal].current and state.chart.rows[ordinal].confidence == "visited", "Chart did not reflect actual arrival")
		check(state.station_available == (leg == 1) and view.home_marker.visible == (leg == 1), "Neighbor retained home station cues or return lost them")
		if leg == 0:
			check(owner.get_freedom_station_geometry().is_empty() and not owner.select_freedom_port(1) and not owner.replenish_freedom_resources(), "Neighbor exposed phantom home station/service")
			# Exercise actual asset staging while outside home, not only the C++ loader.
			var neighbor_shell := Shell.new()
			neighbor_shell.presentation_only = true
			root.add_child(neighbor_shell)
			neighbor_shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			check(neighbor_shell.select_start(ClassDB.instantiate("FreedomBridge"), {"mode": "continue", "value": directory.path_join("phase.json"), "assets": directory.path_join("native-assets")}), "Neighbor Continue failed asset staging")
			var neighbor_view: Control = neighbor_shell.current_view
			neighbor_view.controls_menu.load_button.pressed.emit()
			neighbor_shell.load_dialog.file_selected.emit(trace + ".leg0.360.json")
			neighbor_shell.confirm_load()
			neighbor_view = neighbor_shell.current_view
			var loaded_owner: Variant = neighbor_shell.bridge
			check(neighbor_shell.load_origin == null and neighbor_view.paused and neighbor_view.state.jump.phase == "transit", "In-game Load lost committed jump phase/pause")
			checkpoint(loaded_owner, directory.path_join("loaded-jump.json"), trace + ".leg0.360.json")
			var loaded_state: Dictionary = loaded_owner.get_freedom_flight_state()
			neighbor_view._process(0.125)
			check(loaded_owner.get_freedom_flight_state() == loaded_state, "Paused loaded jump advanced under reference UI")
			neighbor_view.controls_menu.title_button.pressed.emit()
			neighbor_view.toggle_pause()
			neighbor_view._process(0.125)
			checkpoint(loaded_owner, directory.path_join("title-jump.json"), trace + ".leg0.360.json")
			check(neighbor_shell.title_origin == neighbor_view and neighbor_view.paused, "Committed jump escaped Title pause")
			neighbor_shell.title_confirmation.get_cancel_button().pressed.emit()
			check(neighbor_shell.current_view == neighbor_view and neighbor_view.paused, "Committed Title cancel lost current journey")
			neighbor_view.controls_menu.title_button.pressed.emit()
			neighbor_shell.confirm_title()
			check(neighbor_shell.current_view == null and neighbor_shell.bridge.get_freedom_start().is_empty(), "Committed Title did not discard only after confirmation")
			neighbor_shell.title_view.choose_continue(trace + ".leg0.360.json")
			await process_frame
			check(neighbor_shell.current_view != null and neighbor_shell.current_view.paused, "Committed Continue after Title failed")
			checkpoint(neighbor_shell.bridge, directory.path_join("title-continued-jump.json"), trace + ".leg0.360.json")
			neighbor_shell.free()
		await process_frame
	shell.free()
	await process_frame
	print("Native first jump: %d failures" % failures)
	quit(1 if failures else 0)
