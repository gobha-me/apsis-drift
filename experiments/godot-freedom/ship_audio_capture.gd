extends SceneTree
## Offline comparison only. No AudioStreamPlayer/device, no generated media inputs.
const ShipAudio = preload("res://ship_audio.gd")
const CASES := [
	{"name": "cockpit-idle-vacuum", "cockpit": true, "main": 0.0, "density": 0.0, "pressure": 0.0},
	{"name": "cockpit-thrust-vacuum", "cockpit": true, "main": 0.85, "density": 0.0, "pressure": 0.0},
	{"name": "cockpit-flight-atmosphere", "cockpit": true, "main": 0.85, "density": 0.1, "pressure": 20000.0},
	{"name": "exterior-flight-atmosphere", "cockpit": false, "main": 0.85, "density": 0.1, "pressure": 20000.0},
	{"name": "exterior-thrust-vacuum", "cockpit": false, "main": 0.85, "density": 0.0, "pressure": 0.0},
	{"name": "cockpit-throttle-cycle", "cockpit": true, "main": 0.0, "density": 0.0, "pressure": 0.0, "cycle": true}
]

func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		push_error("Pass a new ignored output directory for five four-second comparisons and an eight-second throttle cycle")
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
	var manifest := {"schema_version": 1, "study": "native-ship-audio-254-growl-v2",
		"source_kind": "code-authored", "license": "BSD-3-Clause", "license_terms": "LICENSE.md",
		"attribution": "Copyright (c) 2026, Jeffrey Smith; Apsis Drift contributors",
		"source": "experiments/godot-freedom/ship_audio.gd",
		"source_sha256": FileAccess.get_sha256("res://ship_audio.gd"),
		"capture_source_sha256": FileAccess.get_sha256("res://ship_audio_capture.gd"),
		"tool": "Godot " + Engine.get_version_info().string,
		"sample_rate": ShipAudio.SAMPLE_RATE, "channels": 2, "format": "PCM16 WAV",
		"recipe": "Each independent private-seed synthesis: silence 0..0.2s; scenario active 0.2..3.4s; 60ms envelope release then silence. Lower54..68Hz body with audible harmonics, irregular band-limited mechanical growl, quiet coolant texture,0.55s up/0.9s down spool. Correlated stereo airflow preserves mono. Control edges quantized to512-frame capture blocks; no normalization or external media.",
		"throttle_cycle_recipe": "Eight seconds: idle until1.2s;25% thrust1.2..2.7s;85%2.7..4.7s;15%4.7..6.5s;idle6.5..7.4s;inactive thereafter. Initial0.2s inactive. Same512-frame control quantization.",
		"qualification": "CPU/offline artifacts only; not a listening, device, latency, or performance acceptance. Exterior vacuum deliberately silent.",
		"files": []}
	for scenario in CASES:
		var audio := ShipAudio.new()
		var pcm := PackedByteArray()
		var total: int = ShipAudio.SAMPLE_RATE * (8 if scenario.get("cycle", false) else 4)
		pcm.resize(total * 4)
		var position := 0
		var peak := 0.0
		var squares := 0.0
		while position < total:
			var seconds := float(position) / ShipAudio.SAMPLE_RATE
			var power: float = scenario.main
			var active_end := 3.4
			if scenario.get("cycle", false):
				active_end = 7.4
				power = 0.25 if seconds >= 1.2 and seconds < 2.7 else (0.85 if seconds >= 2.7 and seconds < 4.7 else (0.15 if seconds >= 4.7 and seconds < 6.5 else 0.0))
			audio.update_telemetry({"main_thrust": power, "retro_thrust": 0.0,
				"dynamic_pressure": scenario.pressure, "effective_air_density": scenario.density},
				scenario.cockpit, seconds >= 0.2 and seconds < active_end)
			var samples: PackedVector2Array = audio.synthesize_frames(mini(512, total - position))
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
			"telemetry": scenario, "duration_seconds": float(total) / ShipAudio.SAMPLE_RATE,
			"peak": peak, "rms": sqrt(squares / (total * 2))})
		audio.free()
	var sidecar := FileAccess.open(directory.path_join("provenance.json"), FileAccess.WRITE)
	if sidecar == null:
		quit(1)
		return
	sidecar.store_string(JSON.stringify(manifest, "\t") + "\n")
	print("Wrote five four-second comparisons, eight-second throttle cycle and provenance; human listening required")
	quit(0)
