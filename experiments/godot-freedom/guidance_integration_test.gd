extends SceneTree
## Headless integration of inherited main methods and the real native bridge.
## Only graphics bootstrap/world-view drawing are stubbed; no GPU, assets,
## controller hardware, SteamOS, pixels or terrain-render qualification claimed.
class HeadlessMain extends "res://main.gd":
	func _ready() -> void:
		set_process(false)

class NoWorldDrawing extends Node:
	func refresh(_study: Node, _altitude: float, _radius: float) -> void:
		pass

const DEVICE := 77
var failures := 0
var study: Variant

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func pad(code: JoyButton, pressed: bool, device := DEVICE) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func press_pad(code: JoyButton, device := DEVICE) -> void:
	pad(code, true, device)
	pad(code, false, device)

func press_key(code: Key) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.physical_keycode = code
		event.keycode = code
		event.pressed = pressed
		Input.parse_input_event(event)
		Input.flush_buffered_events()

func refresh() -> void:
	# Invoke the actual scene update with zero elapsed time: refresh presentation
	# while proving that selecting/querying guidance cannot advance flight.
	study.guidance_elapsed = 1.0
	study.flight_displays.elapsed = 1.0
	study._process(0)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var snapshot_path := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot_path = argument.trim_prefix("--snapshot=")
	if snapshot_path.is_empty():
		push_error("Guidance integration requires --snapshot=/path/to/native-snapshot.json")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Native bridge missing")
		quit(1)
		return
	study = HeadlessMain.new()
	root.add_child(study)
	study.snapshot_text = FileAccess.get_file_as_string(snapshot_path)
	study.data = JSON.parse_string(study.snapshot_text)
	study.options = {"--flight-model": "thrust", "--controls-persist": "false"}
	study.live_bridge = ClassDB.instantiate("FreedomBridge")
	if not study.live_bridge.initialize(study.snapshot_text) or not study.live_bridge.enable_surface_practice() or not study.live_bridge.enable_streaming() or not study.live_bridge.start_practice(false):
		push_error("Native integration fixture could not initialize orbit practice")
		quit(1)
		return
	study.live_presentation = true
	study.streaming_requested = true
	study.pilot_view = true
	study.ship = Node3D.new()
	study.add_child(study.ship)
	study.camera = Camera3D.new()
	study.add_child(study.camera)
	study.pilot_cockpit = Node3D.new()
	study.ship.add_child(study.pilot_cockpit)
	study.scene_environment = Environment.new()
	study.scene_environment.sky = Sky.new()
	study.navigation_sky = ShaderMaterial.new()
	study.navigation_sky.shader = preload("res://navigation_sky.gdshader")
	study.world_view = NoWorldDrawing.new()
	study.add_child(study.world_view)
	study.debug_backdrop = ColorRect.new()
	study.add_child(study.debug_backdrop)
	study.label = Label.new()
	study.add_child(study.label)
	study.caption = Label.new()
	study.add_child(study.caption)
	study.flight_displays = preload("res://flight_displays.gd").new()
	study.pilot_cockpit.add_child(study.flight_displays)
	study.setup_player_controls()
	var controls: Node = study.player_input
	controls.device = DEVICE
	controls.install()
	controls.assist = study.live_bridge.get_state().assist
	controls.sample()
	check(not controls.persist, "Integration fixture must never write player settings")
	var initial: Dictionary = study.live_bridge.get_state()
	refresh()
	check(study.flight_plan == 0 and study.flight_guidance.is_empty() and not study.flight_plan_menu.active, "Native guidance not optional/default-off")
	check(not study.guidance_overlay.visible and not study.flight_displays.guidance_screen.visible, "Default flight replaced NAV with guidance")
	check(study.live_bridge.get_state() == initial, "Zero-time presentation refresh mutated authoritative flight")

	# Native queries, including malformed/clear requests, are strictly read-only.
	for plan in [-1, 0, 1, 2, 3, 4]:
		var guidance: Dictionary = study.live_bridge.get_flight_guidance(plan)
		check(bool(guidance.ok) == (plan >= 0 and plan <= 3), "Guidance mode validation changed")
		check(study.live_bridge.get_state() == initial, "Guidance query mutated native state/checksum")
		if plan > 0 and plan < 4:
			check(guidance.mode == plan and guidance.coast.size() >= 1 and guidance.coast.size() <= 129 and guidance.reference.size() <= 97, "Bridge guidance buffers/version mode invalid")
			for points in [guidance.coast, guidance.reference]:
				for point in points:
					check(point.is_finite(), "Nonfinite trajectory reached presentation")
			check(guidance.cue is String, "Cue ordinal leaked across presentation boundary")
			check(guidance.assist == initial.assist and is_finite(guidance.seconds) and is_finite(guidance.delta_speed) and guidance.body_delta.is_finite(), "Guidance telemetry inconsistent or nonfinite")
	check(study.live_bridge.get_flight_guidance(2).cue == "return", "Return reference cue has wrong semantic mapping")
	check(study.live_bridge.get_flight_guidance(3).cue == "escape", "Bound-orbit escape cue has wrong semantic mapping")
	press_pad(JOY_BUTTON_DPAD_UP, DEVICE + 1)
	check(not study.flight_plan_menu.active, "Inactive controller opened live guidance")
	press_pad(JOY_BUTTON_DPAD_UP)
	check(study.flight_plan_menu.active and not study.live_paused, "D-pad opener paused flight or failed")
	press_pad(JOY_BUTTON_DPAD_DOWN)
	press_pad(JOY_BUTTON_A)
	check(study.flight_plan == 1 and not study.flight_plan_menu.active and not study.live_paused, "Controller orbit selection failed")
	check(study.live_bridge.get_state() == initial and controls.assist == initial.assist, "Selecting orbit actuated or altered assistance")
	refresh()
	check(study.flight_guidance.ok and study.flight_guidance.mode == 1 and study.flight_displays.guidance_screen.visible and not study.guidance_overlay.visible, "Cockpit selected guidance not confined to NAV display")
	check(study.flight_displays.screens[1].state.checksum == initial.checksum and study.flight_displays.screens[2].state.assist == initial.assist, "Guidance corrupted other instrument telemetry")
	study.pilot_view = false
	refresh()
	check(study.guidance_overlay.visible and study.guidance_overlay.guidance == study.flight_guidance, "Chase view missing optional guidance overlay")
	press_key(KEY_G)
	refresh()
	check(study.flight_plan_menu.active and not study.guidance_overlay.visible, "Selector and chase guidance overlap")
	var before: Dictionary = study.live_bridge.get_state()
	Input.action_press("pilot_forward", 0.6)
	for tick in 24:
		study._process(1.0 / 120)
	var moving: Dictionary = study.live_bridge.get_state()
	check(study.flight_plan_menu.active and not study.live_paused and moving.tick == before.tick + 24 and moving.main_thrust > 0, "Live menu stopped flight or stole held engine input")
	Input.action_release("pilot_forward")
	# Consume the player's release before isolating the clear operation. The
	# bridge updates request-button telemetry even on a zero-tick input sample.
	study._process(0)
	check(study.live_bridge.get_state().checksum == moving.checksum and study.live_bridge.get_state().tick == moving.tick, "Zero-time trigger release changed physical state")
	press_pad(JOY_BUTTON_START)
	check(not study.flight_plan_menu.active and not study.live_paused and not study.pause_menu.panel.visible, "Start closing selector unexpectedly paused simulation")
	before = study.live_bridge.get_state()
	study.select_flight_plan(0)
	check(study.live_bridge.get_state() == before, "Clearing guidance itself mutated flight")
	refresh()
	check(study.live_bridge.get_state() == before and study.flight_guidance.is_empty() and not study.guidance_overlay.visible and not study.flight_displays.guidance_screen.visible, "Clearing guidance altered flight or failed to restore normal NAV")
	study.select_flight_plan(-1)
	study.select_flight_plan(4)
	check(study.flight_plan == 0, "Invalid UI selection changed plan")

	# Legacy custom mappings own their input. The pause-menu fallback remains.
	check(controls.rebind("strafe_left", "pad", controls.button(JOY_BUTTON_DPAD_DOWN)), "Custom D-pad fixture failed")
	controls.sample()
	press_pad(JOY_BUTTON_DPAD_UP)
	check(not study.flight_plan_menu.active and not study.plan_shortcut_available(true), "Opener ignored existing D-pad flight remap")
	check(controls.rebind("forward", "key", {"kind": "key", "code": KEY_G}), "Custom guidance-key fixture failed")
	controls.sample()
	press_key(KEY_G)
	check(not study.flight_plan_menu.active and not study.plan_shortcut_available(false), "Guidance opener stole custom G thrust binding")
	study.set_player_paused(true)
	check(study.pause_menu.panel.visible, "Pause fallback did not open")
	study.pause_menu.guidance_requested.emit()
	check(study.flight_plan_menu.active and not study.live_paused and controls.enabled, "Pause-menu guidance fallback did not resume live selector")
	before = study.live_bridge.get_state()
	press_pad(JOY_BUTTON_DPAD_DOWN)
	check(study.flight_plan_menu.selected_plan == 0 and study.live_bridge.get_state() == before, "Live selector took mapped D-pad flight button")
	press_key(KEY_ESCAPE)
	check(not study.flight_plan_menu.active and not study.live_paused, "Escape from guidance paused instead of closing")
	controls.defaults()
	controls.install()
	controls.sample()
	press_key(KEY_G)
	check(study.flight_plan_menu.active, "Restored keyboard opener failed")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	before = study.live_bridge.get_state()
	for tick in 8:
		study._process(1.0 / 120)
	check(not study.flight_plan_menu.active and study.live_paused and study.pause_menu.panel.visible and study.live_bridge.get_state() == before, "Focus loss leaked selector or advanced simulation")
	press_pad(JOY_BUTTON_A)
	check(study.live_paused and study.flight_plan == 0, "Unfocused confirmation resumed or selected guidance")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	study._process(1.0 / 120)
	check(study.live_paused and study.live_bridge.get_state() == before, "Focus regain silently resumed flight")
	study.set_player_paused(false)
	controls.sample()
	check(study.live_bridge.get_state() == before, "Explicit resume itself advanced state")
	print("Guidance main/bridge integration: %d failures; real input routing and C++ state, graphics bootstrap stubbed; no GPU/hardware qualification" % failures)
	study.queue_free()
	quit(0 if failures == 0 else 1)
