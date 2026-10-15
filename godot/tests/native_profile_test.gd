extends SceneTree
## Real provider, staged native views and isolated storage; no player files.
class TestShell extends "res://scripts/native/native_start_shell.gd":
	func _ready() -> void: pass
var failures := 0
func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)
func _initialize() -> void: call_deferred("run")
func settle() -> void:
	for frame in 4: await process_frame
func snapshot(owner: Variant, path: String) -> String:
	check(owner.save_freedom_as(path), "Cannot snapshot authoritative bytes")
	return FileAccess.get_sha256(path)
func key(code: Key) -> void:
	for down in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.physical_keycode = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame
func pad(code: JoyButton, device := 0) -> void:
	for down in [true, false]:
		var event := InputEventJoypadButton.new()
		event.device = device
		event.button_index = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame
func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy": quit(1); return
	var data := OS.get_environment("XDG_DATA_HOME").path_join("profile-contract-%d" % OS.get_process_id())
	OS.set_environment("XDG_DATA_HOME", data)
	check(not DirAccess.dir_exists_absolute(data), "Profile fixture storage already exists")
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var empty: Dictionary = owner.list_freedom_profiles()
	check(empty.writable and empty.entries.is_empty() and empty.continue_index == -1 and not DirAccess.dir_exists_absolute(data), "Catalog scan created storage or chose an empty entry")
	var shell := TestShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	check(shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": args[0]}), "New Game did not stage")
	var view: Control = shell.current_view
	view.pause_controls("Profile contract paused")
	await settle()
	var initial: Dictionary = owner.get_freedom_walk_state()
	var raw := args[1].get_base_dir().path_join("profile-authority.json")
	var baseline := snapshot(owner, raw)
	check(owner.get_freedom_profile_state().dirty and not owner.get_freedom_profile_state().can_save and owner.get_freedom_profile_state().can_save_as, "New Game has incorrect unsaved metadata")
	view.available_pads[0] = true
	view.selected_pad = 0
	view.save_button.grab_focus()
	await key(KEY_ENTER)
	check(shell.save_confirmation.visible and view.process_mode == Node.PROCESS_MODE_DISABLED and owner.get_freedom_walk_state() == initial, "Save As did not open a paused nested confirmation")
	for size in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = size
		await settle()
		check(shell.save_confirmation.theme.default_font_size >= 20 and owner.get_freedom_walk_state() == initial, "Nested resize shrank text or advanced world")
	await pad(JOY_BUTTON_START)
	await settle()
	check(shell.save_origin == null and view.paused and not DirAccess.dir_exists_absolute(data) and snapshot(owner, raw) == baseline, "Canceled Save As created/adopted a slot or advanced world")
	view.save_button.grab_focus()
	await pad(JOY_BUTTON_A)
	check(shell.save_confirmation.visible, "Selected controller could not open catalog Save As")
	await pad(JOY_BUTTON_DPAD_RIGHT, 1)
	check(shell.save_confirmation.get_cancel_button().has_focus(), "Unselected controller changed Save As focus")
	await pad(JOY_BUTTON_DPAD_RIGHT)
	await pad(JOY_BUTTON_A)
	await settle()
	var clean: Dictionary = owner.get_freedom_profile_state()
	check(clean.id == "1" and clean.sequence == "1" and not clean.dirty and clean.can_save and view.paused and root.gui_get_focus_owner() == view.save_button and snapshot(owner, raw) == baseline, "Successful Save As did not adopt clean metadata or preserve world/focus")
	var directory := data.path_join("apsis-drift/profiles")
	var first := directory.path_join("profile-0000000000000001.json")
	var first_hash := FileAccess.get_sha256(first)
	view.save_button.pressed.emit()
	shell.save_confirmation.confirmed.emit()
	await settle()
	check(owner.get_freedom_profile_state().id == "2" and FileAccess.get_sha256(first) == first_hash, "Save As overwrote the former slot")
	var second := directory.path_join("profile-0000000000000002.json")
	var second_bytes := FileAccess.get_file_as_bytes(second)
	var output := FileAccess.open(second, FileAccess.WRITE)
	output.store_string("external change")
	output.close()
	var before_failure: Dictionary = owner.get_freedom_profile_state()
	view.replace_save_button.pressed.emit()
	check(owner.get_freedom_profile_state() == before_failure and "Save failed" in view.save_status.text and FileAccess.get_file_as_string(second) == "external change" and snapshot(owner, raw) == baseline, "Conflicted Save altered metadata, source or world")
	output = FileAccess.open(second, FileAccess.WRITE)
	output.store_buffer(second_bytes)
	output.close()
	view.replace_save_button.pressed.emit()
	check(owner.get_freedom_profile_state().sequence == "3" and not owner.get_freedom_profile_state().dirty and snapshot(owner, raw) == baseline, "Save failed to advance ordering without a world tick")
	check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, 0, initial.heading_radians])), "Dirty progress fixture refused")
	view.update_view()
	var progressed: Dictionary = owner.get_freedom_walk_state()
	var dirty_baseline := snapshot(owner, raw)
	check(owner.get_freedom_profile_state().dirty, "One authoritative tick did not mark dirty")
	shell.request_load()
	check(shell.load_browser.visible and view.process_mode == Node.PROCESS_MODE_DISABLED and owner.get_freedom_walk_state() == progressed, "Catalog Load did not remain paused")
	var browser_focus := root.gui_get_focus_owner()
	await pad(JOY_BUTTON_DPAD_DOWN, 1)
	await pad(JOY_BUTTON_A, 1)
	check(root.gui_get_focus_owner() == browser_focus and shell.load_browser.visible and not shell.load_confirmation.visible and owner.get_freedom_walk_state() == progressed, "Unselected controller navigated or activated the catalog")
	for size in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = size
		await settle()
		for button in shell.load_browser.buttons:
			if button.disabled: continue
			button.grab_focus()
			await settle()
			check(shell.load_browser.scroll.get_global_rect().grow(1.0).encloses(button.get_global_rect()), "Catalog row cannot scroll into view")
	await key(KEY_ESCAPE)
	check(shell.load_origin == null and view.paused and snapshot(owner, raw) == dirty_baseline, "Catalog Back resumed or altered dirty world")
	shell.request_load()
	shell.choose_load_profile(1)
	check(shell.load_confirmation.visible and owner.get_freedom_profile_state().dirty, "Dirty Load omitted discard confirmation")
	shell.load_confirmation.canceled.emit()
	await settle()
	check(shell.current_view == view and view.paused and snapshot(owner, raw) == dirty_baseline, "Load cancel changed session")
	shell.request_title()
	check(shell.title_confirmation.visible, "Dirty Title omitted confirmation")
	shell.title_confirmation.canceled.emit()
	await settle()
	check(shell.current_view == view and snapshot(owner, raw) == dirty_baseline, "Title cancel discarded progress")
	shell.request_load()
	shell.choose_load_profile(1)
	shell.load_confirmation.confirmed.emit()
	await settle()
	view = shell.current_view
	view.pause_controls("Loaded profile contract paused")
	check(owner.get_freedom_profile_state().id == "1" and not owner.get_freedom_profile_state().dirty and snapshot(owner, raw) == baseline, "Load failed exact world/slot replacement")
	shell.request_title()
	await settle()
	check(shell.title_view != null and shell.current_view == null and shell.title_view.catalog_continue_button.disabled == false, "Clean Title did not expose latest catalog Continue")
	var title: Control = shell.title_view
	# The real title catalog must keep the input provider's controller owner
	# when a replacement connects while the browser is already open.
	var catalog_bytes := FileAccess.get_file_as_bytes(second)
	title.controls.available_pads = {0: true, 1: true}
	title.controls.device = 0
	title.controls.install()
	title.open_catalog()
	await settle()
	var title_browser: Control = title.catalog_browser
	check(title_browser.visible and title_browser.buttons.size() == 2 and title_browser.buttons[0].has_focus(), "Title did not open the actual saved-journey catalog")
	await pad(JOY_BUTTON_DPAD_DOWN, 1)
	await pad(JOY_BUTTON_START, 1)
	check(title_browser.visible and title_browser.buttons[0].has_focus() and title.controls.device == 0, "Unselected controller took over the title catalog")
	title.controls.connection_changed(0, false)
	title.controls.connection_changed(2, true)
	await pad(JOY_BUTTON_DPAD_DOWN, 0)
	check(title_browser.visible and title_browser.buttons[0].has_focus(), "Disconnected controller navigated the title catalog")
	title_browser.buttons[0].grab_focus()
	await pad(JOY_BUTTON_DPAD_DOWN, 2)
	check(title_browser.buttons[1].has_focus() and title.controls.device == 2, "Newly connected controller could not navigate the open title catalog")
	var inactive_focus := root.gui_get_focus_owner()
	title._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	title_browser._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	await pad(JOY_BUTTON_B, 2)
	await pad(JOY_BUTTON_START, 2)
	check(title_browser.visible and root.gui_get_focus_owner() == inactive_focus, "Unfocused controller closed or navigated the catalog")
	title._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	title_browser._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	await pad(JOY_BUTTON_B, 2)
	check(not title_browser.visible and title.catalog_load_button.has_focus(), "Replacement controller could not leave the title catalog")
	if title_browser.visible: title.back_from_catalog()
	title.open_catalog()
	title.controls.connection_changed(2, false)
	# Device1 was already present: no new connection edge will select it.
	await pad(JOY_BUTTON_START, 1)
	check(title_browser.visible and title.controls.device == 1, "First backup Start did not select without leaving the catalog")
	await pad(JOY_BUTTON_DPAD_DOWN, 1)
	check(title_browser.buttons[1].has_focus(), "Already-connected backup could not navigate the catalog")
	await pad(JOY_BUTTON_START, 1)
	check(not title_browser.visible and title.catalog_load_button.has_focus(), "Selected backup Start could not return to title")
	check(shell.current_view == null and shell.bridge.get_freedom_start().is_empty() and shell.bridge.get_pending_freedom_start().is_empty(), "Controller-only catalog actions generated or selected a journey")
	check(FileAccess.get_sha256(first) == first_hash and FileAccess.get_file_as_bytes(second) == catalog_bytes and snapshot(owner, raw) == baseline, "Controller-only catalog actions changed a source save or retained authority")
	# Keep a failed ownership assertion from cascading into a null journey.
	if failures:
		shell.free()
		await settle()
		quit(1)
		return
	title.continue_catalog()
	await settle()
	var continued: Variant = shell.bridge
	view = shell.current_view
	view.pause_controls("Continued profile contract paused")
	check(continued.get_freedom_profile_state().id == "2", "Continue did not choose greatest durable save sequence")
	# File-path sources remain explicit and cannot enter catalog persistence.
	var explicit_owner: Variant = ClassDB.instantiate("FreedomBridge")
	check(explicit_owner.stage_freedom_continue(args[1]), "Explicit source did not stage")
	var pending: Dictionary = explicit_owner.get_pending_freedom_start()
	check(explicit_owner.commit_pending_freedom_start(pending.candidate_id), "Explicit source did not commit")
	var explicit_state: Dictionary = explicit_owner.get_freedom_profile_state()
	check(explicit_state.explicit_path and not explicit_state.can_save and not explicit_state.can_save_as and not explicit_owner.save_freedom_profile(true), "Explicit file source silently entered catalog")
	# The catalog persistence path above uses station walking. Exercise its
	# completion callback on a real FlightView too: the menu rebuilds its
	# footer from controls.status on subsequent frames.
	var flight_owner: Variant = ClassDB.instantiate("FreedomBridge")
	check(shell.select_start(flight_owner, {"mode": "continue", "value": args[1].get_base_dir().path_join("wayfarer-flight.json"), "assets": args[0]}), "Flight save-feedback view refused")
	view = shell.current_view
	view.pause_controls("Flight save-feedback contract")
	var flight_before := snapshot(flight_owner, raw)
	for notice in ["Saved. Resume explicitly after neutral.", "Save failed: declared storage conflict"]:
		shell.save_origin = view
		shell.save_previous_mode = view.process_mode
		shell.save_as_pending = true
		view.journey_dialog_open = true
		view.process_mode = Node.PROCESS_MODE_DISABLED
		shell.finish_catalog_save(notice)
		await settle()
		check(view.controls_menu.panel.visible and view.controls_menu.message.is_visible_in_tree() and notice in view.controls_menu.message.text and view.player_input.status == notice, "Flight save result disappeared on the next menu frame")
		check(view.paused and not view.journey_dialog_open and shell.save_origin == null and snapshot(flight_owner, raw) == flight_before, "Save feedback callback altered the flight or left its modal lock")
	flight_owner = null
	shell.free()
	owner = null
	continued = null
	explicit_owner = null
	await settle()
	print("Native profile contract: %d failures" % failures)
	quit(0 if failures == 0 else 1)
