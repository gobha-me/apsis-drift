extends SceneTree
## Synthetic GPU integration proof, not physical-controller or SteamOS qualification.
var study: Variant
var failures := 0
const DEVICE := 77

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func frames(count: int) -> void:
	for i in count:
		await process_frame

func axis(code: int, value: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = DEVICE
	event.axis = code
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func button(code: int, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = DEVICE
	event.button_index = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func seated_camera() -> Transform3D:
	return study.ship.transform.affine_inverse() * study.camera.transform

func check_centered_camera(message: String) -> void:
	var expected: Transform3D = study.CockpitLayout.mount() * Transform3D(Basis.IDENTITY, study.pilot_eye)
	check(study.pilot_view and study.head_angles == Vector2.ZERO and seated_camera().is_equal_approx(expected) and study.camera.fov == 75, message)

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	if study.player_input == null or study.live_bridge == null:
		push_error("Controller smoke requires --live=true --controls-persist=false and no --capture")
		quit(1)
		return
	var controls: Node = study.player_input
	if study.options.get("--start-paused", "false") == "true":
		check(study.pause_menu.panel.visible and study.live_paused, "Start-paused did not open safe setup screen")
	await frames(15)
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study.set_player_paused(false)
	controls.device = DEVICE
	controls.install()
	var deadline := Time.get_ticks_msec() + 30000
	while study.planet_stream != null and not study.planet_stream.is_ready:
		# Window-manager startup focus events can arrive after our first resume.
		# Keep only setup active; later tests explicitly exercise focus safety.
		if study.live_paused:
			study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
			study.set_player_paused(false)
		if Time.get_ticks_msec() > deadline:
			check(false, "Terrain stream did not prepare before controller test")
			quit(1)
			return
		await frames(1)
	print("Controller smoke: terrain ready")
	await frames(3)
	check_centered_camera("Initial cockpit camera differs from recentered seat view")
	# Use the real camera action; compare ship-relative poses as physics continues.
	for cycle in 2:
		study.turn_head(Vector2(90, 35))
		button(JOY_BUTTON_X, true)
		button(JOY_BUTTON_X, false)
		await frames(2)
		check(not study.pilot_view, "Camera action did not leave cockpit")
		button(JOY_BUTTON_X, true)
		button(JOY_BUTTON_X, false)
		await frames(2)
		check_centered_camera("Cockpit/chase round trip changed centered camera")
		study.turn_head(Vector2(-70, 40))
		controls.recenter_requested.emit()
		check_centered_camera("Explicit recenter differs from cockpit entry")
	var initial: Dictionary = study.live_bridge.get_state()
	axis(JOY_AXIS_TRIGGER_RIGHT, 0.6)
	axis(JOY_AXIS_RIGHT_X, 0.4)
	await frames(12)
	check(study.live_bridge.get_state().heading != initial.heading, "Right stick did not yaw C++ flight")
	button(JOY_BUTTON_LEFT_STICK, true)
	axis(JOY_AXIS_RIGHT_X, 0.5)
	await frames(20)
	var moved: Dictionary = study.live_bridge.get_state()
	check(moved.tick > initial.tick and moved.speed > 0, "Trigger did not move C++ flight")
	check(study.head_angles.y < 0, "Right stick did not turn pilot head")
	check((int(moved.controls) & 204) == 0, "Head-look also commanded yaw/heave")
	button(JOY_BUTTON_LEFT_STICK, false)
	await frames(2)
	check(study.head_angles == Vector2.ZERO, "Releasing look did not snap to ship centerline")
	check_centered_camera("Head-look release differs from cockpit entry/recenter")
	check((int(study.live_bridge.get_state().controls) & 204) == 0, "Release leaked held stick into yaw/heave")
	if controls.thrust_mode:
		axis(JOY_AXIS_RIGHT_X, 0)
		axis(JOY_AXIS_LEFT_X, 0)
		axis(JOY_AXIS_TRIGGER_RIGHT, 0)
		await frames(3)
		var attitude: Basis = study.live_bridge.get_state().body_basis
		axis(JOY_AXIS_LEFT_Y, 0.65)
		await frames(12)
		check(not study.live_bridge.get_state().body_basis.is_equal_approx(attitude), "Pitch did not reach C++ attitude")
		axis(JOY_AXIS_LEFT_Y, 0)
		await frames(3)
		attitude = study.live_bridge.get_state().body_basis
		axis(JOY_AXIS_LEFT_X, 0.7)
		await frames(12)
		check(not study.live_bridge.get_state().body_basis.is_equal_approx(attitude), "Roll did not reach C++ attitude")
		check((int(study.live_bridge.get_state().controls) & 48) == 0, "Roll also strafed")
		check(study.ship.basis.is_equal_approx(study.live_bridge.get_state().body_basis), "Godot ignored full ship attitude")
		check(study.camera.basis.is_equal_approx((study.ship.transform * study.CockpitLayout.mount()).basis), "Cockpit camera did not inherit pitch/roll")
		axis(JOY_AXIS_LEFT_X, 0)
		button(JOY_BUTTON_LEFT_SHOULDER, true)
		await frames(3)
		check((int(study.live_bridge.get_state().controls) & 48) == 16, "Left bumper did not command left strafe")
		button(JOY_BUTTON_LEFT_SHOULDER, false)
		button(JOY_BUTTON_RIGHT_SHOULDER, true)
		await frames(3)
		check((int(study.live_bridge.get_state().controls) & 48) == 32, "Right bumper did not command right strafe")
		button(JOY_BUTTON_RIGHT_SHOULDER, false)
		axis(JOY_AXIS_TRIGGER_LEFT, 0.7)
		await frames(12)
		check(study.live_bridge.get_state().retro_thrust > 0.1, "LT did not reach retro actuator")
		axis(JOY_AXIS_TRIGGER_LEFT, 0)
		button(JOY_BUTTON_Y, true)
		button(JOY_BUTTON_Y, false)
		await frames(3)
		check(not study.live_bridge.get_state().assist, "Assist toggle not consumed by physics")
		if study.live_bridge.get_state().flight_model == "thrust-lab-2":
			check(not study.live_bridge.get_state().attitude_stabilized, "Assist-off still reports rotational stabilization")
			axis(JOY_AXIS_LEFT_X, 0.7)
			await frames(15)
			axis(JOY_AXIS_LEFT_X, 0)
			var spinning: Dictionary = study.live_bridge.get_state()
			check(spinning.angular_velocity_body.length() > 0.02, "Lab2 stick did not establish spin")
			await frames(15)
			var coasting: Dictionary = study.live_bridge.get_state()
			check(not coasting.body_basis.is_equal_approx(spinning.body_basis) and coasting.angular_velocity_body.length() > 0.02, "Neutral assist-off stopped rotating")
			check(study.ship.basis.is_equal_approx(coasting.body_basis), "Godot ignored lab2 full attitude")
			button(JOY_BUTTON_Y, true)
			button(JOY_BUTTON_Y, false)
			await frames(45)
			var stabilized: Dictionary = study.live_bridge.get_state()
			check(stabilized.assist and stabilized.attitude_stabilized and stabilized.angular_velocity_body.length() < coasting.angular_velocity_body.length(), "Assist-on did not recover from spin with bounded control")
	button(JOY_BUTTON_START, true)
	button(JOY_BUTTON_START, false)
	await frames(2)
	check(study.pause_menu.panel.visible and study.live_paused, "Start did not open pause menu")
	var paused_tick: int = study.live_bridge.get_state().tick
	axis(JOY_AXIS_TRIGGER_RIGHT, 0)
	axis(JOY_AXIS_LEFT_X, 0)
	axis(JOY_AXIS_RIGHT_X, 0)
	await frames(10)
	check(study.live_bridge.get_state().tick == paused_tick, "Menu advanced simulation")
	check(root.gui_get_focus_owner() == study.pause_menu.resume_button, "Menu has no initial focus")
	button(JOY_BUTTON_DPAD_DOWN, true)
	button(JOY_BUTTON_DPAD_DOWN, false)
	await frames(2)
	check(root.gui_get_focus_owner() != study.pause_menu.resume_button, "Controller could not navigate menu")
	# Exercise a settings control through native GUI input, not direct assignment.
	var sliders: Array[Node] = study.pause_menu.find_children("*", "HSlider", true, false)
	check(sliders.size() == 4, "Controller settings sliders missing")
	var deadzone: HSlider = sliders[0]
	deadzone.grab_focus()
	var old_deadzone: float = controls.settings.deadzone
	button(JOY_BUTTON_DPAD_RIGHT, true)
	await frames(1)
	button(JOY_BUTTON_DPAD_RIGHT, false)
	await frames(2)
	check(controls.settings.deadzone > old_deadzone, "Controller could not adjust deadzone")
	# Restore the demonstration value via the same live settings control.
	deadzone.value = old_deadzone
	study.pause_menu.resume_button.grab_focus()
	# Rebind through the capture UI, preserving the other input family.
	var bind_button: Button = study.pause_menu.binding_buttons[0].button
	bind_button.grab_focus()
	button(JOY_BUTTON_A, true)
	await frames(1)
	button(JOY_BUTTON_A, false)
	await frames(2)
	check(controls.waiting_action == "forward", "Controller could not open remapping")
	var key := InputEventKey.new()
	key.physical_keycode = KEY_T
	key.keycode = KEY_T
	key.pressed = true
	Input.parse_input_event(key)
	Input.flush_buffered_events()
	await frames(1)
	key = key.duplicate()
	key.pressed = false
	Input.parse_input_event(key)
	Input.flush_buffered_events()
	check(controls.bindings.forward.key.code == KEY_T and controls.waiting_action.is_empty(), "Remapping capture did not save key")
	controls.defaults()
	controls.install()
	controls.settings.prompts = 0
	study.pause_menu.refresh_bindings()
	study.pause_menu.resume_button.grab_focus()
	await frames(3)
	for scroll in study.pause_menu.find_children("*", "ScrollContainer", true, false):
		scroll.scroll_vertical = 0
	await frames(2)
	await RenderingServer.frame_post_draw
	var output: String = study.options.get("--controller-capture", "")
	if not output.is_empty():
		var picture := root.get_texture().get_image()
		check(picture.get_size() == Vector2i(1920, 1080), "Menu capture resolution mismatch")
		check(picture.save_png(output) == OK, "Menu PNG failed")
	button(JOY_BUTTON_A, true)
	await frames(2)
	button(JOY_BUTTON_A, false)
	await frames(3)
	check(not study.pause_menu.panel.visible and not study.live_paused, "Controller could not resume")
	axis(JOY_AXIS_TRIGGER_RIGHT, 0.7)
	await frames(3)
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	paused_tick = study.live_bridge.get_state().tick
	await frames(4)
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	await frames(4)
	check(study.pause_menu.panel.visible and study.live_bridge.get_state().tick == paused_tick, "Focus regain auto-resumed")
	study.set_player_paused(false)
	await frames(4)
	check(controls.needs_neutral and study.live_bridge.get_state().controls == 0, "Held trigger rearmed on resume")
	axis(JOY_AXIS_TRIGGER_RIGHT, 0)
	await frames(2)
	axis(JOY_AXIS_TRIGGER_RIGHT, 0.7)
	await frames(3)
	check(not controls.needs_neutral and study.live_bridge.get_state().controls != 0, "Fresh trigger failed after safety gate")
	controls.connection_changed(DEVICE, false)
	await frames(2)
	check(study.pause_menu.panel.visible, "Disconnect did not open safety pause")
	paused_tick = study.live_bridge.get_state().tick
	controls.connection_changed(DEVICE, true)
	await frames(4)
	check(study.live_bridge.get_state().tick == paused_tick, "Reconnect auto-resumed")
	print("Controller native smoke: %d failures; flight model %s, analog propulsion, attitude, look, menu, pause, focus, neutral gate and hotplug" % [failures, study.live_bridge.get_state().flight_model])
	quit(0 if failures == 0 else 1)
