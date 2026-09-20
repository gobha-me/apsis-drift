extends SceneTree
## Offline comparison only. No AudioStreamPlayer/device, no generated media inputs.
const ShipAudio = preload("res://ship_audio.gd")
const CASES := [
	{"name": "01-native-constant-level-sweep-16s", "seconds": 16, "stop": 15.1,
		"times": [0, 2, 4, 5, 7, 8, 10, 11, 14, 16], "loads": [0, 0, 0.25, 0.25, 0.55, 0.55, 1, 1, 0, 0],
		"density": 0.0, "pressure": 0.0},
	{"name": "02-native-constant-level-comfort-40s", "seconds": 40, "stop": 39.1,
		"times": [0, 8, 10, 17, 19, 26, 28, 35, 37.5, 40], "loads": [0, 0, 0.25, 0.25, 0.55, 0.55, 1, 1, 0, 0],
		"density": 0.0, "pressure": 0.0},
	{"name": "03-native-atmospheric-layer-8s", "seconds": 8, "stop": 7.2,
		"times": [0, 2, 4, 6, 8], "loads": [0, 0.25, 0.85, 0.25, 0], "density": 0.1, "pressure": 20000.0}
]

func load_at(scenario: Dictionary, seconds: float) -> float:
	for index in scenario.times.size()-1:
		if seconds < scenario.times[index+1]:
			return lerpf(scenario.loads[index], scenario.loads[index+1],
				(seconds-scenario.times[index])/(scenario.times[index+1]-scenario.times[index]))
	return scenario.loads[-1]

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		push_error("Pass a NEW ignored directory for native16s/40s and atmospheric8s auditions")
		quit(1)
		return
	var directory: String = args[0]
	if DirAccess.dir_exists_absolute(directory):
		push_error("Refusing to overwrite an existing audition directory")
		quit(1)
		return
	if DirAccess.make_dir_recursive_absolute(directory) != OK:
		quit(1)
		return
	var manifest := {"schema_version": 1, "study": "native-direction24-constant-level-port",
		"source_kind": "code-authored", "license": "BSD-3-Clause", "license_terms": "LICENSE.md",
		"attribution": "Copyright (c) 2026, Jeffrey Smith; Apsis Drift contributors",
		"source": "experiments/godot-freedom/ship_audio.gd",
		"source_sha256": FileAccess.get_sha256("res://ship_audio.gd"),
		"capture_source_sha256": FileAccess.get_sha256("res://ship_audio_capture.gd"),
		"tool": "Godot " + Engine.get_version_info().string,
		"sample_rate": ShipAudio.SAMPLE_RATE, "channels": 2, "format": "PCM16 WAV",
		"recipe": "Approved direction24carrier/body/weights/leveltable;128-frame telemetry cadence. Poweredidle continuous at0load. Airflow separate. No extra bed/throttlegain/clipnormalization.",
		"native_deviations": "Bounded privateLCG + causal difference-of-lowpass textures replace offlinePCG/FFT and finite-record normalization. Same carriercoefficients and .4/.65s loadstate. Native60ms gate fades replace offline.35/.4s auditionfades. Not bit-identicalofflinePCM.",
		"qualification": "CPU native samples only, NOT human-listening/perceivedloudness/device/performance qualification; no livegame launched.",
		"files": []}
	for scenario in CASES:
		var audio := ShipAudio.new()
		var pcm := PackedByteArray()
		var total: int = ShipAudio.SAMPLE_RATE * scenario.seconds
		pcm.resize(total * 4)
		var position := 0
		var peak := 0.0
		var squares := 0.0
		while position < total:
			var seconds := float(position) / ShipAudio.SAMPLE_RATE
			audio.update_telemetry({"main_thrust": load_at(scenario, seconds), "retro_thrust": 0.0,
				"dynamic_pressure": scenario.pressure, "effective_air_density": scenario.density},
				true, seconds < scenario.stop)
			var samples: PackedVector2Array = audio.synthesize_frames(mini(128, total - position))
			for sample in samples:
				if not is_finite(sample.x) or not is_finite(sample.y) or maxf(absf(sample.x), absf(sample.y)) >= ShipAudio.LIMIT:
					push_error("Capture sample nonfinite or reached safety clamp")
					audio.free()
					quit(1)
					return
				peak = maxf(peak, maxf(absf(sample.x), absf(sample.y)))
				squares += sample.x * sample.x + sample.y * sample.y
				pcm.encode_s16(position * 4, int(round(sample.x * 32767)))
				pcm.encode_s16(position * 4 + 2, int(round(sample.y * 32767)))
				position += 1
		var wav := AudioStreamWAV.new()
		wav.format = AudioStreamWAV.FORMAT_16_BITS
		wav.mix_rate = ShipAudio.SAMPLE_RATE
		wav.stereo = true
		wav.data = pcm
		var filename: String = scenario.name + ".wav"
		var path := directory.path_join(filename)
		if wav.save_to_wav(path) != OK:
			audio.free()
			quit(1)
			return
		manifest.files.append({"file": filename, "sha256": FileAccess.get_sha256(path),
			"telemetry": scenario, "duration_seconds": float(total) / ShipAudio.SAMPLE_RATE,
			"peak": peak, "rms": sqrt(squares / (total * 2))})
		audio.free()
	var sidecar := FileAccess.open(directory.path_join("provenance.json"), FileAccess.WRITE)
	if sidecar == null:
		quit(1)
		return
	sidecar.store_string(JSON.stringify(manifest, "\t") + "\n")
	print("Nativeconstant-level16s/40s + atmospheric8s auditions written; human comparison required")
	quit(0)
