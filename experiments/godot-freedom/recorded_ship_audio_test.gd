extends SceneTree
const RecordedAudio = preload("res://recorded_ship_audio.gd")
var failures := 0

class DummyAudio extends RecordedAudio:
	func _device_output_allowed() -> bool:
		return AudioServer.get_driver_name() == "Dummy"

func check(condition: bool, description: String) -> void:
	if not condition:
		failures += 1
		push_error(description)

static func fixture(channels: int = 1) -> PackedByteArray:
	# Entirely code-authored one-second integer-cycle100Hz fixture, not source media.
	var bytes := PackedByteArray()
	bytes.resize(44 + 24000 * channels * 2)
	for pair in [[0, "RIFF"], [8, "WAVE"], [12, "fmt "], [36, "data"]]:
		var text: PackedByteArray = pair[1].to_ascii_buffer()
		for index in 4:
			bytes[pair[0]+index] = text[index]
	bytes.encode_u32(4, bytes.size() - 8)
	bytes.encode_u32(16, 16)
	bytes.encode_u16(20, 1)
	bytes.encode_u16(22, channels)
	bytes.encode_u32(24, 24000)
	bytes.encode_u32(28, 24000 * channels * 2)
	bytes.encode_u16(32, channels * 2)
	bytes.encode_u16(34, 16)
	bytes.encode_u32(40, bytes.size() - 44)
	for frame in 24000:
		for channel in channels:
			bytes.encode_s16(44 + (frame * channels + channel) * 2, roundi(sin(TAU * frame / 240.0) * 3200))
	return bytes

static func state(load_value: float = 0.0, density: float = 0.0, pressure: float = 0.0) -> Dictionary:
	return {"main_thrust": load_value, "retro_thrust": 0.0,
		"effective_air_density": density, "dynamic_pressure": pressure}

func pump(audio: Node, ticks: int, telemetry: Dictionary, cockpit: bool = true, active: bool = true) -> void:
	for tick in ticks:
		audio.update_telemetry(telemetry, cockpit, active)
		audio._process(0.01)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if AudioServer.get_driver_name() != "Dummy":
		push_error("Requires Dummy driver; refusing audible playback")
		quit(1)
		return
	for channels in [1, 2]:
		var decoded: Dictionary = RecordedAudio.decode_loop_wav(fixture(channels))
		check(decoded.has("stream"), "Valid generated WAV rejected")
		if decoded.has("stream"):
			check(decoded.stream.loop_begin == 0 and decoded.stream.loop_end == 24000 and decoded.stream.loop_mode == AudioStreamWAV.LOOP_FORWARD, "Incorrect native loop extent")
	for size in [0, 8, 43]:
		check(RecordedAudio.decode_loop_wav(fixture().slice(0, size)).has("error"), "Truncated header accepted")
	for mutation in [[0, 0], [4, 0], [16, 18], [20, 3], [22, 3], [24, 22050], [28, 1], [32, 4], [34, 8], [40, 0xffffffff]]:
		var corrupt := fixture()
		corrupt.encode_u32(mutation[0], mutation[1])
		check(RecordedAudio.decode_loop_wav(corrupt).has("error"), "Malformed PCM accepted at%d" % mutation[0])
	var loud := fixture()
	loud.encode_s16(200, 30000)
	check(RecordedAudio.decode_loop_wav(loud).has("error"), "Excessive peak accepted")
	var seam := fixture()
	seam.encode_s16(seam.size() - 2, 4000)
	check(RecordedAudio.decode_loop_wav(seam).has("error"), "Bad loop seam accepted")
	var dc := fixture()
	for i in 24000:
		dc.encode_s16(44+i*2, 1000)
	check(RecordedAudio.decode_loop_wav(dc).has("error"), "DC loop accepted")
	var silent := fixture()
	for i in 24000:
		silent.encode_s16(44+i*2, 0)
	check(RecordedAudio.decode_loop_wav(silent).has("error"), "Silent recording accepted")
	var oversize := PackedByteArray()
	oversize.resize(RecordedAudio.MAX_WAV_BYTES + 1)
	check(RecordedAudio.decode_loop_wav(oversize).has("error"), "Oversize file accepted")
	oversize.clear()
	# Duplicate format/data and embedded loop metadata are rejected even if RIFF
	# extents themselves are consistent. Unknown bounded metadata is harmless.
	for tag in ["fmt ", "data", "smpl"]:
		var duplicate := fixture()
		var start := duplicate.size()
		duplicate.resize(start + 8)
		for i in 4:
			duplicate[start+i] = tag.to_ascii_buffer()[i]
		duplicate.encode_u32(start + 4, 0)
		duplicate.encode_u32(4, duplicate.size() - 8)
		check(RecordedAudio.decode_loop_wav(duplicate).has("error"), "Duplicate/ambiguous loop chunk accepted")
	var path := "user://recorded-audio-fixture-%d.wav" % OS.get_process_id()
	var writer := FileAccess.open(path, FileAccess.WRITE)
	writer.store_buffer(fixture(2))
	writer.close()
	var audio := DummyAudio.new()
	check(not audio.diagnostics().configured and not audio.diagnostics().playback_running, "Node did not start silent")
	for bad in ["", "relative.wav", "https://example.test/a.wav", "user://missing.mp3", "user://missing.wav"]:
		check(not audio.configure_recordings(bad, path), "Unsafe/missing path accepted")
	check(audio.configure_recordings(path, path), "Configure before add_child failed")
	root.add_child(audio)
	audio.set_process(false)
	check(audio.diagnostics().targets == Vector3.ZERO, "Configured node became audible before telemetry")
	for key in state():
		for value in [NAN, INF, -INF, -1.0, "0", true, null]:
			var invalid := state()
			invalid[key] = value
			check(not audio.update_telemetry(invalid, true, true), "Invalid telemetry accepted")
			check(audio.diagnostics().targets == Vector3.ZERO, "Invalid telemetry retained demand")
		var missing := state()
		missing.erase(key)
		check(not audio.update_telemetry(missing, true, true), "Missing telemetry accepted")
	check(not audio.update_telemetry(state(1.1), true, true), "Thrust above1 accepted")
	var excessive_retro := state()
	excessive_retro.retro_thrust = 1.1
	check(not audio.update_telemetry(excessive_retro, true, true), "Retro above1 accepted")
	for value in [NAN, INF, -0.01, 1.01]:
		var before: Vector4 = audio.diagnostics().mix_levels
		check(not audio.set_mix_levels(value, 1, 1, 1), "Invalid master accepted")
		check(not audio.set_mix_levels(1, value, 1, 1), "Invalid machinery accepted")
		check(not audio.set_mix_levels(1, 1, value, 1), "Invalid propulsion accepted")
		check(not audio.set_mix_levels(1, 1, 1, value), "Invalid air accepted")
		check(audio.diagnostics().mix_levels == before, "Invalid mix was nontransactional")
	pump(audio, 12, state())
	check(audio.diagnostics().gains.x > 0.99 and audio.diagnostics().gains.y == 0 and audio.diagnostics().gains.z == 0, "Powered cockpit idle absent or doubled")
	check(audio.diagnostics().playback_running and audio.diagnostics().player_count == 3, "Native Dummy players did not start")
	for load_value in [0.0, 0.25, 0.5, 0.75, 1.0]:
		pump(audio, 100, state(load_value))
		var gains: Vector3 = audio.diagnostics().gains
		check(absf(gains.length_squared()-1.0) < 0.00001 and gains.z == 0, "Timbre weights not equal-power at%f" % load_value)
		for player in audio._players:
			check(player.pitch_scale == 1.0, "Thrust changed recording pitch")
	check(audio.set_mix_levels(0.5, 0.4, 0.3, 0.2), "Valid mix rejected")
	pump(audio, 20, state(1))
	check(absf(audio.diagnostics().gains.y - 0.15) < 0.00001, "Individual/master scaling wrong")
	audio.set_mix_levels(1, 1, 1, 1)
	pump(audio, 20, state(1, 0, 99999), false)
	check(audio.diagnostics().gains == Vector3.ZERO and not audio.diagnostics().playback_running, "Exterior vacuum not silent")
	pump(audio, 20, state(0.5, 0.1, 25000), true)
	check(audio.diagnostics().gains.z > 0, "Separate atmosphere absent")
	var stream_before: AudioStreamWAV = audio._streams[0]
	check(not audio.configure_recordings(path, "user://missing.wav"), "Failed pair unexpectedly configured")
	check(audio._streams[0] == stream_before, "Failed replacement mutated previous stream")
	audio.set_muted(true)
	pump(audio, 20, state(1))
	check(not audio.diagnostics().playback_running and audio.diagnostics().gains == Vector3.ZERO, "Mute failed to stop")
	audio.update_telemetry(state(1), true, false)
	audio.set_muted(false)
	audio._process(0.01)
	check(audio.diagnostics().targets == Vector3.ZERO, "Unmute reactivated inactive state")
	pump(audio, 20, state(1))
	audio._process(0.5)
	check(not audio.diagnostics().playback_running and audio.diagnostics().gains == Vector3.ZERO, "Stall replayed stale power")
	audio.set_muted(false)
	audio._process(0.01)
	check(audio.diagnostics().targets == Vector3.ZERO, "Stale state revived")
	pump(audio, 20, state(1))
	for tick in 40:
		audio._process(0.01)
	check(not audio.diagnostics().playback_running and not audio.diagnostics().active, "Short-frame telemetry expiry failed")
	pump(audio, 20, state(1))
	var previous: Vector3 = audio.diagnostics().gains
	audio.set_muted(true)
	audio._process(0.01)
	check(audio.diagnostics().gains.length() < previous.length() and audio.diagnostics().gains.length() > 0, "Ordinary mute skipped gradual envelope")
	audio.set_muted(false)
	for delta in [NAN, INF, -0.01]:
		pump(audio, 10, state(1))
		audio._process(delta)
		check(not audio.diagnostics().playback_running and audio.diagnostics().targets == Vector3.ZERO, "Bad delta retained playback")
	pump(audio, 20, state(1))
	check(audio.configure_recordings(path, path), "Safe hot replacement failed")
	check(not audio.diagnostics().playback_running and not audio.diagnostics().active, "Replacement retained old demand")
	check(audio._players[0].stream == audio._streams[0], "Replacement player kept old stream")
	# Let the actual Dummy audio backend pass a loop boundary with fresh telemetry.
	for tick in 24:
		pump(audio, 1, state(0.5))
		await create_timer(0.05).timeout
	check(audio.diagnostics().playback_running and not audio.diagnostics().device_failed, "Native loop stopped at boundary")
	audio.free()
	await create_timer(0.15).timeout # Retire native audio-thread playback references.
	check(DirAccess.remove_absolute(ProjectSettings.globalize_path(path)) == OK, "Test fixture cleanup failed")
	print("Recorded ship audio CPU/native Dummy: %d failures; no device/listening qualification" % failures)
	quit(0 if failures == 0 else 1)
