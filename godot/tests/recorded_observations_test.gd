extends SceneTree
const Observations = preload("res://scripts/native/recorded_observations.gd")
const Controls = preload("res://scripts/ui/player_input.gd")
const Menu = preload("res://scripts/ui/pause_menu.gd")
var failures := 0
var queries := 0

func check(ok: bool, reason: String) -> void:
	if not ok:
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
	event.device = 0
	event.button_index = button
	event.pressed = down
	Input.parse_input_event(event)
	await process_frame

func run() -> void:
	failures += preload("res://tests/exploration_notes_test.gd").run()
	var fact := {"fact": "physical", "confidence": "probable", "source": "local_ship", "source_id": "17", "tick": "120", "craft_id": "23", "value": {}}
	var subject := {"name": "Unresolved planet", "kind": "planet", "system_id": "system-fixture", "subject_id": "27", "facts": [fact]}
	var readings := {"selected": true, "tick": "120", "subjects": [subject]}
	check(Observations.valid_readings(readings), "Redacted typed record refused")
	var text := Observations.subject_text(subject)
	check("Exact values unresolved." in text and "PROBABLE" in text and "ship time 1.0 s" in text and "craft 23" in text, "Unresolved record lost confidence/provenance")
	var leaked := readings.duplicate(true)
	leaked.subjects[0].facts[0].value = {"radius_kilometres": 1234, "gravity_milli_g": 1000}
	check(not Observations.valid_readings(leaked), "Uncertain numeric value reached the reader")
	for bad in [null, [], {}, {"selected": true, "tick": "120", "subjects": {}}, {"selected": false, "tick": "120", "subjects": [subject]}]:
		check(not Observations.valid_readings(bad), "Malformed observation container accepted")
	leaked.subjects[0].facts[0].confidence = "resolved"
	for bad in [NAN, INF, "1234", true, null]:
		var malformed := leaked.duplicate(true)
		malformed.subjects[0].facts[0].value.radius_kilometres = bad
		check(not Observations.valid_readings(malformed), "Malformed exact measurement accepted")
	check(Observations.valid_readings(leaked) and "1.000 g" in Observations.subject_text(leaked.subjects[0]), "Resolved physical units lost")
	check("Surface model" in Observations.value_text("atmosphere", {"class": "dense", "pressure_millibars": 3000}), "Surface pressure presented as live air")
	check("Terrain model" in Observations.value_text("landing", {"character": "rugged", "water_basis_points": 1234}), "Terrain estimate presented as certified landing site")
	# Real nested controls and short-window input, using authored records only.
	root.size = Vector2i(960, 540)
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	root.add_child(controls)
	controls.device = 0
	controls.install()
	var menu := Menu.new()
	menu.controls = controls
	menu.saved_flight = true
	menu.observations_query = func():
		queries += 1
		return {"selected": true, "tick": "120", "subjects": [subject, leaked.subjects[0]]}
	root.add_child(menu)
	controls.pause_requested.connect(func(): menu.back_from_nested())
	controls.set_enabled(false)
	menu.show_menu()
	for i in 4: await process_frame
	menu.observations_button.grab_focus()
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	var view: Control = menu.observations_view
	check(queries == 1 and view.visible and not menu.menu_margin.visible and not controls.enabled and root.gui_get_focus_owner() == view.next_button, "Observation entry lost pause/focus or queried every frame")
	await pad(JOY_BUTTON_A, true)
	await pad(JOY_BUTTON_A, false)
	check(view.page == 1 and "1234 km" in view.body.text and queries == 1, "Controller could not inspect second retained record")
	var long_subject := subject.duplicate(true)
	long_subject.facts = []
	for i in 8: long_subject.facts.append(fact.duplicate(true))
	view.readings.subjects[1] = long_subject
	view.refresh()
	for i in 4: await process_frame
	await key(KEY_DOWN, true)
	await key(KEY_DOWN, false)
	check(view.scroll.scroll_vertical > 0, "Keyboard did not scroll long recorded evidence")
	await key(KEY_ESCAPE, true)
	await key(KEY_ESCAPE, false)
	check(not view.visible and menu.menu_margin.visible and not controls.enabled and root.gui_get_focus_owner() == menu.observations_button, "First Back resumed flight or lost focus")
	menu.show_observations()
	check(queries == 2 and view.page == 0, "Reopening did not refresh transient records")
	menu.show_menu("Focus lost. Stay paused.")
	check(view.visible and not controls.enabled, "Safety pause resumed or discarded nested reader")
	menu.hide_menu()
	check(not view.visible and not menu.panel.visible, "Hidden controls left observations interactive")
	view.query = func(): return {"selected": true, "tick": "120", "subjects": []}
	menu.show_menu()
	menu.show_observations()
	check("No observations have been recorded." in view.body.text and root.gui_get_focus_owner() == view.back_button and view.next_button.disabled and view.previous_button.disabled, "Selected empty archive lost truthful text or Back focus")
	view.query = func(): return {"selected": false, "tick": "0", "subjects": []}
	view.open()
	check("no selected observation records" in view.body.text and root.gui_get_focus_owner() == view.back_button, "Unselected archive was presented as an empty survey")
	# Appended field pages preserve fact order and query only on opening.
	var notes_calls := [0]
	var note := preload("res://tests/exploration_notes_test.gd").row()
	view.query = func(): return {"selected": true, "tick": "120", "subjects": [subject]}
	view.notes_query = func():
		notes_calls[0] += 1
		return {"samples": {"selected": true, "capacity": 4, "carried": [], "journal": [note]}, "derelict": null}
	view.open()
	check(view.titles().size() == 3 and view.page == 0 and "Unresolved planet" in view.body.text and notes_calls[0] == 1, "Field notes displaced facts or queried more than once")
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	check(view.page == 1 and "FIELD SAMPLES" in view.body.text, "Keyboard did not reach appended pouch summary")
	await key(KEY_ENTER, true)
	await key(KEY_ENTER, false)
	check(view.page == 2 and note.label in view.body.text and "Specimen not carried." in view.body.text and "ship time 0.0 s" in view.body.text and notes_calls[0] == 1, "Note page lost first provenance, disposition or query neutrality")
	view.open()
	check(view.page == 0 and notes_calls[0] == 2, "Reopened field archive did not refresh exactly once")
	view.notes_query = Callable()
	menu.back_from_nested()
	menu.observations_query = func(): return null
	view.query = menu.observations_query
	menu.show_menu()
	menu.show_observations()
	check("unavailable" in view.body.text and root.gui_get_focus_owner() == view.back_button, "Bad provider did not produce a focusable safe empty view")
	menu.free()
	controls.free()
	print("Recorded observations checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)
