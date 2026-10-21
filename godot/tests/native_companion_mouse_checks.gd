extends RefCounted
## Actual charged arrival saves, ordinary UI commands and independent C++ ticks.
const Shell = preload("res://scripts/native/native_start_shell.gd")

static func motion(view: Control, relative: Vector2) -> void:
	var event := InputEventMouseMotion.new()
	event.relative = relative
	view._input(event)

static func right_button(view: Control, pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_RIGHT
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	view._input(event)

static func save(owner: Variant, path: String, check: Callable) -> PackedByteArray:
	if not check.call(owner.save_freedom_as(path), "Companion mouse Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

static func resume(view: Control, check: Callable) -> bool:
	view.focused = true
	view.player_input.focused = true
	view.observe_neutral_controls()
	view.toggle_pause()
	# The next ordinary frame resynchronizes the cue after enabling input.
	view.sync_mouse_look()
	return check.call(not view.paused and view.controls_armed and view.mouse_look.active and view.mouse_steering_available(), "Companion mouse failed explicit neutral Resume")

static func outside(view: Control, owner: Variant, output: String, check: Callable) -> void:
	if not check.call(view.surface_walking() and not view.paused and view.mouse_look.active and view.flight_mouse != null, "Actual Moon walker lacks its active camera-only mouse context"): return
	var before: Dictionary = owner.get_freedom_flight_state()
	var frozen := save(owner, output.path_join("outside-mouse-before.json"), check)
	var look: Vector2 = view.look_offset
	motion(view, Vector2(0, 8))
	check.call(view.look_offset.y != look.y and view.look_offset.x == look.x and not view.mouse_steering_available() and view.flight_mouse.offset == Vector2.ZERO and not view.flight_mouse.visible, "Moon on-foot mouse entered flight steering instead of camera look")
	check.call(owner.get_freedom_flight_state() == before and save(owner, output.path_join("outside-mouse-after.json"), check) == frozen, "On-foot mouse camera look advanced the actor or changed complete Save")

static func run(tree: SceneTree, source: String, assets: String, output: String, kind: String, check: Callable) -> void:
	if not check.call(DirAccess.make_dir_recursive_absolute(output) == OK, "Companion mouse output unavailable"): return
	var original := FileAccess.get_file_as_bytes(source)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check.call(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Companion mouse actual arrival Continue refused: " + shell.error):
		shell.free()
		return
	var view: Control = shell.current_view
	view.set_process(false)
	if not check.call(view.state.body_kind == kind and view.paused and not view.surface_walking() and not view.state.surface.landed and view.flight_mouse != null, "Actual companion arrival has no paused flight mouse owner: " + kind):
		shell.free()
		return
	view.player_input.persist = false
	view.player_input.device = -1
	view.player_input.install()
	var initial: Dictionary = owner.get_freedom_flight_state()
	check.call(save(owner, output.path_join("initial.json"), check) == original and view.flight_mouse.offset == Vector2.ZERO and not view.flight_mouse.visible, "Paused companion Continue changed the complete source or armed mouse steering")
	if not resume(view, check): shell.free(); return
	var head: Vector2 = view.look_offset
	motion(view, view.size * Vector2(0.02, -0.01))
	var demand: Vector2 = view.flight_mouse.demands(view.player_input.settings)
	if not check.call(absf(demand.x) > 0.0 and absf(demand.y) > 0.0 and view.flight_mouse.visible and view.look_offset == head and owner.get_freedom_flight_state() == initial, "Companion mouse failed to request pitch/yaw without moving camera or C++ state"):
		shell.free()
		return
	# Independent public bridge layout: pitch +/- uses6/9, yaw +/- uses10/7.
	# Do not call the production blending or actuator-conversion helper here.
	var expected := PackedFloat64Array()
	expected.resize(12)
	expected[6 if demand.x > 0.0 else 9] = absf(demand.x)
	expected[10 if demand.y > 0.0 else 7] = absf(demand.y)
	if not check.call(peer.stage_freedom_continue(source), "Independent companion mouse C++ peer staging refused"):
		shell.free()
		return
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check.call(peer.commit_pending_freedom_start(pending.candidate_id), "Independent companion mouse C++ peer commit refused"):
		shell.free()
		return
	view._process(1.0 / 120.0)
	check.call(peer.advance_freedom_flight(1.0 / 120.0, expected, false), "Independent companion mouse C++ tick refused")
	var actual: Dictionary = owner.get_freedom_flight_state()
	check.call(view.error.is_empty() and actual == peer.get_freedom_flight_state() and int(actual.tick) == int(initial.tick) + 1, "Companion mouse did not reach the same actual120Hz C++ command path")
	check.call(actual.positive_torque_body[0] + actual.negative_torque_body[0] > 0.0 and actual.positive_torque_body[1] + actual.negative_torque_body[1] > 0.0 and actual.angular_velocity_body != initial.angular_velocity_body, "Companion mouse did not apply real pitch/yaw torque")
	check.call(actual.resources.quantity_quanta != initial.resources.quantity_quanta and int(actual.resources.last_tick_quanta) > 0, "Real companion mouse torque did not debit flight fuel")
	var held_path := output.path_join("held.json")
	var held := save(owner, held_path, check)
	check.call(not held.is_empty() and held == save(peer, output.path_join("peer.json"), check), "Companion mouse complete Save differs from independent C++ peer")
	# Camera-only free look clears existing steering before accepting movement.
	right_button(view, true)
	check.call(view.flight_mouse.offset == Vector2.ZERO and not view.flight_mouse.visible, "Companion free look retained mouse steering")
	var look: Vector2 = view.look_offset
	motion(view, Vector2(8, -4))
	check.call(view.look_offset != look and view.flight_mouse.offset == Vector2.ZERO and owner.get_freedom_flight_state() == actual, "Companion right mouse failed camera-only free look")
	right_button(view, false)
	check.call(view.look_offset == Vector2.ZERO and view.flight_mouse.offset == Vector2.ZERO and owner.get_freedom_flight_state() == actual, "Companion free-look release did not recenter without changing C++ state")
	for boundary in ["pause", "focus"]:
		motion(view, view.size * Vector2(0.02, -0.01))
		check.call(view.flight_mouse.offset != Vector2.ZERO, "Companion boundary lacked held mouse intent: " + boundary)
		if boundary == "pause": view.pause_controls("Companion mouse boundary")
		else: view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
		check.call(view.paused and view.flight_mouse.offset == Vector2.ZERO and not view.flight_mouse.visible and not view.mouse_look.active, "Companion safety boundary retained mouse steering: " + boundary)
		motion(view, Vector2(20, -10))
		view._process(0.125)
		check.call(owner.get_freedom_flight_state() == actual and save(owner, output.path_join(boundary + ".json"), check) == held, "Companion pause/focus callbacks advanced or changed the complete Save: " + boundary)
		if boundary == "focus":
			view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
			check.call(view.paused and view.flight_mouse.offset == Vector2.ZERO, "Companion focus return automatically resumed old mouse intent")
		if not resume(view, check): shell.free(); return
	# Continue the actual post-steering Save with a still-deflected old view.
	motion(view, view.size * Vector2(0.02, -0.01))
	check.call(view.flight_mouse.offset != Vector2.ZERO, "Companion Continue lacked old mouse intent")
	if check.call(shell.select_start(owner, {"mode": "continue", "value": held_path, "assets": assets}), "Companion mouse saved Continue refused: " + shell.error):
		view = shell.current_view
		view.set_process(false)
		check.call(view.paused and view.flight_mouse != null and view.flight_mouse.offset == Vector2.ZERO and not view.flight_mouse.visible and save(owner, output.path_join("continued.json"), check) == held, "Companion Continue restored transient mouse intent or changed full Save")
	check.call(FileAccess.get_file_as_bytes(source) == original, "Companion mouse checks modified the actual arrival source")
	shell.free()
	peer = null
	owner = null
	await tree.process_frame
