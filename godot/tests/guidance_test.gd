extends SceneTree
## Read-only native boundary contract, not GPU/controller qualification.
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func verify(bridge: Variant) -> void:
	var before: Dictionary = bridge.get_state()
	for mode in [0, 1, 2, 3, 0, -1, 4, 9223372036854775807]:
		var result: Dictionary = bridge.get_flight_guidance(mode)
		check(bridge.get_state() == before, "Guidance changed complete flight state")
		check(result.ok == (mode >= 0 and mode <= 3), "Invalid mode boundary")
		if not result.ok or mode == 0:
			continue
		check(result.cue is String and not result.cue.is_empty(), "Missing stable cue identifier")
		check(result.coast.size() >= 1 and result.coast.size() <= 129 and result.reference.size() <= 97, "Unbounded guidance arrays")
		for points in [result.coast, result.reference]:
			for point in points:
				check(point.is_finite(), "Nonfinite guidance projection")
		check(is_finite(result.air_radius) and result.air_radius >= 1 and result.seconds >= 0 and result.seconds <= 900, "Invalid guidance scalars")
		check(result.body_delta.is_finite(), "Nonfinite body delta")
		check(result == bridge.get_flight_guidance(mode), "Guidance readback is unstable")

func _initialize() -> void:
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(not bridge.get_flight_guidance(1).ok, "Uninitialized guidance accepted")
	check(bridge.initialize(FileAccess.get_file_as_string(args[0])), "Fixture initialization")
	check(not bridge.get_flight_guidance(1).ok, "Legacy guidance accepted")
	check(bridge.enable_surface_practice(), "Native fixture initialization")
	verify(bridge)
	check(bridge.get_flight_guidance(2).cue == "in_atmosphere", "Atmospheric return incorrectly tells pilot to climb")
	check(bridge.enable_streaming() and bridge.start_practice(false), "Orbit fixture initialization")
	verify(bridge)
	check(bridge.get_flight_guidance(1).cue == "orbit", "Clear-orbit cue mismatch")
	check(bridge.start_practice(true), "Reentry fixture initialization")
	verify(bridge)
	var observed: Variant = ClassDB.instantiate("FreedomBridge")
	check(observed.initialize(FileAccess.get_file_as_string(args[0])) and observed.enable_surface_practice(), "Compared fixture initialization")
	var unobserved: Variant = ClassDB.instantiate("FreedomBridge")
	check(unobserved.initialize(FileAccess.get_file_as_string(args[0])) and unobserved.enable_surface_practice(), "Baseline fixture initialization")
	for tick in 120:
		observed.get_flight_guidance(1 + tick % 3)
		var axes := PackedFloat64Array([0.2, 0, 0, 0, 0, 0, 0])
		check(observed.advance_thrust(1.0 / 120, axes, true) and unobserved.advance_thrust(1.0 / 120, axes, true), "Flight cadence failed")
	check(observed.get_state() == unobserved.get_state(), "Guidance polling changed physics or clock")
	print("Native guidance contracts: %d failures" % failures)
	quit(1 if failures else 0)
