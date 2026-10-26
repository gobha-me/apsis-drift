extends SceneTree
## Ordinary native title through the real C++ staging seam; no second world.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const Title = preload("res://scripts/native/native_title.gd")
const Walk = preload("res://scripts/native/native_walk_view.gd")

class TestShell extends "res://scripts/native/native_start_shell.gd":
	var loading_stages: Array[Dictionary] = []
	func _ready() -> void: pass
	func select_start(owner: Variant, options: Dictionary) -> bool:
		if title_view != null:
			var title: Control = title_view
			var bounds: Rect2 = title.loading_overlay.get_global_rect()
			var density: Vector2 = get_viewport().get_final_transform().get_scale().abs()
			loading_stages.append({
				"mode": options.mode,
				"frame_opportunities": Engine.get_process_frames() - title.loading_started_frame,
				"visible": title.busy and title.loading_overlay.is_visible_in_tree(),
				"enclosed": title.get_global_rect().encloses(bounds) and bounds.encloses(title.loading_heading.get_global_rect()) and bounds.encloses(title.loading_status.get_global_rect()),
				"readable": title.loading_status.get_theme_font_size("font_size") * minf(density.x, density.y) >= 18.0 and title.loading_status.get_visible_line_count() == title.loading_status.get_line_count(),
				"message": title.loading_heading.text + "\n" + title.loading_status.text,
				"barrier": not title.controls.is_processing_input() and title.new_button.disabled and title.continue_button.disabled and title.network_choice.disabled,
				"unselected": owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(),
				"frontend_token_removed": not options.has("_title_request")})
		return super.select_start(owner, options)

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

func pad(code: JoyButton, device: int = 0) -> void:
	for down in [true, false]:
		var event := InputEventJoypadButton.new()
		event.device = device
		event.button_index = code
		event.pressed = down
		Input.parse_input_event(event)
		await process_frame

func held_stick(value: float, device: int = 41) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = device
	event.axis = JOY_AXIS_LEFT_X
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check_help_bounds(title: Control, choice: OptionButton, help: Label, pixels: Vector2i) -> void:
	var bounds: Rect2 = title.scroll.get_global_rect()
	var detail := "window=%s logical=%s choice=%s scroll=%s selector=%s help=%s" % [pixels, title.size, choice.get_item_text(choice.selected), bounds, choice.get_global_rect(), help.get_global_rect()]
	check(choice.has_focus() and not choice.get_popup().visible, "Choice help stole focus or opened its popup: " + detail)
	check(bounds.encloses(choice.get_global_rect()) and bounds.encloses(help.get_global_rect()), "Choice and visible help do not fit together: " + detail)
	check(help.get_visible_line_count() == help.get_line_count(), "Choice help clipped wrapped text: " + detail)
	check(help.get_theme_font_size("font_size") * float(pixels.x) / title.size.x >= 18.0, "Choice help shrank physical text: " + detail)
	check(help.focus_mode == Control.FOCUS_NONE and help.mouse_filter == Control.MOUSE_FILTER_IGNORE, "Choice help became an input control")

func check_choice_help(title: Control, owner: Variant) -> void:
	var original_size := root.size
	var original_status: String = title.status.text
	check("Home and Nearby" in title.chart_help.text and "no recovery cycle" in title.drive_help.text, "Default choice explanations were not initialized")
	for pixels in [Vector2i(640, 450), Vector2i(1280, 720)]:
		root.size = pixels
		await settle()
		for index in 5:
			title.network_choice.grab_focus()
			title.network_choice.select(index)
			title.network_choice.item_selected.emit(index)
			await settle()
			check_help_bounds(title, title.network_choice, title.chart_help, pixels)
			check(("No remote planetary charts or commission office" in title.chart_help.text) == (index < 2), "Chart help misstated office availability")
			check(("commissions are available at Origin" in title.chart_help.text) == (index >= 2), "Planetary/search profile lost optional work explanation")
			check("not surface findings" in title.chart_help.text and "separate enrollment" in title.chart_help.text, "Chart help confused navigation, surface knowledge or paid enrollment")
			check((not title.derelict_recovery_choice.disabled) == (index in [3, 4]), "Reading chart help changed canister eligibility")
		for index in 3:
			title.recovery_choice.grab_focus()
			title.recovery_choice.select(index)
			title.recovery_choice.item_selected.emit(index)
			await settle()
			check_help_bounds(title, title.recovery_choice, title.drive_help, pixels)
			check("one charge" in title.drive_help.text and "Pause freezes recovery" in title.drive_help.text, "Drive help changed charge or pause rules")
			check(("early jumps" in title.drive_help.text) == (index > 0) and ("45–90" in title.drive_help.text) == (index == 2), "Drive help misstated recovery/strain selection")
		check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty() and not title.busy and title.status.text == original_status, "Reading choices created a journey or overwrote title status")
	# Real popup keyboard and selected-controller paths still own selection.
	title.network_choice.select(0)
	title.network_choice.item_selected.emit(0)
	title.network_choice.grab_focus()
	await settle()

	await key(KEY_ENTER)
	check(title.network_choice.get_popup().visible, "Keyboard could not open the chart popup")
	await key(KEY_DOWN)
	await key(KEY_ENTER)
	await settle()
	check(title.network_choice.selected == 1 and "Four system fixes" in title.chart_help.text, "Keyboard popup did not refresh chart help")
	check_help_bounds(title, title.network_choice, title.chart_help, root.size)
	title.recovery_choice.select(0)
	title.recovery_choice.item_selected.emit(0)
	title.recovery_choice.grab_focus()
	await settle()
	await pad(JOY_BUTTON_A)
	check(title.recovery_choice.get_popup().visible, "Selected controller could not open the drive popup")
	await pad(JOY_BUTTON_DPAD_DOWN)
	await pad(JOY_BUTTON_A)
	await settle()
	check(title.recovery_choice.selected == 1 and "45 seconds" in title.drive_help.text, "Selected controller popup did not refresh drive help")
	check_help_bounds(title, title.recovery_choice, title.drive_help, root.size)
	check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty() and not title.busy and title.status.text == original_status, "Popup choices committed a journey or overwrote status")
	title.network_choice.select(0)
	title.network_choice.item_selected.emit(0)
	title.recovery_choice.select(0)
	title.recovery_choice.item_selected.emit(0)
	root.size = original_size
	title.new_button.grab_focus()
	await settle()

func check_loading_now(title: Control, owner: Variant) -> void:
	check(title.busy and title.loading_overlay.is_visible_in_tree() and root.gui_get_focus_owner() == title.loading_overlay and not title.controls.is_processing_input(), "Accepted request did not immediately acknowledge and isolate loading")
	check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Loading acknowledgement already generated a C++ journey")
	var generation: int = title.request_generation
	title.begin_new()
	title.choose_continue("/not-a-second-selection.json")
	title.choose_catalog(0)
	title.back_to_title()
	title.chooser.canceled.emit()
	for down in [true, false]:
		var event := InputEventKey.new()
		event.keycode = KEY_ENTER
		event.physical_keycode = KEY_ENTER
		event.pressed = down
		Input.parse_input_event(event)
	Input.flush_buffered_events()
	check(title.request_generation == generation and title.busy and not title.controls.is_processing_input() and not title.chooser.visible and owner.get_freedom_start().is_empty(), "Duplicate/cancel/input callbacks bypassed the acknowledged request")

func check_loading_stages(shell: Control) -> void:
	check(not shell.loading_stages.is_empty(), "Actual title staging produced no acknowledgement witness")
	for value in shell.loading_stages:
		var row: Dictionary = value
		check(row.frame_opportunities >= 2 and row.visible and row.enclosed and row.readable and row.barrier and row.unselected and row.frontend_token_removed, "Actual native work began before a readable isolated acknowledgement: " + str(row))
		check(("Starting New Game" in row.message) == (row.mode == "new_game") and ("Opening saved journey" in row.message) == (row.mode in ["continue", "profile"]), "Loading label misrepresented the actual request")

func check_catalog_loading(assets: String, expected: PackedByteArray, output: String) -> void:
	for browse in [false, true]:
		var owner: Variant = ClassDB.instantiate("FreedomBridge")
		var shell := TestShell.new()
		root.add_child(shell)
		shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		shell.open_title(owner, {"mode": "title", "assets": assets, "persist_controls": false})
		var title: Control = shell.title_view
		await settle()
		check(not title.catalog_continue_button.disabled, "Actual saved profile was absent from title Continue")
		if title.catalog_continue_button.disabled:
			shell.free()
			await settle()
			continue
		if browse:
			title.open_catalog()
			check(title.catalog_browser.visible and not title.catalog_browser.buttons.is_empty(), "Actual catalog browser did not expose its saved row")
			if title.catalog_browser.buttons.is_empty():
				shell.free()
				await settle()
				continue
			title.catalog_browser.buttons[0].pressed.emit()
		else:
			title.catalog_continue_button.pressed.emit()
		check_loading_now(title, owner)
		await settle()
		check_loading_stages(shell)
		var witness: String = output.path_join("title-catalog-browser.json" if browse else "title-catalog-continue.json")
		check(shell.title_view == null and shell.current_view is Walk and shell.current_view.paused and owner.save_freedom_as(witness) and FileAccess.get_file_as_bytes(witness) == expected, "Catalog loading acknowledgement changed the committed journey or failed to retire")
		shell.free()
		await settle()

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy": quit(1); return
	var catalog_data: String = OS.get_environment("XDG_DATA_HOME").path_join("title-loading-%d" % OS.get_process_id())
	check(catalog_data.is_absolute_path() and not DirAccess.dir_exists_absolute(catalog_data), "Title catalog test storage was not fresh and isolated")
	if not catalog_data.is_absolute_path() or DirAccess.dir_exists_absolute(catalog_data): quit(1); return
	OS.set_environment("XDG_DATA_HOME", catalog_data)
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
	check(title.network_choice != null and title.network_choice.selected == 0 and title.network_choice.item_count == 5 and title.network_choice.focus_mode == Control.FOCUS_ALL, "Optional chart packages changed default New Game or lost keyboard access")
	check(title.recovery_choice != null and title.recovery_choice.selected == 0 and title.recovery_choice.item_count == 3 and title.recovery_choice.focus_mode == Control.FOCUS_ALL, "Drive rules changed the default or lost independent keyboard selection")
	check(title.origin_service_choice != null and not title.origin_service_choice.button_pressed and title.origin_service_choice.focus_mode == Control.FOCUS_ALL, "Fuel service must remain explicit opt-in with keyboard/controller access")
	await check_choice_help(title, owner)
	# A deferred selection belongs to this title, not a later replacement.
	title.begin_new()
	check_loading_now(title, owner)
	var retired: Control = title
	shell.remove_child(retired)
	shell.open_title(owner, {"mode": "title", "assets": args[0], "persist_controls": false})
	title = shell.title_view
	retired.free()
	title.controls.device = 0
	title.controls.install()
	await settle()
	check(not title.busy and not title.loading_overlay.visible and shell.loading_stages.is_empty() and owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Retired title request opened a world or canceled its replacement")
	title.open_settings()
	var settings: Control = title.settings_view
	title.begin_new()
	check(settings.visible and owner.get_freedom_start().is_empty() and not title.busy, "Settings leaked a New Game selection")
	settings.fields.deadzone.control.value = 0.35
	settings.fields.curve.control.value = 2.0
	settings.fields.look_speed.control.value = 2.4
	settings.fields.invert_look.control.button_pressed = true
	# This run owns its isolated user directory, so the next real walker can
	# consume the same ordinary persisted provider rather than a forged copy.
	title.controls.persist = true
	check(not FileAccess.file_exists(title.controls.settings_path), "Isolated title preferences unexpectedly exist")
	settings.apply_pending()
	var preferences_path: String = title.controls.settings_path
	var preferences_bytes := FileAccess.get_file_as_bytes(preferences_path)
	check(not preferences_bytes.is_empty() and owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Settings Apply created a world or failed persistence")
	settings.cancel()
	check(title.settings_button.has_focus(), "Settings did not restore its title action")
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
		for control in [title.seed, title.network_choice, title.recovery_choice, title.origin_service_choice, title.new_button, title.continue_button, title.reference_button, title.settings_button, title.quit_button]:
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
	var refused_paths: Array[String] = ["relative.json", args[2], args[2].get_base_dir().path_join("missing.json")]
	for index in refused_paths.size():
		root.size = Vector2i(640, 450) if index < 2 else Vector2i(1280, 720)
		await settle()
		var path: String = refused_paths[index]
		title.choose_continue(path)
		if path.is_absolute_path(): check_loading_now(title, owner)
		await settle()
		check(shell.title_view == title and not title.busy and not title.loading_overlay.visible and title.controls.is_processing_input() and not title.new_button.disabled and shell.current_view == null and not title.status.text.is_empty(), "Failed Continue lost usable title or retained loading/input locks")
		check(title.scroll.get_global_rect().encloses(title.status.get_global_rect()), "Continue refusal was not readable after loading closed")
		check(owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Failed Continue left C++ state")
		check(FileAccess.get_file_as_bytes(args[2]) == corrupt, "Continue refusal rewrote source")
	title.focused = false
	title.seed.text = "0"
	title.begin_new()
	check(not title.busy and owner.get_freedom_start().is_empty(), "Unfocused New Game accepted")
	title.focused = true
	var stages_before: int = shell.loading_stages.size()
	title.begin_new()
	check_loading_now(title, owner)
	title.focused = false
	await settle()
	check(not title.busy and not title.loading_overlay.visible and shell.title_view == title and shell.loading_stages.size() == stages_before and owner.get_freedom_start().is_empty(), "Focus lost before deferred selection opened a journey")
	title.focused = true
	title.begin_new()
	check_loading_now(title, owner)
	title._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	title._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	await settle()
	check(not title.busy and shell.loading_stages.size() == stages_before and owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Restored focus revived an earlier canceled selection")
	title.begin_new()
	check_loading_now(title, owner)
	shell.closing = true
	await settle()
	check(not title.busy and not title.loading_overlay.visible and shell.loading_stages.size() == stages_before and owner.get_freedom_start().is_empty(), "Closing shell still opened an acknowledged journey")
	shell.closing = false
	var assets: String = shell.title_options.assets
	shell.title_options.assets = ""
	var requests: Array[Dictionary] = []
	var record_request: Callable = func(value: Dictionary): requests.append(value.duplicate(true))
	title.start_requested.connect(record_request)
	title.begin_new()
	check_loading_now(title, owner)
	var duplicate: Dictionary = requests.back().duplicate(true)
	title.start_requested.emit(duplicate)
	await settle()
	title.start_requested.disconnect(record_request)
	check(shell.title_view == title and not title.busy and not title.loading_overlay.visible and shell.loading_stages.size() == stages_before + 1 and owner.get_freedom_start().is_empty() and owner.get_pending_freedom_start().is_empty(), "Duplicate/late asset refusal committed a title candidate or repeated native staging")
	shell.title_options.assets = assets
	# Two available software pads; explicitly adopt the non-first device through
	# the real Start path. This is input ownership, not hardware qualification.
	title.controls.available_pads = {40: true, 41: true}
	title.controls.device = -1
	title.controls.install()
	await pad(JOY_BUTTON_START, 41)
	check(title.controls.device == 41, "Title did not adopt the selected non-first controller")
	title.recovery_choice.select(2)
	check(owner.get_freedom_start().is_empty(), "Choosing strained drive generated a world before New Game")
	held_stick(0.8)
	title.new_button.grab_focus()
	await key(KEY_ENTER)
	await settle()
	check(shell.title_view == null and shell.current_view is Walk and owner.get_freedom_walk_state().universe_seed == "0", "Actual seed-zero New Game did not start on station")
	check_loading_stages(shell)
	check(shell.current_view.state.actor_id == "1", "New Game did not create the player actor")
	var title_drive: Dictionary = owner.get_freedom_flight_state().jump.recovery
	check(title_drive.version == 2 and title_drive.ready and title_drive.next_recovery_seconds == 45.0, "Actual title selection did not start with the selected strained drive")
	var started: Control = shell.current_view
	started.set_process(false)
	check(not started.preferences_provider.is_processing_input() and not started.observations_view.is_processing_input(), "Staged preference/reader children bypassed station input ownership on tree entry")
	check(started.selected_pad == 41 and started.paused and not started.controls_armed, "New Game lost title controller ownership or resumed its held stick")
	var ownership_save := args[1].get_base_dir().path_join("title-controller-newgame.json")
	check(owner.save_freedom_as(ownership_save), "Could not save title controller baseline")
	var ownership_bytes := FileAccess.get_file_as_bytes(ownership_save)
	started.available_pads[40] = true
	var started_focus := root.gui_get_focus_owner()
	await pad(JOY_BUTTON_START, 40)
	await pad(JOY_BUTTON_A, 40)
	check(started.selected_pad == 41 and started.paused and root.gui_get_focus_owner() == started_focus, "Nonowner controller took New Game walking/menu authority")
	started.resume_requested()
	check(started.paused and not started.controls_armed, "Selected held stick bypassed fresh New Game neutral gate")
	held_stick(0.0)
	check(started.paused, "Releasing the selected stick automatically resumed New Game")
	await pad(JOY_BUTTON_START, 41)
	check(not started.paused and started.controls_armed, "Selected controller could not explicitly resume after neutral")
	started.pause_controls("Controller handoff inspection complete")
	check(owner.save_freedom_as(ownership_save) and FileAccess.get_file_as_bytes(ownership_save) == ownership_bytes, "Controller handoff checks changed complete New Game save bytes")
	check(is_equal_approx(shell.current_view.stick_preferences.deadzone, 0.35) and shell.current_view.stick_preferences.curve == 2.0, "New Game walker did not consume saved title preferences")
	check(FileAccess.get_file_as_bytes(preferences_path) == preferences_bytes, "Starting the journey rewrote preferences")
	check(DirAccess.remove_absolute(preferences_path) == OK, "Could not remove title-owned preferences")
	check(owner.save_freedom_profile(true) and owner.save_freedom_as(ownership_save) and FileAccess.get_file_as_bytes(ownership_save) == ownership_bytes, "Actual catalog enrollment changed the new journey bytes")
	shell.free()
	await settle()
	await check_catalog_loading(args[0], ownership_bytes, args[1].get_base_dir())
	# A fresh title selects an existing actor save through its connected dialog.
	owner = ClassDB.instantiate("FreedomBridge")
	shell = TestShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shell.open_title(owner, {"mode": "title", "assets": args[0], "persist_controls": false})
	title = shell.title_view
	title.controls.available_pads = {40: true, 41: true}
	title.controls.device = -1
	title.controls.install()
	await pad(JOY_BUTTON_START, 41)
	var source := FileAccess.get_file_as_bytes(args[1])
	title.continue_button.pressed.emit()
	title.chooser.file_selected.emit(args[1])
	check_loading_now(title, owner)
	await settle()
	check_loading_stages(shell)
	check(shell.title_view == null and shell.current_view is Walk and shell.current_view.paused, "Continue did not retire title into paused actor")
	check(shell.current_view.selected_pad == 41, "Continue lost the selected title controller")
	var output := args[1].get_base_dir().path_join("title-continued.json")
	check(owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Title Continue changed complete source save")
	check(FileAccess.get_file_as_bytes(args[1]) == source, "Successful title Continue rewrote input")
	var view: Control = shell.current_view
	view.available_pads[40] = true
	var continued_focus := root.gui_get_focus_owner()
	await pad(JOY_BUTTON_START, 40)
	await pad(JOY_BUTTON_A, 40)
	check(view.paused and view.selected_pad == 41 and root.gui_get_focus_owner() == continued_focus and owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Foreign controller escaped paused Continue or changed its complete save")
	view.title_button.pressed.emit()
	check(shell.title_origin == view and view.journey_dialog_open and view.process_mode == Node.PROCESS_MODE_DISABLED and shell.title_confirmation.visible, "Title did not suspend the journey beneath confirmation")
	check(shell.title_confirmation.get_cancel_button().has_focus(), "Title did not default to Cancel")
	view.resume_requested()
	view.load_button.pressed.emit()
	view._process(0.125)
	check(view.paused and shell.load_origin == null and owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Title confirmation resumed/changed world or opened a second dialog")
	for pixels in [Vector2i(1280, 720), Vector2i(800, 450)]:
		root.size = pixels
		await settle()
		shell.layout_title_confirmation()
		await settle()
		var font: float = shell.title_confirmation.get_label().get_theme_font_size("font_size") * float(pixels.x) / view.size.x
		check(font >= 17.5 and font <= 20.5 and shell.title_confirmation.size.x <= view.size.x and shell.title_confirmation.size.y <= view.size.y, "Title confirmation lost readable bounded text")
	shell.title_confirmation.get_cancel_button().pressed.emit()
	check(shell.current_view == view and not view.journey_dialog_open and view.paused and root.gui_get_focus_owner() == view.title_button and owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Title Cancel lost paused action or save bytes")
	view.title_button.pressed.emit()
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	shell.confirm_title()
	check(shell.current_view == view and shell.title_origin == null and view.paused and owner.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Focus loss authorized Title discard")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.title_button.pressed.emit()
	shell.title_confirmation.get_ok_button().pressed.emit()
	await settle()
	check(shell.title_view != null and shell.current_view == null and shell.title_origin == null and not is_instance_valid(view) and shell.bridge != owner, "Confirmed Title retained old active owner/view")
	check(shell.bridge.get_freedom_start().is_empty() and shell.bridge.get_pending_freedom_start().is_empty(), "Return to Title generated a new universe")
	check(shell.title_view.controls.device == 41, "Returning from station walking lost its selected controller")
	check(FileAccess.get_file_as_bytes(args[1]) == source and FileAccess.get_file_as_bytes(output) == source, "Title discard autosaved or rewrote a file")
	shell.title_view.choose_continue(args[1])
	await settle()
	check(shell.current_view is Walk and shell.current_view.paused and shell.title_view == null and shell.bridge.save_freedom_as(output) and FileAccess.get_file_as_bytes(output) == source, "Continue after Title changed complete save")
	check(shell.current_view.selected_pad == 41, "Title roundtrip lost station controller ownership")
	for filename in ["flight-18.json", "wayfarer-flight.json"]:
		var target := args[1].get_base_dir().path_join(filename)
		check(shell.select_start(shell.bridge, {"mode": "continue", "value": target, "assets": args[0]}), "Title flight fixture refused")
		view = shell.current_view
		var expected_device := 41
		if filename == "wayfarer-flight.json":
			# Explicit keyboard ownership must survive even with known backup pads.
			expected_device = -1
			view.player_input.device = -1
			view.player_input.available_pads = {40: true, 41: true}
			view.player_input.install()
		var source_bytes := FileAccess.get_file_as_bytes(target)
		var hold_source := ProjectSettings.globalize_path("user://title-hold-" + filename)
		view.controls_menu.saved_hold_button.pressed.emit()
		check(view.paused and shell.bridge.get_freedom_flight_state().hold_enabled and shell.bridge.save_freedom_as(hold_source), "Explicit hold selection before Title failed")
		check(shell.bridge.save_freedom_as(output), "Pre-title flight save refused")
		var frozen := FileAccess.get_sha256(output)
		view.controls_menu.title_button.pressed.emit()
		view.toggle_pause()
		view._process(0.125)
		check(view.paused and shell.title_origin == view and shell.bridge.save_freedom_as(output) and FileAccess.get_sha256(output) == frozen, "Flight Title confirmation changed complete state")
		shell.title_confirmation.get_cancel_button().pressed.emit()
		check(shell.current_view == view and view.paused and root.gui_get_focus_owner() == view.controls_menu.title_button, "Flight Title Cancel lost invoking action")
		view.controls_menu.title_button.pressed.emit()
		shell.confirm_title()
		check(shell.current_view == null and shell.title_view != null and shell.bridge.get_freedom_start().is_empty(), "Flight Title did not release the active journey")
		check(shell.title_view.controls.device == expected_device and shell.title_view.controls.initial_device == expected_device, "Returning from flight lost selected controller or keyboard ownership")
		shell.title_view.choose_continue(hold_source)
		await settle()
		check(shell.current_view.paused and shell.title_view == null and shell.bridge.save_freedom_as(output) and FileAccess.get_sha256(output) == frozen, "Flight Continue after Title changed complete save")
		check(shell.current_view.player_input.device == expected_device, "Flight Continue implicitly changed controller or keyboard ownership")
		check(shell.bridge.get_freedom_flight_state().hold_enabled and FileAccess.get_file_as_bytes(target) == source_bytes, "Title/Continue lost hold or rewrote the original flight")
	for chart in 3:
		check(shell.select_start(shell.bridge, {"mode": "new_game", "value": "42", "network": chart > 0, "planetary": chart == 2, "drive_strain": true, "assets": args[0]}), "Independent strained-drive/chart staging refused: " + shell.error)
		var strained_state: Dictionary = shell.bridge.get_freedom_flight_state()
		check(shell.current_view is Walk and strained_state.tick == "0" and strained_state.chart.rows.size() == (2 if chart == 0 else 4) and strained_state.jump.recovery.version == 2 and strained_state.jump.recovery.ready and strained_state.jump.recovery.next_recovery_seconds == 45.0, "Strain selection changed charts, world time or initial ready cycle")
		var strained_save: String = args[1].get_base_dir().path_join("strain-start-%d.json" % chart)
		check(shell.bridge.save_freedom_as(strained_save), "Strained-drive station Save refused")
		var strained_hash := FileAccess.get_sha256(strained_save)
		check(shell.select_start(shell.bridge, {"mode": "continue", "value": strained_save, "assets": args[0]}) and shell.current_view.paused, "Strained-drive Continue failed actual station staging")
		check(shell.bridge.save_freedom_as(strained_save) and FileAccess.get_sha256(strained_save) == strained_hash, "Strained-drive Continue changed complete station Save")
	check(shell.select_start(shell.bridge, {"mode": "new_game", "value": "42", "network": true, "planetary": true, "drive_strain": true, "origin_service": true, "assets": args[0]}), "Explicit service/chart/drive staging refused: " + shell.error)
	var service_start: Dictionary = shell.bridge.get_freedom_flight_state()
	check(shell.current_view is Walk and service_start.tick == "0" and service_start.chart.rows.size() == 4 and service_start.jump.recovery.version == 2, "Service choice changed player charts, drive or initial clock")
	var service: Dictionary = service_start.get("origin_service", {})
	check(service.get("selected", false) and service.get("phase") == "Stationed at D2" and service.get("craft", {}).get("visible", false) and service.craft_id != service_start.craft_id, "Actual distinct D2 service ship was not projected at station start")
	check(shell.current_view.service_craft != null and shell.current_view.service_craft.visible, "Selected station service ship was not staged into the scene")
	var service_save: String = args[1].get_base_dir().path_join("service-station.json")
	check(shell.bridge.save_freedom_as(service_save), "Selected service station Save refused")
	var service_hash := FileAccess.get_sha256(service_save)
	check(shell.select_start(shell.bridge, {"mode": "continue", "value": service_save, "assets": args[0]}) and shell.current_view.paused, "Selected service Continue failed actual station staging")
	check(shell.bridge.save_freedom_as(service_save) and FileAccess.get_sha256(service_save) == service_hash and shell.current_view.service_craft != null and shell.current_view.service_craft.visible, "Service Continue changed complete Save or lost its real D2 presentation")
	# One actual title request composes the new chart with existing drive/service choices.
	var frontier_title := Title.new()
	frontier_title.persist_controls = false
	frontier_title.catalog_owner = shell.bridge
	root.add_child(frontier_title)
	var frontier_requests: Array[Dictionary] = []
	frontier_title.start_requested.connect(func(selection: Dictionary): frontier_requests.append(selection))
	frontier_title.network_choice.select(4)
	frontier_title.network_choice.item_selected.emit(4)
	frontier_title.recovery_choice.select(2)
	frontier_title.origin_service_choice.button_pressed = true
	frontier_title.derelict_recovery_choice.button_pressed = true
	check(frontier_title.network_choice.get_item_text(4) == "Outer frontier: four charted systems" and not frontier_title.derelict_recovery_choice.disabled, "Frontier title label or retrieval composition unavailable")
	frontier_title.begin_new()
	check(frontier_requests.size() == 1, "Frontier title emitted duplicate or missing requests")
	if frontier_requests.size() == 1:
		var options: Dictionary = frontier_requests[0].duplicate(true)
		check(options.get("_title_request") == frontier_title.request_generation, "Frontier title request lost its UI-generation guard")
		options.erase("_title_request")
		check(options == {"mode": "new_game", "value": "42", "network": true, "planetary": false, "uncharted": false, "frontier": true, "drive_recovery": true, "drive_strain": true, "origin_service": true, "derelict_recovery": true}, "Title help changed its explicit frontier start arguments")
		options.assets = args[0]
		check(shell.select_start(shell.bridge, options), "Actual frontier title request refused: " + shell.error)
		var frontier_state: Dictionary = shell.bridge.get_freedom_flight_state()
		check(shell.current_view is Walk and frontier_state.tick == "0" and frontier_state.chart.rows.size() == 4 and frontier_state.star_search.get("version") == 2 and frontier_state.jump.recovery.version == 2 and frontier_state.origin_service.selected and frontier_state.derelict_recovery.selected, "Frontier title composition changed clock, starting knowledge or selected options")
		var frontier_save: String = args[1].get_base_dir().path_join("frontier-title-start.json")
		check(shell.bridge.save_freedom_as(frontier_save), "Frontier station save refused")
		var frontier_bytes := FileAccess.get_file_as_bytes(frontier_save)
		check(shell.select_start(shell.bridge, {"mode": "continue", "value": frontier_save, "assets": args[0]}) and shell.current_view.paused and shell.bridge.save_freedom_as(frontier_save) and FileAccess.get_file_as_bytes(frontier_save) == frontier_bytes, "Frontier composed Continue changed complete station Save")
	frontier_title.free()
	shell.free()
	await settle()
	print("Native title: %d failures" % failures)
	quit(1 if failures else 0)
