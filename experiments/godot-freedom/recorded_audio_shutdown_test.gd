extends SceneTree
## Generated fixtures, real bridge and native Dummy playback. No speaker output.
const Fixtures = preload("res://recorded_ship_audio_test.gd")
class NativeAudio extends "res://recorded_ship_audio.gd":
	func _device_output_allowed() -> bool:
		return true
class TestMain extends "res://main.gd":
	var finished := 0
	var finished_at_usec := 0
	func _ready() -> void:
		set_process(false)
	func finish_quit() -> void:
		finished += 1
		finished_at_usec = Time.get_ticks_usec()
var failures := 0

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	call_deferred("run")

func native_references(audio: Node) -> Array[WeakRef]:
	# End all strong loop temporaries before the lifetime assertions below.
	var references: Array[WeakRef] = []
	for stream in audio._streams:
		references.append(weakref(stream))
	for player in audio._players:
		references.append(weakref(player.get_stream_playback()))
	return references

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy":
		push_error("Requires headless Dummy output")
		quit(1)
		return
	var snapshot := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot = argument.trim_prefix("--snapshot=")
	if not snapshot.is_absolute_path():
		push_error("Requires --snapshot=/absolute/native-snapshot.json")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var path := "user://recorded-shutdown-%d.wav" % OS.get_process_id()
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(Fixtures.fixture(2))
	file.close()
	for route in ["menu", "window", "silent", "blocked-retirement"]:
		var study := TestMain.new()
		root.add_child(study)
		study.snapshot_text = FileAccess.get_file_as_string(snapshot)
		study.data = JSON.parse_string(study.snapshot_text)
		study.options = {"--flight-model":"thrust", "--controls-persist":"false"}
		study.live_bridge = ClassDB.instantiate("FreedomBridge")
		check(study.live_bridge.initialize(study.snapshot_text) and study.live_bridge.enable_orbit_practice() and study.live_bridge.enable_streaming() and study.live_bridge.start_practice(false), "Native fixture initialization")
		study.live_presentation = true
		study.pilot_view = true
		var audio := NativeAudio.new()
		check(audio.configure_recordings(path, path), "Fixture configuration")
		study.ship_audio = audio
		study.add_child(audio)
		study.setup_player_controls()
		check(not auto_accept_quit, "Window close bypasses shutdown handler")
		if route != "silent":
			for i in 12:
				audio.update_telemetry(study.live_bridge.get_state(), true, true)
				audio._process(0.01)
			check(audio.diagnostics().playback_running, "No actual native playback before quit")
		var native_refs := native_references(audio)
		# Deliberately retain one real resource outside the node, simulating a
		# backend reference that does not retire. This tests the deadline without
		# mocking shutdown_drained() or changing production playback behavior.
		var retained_stream: AudioStreamWAV = audio._streams[0] if route == "blocked-retirement" else null
		var before: Dictionary = study.live_bridge.get_state()
		var requested_at_usec := Time.get_ticks_usec()
		if route == "window":
			study._notification(Node.NOTIFICATION_WM_CLOSE_REQUEST)
		else:
			study.pause_menu.quit_requested.emit()
		check(study.quitting and study.live_paused and not study.is_processing(), "Quit failed to freeze presentation")
		check(not audio.diagnostics().playback_running and audio.diagnostics().gains == Vector3.ZERO, "Quit retained native playback")
		check(study.finished == (1 if route == "silent" else 0), "Wrong native drain behavior")
		study.request_quit() # Repeated close must not enqueue another exit.
		study.set_player_paused(false)
		study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
		audio.set_muted(false)
		check(audio.set_mix_levels(1.0, 1.0, 1.0, 1.0), "Valid settings failed during shutdown")
		check(not audio.update_telemetry(before, true, true), "Telemetry revived shutting-down audio")
		check(not audio.configure_recordings(path, path), "Reconfiguration revived shutting-down audio")
		audio._process(0.01)
		audio._sync_players()
		study._process(0.1)
		check(study.live_paused and not audio.diagnostics().playback_running, "Input/focus revived flight or sound")
		check(audio.get_child_count() == 0 and audio._players.is_empty() and audio._streams.is_empty(), "Shutdown recreated native nodes or stream ownership")
		check(audio.diagnostics().targets == Vector3.ZERO and audio.diagnostics().gains == Vector3.ZERO, "Shutdown settings revived audible gain")
		await create_timer(0.60, true, false, true).timeout
		check(study.finished == 1, "Repeated quit or unfinished drain")
		if route == "blocked-retirement":
			var elapsed := float(study.finished_at_usec - requested_at_usec) / 1000000.0
			check(not audio.shutdown_drained(), "Retained-resource fixture did not block retirement")
			check(elapsed >= 0.49 and elapsed < 1.0, "Blocked retirement did not honor bounded cooperative deadline")
			print("Blocked native retirement: exit after %.3f seconds (cooperative 0.5-second deadline)" % elapsed)
			retained_stream = null
			check(audio.shutdown_drained(), "Released fixture retained a shutdown resource")
		check(study.live_bridge.get_state() == before, "Shutdown mutated native flight")
		study.free()
		for reference in native_refs:
			check(reference.get_ref() == null, "Native recording/playback reference survived normal shutdown")
	# No configured audio must exit immediately and must not instantiate sound.
	var silent_study := TestMain.new()
	root.add_child(silent_study)
	silent_study.request_quit()
	silent_study.request_quit()
	check(silent_study.finished == 1 and silent_study.ship_audio == null and silent_study.get_child_count() == 0, "No-audio exit created nodes or waited")
	silent_study.free()
	# The preserved procedural path exposes mute, not recorded retirement. Its
	# existing immediate-exit behavior must remain valid without a method error.
	var legacy_study := TestMain.new()
	root.add_child(legacy_study)
	var legacy := load("res://ship_audio.gd").new() as Node
	legacy_study.add_child(legacy)
	legacy_study.ship_audio = legacy
	legacy_study.request_quit()
	legacy_study.request_quit()
	check(legacy_study.finished == 1 and legacy.muted, "Legacy mute-only exit compatibility failed")
	check(legacy.get_child_count() == 0, "Legacy silent exit created playback nodes")
	legacy_study.free()
	check(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "Fixture cleanup")
	print("Recorded audio shutdown: %d failures; menu/window/repeated quit, native Dummy drain/deadline, no restart, legacy/silent exit, unchanged flight" % failures)
	quit(0 if failures == 0 else 1)
