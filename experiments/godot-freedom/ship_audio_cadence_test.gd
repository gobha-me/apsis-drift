extends SceneTree
## Dummy-only cadence/CPU observation, not portable performance acceptance.
const ShipAudio = preload("res://ship_audio.gd")
var failures := 0

class DummyAudio extends ShipAudio:
	func _device_output_allowed() -> bool:
		return AudioServer.get_driver_name() == "Dummy"

func _initialize() -> void:
	call_deferred("run")

func controlled_consumer() -> Array:
	# Standalone real generator, no AudioStreamPlayer and no driver scheduling.
	# Consume in <=64-output-frame slices to separate render cadence from Dummy's
	# unusually large backend bursts. This is not a hardware-device simulation.
	var reports := []
	var output_rate := int(AudioServer.get_mix_rate())
	for fps in [60, 30, 20, 15, 10]:
		var audio := DummyAudio.new()
		var stream := AudioStreamGenerator.new()
		stream.mix_rate = ShipAudio.SAMPLE_RATE
		stream.buffer_length = 0.1
		var playback := stream.instantiate_playback() as AudioStreamGeneratorPlayback
		playback.start()
		audio._playback = playback
		audio._capacity = playback.get_frames_available()
		var skips_before := 0
		var max_work := 0
		for tick in fps * 2:
			if tick == fps:
				skips_before = playback.get_skips()
			audio.update_telemetry({"main_thrust": 0.8, "retro_thrust": 0.0,
				"dynamic_pressure": 20000.0, "effective_air_density": 0.1}, true, true)
			var before: int = audio.diagnostics().rendered_frames
			audio._process(1.0 / fps)
			max_work = maxi(max_work, audio.diagnostics().rendered_frames - before)
			if audio.diagnostics().queued_frames > ShipAudio.QUEUE_FRAMES or max_work > ShipAudio.MAX_PROCESS_FRAMES:
				failures += 1
			var remaining := int(float((tick + 1) * output_rate) / fps) - int(float(tick * output_rate) / fps)
			while remaining > 0:
				var count := mini(64, remaining)
				playback.mix_audio(1.0, count)
				remaining -= count
		var skips := playback.get_skips() - skips_before
		if fps >= 20 and skips != 0:
			failures += 1
		reports.append({"fps": fps, "underruns": skips, "max_process_frames": max_work})
		playback.stop()
		audio.free()
	return reports

func run() -> void:
	if AudioServer.get_driver_name() != "Dummy":
		push_error("Cadence probe requires Dummy; refusing audible output")
		quit(1)
		return
	var controlled := controlled_consumer()
	var reports := []
	for fps in [60, 30, 20, 15, 10]:
		var audio := DummyAudio.new()
		root.add_child(audio)
		audio.set_process(false)
		var elapsed_usec := 0
		var peak_usec := 0
		var skips_before := 0
		var rendered_before := 0
		var begin := 0
		for tick in fps * 2:
			var started := Time.get_ticks_usec()
			audio.update_telemetry({"main_thrust": 0.8, "retro_thrust": 0.0,
				"dynamic_pressure": 20000.0, "effective_air_density": 0.1}, true, true)
			audio._process(1.0 / fps)
			var cost := Time.get_ticks_usec() - started
			if tick == fps:
				skips_before = audio.diagnostics().underruns
				rendered_before = audio.diagnostics().rendered_frames
				begin = Time.get_ticks_usec()
			if tick >= fps:
				elapsed_usec += cost
				peak_usec = maxi(peak_usec, cost)
			await create_timer(maxf(0.0, 1.0 / fps - cost / 1000000.0)).timeout
		var state: Dictionary = audio.diagnostics()
		reports.append({"requested_fps": fps, "measured_wall_seconds": (Time.get_ticks_usec() - begin) / 1000000.0,
			"generator_underruns": state.underruns - skips_before,
			"synthesized_frames": state.rendered_frames - rendered_before,
			"mean_process_usec": float(elapsed_usec) / fps, "peak_process_usec": peak_usec,
			"queued_frames": state.queued_frames, "queue_limit": ShipAudio.QUEUE_FRAMES})
		audio.free()
	print(JSON.stringify({"controlled_fine_grained_consumer": controlled,
		"dummy_cadence_observation": reports, "bounded_consumer_failures": failures,
		"qualification": "Local wall-clock Dummy scheduling observation only; excludes GPU load, hardware output, audio quality and portable CPU guarantees."}))
	# Dummy releases the final stopped player on its next large backend batch.
	await create_timer(0.12).timeout
	quit(0 if failures == 0 else 1)
