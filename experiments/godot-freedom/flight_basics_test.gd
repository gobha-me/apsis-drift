extends SceneTree
## Synthetic GUI/input contract. No visible window, asset, audio or flight writes.
const Controls = preload("res://player_input.gd")
const Menu = preload("res://pause_menu.gd")
const Basics = preload("res://flight_basics.gd")
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
	controls.free()
	print("Flight basics: %d failures; remapped paused reference, controller/keyboard navigation and small viewport wrapping" % failures)
	quit(0 if failures == 0 else 1)
