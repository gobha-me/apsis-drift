extends SceneTree
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
var failures := 0
func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)
func _initialize() -> void:
	call_deferred("run")
func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 5: quit(1); return
	for path in args:
		if not path.is_absolute_path(): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	check(ClassDB.class_exists("FreedomBridge"), "Bridge load refused")
	if not ClassDB.class_exists("FreedomBridge"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if owner == null: quit(1); return
	check(owner.stage_freedom_continue(args[0]), "Existing native save refused: " + str(owner.get_last_error()))
	if failures > 0: owner = null; quit(1); return
	var pending: Dictionary = owner.get_pending_freedom_start()
	check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Read-only terrain fixture refused")
	if failures > 0: owner = null; quit(1); return
	var state: Dictionary = owner.get_freedom_flight_state()
	check(NativeEnvironment.valid(state) and FlightView.valid_state(state), "Actual authoritative environment rejected")
	if failures > 0: owner = null; quit(1); return
	check(owner.save_freedom_as(args[1]), "Before world bytes unavailable")
	if failures > 0: owner = null; quit(1); return
	var before := FileAccess.get_sha256(args[1])
	for key in ["scale_height", "sea_density", "atmosphere_edge", "star_angular_radius"]:
		for value in [NAN, INF, -INF, -1.0, "7", true, null, Vector3.ONE]:
			var malformed := state.duplicate(true)
			malformed.lighting[key] = value
			check(not NativeEnvironment.valid(malformed) and not FlightView.valid_state(malformed), "Malformed coefficient accepted: " + key)
	for key in ["planet_radius", "altitude"]:
		for value in [NAN, INF, "7", true, null]:
			var malformed := state.duplicate(true)
			malformed[key] = value
			check(not NativeEnvironment.valid(malformed), "Malformed body dimensions accepted")
	for key in state.lighting:
		var malformed := state.duplicate(true)
		malformed.lighting.erase(key)
		check(not NativeEnvironment.valid(malformed), "Missing environment field accepted: " + key)
	var environment := Environment.new()
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	var material := NativeEnvironment.install(environment)
	var light := DirectionalLight3D.new()
	light.light_energy = 2.25
	var local_light := DirectionalLight3D.new()
	check(NativeEnvironment.apply(state, environment, light, material, local_light), "Actual environment binding refused")
	check(environment.background_mode == Environment.BG_SKY and material.get_shader_parameter("sun_direction") == state.star_direction and light.light_color == state.lighting.star_color, "Actual star transfer mismatch")
	for pair in [["radius", state.planet_radius], ["altitude", state.altitude], ["sea_density", state.lighting.sea_density], ["scale_height", state.lighting.scale_height], ["atmosphere_edge", state.atmosphere_edge]]:
		check(material.get_shader_parameter(pair[0]) == pair[1], "Coefficient transfer mismatch: " + pair[0])
	var old_energy := environment.ambient_light_energy
	var old_local_energy := local_light.light_energy
	var old_light_color := light.light_color
	var old_direction: Variant = material.get_shader_parameter("sun_direction")
	var invalid := state.duplicate(true)
	invalid.lighting.tick = "18446744073709551615"
	check(NativeEnvironment.sun_visibility(invalid) == -1.0, "Invalid shadow query accepted")
	check(not NativeEnvironment.apply(invalid, environment, light, material, local_light) and environment.ambient_light_energy == old_energy and material.get_shader_parameter("sun_direction") == old_direction and local_light.light_energy == old_local_energy and light.light_energy == 2.25 and light.light_color == old_light_color, "Rejected environment changed material or sunlight")
	# Synthetic direction fixtures qualify consumer behavior, not C++ day/night.
	var energies: Array[float] = []
	for direction in [Vector3.UP, Vector3.RIGHT, Vector3.DOWN]:
		var sample := state.duplicate(true)
		sample.altitude = 0.0
		sample.star_direction = direction
		sample.lighting.direction = direction
		check(NativeEnvironment.apply(sample, environment, light, material, local_light), "Consumer phase fixture refused")
		energies.append(environment.ambient_light_energy)
		check(is_equal_approx(local_light.light_energy, 1.0 if direction == Vector3.UP else (0.5 if direction == Vector3.RIGHT else 0.0)) and light.light_energy == 2.25, "Local shadow incorrectly changed terrain sunlight")
	check(energies[0] > energies[1] and energies[1] > energies[2] and is_equal_approx(energies[2], 0.06), "Ambient phase response is incoherent")
	# Angular-limb regressions: height changes the visible horizon, and a
	# large stellar disk at the anti-solar pole can still be wholly occulted.
	for height_ratio in [0.0, 0.04, 1.5, 10.0]:
		var horizon := -acos(1.0 / (1.0 + height_ratio))
		for offset in [-0.02, 0.0, 0.02]:
			var sample := state.duplicate(true)
			sample.altitude = sample.planet_radius * height_ratio
			sample.star_direction = Vector3(cos(horizon + offset), sin(horizon + offset), 0.0)
			sample.lighting.direction = sample.star_direction
			sample.lighting.star_angular_radius = 0.01
			check(absf(NativeEnvironment.sun_visibility(sample) - (0.0 if offset < 0 else (1.0 if offset > 0 else 0.5))) < 0.0001, "Angular limb coverage changes with height")
	var anti_solar := state.duplicate(true)
	anti_solar.altitude = anti_solar.planet_radius * 1.5
	anti_solar.star_direction = Vector3.DOWN
	anti_solar.lighting.direction = Vector3.DOWN
	anti_solar.lighting.star_angular_radius = 0.183547532694299
	check(NativeEnvironment.sun_visibility(anti_solar) == 0.0, "High-altitude anti-solar sunlight leaked through planet")
	var low_sun := state.duplicate(true)
	low_sun.lighting.star_angular_radius = 0.00465
	low_sun.star_direction = Vector3(1.0, -0.1, 0.0).normalized()
	low_sun.lighting.direction = low_sun.star_direction
	low_sun.altitude = 0.0
	check(NativeEnvironment.sun_visibility(low_sun) == 0.0, "Surface below-horizon star visible")
	low_sun.altitude = 250000.0
	check(NativeEnvironment.sun_visibility(low_sun) == 1.0, "Elevated below-horizontal star wrongly hidden")
	var airless := state.duplicate(true)
	airless.atmosphere_edge = 0.0
	airless.lighting.atmosphere_edge = 0.0
	airless.lighting.scale_height = 0.0
	airless.lighting.sea_density = 0.0
	check(NativeEnvironment.apply(airless, environment, light, material, local_light) and material.get_shader_parameter("sea_density") == 0.0, "Airless fixture refused")
	check(owner.get_freedom_flight_state() == state and owner.save_freedom_as(args[1]) and FileAccess.get_sha256(args[1]) == before, "Presentation changed authoritative world bytes")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual next-tick command refused")
	var advanced: Dictionary = owner.get_freedom_flight_state()
	check(advanced.tick != state.tick and NativeEnvironment.apply(advanced, environment, light, material, local_light) and material.get_shader_parameter("sun_direction") == advanced.star_direction, "Environment retained stale tick or direction")
	var updated := args[1] + ".updated.json"
	check(owner.save_freedom_as(updated), "Updated authoritative save refused")
	var continued: Variant = ClassDB.instantiate("FreedomBridge")
	var loaded: bool = continued.stage_freedom_continue(updated)
	check(loaded, "Updated native Continue refused: " + str(continued.get_last_error()))
	if loaded:
		var next_pending: Dictionary = continued.get_pending_freedom_start()
		check(continued.commit_pending_freedom_start(next_pending.candidate_id), "Updated pending world refused")
		var restored: Dictionary = continued.get_freedom_flight_state()
		check(restored.get("lighting") == advanced.lighting and restored.get("body_basis") == advanced.body_basis, "Continue changed canonical illumination or orientation")
	continued = null
	# Independently generated, actual C++ saved port configurations. Their
	# clocks/catalog1 geometry are real; these are not continuously flown states.
	for index in range(2, 5):
		var configured: Variant = ClassDB.instantiate("FreedomBridge")
		var staged: bool = configured.stage_freedom_continue(args[index])
		check(staged, "Saved shadow configuration refused")
		if staged:
			var candidate: Dictionary = configured.get_pending_freedom_start()
			check(configured.commit_pending_freedom_start(candidate.candidate_id), "Saved shadow configuration did not commit")
			var observed: Dictionary = configured.get_freedom_flight_state()
			check(NativeEnvironment.apply(observed, environment, light, material, local_light), "C++ saved shadow illumination refused")
			var expected := 1.0 if index == 2 else 0.0
			check((is_equal_approx(local_light.light_energy, expected) if index < 4 else local_light.light_energy > 0.0 and local_light.light_energy < 1.0) and light.light_energy == 2.25 and is_equal_approx(material.get_shader_parameter("sun_visibility"), local_light.light_energy), "Local/terrain/sky disagreement at actual saved shadow phase")
			var preserved := args[1] + ".shadow-%d.json" % index
			check(configured.save_freedom_as(preserved) and FileAccess.get_sha256(preserved) == FileAccess.get_sha256(args[index]) and configured.get_freedom_flight_state() == observed, "Shadow presentation changed complete world bytes")
		configured = null
	light.free()
	local_light.free()
	owner = null
	await process_frame
	print("Native environment consumer: %d failures; synthetic angular fixtures and actual saved C++ port phases, no raster/hardware claim" % failures)
	quit(0 if failures == 0 else 1)
