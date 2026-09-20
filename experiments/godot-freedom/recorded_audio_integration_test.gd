extends SceneTree
## Real bridge/menu routing with generated audio only. No renderer bootstrap,
## hardware controller, physical audio device or subjective listening claim.
const Fixtures = preload("res://recorded_ship_audio_test.gd")
class HeadlessMain extends "res://main.gd":
	func _ready() -> void:
		set_process(false)
class SurfaceGate extends Node3D:
	var is_ready := false
var failures := 0
var study: Variant

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func refresh() -> void:
	var before: Dictionary = study.live_bridge.get_state()
	study._process(0)
	check(study.live_bridge.get_state() == before, "Audio refresh modified native state")

func settle() -> void:
	for i in 12:
		study.ship_audio._process(0.01)

func silent(reason: String) -> void:
	check(study.ship_audio.diagnostics().targets == Vector3.ZERO, reason + " target")
	settle()
	check(study.ship_audio.diagnostics().gains == Vector3.ZERO, reason + " envelope")

func key_event(code: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	await process_frame

func pad_event(button: JoyButton, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = 0
	event.button_index = button
	event.pressed = pressed
	Input.parse_input_event(event)
	await process_frame

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy":
		push_error("Requires --headless --audio-driver Dummy")
		quit(1)
		return
	var snapshot := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot = argument.trim_prefix("--snapshot=")
	if snapshot.is_empty() or not snapshot.is_absolute_path():
		push_error("Requires --snapshot=/absolute/native-snapshot.json")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Native bridge unavailable")
		quit(1)
		return
	var fixture_path := "user://recorded-integration-%d.wav" % OS.get_process_id()
	var file := FileAccess.open(fixture_path, FileAccess.WRITE)
	file.store_buffer(Fixtures.fixture(2))
	file.close()
	study = HeadlessMain.new()
	root.add_child(study)
	study.snapshot_text = FileAccess.get_file_as_string(snapshot)
	study.data = JSON.parse_string(study.snapshot_text)
	study.options = {"--flight-model": "thrust", "--ship-audio": "recorded",
		"--controls-persist": "false", "--audio-persist": "false",
		"--audio-hum": fixture_path, "--audio-propulsion": fixture_path}
	study.live_bridge = ClassDB.instantiate("FreedomBridge")
	check(study.live_bridge.initialize(study.snapshot_text) and study.live_bridge.enable_orbit_practice() and study.live_bridge.enable_streaming() and study.live_bridge.start_practice(false), "Bridge fixture setup failed")
	if failures:
		study.free()
		quit(1)
		return
	study.live_presentation = true
	study.pilot_view = true
	var before: Dictionary = study.live_bridge.get_state()
	study.setup_ship_audio()
	check(is_instance_valid(study.ship_audio), "Recorded opt-in failed")
	if not is_instance_valid(study.ship_audio):
		study.free()
		quit(1)
		return
	study.ship_audio.set_process(false)
	study.setup_player_controls()
	check(not study.player_input.persist and not study.audio_preferences.persist, "Integration enabled persistent settings")
	check(study.live_bridge.get_state() == before, "Audio/control setup mutated bridge")
	check(study.pause_menu.audio_sliders.size() == 4, "Missing per-bus controls")
	refresh()
	settle()
	check(study.ship_audio.diagnostics().gains.x > 0.99 and study.ship_audio.diagnostics().gains.y == 0, "Neutral cockpit lacks hum or adds propulsion")
	study.pilot_view = false
	refresh()
	silent("Exterior vacuum")
	study.pilot_view = true
	check(study.live_bridge.advance_thrust(1.0/120, PackedFloat64Array([0.8,0,0,0,0,0,0]), false), "Powered fixture failed")
	for i in 80:
		refresh()
		study.ship_audio._process(0.01)
	check(study.ship_audio.diagnostics().gains.y > 0, "Native power failed to request propulsion")
	study.set_player_paused(true)
	silent("Pause")
	await process_frame
	before = study.live_bridge.get_state()
	for key in ["master", "machinery", "propulsion", "atmosphere"]:
		var slider: HSlider = study.pause_menu.audio_sliders[key].slider
		slider.value = 0.5
		check(study.audio_preferences.levels[key] == 0.5, "Slider did not update preference")
		check(study.ship_audio.diagnostics().mix_levels == Vector4(study.audio_preferences.levels.master, study.audio_preferences.levels.machinery, study.audio_preferences.levels.propulsion, study.audio_preferences.levels.atmosphere), "Slider not connected to node")
		check(slider.focus_mode == Control.FOCUS_ALL, "Slider not keyboard/controller focusable")
	var master: HSlider = study.pause_menu.audio_sliders.master.slider
	master.grab_focus()
	await process_frame
	check(root.gui_get_focus_owner() == master, "Master slider could not acquire actual GUI focus")
	await key_event(KEY_LEFT, true)
	await key_event(KEY_LEFT, false)
	check(master.value < 0.5, "Keyboard left did not operate focused slider")
	var key_level := master.value
	# Explicit fixture device binding; no physical controller required or polled.
	study.player_input.device = 0
	study.player_input.install()
	await pad_event(JOY_BUTTON_DPAD_RIGHT, true)
	await pad_event(JOY_BUTTON_DPAD_RIGHT, false)
	check(master.value > key_level, "D-pad did not operate focused slider")
	await pad_event(JOY_BUTTON_DPAD_DOWN, true)
	await pad_event(JOY_BUTTON_DPAD_DOWN, false)
	check(root.gui_get_focus_owner() == study.pause_menu.audio_sliders.machinery.slider, "D-pad did not move focus to next audio slider")
	check(study.live_paused and study.player_input.sample().thrust_axes == PackedFloat64Array([0,0,0,0,0,0,0]), "Menu navigation leaked into flight demand")
	check(study.live_bridge.get_state() == before, "Menu navigation advanced flight")
	study.pause_menu.audio_toggle.button_pressed = true
	check(study.ship_audio.muted and study.audio_preferences.muted, "Actual mute control not routed")
	var reset: Button
	for button in study.pause_menu.find_children("*", "Button", true, false):
		if button.text == "Restore ship audio defaults":
			reset = button
	check(reset != null, "Missing reset control")
	if reset != null:
		reset.pressed.emit()
	check(not study.ship_audio.muted and not study.audio_preferences.muted and study.ship_audio.diagnostics().mix_levels == Vector4.ONE, "Defaults failed to reset mute/mix")
	silent("Reset while paused")
	study.set_player_paused(false)
	refresh()
	settle()
	check(study.ship_audio.diagnostics().gains.length() > 0, "Resume did not restore fresh cockpit sound")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	silent("Focus loss")
	var level_before: float = master.value
	master.grab_focus()
	await key_event(KEY_LEFT, true)
	await key_event(KEY_LEFT, false)
	check(master.value == level_before, "Unfocused safety-pause consumed queued slider navigation")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	refresh()
	silent("Focus regain must not auto-resume")
	study.pause_menu.resumed.emit()
	refresh()
	study.ship_audio._process(0.5)
	silent("Stale render interval")
	refresh()
	settle()
	check(study.ship_audio.diagnostics().gains.length() > 0, "Fresh telemetry did not restore after stall")
	for mode in [2,3,4]:
		study.mode = mode
		refresh()
		silent("Inspection mode")
	study.mode = 1
	var gate := SurfaceGate.new()
	study.add_child(gate)
	study.planet_stream = gate
	refresh()
	silent("Terrain not ready")
	gate.is_ready = true
	refresh()
	settle()
	check(study.ship_audio.diagnostics().gains.length() > 0, "Ready terrain failed to restore")
	study.live_presentation = false
	refresh()
	silent("Non-live presentation")
	study.live_presentation = true
	var bridge: Variant = study.live_bridge
	before = bridge.get_state()
	study.live_bridge = null
	study._process(0)
	silent("Missing bridge")
	check(bridge.get_state() == before, "Availability gate mutated bridge")
	study.live_bridge = bridge
	# Explicit native fixture relocation exercises real effective density/q;
	# these changes belong to the test, never to the audio update itself.
	check(bridge.set_survey_pose(0.25, 0.4, 10000.0), "Atmospheric fixture relocation failed")
	check(bridge.advance_thrust(0.1, PackedFloat64Array([1,0,0,0,0,0,0]), false), "Atmospheric native thrust step failed")
	var air_state: Dictionary = bridge.get_state()
	check(air_state.effective_air_density > 0 and air_state.dynamic_pressure > 0, "Snapshot lacked atmospheric audio fixture")
	refresh()
	check(study.ship_audio.diagnostics().targets.z > 0, "Real atmospheric pressure failed to enable separate airflow")
	check(not study.ship_audio.diagnostics().playback_running, "Production headless path opened audio device")
	study.free()
	await process_frame
	check(DirAccess.remove_absolute(ProjectSettings.globalize_path(fixture_path)) == OK, "Fixture cleanup failed")
	print("Recorded audio main/bridge/GUI-event integration: %d failures; headless Dummy, no hardware/listening claim" % failures)
	quit(0 if failures == 0 else 1)
