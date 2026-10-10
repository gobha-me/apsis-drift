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
	if args.size() != 2: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	check(ClassDB.class_exists("FreedomBridge"), "Bridge load refused")
	if not ClassDB.class_exists("FreedomBridge"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if owner == null: quit(1); return
	check(owner.stage_freedom_continue(args[0]), "Existing native save refused")
	var pending: Dictionary = owner.get_pending_freedom_start()
	check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Read-only terrain fixture refused")
	var state: Dictionary = owner.get_freedom_flight_state()
	check(NativeEnvironment.valid(state) and FlightView.valid_state(state), "Actual authoritative environment rejected")
	check(owner.save_freedom_as(args[1]), "Before world bytes unavailable")
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
	check(NativeEnvironment.apply(state, environment, light, material), "Actual environment binding refused")
	check(environment.background_mode == Environment.BG_SKY and material.get_shader_parameter("sun_direction") == state.star_direction and light.light_color == state.lighting.star_color, "Actual star transfer mismatch")
	for pair in [["radius", state.planet_radius], ["altitude", state.altitude], ["sea_density", state.lighting.sea_density], ["scale_height", state.lighting.scale_height], ["atmosphere_edge", state.atmosphere_edge]]:
		check(material.get_shader_parameter(pair[0]) == pair[1], "Coefficient transfer mismatch: " + pair[0])
	var old_energy := environment.ambient_light_energy
	var old_direction: Variant = material.get_shader_parameter("sun_direction")
	var invalid := state.duplicate(true)
	invalid.lighting.tick = "18446744073709551615"
	check(not NativeEnvironment.apply(invalid, environment, light, material) and environment.ambient_light_energy == old_energy and material.get_shader_parameter("sun_direction") == old_direction, "Rejected environment changed material")
	# Synthetic direction fixtures qualify consumer behavior, not C++ day/night.
	var energies: Array[float] = []
	for direction in [Vector3.UP, Vector3.RIGHT, Vector3.DOWN]:
		var sample := state.duplicate(true)
		sample.altitude = 0.0
		sample.star_direction = direction
		sample.lighting.direction = direction
		check(NativeEnvironment.apply(sample, environment, light, material), "Consumer phase fixture refused")
		energies.append(environment.ambient_light_energy)
	check(energies[0] > energies[1] and energies[1] > energies[2] and is_equal_approx(energies[2], 0.06), "Ambient phase response is incoherent")
	var airless := state.duplicate(true)
	airless.atmosphere_edge = 0.0
	airless.lighting.atmosphere_edge = 0.0
	airless.lighting.scale_height = 0.0
	airless.lighting.sea_density = 0.0
	check(NativeEnvironment.apply(airless, environment, light, material) and material.get_shader_parameter("sea_density") == 0.0, "Airless fixture refused")
	check(owner.get_freedom_flight_state() == state and owner.save_freedom_as(args[1]) and FileAccess.get_sha256(args[1]) == before, "Presentation changed authoritative world bytes")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual next-tick command refused")
	var advanced: Dictionary = owner.get_freedom_flight_state()
	check(advanced.tick != state.tick and NativeEnvironment.apply(advanced, environment, light, material) and material.get_shader_parameter("sun_direction") == advanced.star_direction, "Environment retained stale tick or direction")
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
	light.free()
	owner = null
	await process_frame
	print("Native environment consumer: %d failures; synthetic phase fixtures, no raster/hardware claim" % failures)
	quit(0 if failures == 0 else 1)
