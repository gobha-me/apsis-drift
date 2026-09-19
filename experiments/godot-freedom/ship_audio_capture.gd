extends SceneTree
## Offline comparison only. No AudioStreamPlayer/device, no generated media inputs.
const ShipAudio = preload("res://ship_audio.gd")
const CASES := [
	{"name": "cockpit-idle-vacuum", "cockpit": true, "main": 0.0, "density": 0.0, "pressure": 0.0},
	{"name": "cockpit-thrust-vacuum", "cockpit": true, "main": 0.85, "density": 0.0, "pressure": 0.0},
	{"name": "cockpit-flight-atmosphere", "cockpit": true, "main": 0.85, "density": 0.1, "pressure": 20000.0},
	{"name": "exterior-flight-atmosphere", "cockpit": false, "main": 0.85, "density": 0.1, "pressure": 20000.0},
	{"name": "exterior-thrust-vacuum", "cockpit": false, "main": 0.85, "density": 0.0, "pressure": 0.0}
]

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		push_error("Pass a new ignored output directory for five four-second WAV comparisons")
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
	var manifest := {"schema_version": 1, "study": "native-ship-audio-254-v1",
		"source_kind": "code-authored", "license": "BSD-3-Clause", "license_terms": "LICENSE.md",
		"attribution": "Copyright (c) 2026, Jeffrey Smith; Apsis Drift contributors",
		"source": "experiments/godot-freedom/ship_audio.gd",
		"source_sha256": FileAccess.get_sha256("res://ship_audio.gd"),
		"capture_source_sha256": FileAccess.get_sha256("res://ship_audio_capture.gd"),
		"tool": "Godot " + Engine.get_version_info().string,
		"sample_rate": ShipAudio.SAMPLE_RATE, "channels": 2, "format": "PCM16 WAV",
		"recipe": "Each independent private-seed synthesis: silence 0..0.2s; scenario active 0.2..3.4s; 60ms envelope release then silence. Control edges quantized to 512-frame capture blocks; no normalization, compression or external media.",
		"qualification": "CPU/offline artifacts only; not a listening, device, latency, or performance acceptance. Exterior vacuum deliberately silent.",
		"files": []}
	for scenario in CASES:
		var audio := ShipAudio.new()
		var pcm := PackedByteArray()
		const TOTAL := ShipAudio.SAMPLE_RATE * 4
		pcm.resize(TOTAL * 4)
		var position := 0
		var peak := 0.0
		var squares := 0.0
		while position < TOTAL:
			var seconds := float(position) / ShipAudio.SAMPLE_RATE
			audio.update_telemetry({"main_thrust": scenario.main, "retro_thrust": 0.0,
				"dynamic_pressure": scenario.pressure, "effective_air_density": scenario.density},
				scenario.cockpit, seconds >= 0.2 and seconds < 3.4)
			var samples: PackedVector2Array = audio.synthesize_frames(mini(512, TOTAL - position))
			for sample in samples:
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
			"telemetry": scenario, "peak": peak, "rms": sqrt(squares / (TOTAL * 2))})
		audio.free()
	var sidecar := FileAccess.open(directory.path_join("provenance.json"), FileAccess.WRITE)
	if sidecar == null:
		quit(1)
		return
	sidecar.store_string(JSON.stringify(manifest, "\t") + "\n")
	print("Wrote five bounded four-second comparisons and provenance; human listening required")
	quit(0)
