extends SceneTree
## Synthetic native input smoke. Focus notifications are simulated explicitly;
## this is not a substitute for a human desktop/controller playtest.

var study: Variant
var frame := 0
var paused_tick := 0
var focus_tick := 0

func _initialize() -> void:
	call_deferred("start")

func start() -> void:
	study = load("res://main.tscn").instantiate()
	if study.get_script() == null:
		push_error("Native study script failed to load")
		study.free()
		study = null
		quit(1)
		return
	root.add_child(study)

func key(code: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		quit(1)

func _process(_delta: float) -> bool:
	if study == null or study.live_bridge == null:
		return false
	if study.planet_stream != null and not study.planet_stream.is_ready:
		return false
	frame += 1
	if frame == 20:
		study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	if frame == 30:
		key(KEY_W, true)
	if frame == 45:
		key(KEY_D, true)
	if frame == 75:
		key(KEY_D, false)
	if frame == 90:
		key(KEY_W, false)
	if frame == 100:
		var state: Dictionary = study.live_bridge.get_state()
		check(state.tick > 0 and state.speed > 0 and abs(state.heading - 0.3) > 0.001,
			"Native movement input did not reach C++ flight")
		key(KEY_V, true)
		key(KEY_V, false)
	if frame == 102:
		check(study.live_paused, "Pause key did not toggle live pause")
		paused_tick = study.live_bridge.get_state().tick
	if frame == 120:
		check(study.live_bridge.get_state().tick == paused_tick, "Pause advanced C++ flight")
		key(KEY_V, true)
		key(KEY_V, false)
	if frame == 125:
		key(KEY_W, true)
	if frame == 130:
		study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
		focus_tick = study.live_bridge.get_state().tick
	if frame == 140:
		check(study.live_bridge.get_state().tick == focus_tick, "Focus loss advanced C++ flight")
		study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	if frame == 150:
		check(study.live_bridge.get_state().controls == 0, "Focus regain retained a held flight key")
		key(KEY_W, false)
		key(KEY_W, true)
		key(KEY_C, true)
		key(KEY_C, false)
	if frame == 160:
		check(study.live_bridge.get_state().tick > focus_tick, "Focus regain did not resume flight")
		check(study.pilot_view and study.pilot_cockpit.visible and study.ship.visible,
			"Pilot view toggle failed")
		print("Native synthetic input: thrust, turn, release, pause, focus and pilot camera checks passed")
	if frame == 161:
		var mouse := InputEventMouseButton.new()
		mouse.button_index = MOUSE_BUTTON_RIGHT
		mouse.pressed = true
		Input.parse_input_event(mouse)
		Input.flush_buffered_events()
		var motion := InputEventMouseMotion.new()
		motion.relative = Vector2(-220, 90)
		Input.parse_input_event(motion)
		Input.flush_buffered_events()
	if frame == 164:
		check(study.head_angles.y > 0.5, "RMB motion did not turn seated head")
		var seated: Vector3 = study.ship.transform.affine_inverse() * study.camera.position
		check(seated.distance_to(study.CockpitLayout.mount() * study.pilot_eye) < 0.001, "Head-look moved camera out of seat")
		var mouse := InputEventMouseButton.new()
		mouse.button_index = MOUSE_BUTTON_RIGHT
		mouse.pressed = false
		Input.parse_input_event(mouse)
		study.head_angles = Vector2(-0.19, 0)
		print("Native head-look input preserves the ship-relative pilot eye")
	return false
