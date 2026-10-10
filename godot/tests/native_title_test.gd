extends SceneTree
## Ordinary native title through the real C++ staging seam; no second world.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const Title = preload("res://scripts/native/native_title.gd")
const Walk = preload("res://scripts/native/native_walk_view.gd")

class TestShell extends "res://scripts/native/native_start_shell.gd":
	func _ready() -> void: pass

var failures := 0

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	call_deferred("run")

func settle() -> void:
	for frame in 4: await process_frame

func key(code: Key) -> void:
	for down in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.physical_keycode = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame

func pad(code: JoyButton) -> void:
	for down in [true, false]:
		var event := InputEventJoypadButton.new()
		event.device = 0
		event.button_index = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy": quit(1); return
	for value in ["", "-1", "+1", "01", "00", " 42", "42 ", "4.2", "18446744073709551616", "1".repeat(21), "１２", "NaN"]:
		check(not Title.valid_seed(value), "Invalid title seed accepted: " + value)
	for value in ["0", "42", "9223372036854775808", "18446744073709551615"]:
		check(Title.valid_seed(value), "Valid unsigned seed refused: " + value)
	var shell := TestShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	check(shell.parse_selection(PackedStringArray()).get("mode") == "title", "No-argument startup did not select title")
	check(shell.parse_selection(PackedStringArray(["--assets=" + args[0]])).get("mode") == "title", "Prepared startup did not select title")
	for bad in [["--validate-only"], ["--unknown"], ["--new-game="], ["--continue="], ["--new-game=42", "--continue=" + args[1]], ["--assets=relative"]]:
		check(shell.parse_selection(PackedStringArray(bad)).is_empty(), "Malformed CLI selection accepted")
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"): shell.free(); quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	shell.open_title(owner, {"mode": "title", "assets": args[0], "persist_controls": false})
	var title: Control = shell.title_view
	title.controls.device = 0
	title.controls.install()
	await settle()
	check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty() and shell.current_view == null, "Title generated a world before selection")
	check(root.gui_get_focus_owner() == title.new_button, "Title did not focus New Game")
	title.seed.text = "-1"
	title.new_button.pressed.emit()
	await settle()
	check(not title.busy and root.gui_get_focus_owner() == title.seed and owner.get_freedom_start().is_empty(), "Rejected seed opened a world or lost entry focus")
	title.reference_button.grab_focus()
	await key(KEY_ENTER)
	check(title.reference.visible and not title.panel.visible and "NO JOURNEY RUNNING" in title.reference._paused.text, "Reference claims a live paused flight or did not open")
	check(owner.get_freedom_start().is_empty(), "Reference created a world")
	for size in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = size
		await settle()
		for control in [title.reference.heading, title.reference.scroll, title.reference.back_button]:
			check(root.get_visible_rect().encloses(control.get_global_rect()), "Title reference clipped at " + str(size))
	await key(KEY_ESCAPE)
	check(not title.reference.visible and root.gui_get_focus_owner() == title.reference_button, "Reference Back lost title focus")
	for size in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = size
		await settle()
		for control in [title.seed, title.new_button, title.continue_button, title.reference_button, title.quit_button]:
			control.grab_focus()
			await settle()
			check(title.scroll.get_global_rect().encloses(control.get_global_rect()), "Title focused action cannot scroll into view: " + str(size))
		check(absf(title.seed.get_theme_font_size("font_size") / title.readable_scale - 20) < 1, "Title shrank physical input text")
	title.continue_button.pressed.emit()
	check(title.chooser.visible, "Continue did not open file chooser")
	title.chooser.get_cancel_button().pressed.emit()
	await settle()
	check(not title.chooser.visible and shell.title_view == title and root.gui_get_focus_owner() == title.continue_button and owner.get_freedom_start().is_empty(), "Continue cancel opened a world or lost title")
	title.new_button.grab_focus()
	await pad(JOY_BUTTON_DPAD_DOWN)
	check(root.gui_get_focus_owner() == title.continue_button, "Controller navigation did not select Continue")
	await pad(JOY_BUTTON_A)
	check(title.chooser.visible, "Controller accept could not open Continue")
	await key(KEY_ESCAPE)
	await settle()
	check(not title.chooser.visible and owner.get_freedom_start().is_empty(), "Escape could not cancel the exclusive chooser")
	var corrupt := FileAccess.get_file_as_bytes(args[2])
	for path in ["relative.json", args[2], args[2].get_base_dir().path_join("missing.json")]:
		title.choose_continue(path)
		await settle()
		check(shell.title_view == title and not title.busy and shell.current_view == null and not title.status.text.is_empty(), "Failed Continue lost title")
		check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Failed Continue left C++ state")
		check(FileAccess.get_file_as_bytes(args[2]) == corrupt, "Continue refusal rewrote source")
	title.focused = false
	title.seed.text = "0"
	title.begin_new()
	check(not title.busy and owner.get_freedom_start().is_empty(), "Unfocused New Game accepted")
	title.focused = true
	title.begin_new()
	title.focused = false
	await settle()
	check(not title.busy and shell.title_view == title and owner.get_freedom_start().is_empty(), "Focus lost before deferred selection opened a journey")
	title.focused = true
	var assets: String = shell.title_options.assets
	shell.title_options.assets = ""
	title.begin_new()
	await settle()
	check(shell.title_view == title and not title.busy and owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Late asset refusal committed a title candidate")
	shell.title_options.assets = assets
	title.new_button.grab_focus()
	await key(KEY_ENTER)
	await settle()
	check(shell.title_view == null and shell.current_view is Walk and owner.get_freedom_walk_state().universe_seed == "0", "Actual seed-zero New Game did not start on station")
	check(shell.current_view.state.actor_id == "1", "New Game did not create the player actor")
	shell.free()
	await settle()
	# A fresh title selects an existing actor save through its connected dialog.
	owner = ClassDB.instantiate("FreedomBridge")
	shell = TestShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shell.open_title(owner, {"mode": "title", "assets": args[0], "persist_controls": false})
	title = shell.title_view
	var source := FileAccess.get_file_as_bytes(args[1])
	title.continue_button.pressed.emit()
	title.chooser.file_selected.emit(args[1])
	await settle()
	check(shell.title_view == null and shell.current_view is Walk and shell.current_view.paused, "Continue did not retire title into paused actor")
	var output := args[1].get_base_dir().path_join("title-continued.json")
	check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Title Continue changed complete source save")
	check(FileAccess.get_file_as_bytes(args[1]) == source, "Successful title Continue rewrote input")
	shell.free()
	await settle()
	print("Native title: %d failures" % failures)
	quit(1 if failures else 0)
