extends SceneTree
const ShipAudio = preload("res://ship_audio.gd")
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func telemetry(main: float = 0.0, density: float = 0.0, pressure: float = 0.0) -> Dictionary:
	return {"main_thrust": main, "retro_thrust": 0.0, "dynamic_pressure": pressure, "effective_air_density": density}

func render(audio: Node, frames: int) -> PackedVector2Array:
	var samples := PackedVector2Array()
	while frames > 0:
		var count := mini(frames, ShipAudio.MAX_BLOCK)
		samples.append_array(audio.synthesize_frames(count))
		frames -= count
	return samples

func peak(samples: PackedVector2Array) -> float:
	var value := 0.0
	for sample in samples:
		check(is_finite(sample.x) and is_finite(sample.y), "Non-finite audio sample")
		value = maxf(value, maxf(absf(sample.x), absf(sample.y)))
	return value

func _initialize() -> void:
	var audio := ShipAudio.new()
	check(peak(render(audio, 2000)) == 0, "Prototype did not start silent")
	var original: Dictionary = audio.diagnostics()
	for count in [-1, 0, ShipAudio.MAX_BLOCK + 1, 100000000]:
		check(audio.synthesize_frames(count).is_empty(), "Invalid block size accepted")
		check(audio.diagnostics() == original, "Invalid block size changed synth state")
	for key in telemetry():
		for invalid in [NAN, INF, -INF, -1.0, "0", null, true]:
			var state := telemetry()
			state[key] = invalid
			check(not audio.update_telemetry(state, true, true), "Invalid telemetry accepted")
			check(audio.diagnostics().targets == Vector3.ZERO, "Invalid telemetry retained an audible target")
		var missing := telemetry()
		missing.erase(key)
		check(not audio.update_telemetry(missing, true, true), "Missing telemetry accepted")
	for key in ["main_thrust", "retro_thrust"]:
		var excessive := telemetry()
		excessive[key] = 1.001
		check(not audio.update_telemetry(excessive, true, true), "Excessive thrust accepted")
	check(audio.update_telemetry(telemetry(1), true, true), "Valid cockpit vacuum rejected")
	var vacuum := render(audio, 4000)
	check(peak(vacuum) > 0.01 and peak(vacuum) <= ShipAudio.LIMIT, "Cockpit vacuum conduction absent/unbounded")
	check(audio.diagnostics().targets.z == 0, "Vacuum airflow target nonzero")
	# Conservative neighbor-step bound catches hard gain/phase resets, not an acoustic certification.
	var previous := vacuum[-1].x
	for mode in [0, 1, 2, 3]:
		if mode == 0:
			audio.set_muted(true)
		elif mode == 1:
			audio.set_muted(false)
		elif mode == 2:
			audio.update_telemetry(telemetry(1, 1.2, 100000), false, true)
		else:
			audio.update_telemetry(telemetry(1), false, true)
		var samples := render(audio, 4000)
		for sample in samples:
			check(absf(sample.x - previous) < 0.08, "Transition introduced a sharp waveform step")
			previous = sample.x
		check(peak(samples) <= ShipAudio.LIMIT, "Transition exceeded bounded gain")
	check(peak(render(audio, 2000)) == 0, "Exterior vacuum did not become exactly silent")
	audio.update_telemetry(telemetry(1, 0, 99999), false, true)
	check(audio.diagnostics().targets == Vector3.ZERO, "Pressure without medium created exterior vacuum sound")
	audio.update_telemetry(telemetry(1, 1.0, 99999), true, true)
	check(audio.diagnostics().targets.z > 0, "Atmospheric airflow absent")
	render(audio, 4000)
	audio.update_telemetry(telemetry(1), true, false)
	audio.set_muted(true)
	audio.set_muted(false)
	check(not audio.diagnostics().active and audio.diagnostics().targets == Vector3.ZERO, "Unmute reactivated paused sound")
	render(audio, 4000)
	check(peak(render(audio, 2000)) == 0, "Paused sound failed to ramp silent")
	audio.update_telemetry(telemetry(1), true, true)
	audio.advance_presentation(0.3)
	audio.set_muted(false)
	check(not audio.diagnostics().active and audio.diagnostics().targets == Vector3.ZERO, "Stale telemetry replayed on return")
	for delta in [NAN, INF, -0.1]:
		audio.update_telemetry(telemetry(1), true, true)
		audio.advance_presentation(delta)
		check(audio.diagnostics().targets == Vector3.ZERO, "Invalid presentation delta retained sound")
	# Separate instances and chunking use independent identical noise/phase state.
	var a := ShipAudio.new()
	var b := ShipAudio.new()
	a.update_telemetry(telemetry(0.7, 0.1, 30000), true, true)
	b.update_telemetry(telemetry(0.7, 0.1, 30000), true, true)
	var grouped := render(a, 30000) # Cross wrapped carrier phase and block boundaries.
	var fragmented := PackedVector2Array()
	for index in 30000:
		fragmented.append_array(b.synthesize_frames(1))
	check(grouped == fragmented, "Synth depends on rendering chunk size or shared noise")
	check(peak(grouped) <= ShipAudio.LIMIT, "Combined layers exceeded limit")
	# Full demand changes texture gradually, rather than sweeping a clean high tone.
	var spool := ShipAudio.new()
	spool.update_telemetry(telemetry(1), true, true)
	render(spool, 2400)
	check(spool.diagnostics().spool > 0.1 and spool.diagnostics().spool < 0.25, "Mechanical spool lacks bounded spin-up inertia")
	check(spool.diagnostics().engine_hz >= 91 and spool.diagnostics().engine_hz < 91.5, "Thrust immediately jumped to a high pure pitch")
	render(spool, 14000)
	check(spool.diagnostics().spool > 0.8 and spool.diagnostics().spool < 0.85, "Exponential loadstate lost its approved inertia")
	render(spool, 48000)
	check(spool.diagnostics().spool > 0.99 and spool.diagnostics().engine_hz <= 92.6381, "Loadstate failed to settle within restrained carrier range")
	spool.update_telemetry(telemetry(), true, true)
	render(spool, 2400)
	check(spool.diagnostics().spool > 0.8 and spool.diagnostics().spool < 0.95, "Mechanical spin-down lost inertia")
	spool.set_muted(true)
	render(spool, 2000)
	check(peak(render(spool, 1024)) == 0, "Spool inertia bypassed mute fade")
	spool.free()
	var idle := ShipAudio.new()
	idle.update_telemetry(telemetry(), true, true)
	check(idle.diagnostics().targets.x == 0 and idle.diagnostics().targets.y == 1, "Poweredidle missing or old machinerybed doubled")
	check(peak(render(idle, 4000)) > 0.01, "Zero thrust incorrectly silenced poweredidle")
	idle.update_telemetry({}, true, true)
	render(idle, 2000)
	check(peak(render(idle, 1024)) == 0, "Invalid input retained continuous idle")
	idle.free()
	var mono_energy := 0.0
	var stereo_energy := 0.0
	for sample in grouped:
		mono_energy += pow((sample.x + sample.y) * 0.5, 2)
		stereo_energy += (sample.x * sample.x + sample.y * sample.y) * 0.5
	check(mono_energy >= stereo_energy * 0.99, "Stereo texture cancels significantly in mono")
	var vacuum_core := ShipAudio.new()
	var air_core := ShipAudio.new()
	vacuum_core.update_telemetry(telemetry(0.8), true, true)
	air_core.update_telemetry(telemetry(0.8, 1.0, 0.0), true, true)
	check(render(vacuum_core, 4000) == render(air_core, 4000), "Atmosphere replaced the conducted engine identity even without airflow")
	vacuum_core.free()
	air_core.free()
	# Live adapter must not open a device in this headless/Dummy test.
	root.add_child(audio)
	audio.update_telemetry(telemetry(1), true, true)
	audio._process(0.016)
	check(audio.diagnostics().queued_frames == 0, "Headless adapter unexpectedly queued device audio")
	audio.free()
	a.free()
	b.free()
	print("Ship audio CPU/Dummy acceptance: %d failures (human listening still required)" % failures)
	quit(0 if failures == 0 else 1)
