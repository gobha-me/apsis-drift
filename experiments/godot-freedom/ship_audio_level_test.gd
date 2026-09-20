extends SceneTree
## Native causal-filter qualification of approved static coherent-sum table.
const ShipAudio = preload("res://ship_audio.gd")
var failures := 0

func _initialize() -> void:
	var records := []
	var lowest := 100.0
	var highest := -100.0
	var max_peak := 0.0
	for index in 41:
		var load_value := float(index) / 40.0 # Includes all21knots +20midpoints.
		var audio := ShipAudio.new()
		audio.update_telemetry({"main_thrust": load_value, "retro_thrust": 0.0,
			"dynamic_pressure": 0.0, "effective_air_density": 0.0}, true, true)
		# Stationary recipe fixture; dynamic response is covered in CPU/captures.
		audio._spool = load_value
		var squares := 0.0
		var mean := 0.0
		var samples := 0
		var previous := 0.0
		var max_step := 0.0
		for block in 96: # First~2s warmup; following~2s measurement.
			var frames: PackedVector2Array = audio.synthesize_frames(1024)
			if block < 48:
				continue
			for frame in frames:
				if not is_finite(frame.x) or not is_finite(frame.y) or frame.x != frame.y:
					failures += 1
				squares += frame.x*frame.x
				mean += frame.x
				max_peak = maxf(max_peak, absf(frame.x))
				if samples > 0:
					max_step = maxf(max_step, absf(frame.x-previous))
				previous = frame.x
				samples += 1
		var level := linear_to_db(sqrt(squares/samples))
		lowest = minf(lowest, level)
		highest = maxf(highest, level)
		if absf(level + 26.0) > 0.30 or absf(mean/samples) > 0.001 or max_step > 0.04:
			failures += 1
		records.append({"load": load_value, "rms_dbfs": level, "dc": mean/samples, "max_step": max_step})
		audio.free()
	if max_peak >= 0.24 or highest-lowest > 0.35:
		failures += 1
	print(JSON.stringify({"native_load_grid": records, "rms_min": lowest, "rms_max": highest,
		"peak": max_peak, "failures": failures, "qualification": "Native causal PCM numericallevel only, not perceptual/listening acceptance"}))
	quit(0 if failures == 0 else 1)
