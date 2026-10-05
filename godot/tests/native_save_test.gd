extends SceneTree
## Real C++ writes/reloads and inherited shell callbacks, in isolated test files.
## Dialog signals are exercised headlessly; this is not a visual/GPU review.

class SaveShell extends "res://scripts/native/native_start_shell.gd":
	func _ready() -> void:
		pass # Select explicit test inputs below, rather than launcher arguments.

func commit_fixture_new_game(owner: Variant, seed: String) -> bool:
	if not owner.stage_freedom_new_game(seed): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

var failures := 0


# This test owns C++ source fixtures only; renderer admission is exercised by
# native_start_staging, not by a direct fixture token commit.
func commit_fixture_continue(owner: Variant, path: String) -> bool:
	if not owner.stage_freedom_continue(path): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func refuse(bridge: Variant, path: String, expected: Dictionary) -> void:
	check(not bridge.save_freedom_as(path), "Invalid native Save As accepted")
	check(not str(bridge.get_last_error()).is_empty(), "Save refusal lacks diagnostic")
	check(bridge.get_freedom_start() == expected, "Save refusal changed native session")


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 6:
		push_error("Expected tick-zero, progressed, career, corrupt saves, snapshot and prepared assets")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the live C++ bridge first")
		quit(1)
		return
	var directory := args[0].get_base_dir()
	var destination := directory.path_join("native-saved-é.json")
	var original: Array[PackedByteArray] = []
	for path in args.slice(0, 4):
		original.append(FileAccess.get_file_as_bytes(path))
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	refuse(bridge, destination, {})
	check(not FileAccess.file_exists(destination), "Uninitialized bridge wrote a save")
	check(bridge.initialize(FileAccess.get_file_as_string(args[4])), "Study fixture refused")
	var study: Dictionary = bridge.get_state()
	refuse(bridge, destination, {})
	check(bridge.get_state() == study and not FileAccess.file_exists(destination), "Study Save As wrote a file or changed flight")
	check(bridge.initialize_freedom_continue(args[0]), "Historical station17 Continue refused")
	var fresh: Dictionary = bridge.get_freedom_start()
	check(bridge.save_freedom_as(destination), "Historical station17 Save As refused: " + str(bridge.get_last_error()))
	check(FileAccess.get_file_as_bytes(destination) == original[0], "Historical station17 save differs from C++ fixture bytes")
	check(bridge.get_freedom_start() == fresh, "Saving changed historical station selection")
	var reloaded: Variant = ClassDB.instantiate("FreedomBridge")
	check(reloaded.initialize_freedom_continue(destination), "Native-written save could not Continue")
	var expected := fresh.duplicate(true)
	expected.continued = true
	check(reloaded.get_freedom_start() == expected, "Native save/reload changed C++ start or station geometry")
	check(bridge.initialize_freedom_continue(args[1]), "Progressed Continue refused")
	var progressed: Dictionary = bridge.get_freedom_start()
	var saved_bytes := FileAccess.get_file_as_bytes(destination)
	# Godot replaces embedded NULs during String construction; the raw-path
	# rejection is exercised by the headless C++ contract instead.
	for path in ["", "relative.json", directory, directory + "/missing-parent/save.json", "/", "/" + "x".repeat(4096)]:
		refuse(bridge, path, progressed)
		check(FileAccess.get_file_as_bytes(destination) == saved_bytes, "Refusal damaged prior destination")
	check(bridge.save_freedom_as(destination), "Explicit progressed replacement refused")
	check(str(bridge.get_last_error()).is_empty(), "Successful save retained prior refusal")
	check(FileAccess.get_file_as_bytes(destination) == original[1], "Replacement lost saved clock, discoveries or deltas")
	check(bridge.get_freedom_start() == progressed, "Save As changed continued session")
	check(reloaded.initialize_freedom_continue(destination) and reloaded.get_freedom_start() == progressed, "Progressed native save failed exact station reload")

	# Build the production UI, then exercise its connected buttons/dialog signals.
	var shell := SaveShell.new()
	root.add_child(shell)
	shell.bridge = bridge
	shell.selected = progressed
	shell.assets_root = args[5]
	check(shell.build_view(shell.dock_geometry(progressed), bridge.get_freedom_station_geometry()), "Explicit historical station view refused")
	var ui_destination := directory.path_join("native-shell-saved.json")
	var initial_status: String = shell.save_status.text
	check(shell.save_dialog.file_mode == FileDialog.FILE_MODE_SAVE_FILE and shell.save_dialog.access == FileDialog.ACCESS_FILESYSTEM, "Save chooser lacks save-file/overwrite semantics")
	shell.save_button.pressed.emit()
	check(shell.save_dialog.visible, "Save As button did not open chooser")
	shell.save_dialog.get_cancel_button().pressed.emit()
	await process_frame
	check(not shell.save_dialog.visible and shell.save_status.text == initial_status and not FileAccess.file_exists(ui_destination), "Cancel wrote a file or changed status")
	check(bridge.get_freedom_start() == progressed, "Cancel changed native state")
	shell.save_dialog.file_selected.emit(directory + "/missing-parent/save.json")
	check(shell.save_status.text.begins_with("Save failed:") and not str(bridge.get_last_error()).is_empty(), "Shell hid save refusal")
	check(shell.is_inside_tree() and bridge.get_freedom_start() == progressed, "Save refusal closed or changed the session")
	shell.save_dialog.file_selected.emit(ui_destination)
	check(shell.save_status.text == "Saved to " + ui_destination and str(bridge.get_last_error()).is_empty(), "Shell failed to show successful retry")
	check(FileAccess.get_file_as_bytes(ui_destination) == original[1], "Shell wrote competing save state")
	check(reloaded.initialize_freedom_continue(ui_destination) and reloaded.get_freedom_start() == progressed, "Shell-written save failed Continue")

	# Full unsigned seed precision must survive the actual writer, too.
	check(commit_fixture_new_game(bridge, "18446744073709551615"), "Maximum uint64 seed refused")
	var extreme: Dictionary = bridge.get_freedom_walk_state()
	check(bridge.save_freedom_as(destination) and commit_fixture_continue(reloaded, destination), "Maximum-seed native save refused")
	extreme.continued = true
	check(not extreme.is_empty() and reloaded.get_freedom_walk_state() == extreme, "Maximum-seed actor save lost identity precision")
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == original[i], "Native save modified a source fixture")
	for filename in DirAccess.get_files_at(directory):
		check(not filename.contains(".tmp."), "Save refusal left a temporary file")
	shell.free()
	print("Native Save As: %d failures; real C++ writes/reloads and shell signals, no GPU qualification" % failures)
	quit(0 if failures == 0 else 1)
