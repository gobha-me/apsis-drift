extends Node
## Native recording presentation, BSD-3-Clause code; recordings have separate rights.
## No simulation mutation, playback-rate modulation, global RNG or sample mixer.
## Prepared recordings already contain approved levels; this node adds no trim.

const TELEMETRY_TIMEOUT := 0.25
const FADE_SECONDS := 0.08
const MAX_WAV_BYTES := 16 * 1024 * 1024
const MAX_SECONDS := 60
const MAX_PEAK := 0.5
const MAX_DC := 0.01
const MAX_SEAM_STEP := 0.01

var muted := false
var _configured := false
var _active := false
var _valid := false
var _age := TELEMETRY_TIMEOUT
var _power := 0.0
var _spool := 0.0
var _conduction := 0.0
var _air := 0.0
var _mix := Vector4.ONE # master, machinery, propulsion, atmosphere
var _targets := Vector3.ZERO
var _gains := Vector3.ZERO
var _streams: Array[AudioStreamWAV] = []
var _players: Array[AudioStreamPlayer] = []
var _receipts: Array[Dictionary] = []
var _rejected := 0
var _device_failed := false
var _last_error := ""

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS

static func _fourcc(data: PackedByteArray, offset: int) -> String:
	for i in 4:
		if data[offset + i] < 32 or data[offset + i] > 126:
			return ""
	return data.slice(offset, offset + 4).get_string_from_ascii()

static func decode_loop_wav(bytes: PackedByteArray) -> Dictionary:
	# Strict bounded RIFF reader, avoiding permissive decoder repair/format fallback.
	if bytes.size() < 44 or bytes.size() > MAX_WAV_BYTES:
		return {"error": "WAV size outside bound"}
	if _fourcc(bytes, 0) != "RIFF" or _fourcc(bytes, 8) != "WAVE" or bytes.decode_u32(4) != bytes.size() - 8:
		return {"error": "Invalid RIFF extent"}
	var cursor := 12
	var format_offset := -1
	var pcm_offset := -1
	var pcm_size := 0
	while cursor < bytes.size():
		if bytes.size() - cursor < 8:
			return {"error": "Truncated chunk header"}
		var tag := _fourcc(bytes, cursor)
		var size := int(bytes.decode_u32(cursor + 4))
		var end := cursor + 8 + size
		if end > bytes.size() or end + (size & 1) > bytes.size():
			return {"error": "Truncated chunk extent"}
		if tag == "fmt ":
			if format_offset >= 0 or size != 16:
				return {"error": "Require one canonical PCM fmt chunk"}
			format_offset = cursor + 8
		elif tag == "data":
			if pcm_offset >= 0:
				return {"error": "Duplicate PCM chunk"}
			pcm_offset = cursor + 8
			pcm_size = size
		elif tag == "smpl":
			return {"error": "Embedded loops unsupported; prepare a whole-file loop"}
		cursor = end + (size & 1)
	if format_offset < 0 or pcm_offset < 0:
		return {"error": "Missing format or PCM"}
	var channels := int(bytes.decode_u16(format_offset + 2))
	var rate := int(bytes.decode_u32(format_offset + 4))
	if bytes.decode_u16(format_offset) != 1 or bytes.decode_u16(format_offset + 14) != 16 or channels not in [1, 2] or rate not in [24000, 44100, 48000]:
		return {"error": "Require 16-bit mono/stereo PCM at24/44.1/48kHz"}
	var stride := channels * 2
	if bytes.decode_u16(format_offset + 12) != stride or bytes.decode_u32(format_offset + 8) != rate * stride or pcm_size % stride != 0:
		return {"error": "Invalid PCM alignment/rate"}
	var frames := pcm_size / stride
	if frames < rate or frames > rate * MAX_SECONDS:
		return {"error": "Loop must be1–60seconds"}
	var peak := 0.0
	var sums := Vector2.ZERO
	for frame in frames:
		for channel in channels:
			var value := float(bytes.decode_s16(pcm_offset + frame * stride + channel * 2)) / 32768.0
			peak = maxf(peak, absf(value))
			sums[channel] += value
	if peak <= 0.00001 or peak > MAX_PEAK:
		return {"error": "Silent or excessive peak recording"}
	var seam := 0.0
	for channel in channels:
		var first := float(bytes.decode_s16(pcm_offset + channel * 2)) / 32768.0
		var last := float(bytes.decode_s16(pcm_offset + (frames - 1) * stride + channel * 2)) / 32768.0
		seam = maxf(seam, absf(first - last))
		if absf(sums[channel] / frames) > MAX_DC:
			return {"error": "Excessive recording DC"}
	if seam > MAX_SEAM_STEP:
		return {"error": "Loop endpoint discontinuity"}
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = rate
	stream.stereo = channels == 2
	stream.data = bytes.slice(pcm_offset, pcm_offset + pcm_size)
	stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
	stream.loop_begin = 0
	stream.loop_end = frames
	return {"stream": stream, "frames": frames, "rate": rate, "channels": channels,
		"peak": peak, "seam_step": seam, "dc": sums / frames}

static func _load_loop(path: String) -> Dictionary:
	if path.is_empty() or path.length() > 4096 or path.get_extension().to_lower() != "wav":
		return {"error": "Invalid recording path"}
	for i in path.length():
		if path.unicode_at(i) < 32:
			return {"error": "Control character in recording path"}
	if not path.is_absolute_path() and not path.begins_with("res://") and not path.begins_with("user://"):
		return {"error": "Recording path must be local and absolute"}
	if path.contains("://") and not path.begins_with("res://") and not path.begins_with("user://"):
		return {"error": "Network recording paths forbidden"}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"error": "Recording unavailable"}
	var size := file.get_length()
	if size < 44 or size > MAX_WAV_BYTES:
		return {"error": "Recording size outside bound"}
	return decode_loop_wav(file.get_buffer(size))

func configure_recordings(hum_path: String, propulsion_path: String) -> bool:
	# Transactional: a failed replacement leaves previous streams/settings intact.
	var hum := _load_loop(hum_path)
	if hum.has("error"):
		_last_error = hum.error
		return false
	var propulsion := _load_loop(propulsion_path)
	if propulsion.has("error"):
		_last_error = propulsion.error
		return false
	_stop_players()
	_streams = [hum.stream, propulsion.stream, _make_air_loop()]
	for i in _players.size():
		_players[i].stream = _streams[i]
	hum.erase("stream")
	propulsion.erase("stream")
	_receipts = [hum, propulsion]
	_configured = true
	_valid = false
	_active = false
	_age = TELEMETRY_TIMEOUT
	_power = 0.0
	_spool = 0.0
	_targets = Vector3.ZERO
	_device_failed = false
	_last_error = ""
	return true

static func _make_air_loop() -> AudioStreamWAV:
	# One bounded startup-only authored noise buffer, private integer state. No
	# GDScript synthesis during playback; air is intentionally secondary and mono.
	var data := PackedByteArray()
	data.resize(48000)
	var state: int = 0x51A71
	var low := 0.0
	var high := 0.0
	for i in 24000:
		state = (state * 1103515245 + 12345) & 0x7fffffff
		var n := float(state) / 1073741824.0 - 1.0
		low += 0.20 * (n - low)
		high += 0.025 * (n - high)
		# Rounded quiet seam, not a claim of non-repeating atmospheric acoustics.
		var fade := minf(1.0, minf(float(i) / 480.0, float(23999-i) / 480.0))
		data.encode_s16(i * 2, roundi(clampf((low - high) * 0.3, -0.3, 0.3) * fade * 32767.0))
	var stream := AudioStreamWAV.new()
	stream.format = AudioStreamWAV.FORMAT_16_BITS
	stream.mix_rate = 24000
	stream.data = data
	stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
	stream.loop_end = 24000
	return stream

func set_mix_levels(master: float, machinery: float, propulsion: float, atmosphere: float) -> bool:
	var candidate := Vector4(master, machinery, propulsion, atmosphere)
	for value in [master, machinery, propulsion, atmosphere]:
		if not is_finite(value) or value < 0.0 or value > 1.0:
			return false
	_mix = candidate
	_refresh_targets()
	return true

func update_telemetry(state: Dictionary, cockpit: bool, active: bool) -> bool:
	for key in ["main_thrust", "retro_thrust", "dynamic_pressure", "effective_air_density"]:
		if not state.has(key) or typeof(state[key]) not in [TYPE_FLOAT, TYPE_INT]:
			return _reject()
		if not is_finite(float(state[key])) or float(state[key]) < 0:
			return _reject()
	if state.main_thrust > 1 or state.retro_thrust > 1:
		return _reject()
	_valid = true
	_active = active
	_age = 0.0
	_power = maxf(state.main_thrust, state.retro_thrust * 0.65)
	var medium := clampf(float(state.effective_air_density) / 0.02, 0.0, 1.0)
	_conduction = 1.0 if cockpit else medium
	_air = sqrt(clampf(float(state.dynamic_pressure) / 25000.0, 0.0, 1.0)) * medium * (0.55 if cockpit else 1.0)
	_refresh_targets()
	return true

func _reject() -> bool:
	_rejected += 1
	_valid = false
	_active = false
	_power = 0.0
	_refresh_targets()
	return false

func set_muted(value: bool) -> void:
	muted = value
	_refresh_targets()

func _refresh_targets() -> void:
	_targets = Vector3.ZERO
	if not _configured or not _active or not _valid or muted or _age >= TELEMETRY_TIMEOUT:
		return
	var theta := clampf(_spool, 0.0, 1.0) * PI * 0.5
	_targets = Vector3(cos(theta) * _mix.y, sin(theta) * _mix.z, _air * 0.12 * _mix.w) * _mix.x
	_targets.x *= _conduction
	_targets.y *= _conduction
	if _spool == 1.0:
		_targets.x = 0.0

func advance_presentation(delta: float) -> void:
	if not is_finite(delta) or delta < 0:
		_reject()
		_stop_players()
		return
	_age = minf(_age + delta, TELEMETRY_TIMEOUT)
	if _age >= TELEMETRY_TIMEOUT:
		_active = false
		_power = 0.0
	if delta >= TELEMETRY_TIMEOUT:
		# No retained high-power playback after a render stall; no catch-up work.
		_stop_players()
		_spool = 0.0
	var wanted := _power if _active and _valid and not muted else 0.0
	_spool = move_toward(_spool, wanted, minf(delta, TELEMETRY_TIMEOUT) / (0.65 if wanted > _spool else 0.85))
	_refresh_targets()
	_gains = _gains.move_toward(_targets, minf(delta, TELEMETRY_TIMEOUT) / FADE_SECONDS)
	if _targets == Vector3.ZERO and _gains.length_squared() < 0.00000001:
		_gains = Vector3.ZERO

func _device_output_allowed() -> bool:
	return DisplayServer.get_name() != "headless" and AudioServer.get_driver_name() != "Dummy"

func _sync_players() -> void:
	if _gains == Vector3.ZERO:
		_stop_players()
		return
	if not _configured or _device_failed or not _device_output_allowed() or not is_inside_tree():
		return
	if _players.is_empty():
		for stream in _streams:
			var player := AudioStreamPlayer.new()
			player.process_mode = Node.PROCESS_MODE_ALWAYS
			player.stream = stream
			player.volume_linear = 0.0
			player.max_polyphony = 1
			player.mix_target = AudioStreamPlayer.MIX_TARGET_STEREO
			add_child(player)
			_players.append(player)
	for i in _players.size():
		var player := _players[i]
		# Start at zero gain; apply frame-driven envelopes. Keep all
		# three cursors moving while active, including a currently quiet blend bus.
		if not player.playing:
			player.volume_linear = 0.0
			player.play()
			if not player.has_stream_playback():
				_device_failed = true
				_stop_players()
				return
		player.volume_linear = _gains[i]

func _stop_players() -> void:
	for player in _players:
		player.stop()
		player.volume_linear = 0.0
	_gains = Vector3.ZERO

func _process(delta: float) -> void:
	advance_presentation(delta)
	_sync_players()

func diagnostics() -> Dictionary:
	var running := false
	for player in _players:
		running = running or player.playing
	return {"configured": _configured, "muted": muted, "active": _active,
		"valid": _valid, "targets": _targets, "gains": _gains, "spool": _spool,
		"requested_load": _power, "mix_levels": _mix, "telemetry_age": _age,
		"rejected_updates": _rejected, "playback_running": running,
		"device_failed": _device_failed, "last_error": _last_error,
		"recordings": _receipts.duplicate(true), "player_count": _players.size()}
