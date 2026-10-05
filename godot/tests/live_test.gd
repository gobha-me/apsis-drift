extends SceneTree
## Real extension boundary tests: no GPU or platform-specific golden rewrites.

var failures := 0

func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func make_bridge(snapshot: String) -> Variant:
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.initialize(snapshot), "Live bridge initialization failed: " + str(bridge.get_last_error()))
	return bridge


func reject_snapshot(bridge: Variant, data: Dictionary, expected: Dictionary, label: String) -> void:
	check(not bridge.initialize(JSON.stringify(data)), "Invalid snapshot accepted: " + label)
	check(bridge.get_state() == expected, "Rejected snapshot changed session: " + label)
	check(not str(bridge.get_last_error()).is_empty(), "Refusal lacks diagnostic: " + label)


func descriptor_contract(bridge: Variant, data: Dictionary, expected: Dictionary) -> void:
	var reencoded: Variant = make_bridge(JSON.stringify(data))
	check(reencoded.get_state().checksum == data.replay[0].checksum, "Godot numeric re-encoding changed descriptor compatibility")
	var reordered: Dictionary = data.duplicate(true)
	var keys: Array = reordered.planet.keys()
	keys.reverse()
	var planet := {}
	for key in keys:
		planet[key] = reordered.planet[key]
	reordered.planet = planet
	check(reencoded.initialize(JSON.stringify(reordered)), "Descriptor key order changed compatibility")
	check(bridge.enable_streaming(), "Could not create retained-stream fixture")
	check(bridge.get_state().checksum == expected.checksum, "Stream setup changed flight checksum")
	# Enabling the stream changes presentation telemetry, not flight. Snapshot
	# the whole dictionary after that legitimate change for refusal assertions.
	expected = bridge.get_state()
	var anchors := PackedFloat64Array([1, 2, 3])
	var transforms: Array = bridge.stream_transforms(anchors)
	check(transforms.size() == 1, "Retained-stream fixture lacks transform")
	# Every canonical field, including nested palette entries, is authoritative.
	for key in data.planet:
		if data.planet[key] is Dictionary:
			for nested in data.planet[key]:
				var bad: Dictionary = data.duplicate(true)
				bad.planet[key][nested] = null
				reject_snapshot(bridge, bad, expected, "planet.%s.%s" % [key, nested])
		else:
			var bad: Dictionary = data.duplicate(true)
			bad.planet[key] = null
			reject_snapshot(bridge, bad, expected, "planet." + key)
	for key in ["radius_km", "surface_gravity_milli_g", "water_coverage_basis_points"]:
		for replacement in [float(data.planet[key]) + 1, 3.5, true]:
			var bad: Dictionary = data.duplicate(true)
			bad.planet[key] = replacement
			reject_snapshot(bridge, bad, expected, "altered " + key)
	for replacement in [float(data.planet.atmosphere.pressure_millibars) + 1, 3.5, false]:
		var bad: Dictionary = data.duplicate(true)
		bad.planet.atmosphere.pressure_millibars = replacement
		reject_snapshot(bridge, bad, expected, "altered pressure")
	for key in ["planet_id", "planet_seed", "display_name"]:
		var bad: Dictionary = data.duplicate(true)
		bad.planet[key] = str(data.planet[key]) + "0"
		reject_snapshot(bridge, bad, expected, "altered " + key)
	var extra: Dictionary = data.duplicate(true)
	extra.planet.owner = {"catalog_kind": "origin_home"}
	reject_snapshot(bridge, extra, expected, "unrecognized planet owner")
	var missing: Dictionary = data.duplicate(true)
	missing.planet.erase("radius_km")
	reject_snapshot(bridge, missing, expected, "missing radius")
	var palette: Dictionary = data.duplicate(true)
	palette.planet.palette.lowland = "#ffffff" if data.planet.palette.lowland != "#ffffff" else "#000000"
	reject_snapshot(bridge, palette, expected, "changed valid-shaped palette color")
	for key in ["samples", "span_metres", "lod", "experimental_relief_version"]:
		for replacement in [true, false, "1"]:
			var bad: Dictionary = data.duplicate(true)
			bad[key] = replacement
			reject_snapshot(bridge, bad, expected, "nonnumeric " + key)
	for key in ["schema_version", "terrain_generator_version", "seed_derivation_version", "lod"]:
		for replacement in [true, 3.5]:
			var bad: Dictionary = data.duplicate(true)
			bad[key] = replacement
			reject_snapshot(bridge, bad, expected, "invalid integer " + key)
	for key in ["latitude_radians", "longitude_radians"]:
		var bad: Dictionary = data.duplicate(true)
		bad.frame[key] = false
		reject_snapshot(bridge, bad, expected, "boolean " + key)
	check(bridge.stream_transforms(anchors) == transforms, "Rejected snapshot destroyed or altered stream placement")


func _initialize() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1:
		push_error("Expected snapshot path")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the live C++ adapter first")
		quit(1)
		return
	var snapshot := FileAccess.get_file_as_string(arguments[0])
	var data: Dictionary = JSON.parse_string(snapshot)
	var reference: Variant = make_bridge(snapshot)
	if reference.get_state().is_empty():
		quit(1)
		return
	check(reference.get_state().checksum == data.replay[0].checksum, "Initial checksum mismatch")
	var initial: Dictionary = reference.get_state()
	for elapsed in [-1.0, NAN, INF, 1.0e300]:
		check(not reference.advance(elapsed, 0), "Invalid time accepted")
		check(reference.get_state() == initial, "Rejected time mutated flight")
	for buttons in [-1, 256, 9223372036854775807]:
		check(not reference.advance(1.0 / 120, buttons), "Invalid controls accepted")
		check(reference.get_state() == initial, "Rejected controls mutated flight")
	check(not reference.initialize("{}"), "Malformed snapshot accepted")
	check(reference.get_state() == initial, "Rejected initialization replaced session")
	var bad: Dictionary = data.duplicate(true)
	bad.replay[0].checksum = "1"
	check(not reference.initialize(JSON.stringify(bad)), "Wrong C++ state checksum accepted")
	check(reference.get_state() == initial, "Wrong checksum reset session")
	for size in [-1, 0, 513.5, 514, 1.0e30]:
		var invalid_size: Dictionary = data.duplicate(true)
		invalid_size.samples = size
		check(not reference.initialize(JSON.stringify(invalid_size)), "Invalid dimensions accepted")
		check(reference.get_state() == initial, "Bad dimensions replaced session")
	for version in [-1, 0.5, 2, "1"]:
		var invalid_version: Dictionary = data.duplicate(true)
		invalid_version.experimental_relief_version = version
		check(not reference.initialize(JSON.stringify(invalid_version)), "Unknown relief version accepted")
		check(reference.get_state() == initial, "Bad relief version replaced session")
	for tick in range(1200):
		check(reference.advance(1.0 / 120, 1), "Fixed step rejected")
		if (tick + 1) % 4 == 0:
			check(reference.get_state().checksum == data.replay[int((tick + 1) / 4)].checksum,
				"Live C++ state differs from exported replay at tick %d" % (tick + 1))
	var expected: Dictionary = reference.get_state()
	# Reject after real progress, not only at an indistinguishable tick-zero pose.
	descriptor_contract(reference, data, expected)
	for fps in [30, 60, 144]:
		var bridge: Variant = make_bridge(snapshot)
		for frame in range(fps * 10):
			check(bridge.advance(1.0 / fps, 1), "Frame-clock advance failed")
		check(bridge.get_state() == expected, "Presentation cadence changed live state at %d FPS" % fps)
	var release: Variant = make_bridge(snapshot)
	check(release.advance(1.0 / 120, 1), "Press failed")
	check(release.advance(1.0 / 120, 0), "Release failed")
	check(release.get_state().tick == 2, "Command edges produced wrong tick count")
	var catch_up: Variant = make_bridge(snapshot)
	check(catch_up.advance(1.0, 0), "Catch-up failed")
	check(catch_up.get_state().tick == 15 and catch_up.get_state().dropped_seconds > 0.8,
		"Existing catch-up budget not preserved")
	var analog: Variant = make_bridge(snapshot)
	var original: Dictionary = analog.get_state()
	for bad_axes in [[], [0], [0, 0, 0], [0, 0, 0, 0, 0], [NAN, 0, 0, 0], [0, INF, 0, 0], [0, 0, -1.01, 0], [0, 0, 0, 1.01]]:
		check(not analog.advance_analog(1.0 / 120, PackedFloat64Array(bad_axes)), "Bad analog buffer accepted")
		check(analog.get_state() == original, "Rejected analog input mutated flight")
	for elapsed in [-1, NAN, INF, 61]:
		check(not analog.advance_analog(elapsed, PackedFloat64Array([0, 0, 0, 0])), "Bad analog time accepted")
		check(analog.get_state() == original, "Rejected analog time mutated flight")
	var analog_expected := {}
	for fps in [30, 60, 144]:
		var session: Variant = make_bridge(snapshot)
		for frame in fps * 2:
			check(session.advance_analog(1.0 / fps, PackedFloat64Array([0.4, 0.2, -0.1, 0.15])), "Analog frame failed")
		if analog_expected.is_empty():
			analog_expected = session.get_state()
		else:
			check(session.get_state() == analog_expected, "Cadence changed constant analog command trace")
	check(analog.advance_analog(1.0 / 120, PackedFloat64Array([1, 0, 0, 0])), "Analog press failed")
	check(analog.advance(1.0 / 120, 0), "Analog-to-digital release failed")
	check(analog.get_state().controls == 0, "Analog-to-digital transition stuck")
	print("Live C++ bridge checks complete: %d failures; final checksum %s" % [failures, expected.checksum])
	quit(0 if failures == 0 else 1)
