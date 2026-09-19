extends SceneTree
const ShipAudio = preload("res://ship_audio.gd")
var failures := 0

class DummyAudio extends ShipAudio:
	func _device_output_allowed() -> bool:
		# Never permit this test adapter to use an audible output driver.
		return AudioServer.get_driver_name() == "Dummy"

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func telemetry() -> Dictionary:
	return {"main_thrust": 1.0, "retro_thrust": 0.0, "dynamic_pressure": 20000.0, "effective_air_density": 0.1}

func pump(audio: Node, ticks: int, active: bool) -> void:
	for tick in ticks:
		audio.update_telemetry(telemetry(), true, active)
		audio._process(0.016)
		var state: Dictionary = audio.diagnostics()
		check(state.queued_frames >= 0 and state.queued_frames <= ShipAudio.QUEUE_FRAMES, "Playback queue exceeded its bound")
		check(not state.device_failed, "Dummy playback adapter failed")
		await create_timer(0.016).timeout

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if AudioServer.get_driver_name() != "Dummy":
		push_error("Playback test requires --audio-driver Dummy; refusing any audible device")
		quit(1)
		return
	var audio := DummyAudio.new()
	root.add_child(audio)
	audio.set_process(false)
	check(not audio.diagnostics().playback_running, "Playback opened before activity")
	await pump(audio, 12, true)
	check(audio.diagnostics().playback_running and audio.diagnostics().rendered_frames > 1000, "Real Dummy generator path did not run")
	audio.set_muted(true)
	await pump(audio, 30, true)
	check(not audio.diagnostics().playback_running and audio.diagnostics().queued_frames == 0, "Mute did not drain, stop and retire queued demand")
	audio.update_telemetry(telemetry(), true, false)
	audio.set_muted(false)
	await pump(audio, 3, false)
	check(not audio.diagnostics().playback_running, "Unmute replayed paused demand")
	await pump(audio, 10, true)
	check(audio.diagnostics().playback_running, "Fresh active telemetry did not resume")
	audio._process(0.5)
	check(not audio.diagnostics().playback_running and audio.diagnostics().queued_frames == 0, "Long stall retained a stale playback queue")
	audio._process(0.016)
	check(not audio.diagnostics().playback_running, "Stall recovery replayed stale targets")
	await pump(audio, 10, true)
	await pump(audio, 30, false)
	check(not audio.diagnostics().playback_running and audio.diagnostics().gains == Vector3.ZERO, "Inactive transition failed to drain and silence")
	audio.free()
	print("Ship audio real-generator Dummy lifecycle: %d failures (no audible device/hotplug qualification)" % failures)
	quit(0 if failures == 0 else 1)
