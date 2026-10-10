extends SceneTree
## Generated WAVs and Dummy playback only; no subjective/device qualification.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const SessionAudio = preload("res://scripts/audio/native_ship_audio.gd")
const Fixtures = preload("res://tests/recorded_ship_audio_test.gd")
var failures := 0

class TestShell extends "res://scripts/native/native_start_shell.gd":
	var quit_finished := false
	func finish_native_quit() -> void:
		quit_finished = true


func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)


func settle(session: Node, count := 12) -> void:
	for i in count:
		session.refresh()
		session.audio._process(0.01)


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or AudioServer.get_driver_name() != "Dummy": quit(1); return
	var shell := TestShell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	var wave := ProjectSettings.globalize_path("user://native-audio.wav")
	var output := ProjectSettings.globalize_path("user://native-audio-save.json")
	for bad in [["--new-game=42", "--audio-hum=" + wave], ["--new-game=42", "--audio-persist=false"], ["--new-game=42", "--audio-hum=relative.wav", "--audio-propulsion=" + wave], ["--new-game=42", "--audio-hum=" + wave, "--audio-hum=" + wave, "--audio-propulsion=" + wave], ["--new-game=42", "--audio-hum=" + wave, "--audio-propulsion=" + wave, "--audio-persist=invalid"]]:
		check(shell.parse_selection(PackedStringArray(bad)).is_empty(), "Malformed audio selection accepted")
	var selection := shell.parse_selection(PackedStringArray(["--continue=" + args[0], "--audio-hum=" + wave, "--audio-propulsion=" + wave, "--audio-persist=false"]))
	check(not selection.is_empty() and not selection.audio_persist, "Valid paired recording selection refused")
	var file := FileAccess.open(wave, FileAccess.WRITE)
	file.store_buffer(Fixtures.fixture(2))
	file.close()
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"): shell.free(); quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	selection.assets = args[2]
	var source_hash := FileAccess.get_sha256(args[0])
	check(shell.select_start(owner, selection), "Audio flight did not stage: " + shell.error)
	if failures: shell.free(); quit(1); return
	var view: Control = shell.current_view
	var session: Node = shell.audio_session
	var audio: Node = session.audio
	session.set_process(false)
	audio.set_process(false)
	check(not session.preferences.persist and view.controls_menu.audio_sliders.size() == 4, "Saved flight lost nonpersistent volume controls")
	var state: Dictionary = owner.get_freedom_flight_state()
	check(not SessionAudio.telemetry(state).is_empty(), "Actual C++ telemetry refused")
	for key in ["air_density", "dynamic_pressure"]:
		for value in [NAN, INF, -INF, -1.0, "0", true, null]:
			var bad := state.duplicate(true)
			bad[key] = value
			check(SessionAudio.telemetry(bad).is_empty(), "Invalid medium telemetry accepted")
	for key in ["positive_force_body", "negative_force_body", "positive_force_ratings", "negative_force_ratings"]:
		for size in [0, 2, 4]:
			var bad := state.duplicate(true)
			var values := PackedFloat64Array()
			values.resize(size)
			bad[key] = values
			check(SessionAudio.telemetry(bad).is_empty(), "Invalid applied-force dimensions accepted")
		var bad := state.duplicate(true)
		bad[key][0] = NAN
		check(SessionAudio.telemetry(bad).is_empty(), "Nonfinite applied force accepted")
	check(owner.save_freedom_as(output), "Could not save audio baseline")
	var baseline := FileAccess.get_sha256(output)
	settle(session)
	check(audio.diagnostics().targets == Vector3.ZERO, "Paused Continue became audible")
	view.cockpit = true
	view.toggle_pause()
	settle(session)
	check(audio.diagnostics().gains.x > 0.99 and audio.diagnostics().requested_load == 0.0, "Neutral cockpit lost machinery or invented propulsion")
	view.cockpit = false
	settle(session)
	check(audio.diagnostics().gains == Vector3.ZERO, "Exterior vacuum retained internal sound")
	view.cockpit = true
	var commands := PackedFloat64Array([0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0])
	check(owner.advance_freedom_flight(1.0 / 120.0, commands, false), "Actual propulsion refused")
	view.state = owner.get_freedom_flight_state()
	check(owner.save_freedom_as(output), "Could not save propulsion checkpoint")
	baseline = FileAccess.get_sha256(output)
	settle(session, 80)
	check(audio.diagnostics().requested_load > 0.0 and audio.diagnostics().gains.y > 0.0, "Actual applied thrust did not drive recording")
	check(owner.save_freedom_as(output) and FileAccess.get_sha256(output) == baseline, "Playback changed complete flight save")
	# Both real opposing channels remain audible even when net effort differs.
	commands[2] = 1.0
	check(owner.advance_freedom_flight(1.0 / 120.0, commands, false), "Opposing propulsion refused")
	view.state = owner.get_freedom_flight_state()
	var applied := SessionAudio.telemetry(view.state)
	check(applied.main_thrust > 0.0 and applied.retro_thrust > 0.0, "Opposing gross firing was erased")
	var transit: Dictionary = view.state.duplicate(true)
	transit.jump = {"phase": "transit"}
	var bubble := SessionAudio.telemetry(transit)
	check(bubble.main_thrust == 0.0 and bubble.retro_thrust == 0.0 and bubble.dynamic_pressure == 0.0, "Transit invented propulsion or airflow")
	check(owner.save_freedom_as(output), "Could not save opposing checkpoint")
	baseline = FileAccess.get_sha256(output)
	settle(session)
	audio._process(0.3)
	check(audio.diagnostics().gains == Vector3.ZERO, "Long frame retained stale playback")
	view.focused = false
	settle(session)
	check(audio.diagnostics().gains == Vector3.ZERO, "Focus loss retained playback")
	view.focused = true
	view.pause_controls("Audio test pause")
	settle(session)
	check(audio.diagnostics().gains == Vector3.ZERO, "Pause retained playback")
	view.controls_menu.audio_toggle.set_pressed(true)
	check(session.preferences.muted and audio.diagnostics().muted, "Native mute did not reach playback")
	view.controls_menu.audio_sliders.master.slider.value = 0.5
	check(session.preferences.levels.master == 0.5 and audio.diagnostics().mix_levels.x == 0.5, "Native slider did not reach shared mix")
	check(owner.save_freedom_as(output) and FileAccess.get_sha256(output) == baseline, "Audio UI changed complete save")
	var playback_id := audio.get_instance_id()
	check(shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": args[2]}), "Walking selection refused")
	check(shell.audio_session.audio.get_instance_id() == playback_id, "View handoff duplicated recording owner")
	settle(session)
	check(audio.diagnostics().gains == Vector3.ZERO, "Station walking retained craft playback")
	var before: Dictionary = owner.get_freedom_walk_state()
	check(not shell.select_start(owner, {"mode": "continue", "value": args[1], "assets": args[2]}), "Corrupt Continue accepted")
	check(owner.get_freedom_walk_state() == before and session.view == shell.current_view, "Failed staging changed active owner")
	check(FileAccess.get_sha256(args[0]) == source_hash, "Audio rewrote source save")
	# The actual close path drains reused, stopped players while preserving state.
	shell._notification(Node.NOTIFICATION_WM_CLOSE_REQUEST)
	var deadline := Time.get_ticks_msec() + 750
	while not shell.quit_finished and Time.get_ticks_msec() < deadline:
		await process_frame
	check(shell.quit_finished and session.shutdown_drained(), "Window close did not finish cooperative shutdown")
	check(owner.get_freedom_walk_state() == before, "Shutdown advanced the authoritative clock")
	shell.free()
	DirAccess.remove_absolute(wave)
	DirAccess.remove_absolute(output)
	print("Native saved audio: %d failures" % failures)
	quit(1 if failures else 0)
