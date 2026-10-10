extends SceneTree
## Synthetic GUI/input contract. No visible window, asset, audio or flight writes.
const Controls = preload("res://scripts/ui/player_input.gd")
const Menu = preload("res://scripts/ui/pause_menu.gd")
const Basics = preload("res://scripts/flight/flight_basics.gd")
var failures := 0
var controls: Node
var menu: CanvasLayer
var resumes := 0

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	call_deferred("run")

func key(code: Key, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	await process_frame

func pad(button: JoyButton, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.button_index = button
	event.device = 0
	event.pressed = down
	Input.parse_input_event(event)
	await process_frame

func pause() -> void:
	controls.set_enabled(false)
	menu.show_menu()

func resume() -> void:
	resumes += 1
	menu.hide_menu()
	controls.set_enabled(true)

func layout_settle() -> void:
	for i in 4:
		await process_frame

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy":
		push_error("Requires --headless --audio-driver Dummy")
		quit(1)
		return
	root.size = Vector2i(1280, 720)
	# Validate absent/broken readouts before the context is shown in a real menu.
	var snapshot := {"altitude": 900.0, "surface_speed": 0.0, "radial_rate": 0.0, "air_density": 1.0, "attached": false, "surface": {"landed": true}, "surface_walk": {}, "resources": {"selected": true, "fraction": 0.25, "jump_charges": 1}, "jump": {"phase": "idle"}, "chart": {"rows": []}}
	var original := snapshot.duplicate(true)
	for damaged in [{}, {"altitude": NAN}, {"altitude": 900.0, "surface": []}]:
		check("unavailable" in Basics.NativeStatus.summary(damaged), "Malformed help context manufactured current observations")
	check(snapshot == original, "Help observations mutated the context")
	controls = Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	root.add_child(controls)
	controls.device = 0
	controls.install()
	menu = Menu.new()
	menu.controls = controls
	menu.rotational_coasting = true
	menu.orbit_preserving_assist = true
	root.add_child(menu)
	menu.resumed.connect(resume)
	controls.pause_requested.connect(func():
		if menu.panel.visible:
			resume()
		else:
			pause())
	pause()
	await layout_settle()
	check(root.gui_get_focus_owner() == menu.resume_button, "First-open Resume focus changed")
	check(menu.left_scroll.get_global_rect().encloses(menu.resume_button.get_global_rect()), "Resume is not visible")
	menu.basics_button.grab_focus()
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	check(menu.basics.visible and not menu.menu_margin.visible and not controls.enabled, "Help did not open paused")
	check(root.gui_get_focus_owner() == menu.basics.next_button, "Help entry focus lost")
	await pad(JOY_BUTTON_A, true)
	await pad(JOY_BUTTON_A, false)
	check(menu.basics.page == 1, "Controller accept did not advance help page")
	await pad(JOY_BUTTON_DPAD_LEFT, true)
	await pad(JOY_BUTTON_DPAD_LEFT, false)
	check(root.gui_get_focus_owner() == menu.basics.previous_button, "D-pad focus navigation failed")
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	check(menu.basics.page == 0, "Keyboard accept did not return page")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_J}), "Remap fixture failed")
	menu.refresh_bindings()
	check(Basics.binding(controls, "forward") in menu.basics.body.text and controls.binding_label("forward", "key") == "J", "Help retained stale binding text")
	controls.settings.prompts = 2
	menu.refresh_bindings()
	check("R2" in menu.basics.body.text, "PlayStation prompt did not update")
	for size in [Vector2i(1280, 720), Vector2i(960, 540), Vector2i(800, 450)]:
		root.size = size
		for index in Basics.TITLES.size():
			menu.basics.page = index
			menu.basics.refresh()
			await layout_settle()
			var viewport := root.get_visible_rect()
			for item in [menu.basics.heading, menu.basics.scroll, menu.basics.previous_button, menu.basics.next_button, menu.basics.back_button]:
				check(viewport.encloses(item.get_global_rect()), "Help control clipped at %s page %d" % [size, index])
			check(menu.basics.body.size.x <= menu.basics.scroll.size.x + 1, "Reference body overflowed horizontally")
			var physical_font: float = menu.basics.body.get_theme_font_size("font_size")/menu.basics._readable_scale
			check(absf(physical_font-26) < 1, "Small viewport reduced physical reference font")
	# Stress ordinary wrapping and controller-only access to overflow text.
	menu.set_process(false)
	menu.basics.body.text = "Long reference / ".repeat(500)
	await layout_settle()
	menu.basics.scroll.scroll_vertical = 0
	await pad(JOY_BUTTON_DPAD_DOWN, true)
	await pad(JOY_BUTTON_DPAD_DOWN, false)
	check(menu.basics.scroll.scroll_vertical > 0, "Controller cannot scroll long help")
	check(root.gui_get_focus_owner() == menu.basics.previous_button, "Text scroll stole button focus")
	menu.basics.back_button.grab_focus()
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	check(not menu.basics.visible and menu.menu_margin.visible and not controls.enabled and resumes == 0, "Back to controls resumed flight")
	check(root.gui_get_focus_owner() == menu.basics_button, "Back did not restore entry focus")
	root.size = Vector2i(1280, 720)
	menu.set_process(true)
	menu.show_basics()
	await key(KEY_J, true)
	check(controls.sample().thrust_axes == PackedFloat64Array([0,0,0,0,0,0,0]), "Help allowed thrust while paused")
	await key(KEY_ESCAPE, true)
	await key(KEY_ESCAPE, false)
	check(resumes == 1 and not menu.panel.visible, "Esc no longer resumes from help")
	check(controls.sample().thrust_axes == PackedFloat64Array([0,0,0,0,0,0,0]), "Held thrust leaked through resume neutral gate")
	await key(KEY_J, false)
	controls.sample()
	pause()
	await layout_settle()
	check(not menu.basics.visible and root.gui_get_focus_owner() == menu.resume_button, "Reopening menu retained hidden help focus")
	menu.show_basics()
	await pad(JOY_BUTTON_B, true)
	await pad(JOY_BUTTON_B, false)
	check(resumes == 2 and not menu.panel.visible, "Controller Back no longer resumes")
	pause()
	menu.show_basics()
	await pad(JOY_BUTTON_START, true)
	await pad(JOY_BUTTON_START, false)
	check(resumes == 3 and not menu.panel.visible, "Start no longer resumes")
	# Content variants never claim lab3 translation or lab2 rotation in lab1.
	var historical := Basics.page_text(2, controls, false, false)
	check("retains attitude damping" in historical and "both preserve orbital" not in historical, "Historical model received current-model advice")
	var modern := Basics.page_text(2, controls, true, true)
	check("both preserve orbital" in modern and "Counter-steer" in modern, "Modern assist reference missing")
	var orbit := Basics.page_text(1, controls, true, true)
	check("strictly above BOTH" in orbit and "20 km" in orbit and "reference radius" in orbit, "Safe-orbit criterion misstated")
	var limits := Basics.page_text(4, controls, true, true)
	check("not autopilot" in limits and "16 m" in limits and "flight saves are not implemented" in limits, "Prototype limitations missing")
	menu.free()
	# The same widget has explicit ordinary saved and historical craft profiles.
	for wayfarer in [true, false]:
		menu = Menu.new()
		menu.controls = controls
		menu.saved_flight = true
		menu.saved_wayfarer = wayfarer
		root.add_child(menu)
		pause()
		await layout_settle()
		check(is_instance_valid(menu.basics_button) and menu.basics.saved_flight, "Saved reference entry/profile missing")
		menu.sync_saved_state(snapshot, false)
		check(menu.basics.saved_context == snapshot, "Saved reference did not consume the menu's projected observations")
		menu.basics_button.grab_focus()
		await key(KEY_ENTER, true)
		await key(KEY_ENTER, false)
		check(menu.basics.visible and not controls.enabled, "Saved reference entry resumed controls")
		check("Landed" in menu.basics._paused.text and not "Current observations" in menu.basics.body.text, "Context banner hid instructions below repeated telemetry")
		check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_T}), "Saved reference remap fixture failed")
		menu.refresh_bindings()
		check(Basics.binding(controls, "forward") in menu.basics.body.text and "R2" in menu.basics.body.text and controls.binding_label("forward", "key") == "T", "Saved reference failed current remapped/PlayStation labels")
		var complete := ""
		for size in [Vector2i(1280, 720), Vector2i(960, 540), Vector2i(800, 450)]:
			root.size = size
			for index in menu.basics.titles().size():
				menu.basics.page = index
				menu.basics.refresh()
				await layout_settle()
				for item in [menu.basics.heading, menu.basics.scroll, menu.basics.previous_button, menu.basics.next_button, menu.basics.back_button]:
					check(root.get_visible_rect().encloses(item.get_global_rect()), "Saved reference clipped at %s page %d" % [size, index])
				complete += menu.basics.body.text
		check("flight saves are not implemented" not in complete and "practice relocates" not in complete and "16 m" not in complete and "NAV" not in complete, "Saved reference inherited lab capabilities")
		check("actual atmosphere boundary" in complete and "20 km" not in complete and "physical torque fractions" in complete and "automatic hover" in complete, "Saved reference misstated orbit/actuator model")
		check("Quit does not autosave" in complete and "last committed" in complete, "Saved reference lost committed save policy")
		check("Load opens a save chooser" in complete and "Title asks before discarding" in complete and "Neither action saves automatically" in complete, "Reference lost transactional Load/Title instructions")
		check("New Game starts on Origin Station" in complete and "Board Wayfarer" in complete and "Unboard to station" in complete and "historical flight save does not gain station walking" in complete, "Reference omitted or overclaimed station boarding")
		check("Fuel accounting and jump travel are not implemented" not in complete and "three separate jump charges" in complete and "committed transit cannot be canceled" in complete and "Standard replacement" in complete, "Reference denies or misstates shipped Freedom operations")
		check("Current observations" in complete and "Fuel 25.0%" in complete and "Jumps 1/3" in complete and snapshot == original, "Current reference invented resources or edited observations")
		check(("Return through hatch" in complete) == wayfarer and ("This historical craft has no supported Wayfarer landing" in complete) == not wayfarer, "Reference manufactured supported surface actions for the historical craft")
		var ports := Basics.saved_page_text(4, controls, wayfarer)
		check(("Target D1 / D2" in ports) == wayfarer and ("no supported Wayfarer" in ports) == not wayfarer, "Historical profile manufactured port support")
		controls.waiting_action = "forward"
		menu.close_basics()
		menu.show_basics()
		check(not menu.basics.visible, "Remap capture opened saved reference")
		controls.waiting_action = ""
		menu.show_basics()
		menu.basics.back_button.grab_focus()
		await key(KEY_ENTER, true)
		await key(KEY_ENTER, false)
		check(not menu.basics.visible and menu.menu_margin.visible and root.gui_get_focus_owner() == menu.basics_button and not controls.enabled, "Saved Back lost paused entry focus")
		menu.free()
	controls.free()
	print("Flight basics: %d failures; remapped paused reference, controller/keyboard navigation and small viewport wrapping" % failures)
	quit(0 if failures == 0 else 1)
