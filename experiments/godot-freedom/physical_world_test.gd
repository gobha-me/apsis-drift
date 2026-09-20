extends SceneTree
## Actual C++ bridge, silent/headless. No rotating-flight or save-load claim.

var failures := 0

func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func bridge_for(snapshot: String) -> Variant:
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.initialize(snapshot), "Snapshot initialization refused: " + str(bridge.get_last_error()))
	return bridge


func owner_matches(lighting: Dictionary, expected: Dictionary) -> bool:
	var parsed: Variant = JSON.parse_string(lighting.get("world_context_json", "null"))
	return parsed is Dictionary and parsed == expected


func rejection(bridge: Variant, bad: Dictionary, state: Dictionary, lighting: Dictionary, transform: Array, label: String) -> void:
	check(not bridge.initialize(JSON.stringify(bad)), "Forged snapshot accepted: " + label)
	check(bridge.get_state() == state, "Refusal changed flight/session: " + label)
	var error: String = bridge.get_last_error()
	check(not error.is_empty(), "Refusal lacks diagnostic: " + label)
	check(bridge.get_world_lighting() == lighting, "Refusal changed physical owner/lighting: " + label)
	check(bridge.get_last_error() == error, "Read-only lighting query mutated error state")
	check(bridge.stream_transforms(PackedFloat64Array([1, 2, 3])) == transform, "Refusal discarded stream placement: " + label)


func _initialize() -> void:
	call_deferred("run")


func batch(bridge: Variant) -> Dictionary:
	var deadline := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline:
		var result: Dictionary = bridge.poll_stream()
		if not result.is_empty():
			return result
		await create_timer(0.005).timeout
	check(false, "Physical streaming worker timeout")
	return {}


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 4:
		push_error("Expected legacy, physical42, physical43 and procedural-home42 snapshot paths")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the live extension first")
		quit(1)
		return
	var legacy_text := FileAccess.get_file_as_string(args[0])
	var physical_text := FileAccess.get_file_as_string(args[1])
	var other_text := FileAccess.get_file_as_string(args[2])
	var physical: Dictionary = JSON.parse_string(physical_text)
	var other: Dictionary = JSON.parse_string(other_text)
	var alias: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(args[3]))
	check(physical.schema_version == 2 and physical.world_context.family == "physical_circular", "Fixture lacks explicit physical family")
	check(physical.planet.planet_id == alias.planet.planet_id and physical.planet != alias.planet, "Fixture lacks genuine authored/procedural alias")
	var uninitialized: Variant = ClassDB.instantiate("FreedomBridge")
	check(uninitialized.get_world_lighting().has("error"), "Uninitialized lighting did not report refusal")
	check(uninitialized.get_last_error().is_empty(), "Read-only uninitialized query wrote last_error")
	var legacy: Variant = bridge_for(legacy_text)
	check(legacy.get_world_lighting() == {"enabled": false}, "Legacy session silently gained physical lighting")
	var bridge: Variant = bridge_for(JSON.stringify(physical))
	check(bridge.get_state().checksum == physical.replay[0].checksum, "Authored-home replay initial state differs")
	check(bridge.enable_orbit_practice(), "Physical origin surface practice refused")
	check(bridge.enable_streaming(), "Physical origin stream refused")
	var initial: Dictionary = bridge.get_state()
	check(initial.flight_model == "thrust-lab-3", "Catalog bootstrap silently changed flight model")
	check(initial.planet_radius == physical.planet.radius_km * 1000, "Live radius differs from authored descriptor")
	check(initial.surface_start_reference.planet_seed == physical.planet.planet_seed, "Practice reset retained unrelated legacy seed")
	var first: Dictionary = bridge.get_world_lighting()
	check(first.enabled and first.tick == initial.tick, "Physical lighting lacks same simulation tick")
	check(first.model == "physical-circular-rotation-1-presentation" and first.probe_frame == "visual_geodetic", "Lighting mislabels flight-frame authority")
	check(owner_matches(first, physical.world_context), "Lighting owner must be an object matching exact world identity")
	check(first.direction.is_finite() and abs(first.direction.length() - 1) < 0.000002, "Local star direction not unit/finite")
	check(first.local_to_system.is_finite() and abs(first.local_to_system.determinant() - 1) < 0.000002, "Sky basis is invalid or reflected")
	check((first.local_to_system * first.direction).distance_to(first.direction_system) < 0.000003, "Sky and direct-light frames disagree")
	check(abs(first.direction.y - first.solar_elevation_sine) < 0.000001, "Streaming local up disagrees with physical elevation")
	check(first.star_angular_radius_radians > 0 and first.star_angular_radius_radians <= PI / 2, "Star disc angular radius invalid")
	check(abs(sin(first.star_angular_radius_radians) - first.star_radius_metres / first.star_distance_metres) < 0.000000001, "Star disc does not use actual radius/distance")
	check(bridge.get_state() == initial and bridge.get_world_lighting() == first, "Lighting mutated state or advanced a private clock")
	var untouched: Variant = bridge_for(physical_text)
	check(untouched.enable_orbit_practice() and untouched.enable_streaming(), "Control comparison fixture refused")
	var demand := PackedFloat64Array([0.4, 0, 0.1, -0.15, 0.08, 0.1, 0])
	for tick in 120:
		check(bridge.advance_thrust(1.0 / 120, demand, false), "Physical flight step refused")
		check(untouched.advance_thrust(1.0 / 120, demand, false), "Control comparison flight refused")
		var light: Dictionary = bridge.get_world_lighting()
		check(light.enabled and light.tick == tick + 1, "Lighting used a render-owned or stale clock")
	check(bridge.get_state() == untouched.get_state(), "Lighting queries altered authoritative command trace")
	# Populate a real coarse orbital cover, including frontend payload ownership.
	# A changed overlapping cover after failed replacement must reuse resident
	# tiles; a successful new world must generate/export complete payloads.
	var observer := Vector3(0, initial.planet_radius, initial.planet_radius)
	check(bridge.request_stream(observer), "Initial physical stream request refused")
	var populated: Dictionary = await batch(bridge)
	check(populated.has("tiles") and populated.get("generated", 0) > 0, "Physical stream did not populate cache")
	for tile in populated.get("tiles", []):
		check(tile.has("vertices") and tile.has("anchor"), "First cover omitted geometry payload")
	var state: Dictionary = bridge.get_state()
	var lighting: Dictionary = bridge.get_world_lighting()
	var transform: Array = bridge.stream_transforms(PackedFloat64Array([1, 2, 3]))
	for key in physical.world_context:
		if physical.world_context[key] is Dictionary:
			for nested in physical.world_context[key]:
				var bad: Dictionary = physical.duplicate(true)
				bad.world_context[key][nested] = null
				rejection(bridge, bad, state, lighting, transform, "world.%s.%s" % [key, nested])
		else:
			var bad: Dictionary = physical.duplicate(true)
			bad.world_context[key] = null
			rejection(bridge, bad, state, lighting, transform, "world." + key)
	for value in [true, 3.5, 2, "1"]:
		var bad: Dictionary = physical.duplicate(true)
		bad.world_context.generator_version = value
		rejection(bridge, bad, state, lighting, transform, "invalid physical version")
	for key in ["origin_universe_seed", "system_seed", "system_id", "planet_id", "planet_seed", "star_id", "stellar_gm_km3_per_second2"]:
		var bad: Dictionary = physical.duplicate(true)
		bad.world_context[key] = str(bad.world_context[key]) + "0"
		rejection(bridge, bad, state, lighting, transform, "altered " + key)
	var forged: Dictionary = physical.duplicate(true)
	forged.planet = alias.planet
	rejection(bridge, forged, state, lighting, transform, "same-ID procedural descriptor substitution")
	forged = physical.duplicate(true)
	forged.world_context = other.world_context
	rejection(bridge, forged, state, lighting, transform, "other universe owner")
	forged = physical.duplicate(true)
	forged.world_context.unrecognized_owner = 1
	rejection(bridge, forged, state, lighting, transform, "extra owner field")
	forged = physical.duplicate(true)
	forged.erase("world_context")
	rejection(bridge, forged, state, lighting, transform, "missing owner")
	forged = physical.duplicate(true)
	forged.replay[0].checksum = "1"
	rejection(bridge, forged, state, lighting, transform, "candidate replay fails after construction")
	forged = JSON.parse_string(legacy_text)
	forged.world_context = physical.world_context
	rejection(bridge, forged, state, lighting, transform, "physical owner injected into schema1")
	forged = physical.duplicate(true)
	forged.schema_version = 1
	forged.erase("world_context")
	rejection(bridge, forged, state, lighting, transform, "authored home downgraded to schema1")
	check(bridge.request_stream(observer), "Retained physical cover request refused")
	check(not bridge.is_stream_busy() and bridge.poll_stream().is_empty(), "Identical retained cover should deduplicate without a new batch")
	check(bridge.request_stream(Vector3(initial.planet_radius * 0.2, initial.planet_radius * 0.5, initial.planet_radius)), "Changed overlapping cover request refused")
	check(bridge.is_stream_busy(), "Overlapping-cover fixture unexpectedly deduplicated")
	var retained: Dictionary = await batch(bridge) if bridge.is_stream_busy() else {}
	var previous_ids := {}
	for tile in populated.get("tiles", []):
		previous_ids[tile.id] = true
	var overlap := 0
	for tile in retained.get("tiles", []):
		if previous_ids.has(tile.id):
			overlap += 1
			check(not tile.has("vertices"), "Rejected replacement discarded frontend payload ownership")
		else:
			check(tile.has("vertices"), "New cover tile omitted geometry payload")
	check(overlap > 0 and retained.get("reused", 0) == overlap, "Rejected replacement discarded overlapping resident tiles")
	check(retained.get("generated", -1) == retained.get("tiles", []).size() - overlap, "Changed cover generated/reused accounting mismatch")
	check(bridge.initialize(other_text), "Explicit valid universe replacement refused")
	check(bridge.get_state().tick == 0 and not bridge.get_state().streaming, "Replacement retained old clock/stream")
	check(owner_matches(bridge.get_world_lighting(), other.world_context), "Replacement retained stale universe owner")
	check(bridge.enable_orbit_practice() and bridge.enable_streaming(), "Replacement stream setup refused")
	var replacement_radius: float = bridge.get_state().planet_radius
	check(bridge.request_stream(Vector3(0, replacement_radius, replacement_radius)), "Replacement stream request refused")
	var replacement: Dictionary = await batch(bridge)
	check(replacement.has("tiles") and replacement.get("generated", 0) == replacement.get("tiles", []).size() and replacement.get("reused", -1) == 0, "Replacement reused old-world terrain cache")
	for tile in replacement.get("tiles", []):
		check(tile.has("vertices") and tile.has("anchor"), "Replacement reused stale frontend payload ownership")
	check(bridge.initialize(physical_text) and bridge.enable_orbit_practice() and bridge.enable_streaming(), "Selected world reset refused")
	check(bridge.get_state() == initial and bridge.get_world_lighting() == first, "Reset did not reproduce chosen world and lighting")
	check(bridge.initialize(legacy_text) and bridge.get_world_lighting() == {"enabled": false}, "Legacy replacement retained stale physical lighting")
	print("Physical world bridge contracts: %d failures" % failures)
	quit(0 if failures == 0 else 1)
