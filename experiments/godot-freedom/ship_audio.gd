extends Node
## Opt-in native presentation prototype, not authoritative audio/event policy.
## Code-authored synthesis, BSD-3-Clause (repository LICENSE.md); no input media.
## Existing C++ procedural audio, MIDI and First Light assets remain unchanged.
## CPU samples are repeatable locally; cross-platform bit-exact PCM is not promised.

const SAMPLE_RATE := 24000
const MAX_BLOCK := 1024
const MAX_PROCESS_FRAMES := 2048
const QUEUE_FRAMES := 1920 # At most 80 ms of authored samples, excluding device latency.
const TELEMETRY_TIMEOUT := 0.25
const LIMIT := 0.26
const MAX_GAINS := Vector3(0.018, 0.12, 0.10)
const FADE_SECONDS := 0.06

var muted := false
var _active := false
var _valid := false
var _age := TELEMETRY_TIMEOUT
var _desired := Vector3.ZERO
var _targets := Vector3.ZERO
var _gains := Vector3.ZERO
var _engine_hz := 48.0
var _target_hz := 48.0
var _phase := 0.0
var _machinery_phase := 0.0
var _noise_state: int = 0x51A7D123
var _low_noise := 0.0
var _air_low := 0.0
var _air_high := 0.0
var _rendered := 0
var _rejected := 0
var _player: AudioStreamPlayer
var _playback: AudioStreamGeneratorPlayback
var _capacity := 0
var _silent_frames := 0
var _device_failed := false

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	# No device is opened here. Playback is lazy and disabled for Dummy/headless.
	set_process(true)

func update_telemetry(state: Dictionary, cockpit: bool, active: bool) -> bool:
	# Invalid input fails silent rather than retaining the previous engine demand.
	for key in ["main_thrust", "retro_thrust", "dynamic_pressure", "effective_air_density"]:
		if not state.has(key) or (typeof(state[key]) != TYPE_FLOAT and typeof(state[key]) != TYPE_INT):
			return _reject()
		if not is_finite(float(state[key])) or float(state[key]) < 0:
			return _reject()
	if state.main_thrust > 1 or state.retro_thrust > 1:
		return _reject()
	_valid = true
	_active = active
	_age = 0.0
	var power: float = maxf(state.main_thrust, state.retro_thrust * 0.65)
	var air: float = clampf(float(state.effective_air_density) / 0.02, 0.0, 1.0)
	var pressure: float = clampf(float(state.dynamic_pressure) / 25000.0, 0.0, 1.0)
	# Cockpit engine/machinery sound is structure-conducted. Exterior vacuum is silent.
	var conduction := 1.0 if cockpit else air
	_desired = Vector3(MAX_GAINS.x if cockpit else 0.0,
		MAX_GAINS.y * sqrt(power) * conduction,
		MAX_GAINS.z * sqrt(pressure) * air * (0.55 if cockpit else 1.0))
	_target_hz = 48.0 + power * 108.0
	_refresh_targets()
	return true

func _reject() -> bool:
	_rejected += 1
	_valid = false
	_active = false
	_desired = Vector3.ZERO
	_refresh_targets()
	return false

func set_muted(value: bool) -> void:
	muted = value
	# Unmuting never changes activity, validity or telemetry freshness.
	_refresh_targets()

func _refresh_targets() -> void:
	_targets = _desired if _valid and _active and not muted and _age < TELEMETRY_TIMEOUT else Vector3.ZERO

func advance_presentation(delta: float) -> void:
	# Testable presentation watchdog, not a simulation clock or catch-up loop.
	if not is_finite(delta) or delta < 0:
		_reject()
		return
	_age = minf(_age + delta, TELEMETRY_TIMEOUT)
	if _age >= TELEMETRY_TIMEOUT:
		_active = false
	_refresh_targets()

func _noise() -> float:
	# Private 31-bit LCG: integer products fit int64; never calls global rand*.
	_noise_state = (_noise_state * 1103515245 + 12345) & 0x7fffffff
	return float(_noise_state) / 1073741824.0 - 1.0

func _sample() -> Vector2:
	_gains.x = move_toward(_gains.x, _targets.x, MAX_GAINS.x / (FADE_SECONDS * SAMPLE_RATE))
	_gains.y = move_toward(_gains.y, _targets.y, MAX_GAINS.y / (FADE_SECONDS * SAMPLE_RATE))
	_gains.z = move_toward(_gains.z, _targets.z, MAX_GAINS.z / (FADE_SECONDS * SAMPLE_RATE))
	_engine_hz = move_toward(_engine_hz, _target_hz, 240.0 / SAMPLE_RATE)
	_phase = fposmod(_phase + _engine_hz / SAMPLE_RATE, 1.0)
	_machinery_phase = fposmod(_machinery_phase + 37.0 / SAMPLE_RATE, 1.0)
	var noise := _noise()
	_low_noise += 0.025 * (noise - _low_noise)
	_air_low += 0.30 * (noise - _air_low)
	_air_high += 0.035 * (noise - _air_high)
	var machinery := sin(TAU * _machinery_phase) * 0.7 + sin(TAU * _machinery_phase * 3.0) * 0.3
	var engine := sin(TAU * _phase) * 0.55 + sin(TAU * _phase * 2.0) * 0.20 + _low_noise * 0.25
	var wind := (_air_low - _air_high) * 0.7
	var common := _gains.x * machinery + _gains.y * engine
	var left := common + _gains.z * wind
	var right := common + _gains.z * wind * 0.94
	_rendered += 1
	_silent_frames = _silent_frames + 1 if _gains == Vector3.ZERO else 0
	return Vector2(clampf(left, -LIMIT, LIMIT), clampf(right, -LIMIT, LIMIT))

func synthesize_frames(count: int) -> PackedVector2Array:
	# Invalid sizes are transactional: no noise, phases or envelopes are advanced.
	if count < 1 or count > MAX_BLOCK:
		return PackedVector2Array()
	var frames := PackedVector2Array()
	frames.resize(count)
	for index in count:
		frames[index] = _sample()
	return frames

func diagnostics() -> Dictionary:
	return {"muted": muted, "active": _active, "valid": _valid,
		"targets": _targets, "gains": _gains, "rendered_frames": _rendered,
		"rejected_updates": _rejected, "telemetry_age": _age,
		"queued_frames": maxi(0, _capacity - _playback.get_frames_available()) if _playback != null else 0,
		"playback_running": _playback != null,
		"device_failed": _device_failed,
		"underruns": _playback.get_skips() if _playback != null else 0}

func _device_output_allowed() -> bool:
	# A test subclass can opt into Dummy specifically; production remains silent there.
	return DisplayServer.get_name() != "headless" and AudioServer.get_driver_name() != "Dummy"

func _start_playback() -> void:
	if _device_failed or not _device_output_allowed():
		return
	if _player == null:
		_player = AudioStreamPlayer.new()
		_player.process_mode = Node.PROCESS_MODE_ALWAYS
		var stream := AudioStreamGenerator.new()
		stream.mix_rate = SAMPLE_RATE
		stream.buffer_length = 0.1
		_player.stream = stream
		add_child(_player)
	_player.play()
	_playback = _player.get_stream_playback() as AudioStreamGeneratorPlayback
	if _playback == null:
		_device_failed = true
		_player.stop()
		return
	_capacity = _playback.get_frames_available()

func _stop_playback() -> void:
	if _player != null:
		_player.stop()
	_playback = null
	_capacity = 0
	_gains = Vector3.ZERO
	_silent_frames = 0

func _process(delta: float) -> void:
	advance_presentation(delta)
	# A long stall is an underrun/discontinuity, not a backlog to replay on return.
	if delta >= TELEMETRY_TIMEOUT:
		_stop_playback()
	if _playback == null and _targets != Vector3.ZERO:
		_start_playback()
	if _playback == null:
		return
	# Continue processing while paused so the <=80ms queue and 60ms fade drain.
	# Stop only after enough exact-zero samples have passed to retire old audio.
	if _targets == Vector3.ZERO and _silent_frames > QUEUE_FRAMES * 2:
		_stop_playback()
		return
	var queued := maxi(0, _capacity - _playback.get_frames_available())
	var remaining := mini(MAX_PROCESS_FRAMES, mini(_playback.get_frames_available(), maxi(0, QUEUE_FRAMES - queued)))
	# Bounded catch-up: never synthesize a whole delayed wall-clock interval.
	while remaining > 0:
		var count := mini(MAX_BLOCK, remaining)
		if not _playback.push_buffer(synthesize_frames(count)):
			_device_failed = true
			_stop_playback()
			return
		remaining -= count

func _exit_tree() -> void:
	_stop_playback()
