extends SceneTree
## Explicit destruction fixtures after actual C++ neighboring-system travel.
## These prove continuation, not impact detection, wreck generation or rescue.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
var failures := 0

func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func checkpoint(owner: Variant, path: String, expected: String) -> void:
	check(owner.save_freedom_as(path), "Recovery Save As failed")
	check(FileAccess.get_file_as_string(path) == FileAccess.get_file_as_string(expected), "Recovery owner differs from independent C++ oracle: " + expected)

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 5: quit(1); return
	var directory := args[0].get_base_dir()
	var trace := directory.path_join("recovery.json")
	var output: Array = []
	var code := OS.execute(directory.path_join("apsis-drift-freedom-start-fixture"), PackedStringArray([trace, "42", "240", "recovery-trace"]), output, true)
	check(code == 0, "Independent C++ recovery trace failed: " + str(output))
	if code != 0: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"): quit(1); return
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for cause in 2:
		for fallback in 2:
			var prefix := trace + ".%d.%d" % [cause, fallback]
			var owner: Variant = ClassDB.instantiate("FreedomBridge")
			var shell := Shell.new()
			shell.presentation_only = true
			root.add_child(shell)
			check(shell.select_start(owner, {"mode": "continue", "value": prefix + ".pending.json", "assets": directory.path_join("native-assets")}), "Pending loss Continue failed: " + shell.error)
			if shell.current_view == null: shell.free(); quit(1); return
			var old_model: Node3D = shell.current_view.staged_model
			var pending: Dictionary = owner.get_freedom_recovery_state()
			var before: Dictionary = owner.get_freedom_flight_state()
			check(pending.pending and pending.recorded and shell.recovery_overlay != null and not shell.current_view.is_processing() and not shell.current_view.is_processing_input(), "Pending loss did not freeze view and expose explicit continuation")
			check(pending.cause == ("irrecoverable_destruction" if cause == 1 else "recoverable_destruction") and pending.explanation.contains("retained") and pending.explanation.contains("three jump charges") and (pending.explanation.contains("Safe fallback") == (fallback == 1)), "Cause, safe fallback and retained-state explanation differs")
			check(not before.station_available and before.resources.jump_charges == 2 and before.resources.quantity_quanta != before.resources.capacity_quanta, "Loss fixture did not retain actual neighbor/charge/propulsion history")
			check(not owner.advance_freedom_flight(1.0 / 120.0, neutral, false) and not owner.replenish_freedom_resources() and not owner.begin_freedom_jump() and owner.get_freedom_flight_state() == before, "Pending loss allowed flight, service or jump mutation")
			checkpoint(owner, directory.path_join("recovery-native.json"), prefix + ".pending.json")
			# Model-readiness failure must retain the pending source and its scene.
			check(not shell.select_start(owner, {"mode": "recovery", "assets": directory.path_join("missing-assets")}) and owner.get_freedom_recovery_state() == pending and owner.get_pending_freedom_start().is_empty() and is_instance_valid(old_model), "Failed replacement staging consumed the loss or retired view")
			check(shell.select_start(owner, {"mode": "recovery", "assets": directory.path_join("native-assets")}), "Replacement could not stage and commit: " + shell.error)
			if not shell.current_view is WalkView: shell.free(); quit(1); return
			shell.current_view.set_process(false)
			var complete: Dictionary = owner.get_freedom_recovery_state()
			var fresh: Dictionary = owner.get_freedom_flight_state()
			check(not complete.pending and complete.recorded and complete.retired_craft_id == before.craft_id and complete.craft_id != before.craft_id and shell.recovery_overlay == null, "Replacement did not retire exactly one individual craft")
			check(fresh.station_available and fresh.tick == before.tick and fresh.resources.jump_charges == 3 and fresh.resources.quantity_quanta == fresh.resources.capacity_quanta and not owner.get_freedom_walk_state().is_empty() and shell.current_view.paused, "Replacement lost real station/walker/full baseline or changed clock")
			check(not is_instance_valid(old_model) and shell.current_view.staged_model.valid_current_pose(), "Replacement reused the retired scene or lost authored skin readiness")
			checkpoint(owner, directory.path_join("recovery-native.json"), prefix + ".complete.json")
			check(not owner.stage_freedom_recovery() and owner.get_freedom_flight_state() == fresh, "Repeated UI recovery could grant another craft or refill")
			for t in 20:
				check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([1, 0, 0])), "Actual replacement station walking refused")
			checkpoint(owner, directory.path_join("recovery-native.json"), prefix + ".walk.json")
			var rows: Array = owner.get_freedom_flight_state().chart.rows
			check(rows.size() == 2 and rows[1].confidence == "visited", "Replacement erased actual neighboring-system visit")
			check(owner.select_freedom_jump(rows[1].system_id), "Replacement cannot select the known neighbor")
			checkpoint(owner, directory.path_join("recovery-native.json"), prefix + ".selected.json")
			var resumed: Variant = ClassDB.instantiate("FreedomBridge")
			check(resumed.stage_freedom_continue(directory.path_join("recovery-native.json")), "Completed recovery/new selection Continue failed")
			var staged: Dictionary = resumed.get_pending_freedom_start()
			check(not staged.is_empty() and resumed.commit_pending_freedom_start(staged.candidate_id) and resumed.get_freedom_recovery_state() == owner.get_freedom_recovery_state() and resumed.get_freedom_flight_state() == owner.get_freedom_flight_state(), "Recovery owner or travel binding changed on Continue")
			if cause == 0 and fallback == 0:
				for t in 2960:
					check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, -1, 0])), "Replacement entry approach refused")
				check(owner.begin_freedom_boarding(), "Replacement cannot board its own fresh vessel")
				for t in 1440:
					check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, 0, 0])), "Replacement pilot route refused")
				check(owner.release_freedom_port(), "Replacement cannot release actual D1")
				var withdrawal := neutral.duplicate()
				withdrawal[4] = 1.0
				for t in 720:
					check(owner.advance_freedom_flight(1.0 / 120.0, withdrawal, false), "Replacement actual withdrawal refused")
				check(owner.begin_freedom_jump(), "Replacement's current craft identity cannot start a real jump")
				checkpoint(owner, directory.path_join("recovery-native.json"), prefix + ".next-spool.json")
				for t in range(1, 601):
					check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Replacement jump/sensor tick refused")
					if t in [360, 600]:
						checkpoint(owner, directory.path_join("recovery-native.json"), prefix + (".next-commit.json" if t == 360 else ".next-arrival.json"))
				var onward: Dictionary = owner.get_freedom_flight_state()
				check(not onward.station_available and onward.resources.jump_charges == 2 and onward.craft_id == fresh.craft_id and not owner.get_freedom_recovery_state().pending, "Replacement did not reach real neighbor with one new charge bill")
			shell.free()
			await process_frame
	print("Native recovery: %d failures" % failures)
	quit(1 if failures else 0)
