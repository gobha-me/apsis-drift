extends SceneTree
## Silent headless integration: real native telemetry, inherited main audio and
## menu/focus routing. Graphics bootstrap and the flight-frame body are omitted;
## no listening quality, GPU, hardware-controller or acoustic realism claim.
class HeadlessMain extends "res://main.gd":
	func _ready() -> void:
		set_process(false)

class SurfaceGate extends Node3D:
	var is_ready := false

const NEUTRAL := [0, 0, 0, 0, 0, 0, 0]
var failures := 0
var study: Variant

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func sample_peak() -> float:
	var peak := 0.0
	var samples: PackedVector2Array = study.ship_audio.synthesize_frames(1024)
	check(samples.size() == 1024, "Audio integration returned wrong frame count")
	for sample in samples:
		check(sample.is_finite(), "Audio integration emitted non-finite samples")
		peak = maxf(peak, maxf(absf(sample.x), absf(sample.y)))
	return peak

func refresh_audio() -> void:
	# The inherited audio update runs before the omitted graphics/flight body.
	var before: Dictionary = study.live_bridge.get_state()
	study._process(0)
	check(study.live_bridge.get_state() == before, "Audio refresh changed authoritative native state")

func check_silent(reason: String) -> void:
	var before: Dictionary = study.live_bridge.get_state()
	check(study.ship_audio.diagnostics().targets == Vector3.ZERO, reason + " (targets)")
	# The source deliberately fades over 60ms rather than clicking instantly.
	# Two 1024-frame blocks at 24kHz exceed that bounded fade; then require zero.
	sample_peak()
	sample_peak()
	check(sample_peak() == 0.0, reason)
	check(study.ship_audio.diagnostics().gains == Vector3.ZERO, reason + " (settled gains)")
	check(study.live_bridge.get_state() == before, "Silent synthesis changed authoritative native state")

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	# Require the invocation itself to select Dummy: no system volume changes,
	# host devices or audible desktop playback are part of this test.
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy":
		push_error("Run audio integration with --headless --audio-driver Dummy")
		quit(1)
		return
	var snapshot_path := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot_path = argument.trim_prefix("--snapshot=")
	if snapshot_path.is_empty() or not snapshot_path.is_absolute_path():
		push_error("Audio integration requires --snapshot=/absolute/path/to/native-snapshot.json")
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
	if not study.live_bridge.initialize(study.snapshot_text) or not study.live_bridge.enable_orbit_practice() or not study.live_bridge.enable_streaming() or not study.live_bridge.start_practice(false):
		push_error("Native audio fixture could not initialize orbit practice")
		quit(1)
		return
	study.live_presentation = true
	study.pilot_view = true
	study.setup_ship_audio()
	check(not is_instance_valid(study.ship_audio), "Ship audio silently enabled without opt-in")
	study.options["--ship-audio"] = "false"
	study.setup_ship_audio()
	check(not is_instance_valid(study.ship_audio), "Explicitly disabled ship audio instantiated")
	study.options["--ship-audio"] = "true"
	study.live_presentation = false
	study.setup_ship_audio()
	check(not is_instance_valid(study.ship_audio), "Audio instantiated outside native live presentation")
	study.live_presentation = true
	var before: Dictionary = study.live_bridge.get_state()
	study.setup_ship_audio()
	check(is_instance_valid(study.ship_audio), "Opt-in audio did not instantiate")
	if not is_instance_valid(study.ship_audio):
		study.queue_free()
		quit(1)
		return
	study.ship_audio.set_process(false) # Explicit synthesis only; Dummy remains selected.
	study.setup_player_controls()
	study.player_input.assist = false
	study.player_input.sample()
	check(not study.player_input.persist, "Audio fixture must not write player settings")
	check(study.pause_menu.ship_audio_available and is_instance_valid(study.pause_menu.audio_toggle), "Opt-in audio lacks actual pause-menu mute control")
	check(study.live_bridge.get_state() == before, "Audio/menu setup changed authoritative native state")
	check(study.live_bridge.advance_thrust(1.0 / 120, PackedFloat64Array([0.8, 0, 0, 0, 0, 0, 0]), false), "Native powered audio fixture failed")
	before = study.live_bridge.get_state()
	check(before.main_thrust > 0.0, "Powered fixture has no authoritative engine demand")
	refresh_audio()
	check(sample_peak() > 0.0, "Active cockpit telemetry did not produce samples")
	check(study.ship_audio.diagnostics().active and study.ship_audio.diagnostics().valid and study.ship_audio.diagnostics().targets.y > 0.0, "Native thrust was not mapped to active engine target")
	check(study.live_bridge.get_state() == before, "Active synthesis changed authoritative native state")
	study.pilot_view = false
	refresh_audio()
	check_silent("Exterior vacuum view retained cockpit-conducted engine audio")
	study.pilot_view = true
	refresh_audio()
	check(sample_peak() > 0.0, "Cockpit view failed to restore structure-conducted audio")

	# Actual CheckButton -> pause-menu signal -> main connection -> audio node.
	study.pause_menu.audio_toggle.button_pressed = true
	check(study.ship_audio.diagnostics().muted, "Actual mute control did not change session audio flag")
	check_silent("Actual mute control left active samples")
	refresh_audio()
	check_silent("Telemetry refresh bypassed session mute")
	study.pause_menu.audio_toggle.button_pressed = false
	check(not study.ship_audio.diagnostics().muted, "Actual unmute control did not clear session audio flag")
	refresh_audio()
	check(sample_peak() > 0.0, "Actual unmute control did not restore fresh active telemetry")

	study.set_player_paused(true)
	check_silent("Explicit pause left prior engine demand sounding")
	refresh_audio()
	check_silent("Paused frame revived ship audio")
	# Change authoritative telemetry directly while paused to test stale-demand
	# rejection; this is a fixture operation, not paused-flight behavior.
	check(study.live_bridge.advance_thrust(1.0 / 120, PackedFloat64Array(NEUTRAL), false), "Neutral resume fixture failed")
	study.set_player_paused(false)
	refresh_audio()
	check(study.live_bridge.get_state().main_thrust == 0.0, "Resume fixture retained native engine demand")
	var neutral_audio: Dictionary = study.ship_audio.diagnostics()
	check(neutral_audio.active and neutral_audio.valid and neutral_audio.targets.y == 1.0 and neutral_audio.targets.x == 0.0 and neutral_audio.requested_load == 0.0, "Resume failed to restore current neutral powered voice without stale thrust or a duplicate idle bed")

	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	check(study.live_paused, "Focus loss did not invoke safety pause")
	check_silent("Focus loss left ship audio sounding")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	refresh_audio()
	check_silent("Focus regain silently resumed ship audio")
	study.pause_menu.audio_toggle.button_pressed = true
	study.pause_menu.audio_toggle.button_pressed = false
	check_silent("Unmuting while safety-paused revived stale engine audio")
	study.pause_menu.resumed.emit()
	refresh_audio()
	check(sample_peak() > 0.0, "Explicit resume failed to restore cockpit ambience")

	# An open menu also gates audio independently of the live_paused flag.
	study.pause_menu.panel.show()
	refresh_audio()
	check_silent("Visible pause menu failed to gate ship audio")
	study.pause_menu.panel.hide()
	refresh_audio()
	check(sample_peak() > 0.0, "Closing menu failed to restore fresh active telemetry")
	for inspection_mode in [2, 3, 4]:
		study.mode = inspection_mode
		refresh_audio()
		check_silent("Inspection mode retained ship audio")
	study.mode = 1
	var gate := SurfaceGate.new()
	study.add_child(gate)
	study.planet_stream = gate
	refresh_audio()
	check_silent("Terrain-not-ready gate retained ship audio")
	gate.is_ready = true
	refresh_audio()
	check(sample_peak() > 0.0, "Ready terrain did not restore cockpit ambience")
	study.live_presentation = false
	refresh_audio()
	check_silent("Non-live presentation retained ship audio")
	study.live_presentation = true
	refresh_audio()
	var bridge: Variant = study.live_bridge
	before = bridge.get_state()
	study.live_bridge = null
	study._process(0)
	sample_peak()
	sample_peak()
	check(sample_peak() == 0.0, "Unavailable bridge retained ship audio")
	check(not study.ship_audio.diagnostics().valid and not study.ship_audio.diagnostics().active, "Unavailable bridge kept valid active audio telemetry")
	study.live_bridge = bridge
	check(bridge.get_state() == before, "Availability gate changed native state")
	refresh_audio()
	check(sample_peak() > 0.0, "Bridge restoration did not restore fresh cockpit telemetry")
	check(study.ship_audio.diagnostics().queued_frames == 0, "Dummy/headless fixture unexpectedly queued device audio")
	print("Ship audio main/bridge integration: %d failures; Dummy output, real native telemetry/menu/focus gates; graphics bootstrap and flight-frame body omitted" % failures)
	study.queue_free()
	quit(0 if failures == 0 else 1)
