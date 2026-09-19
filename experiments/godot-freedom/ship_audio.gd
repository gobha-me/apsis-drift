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
const MAX_GAINS := Vector3(0.0, 1.0, 0.10) # Reserved old bed, unified powered core, airflow.
const FADE_SECONDS := 0.06
# Approved direction24 coherent-sum calibration,21loads in0.05increments.
# Static correction, NOT signal-following AGC. Native causal-filter acceptance
# qualifies the port separately from the offlineFFT audition (not identicalPCM).
const LEVEL_DB := [-15.95223868, -15.95521183, -15.93961331, -15.95470744,
	-16.01216941, -16.09233273, -16.20744719, -16.38353145, -16.63467375,
	-16.91793885, -17.19583185, -17.40527280, -17.55829733, -17.73349189,
	-17.92624874, -18.11927822, -18.31717124, -18.51508905, -18.69817072,
	-18.86857976, -19.00482827]

var muted := false
var _active := false
var _valid := false
var _age := TELEMETRY_TIMEOUT
var _desired := Vector3.ZERO
var _targets := Vector3.ZERO
var _gains := Vector3.ZERO
var _engine_hz := 91.0
var _power := 0.0
var _spool := 0.0
var _spool_target := 0.0
var _phase := 0.0
var _noise_state: int = 0x51A7D123
var _irregular_low := 0.0
var _irregular_high := 0.0
var _flex_low := 0.0
var _flex_high := 0.0
var _grain_low := 0.0
var _grain_high := 0.0
var _air_low := 0.0
var _air_high := 0.0
var _air_side := 0.0
var _buffet := 0.0
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
	# One calibrated powered voice includes idle. No extra machinery bed and no
	# throttle-volume multiplier. Airflow remains a separate medium-dependent layer.
	var conduction := 1.0 if cockpit else air
	_desired = Vector3(0.0, conduction,
		MAX_GAINS.z * sqrt(pressure) * air * (0.55 if cockpit else 1.0))
	_power = power
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
	_spool_target = _power if _targets.y > 0 else 0.0

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
	_gains.y = move_toward(_gains.y, _targets.y, MAX_GAINS.y / (FADE_SECONDS * SAMPLE_RATE))
	_gains.z = move_toward(_gains.z, _targets.z, MAX_GAINS.z / (FADE_SECONDS * SAMPLE_RATE))
	# Direction24 exponential loadstate drives timbre and STATIC level correction;
	# the independent60ms gate is the only mute/inactive amplitude envelope.
	_spool += (_spool_target - _spool) * (0.00010416124150780526 if _spool_target > _spool else 0.0000641005095770586)
	var clock_rate := 1.0 + 0.018 * _spool
	_engine_hz = 91.0 * clock_rate
	# Integerrelated carriers share a wrapped1Hz phase, including acrossblocks.
	_phase = fposmod(_phase + clock_rate / SAMPLE_RATE, 1.0)
	var clock_phase := TAU * _phase
	# Private causal difference-of-lowpass filters replace offline Fourier noise.
	# Normalizers use analyticalwhite-noise variance, never a running RMS/AGC.
	var n0 := _noise()
	var n1 := _noise()
	var n2 := _noise()
	var air_noise := _noise()
	_irregular_low += 0.0007850898189896149 * (n0 - _irregular_low)
	_irregular_high += 0.00013089112690845006 * (n0 - _irregular_high)
	_flex_low += 0.003658482813704511 * (n1 - _flex_low)
	_flex_high += 0.000523461721680829 * (n1 - _flex_high)
	_grain_low += 0.11808862170182366 * (n2 - _grain_low)
	_grain_high += 0.038508840198592464 * (n2 - _grain_high)
	var irregular := tanh((_irregular_low - _irregular_high) * 113.28837371441487)
	var flex := tanh((_flex_low - _flex_high) * 50.462691367818486)
	var grain := (_grain_low - _grain_high) * 11.529988379887314
	_air_low += 0.23 * (air_noise - _air_low)
	_air_high += 0.025 * (air_noise - _air_high)
	_air_side += 0.11 * (air_noise - _air_side)
	_buffet += 0.0025 * (air_noise - _buffet)
	var s91 := sin(91.0 * clock_phase)
	var s148 := sin(148.0 * clock_phase + 0.02 * flex)
	var s219 := sin(219.0 * clock_phase + 0.045 * flex)
	var s291 := sin(291.0 * clock_phase + 0.04 * flex)
	var s357 := sin(357.0 * clock_phase + 0.03 * flex)
	var idle := tanh((0.27*s91 + 0.34*s148 + 0.20*s219 + 0.12*s291 + 0.07*s357)*1.05)*(0.96+0.018*irregular)+0.004*grain
	var light := (0.15*s91 + 0.30*s148 + 0.27*s219 + 0.18*s291 + 0.10*s357)*(0.94+0.025*irregular)+0.007*grain
	var body := 0.27*s91 + 0.34*s148 + (0.14+0.17*_spool)*s219 + (0.065+0.12*_spool)*s291 + (0.015+0.075*_spool)*s357
	var core := tanh(body*(1.05+0.48*_spool))*(0.94+0.045*irregular)+0.009*grain
	var dense_body := 0.38*s91 + 0.32*s148 + 0.12*s219 + 0.10*sin(182.0*clock_phase) + 0.08*s291
	var dense := tanh(dense_body*(1.12+0.62*_spool))*(0.96+0.018*irregular)
	var blend := blend_weights(_spool)
	var table_position := clampf(_spool * 20.0, 0.0, 20.0)
	var table_index := mini(int(table_position), 19)
	var correction := db_to_linear(lerpf(LEVEL_DB[table_index], LEVEL_DB[table_index+1], table_position-table_index))
	var engine := (idle*blend.x + light*blend.y + core*blend.z + dense*blend.w) * correction
	var wind := (_air_low - _air_high) * 0.60 + _buffet * 1.2
	var common := _gains.y * engine
	var left := common + _gains.z * wind
	# Centered mechanical core, gentle correlated air width; no phase inversion,
	# delay tricks or claimed physical spatialization. Mono fold-down is stable.
	var right := common + _gains.z * (wind * 0.90 + (_air_side - _air_high) * 0.06)
	_rendered += 1
	_silent_frames = _silent_frames + 1 if _gains == Vector3.ZERO else 0
	return Vector2(clampf(left, -LIMIT, LIMIT), clampf(right, -LIMIT, LIMIT))

static func blend_weights(load_value: float) -> Vector4:
	var f: float
	var a: Vector4
	var b: Vector4
	if load_value < 0.25:
		f = load_value / 0.25
		a = Vector4(0.70, 0.20, 0.10, 0.0)
		b = Vector4(0.25, 0.40, 0.32, 0.03)
	elif load_value < 0.55:
		f = (load_value - 0.25) / 0.30
		a = Vector4(0.25, 0.40, 0.32, 0.03)
		b = Vector4(0.05, 0.15, 0.66, 0.14)
	else:
		f = (load_value - 0.55) / 0.45
		a = Vector4(0.05, 0.15, 0.66, 0.14)
		b = Vector4(0.0, 0.05, 0.67, 0.28)
	f = clampf(f, 0.0, 1.0)
	return a.lerp(b, f*f*(3.0-2.0*f))

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
		"spool": _spool, "engine_hz": _engine_hz,
		"requested_load": _power, "blend_weights": blend_weights(_spool),
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
	_spool = 0.0
	_engine_hz = 91.0
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
