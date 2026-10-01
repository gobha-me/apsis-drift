extends SceneTree
## Real selected flight/control/Save As trace against independently written C++.
const FlightView = preload("res://native_flight_view.gd")
const MainExhaust = preload("res://native_main_exhaust.gd")
var failures := 0


func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1


func refused(owner: Variant, elapsed: float, fractions: PackedFloat64Array, paused: bool) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	check(not owner.advance_freedom_flight(elapsed, fractions, paused), "Malformed saved-flight command accepted")
	check(not str(owner.get_last_error()).is_empty(), "Refusal lost diagnostic")
	var diagnostic: String = owner.get_last_error()
	check(owner.get_freedom_flight_state() == before, "Rejected batch changed flight or time")
	check(owner.get_last_error() == diagnostic, "Pure view discarded command diagnostic")


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 6:
		push_error("Expected flight, C++ trace, corrupt, station saves and prepared assets")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Build the C++ bridge first")
		quit(1)
		return
	var original: Array[PackedByteArray] = []
	for path in args.slice(0, 4):
		original.append(FileAccess.get_file_as_bytes(path))
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	refused(owner, 0.0, neutral, false)
	check(not owner.set_freedom_assistance(false), "Uninitialized owner accepted assistance")
	check(owner.initialize_freedom_continue(args[0]), "Selected flight refused: " + str(owner.get_last_error()))
	var start: Dictionary = owner.get_freedom_flight_state()
	check(FlightView.valid_state(start) and start.tick == "25" and start.frame_id == "2" and start.assistance, "Selected state was not the saved Wayfarer")
	check(absf(start.body_basis.determinant() - 1.0) < 0.00001 and absf(start.station_basis.determinant() - 1.0) < 0.00001, "Projection changed orientation handedness")
	check(absf(start.altitude - 500000.0) < 0.001, "Rotation projection changed saved radial altitude")
	check(owner.get_freedom_start().is_empty() and owner.get_state().is_empty(), "Flight created a docked or study owner")
	for size in [0, 1, 11, 13, 1000]:
		var truncated := PackedFloat64Array()
		truncated.resize(size)
		refused(owner, 1.0 / 120.0, truncated, false)
	for bad in [NAN, INF, -INF, -0.01, 1.01]:
		for index in 12:
			var invalid := neutral.duplicate()
			invalid[index] = bad
			refused(owner, 1.0 / 120.0, invalid, false)
			refused(owner, 0.0, invalid, true)
	for bad_elapsed in [NAN, INF, -1.0, 60.01]:
		refused(owner, bad_elapsed, neutral, false)
	check(owner.initialize_freedom_continue(args[5]), "Valid terminal clock fixture refused")
	refused(owner, 0.1, neutral, false)
	check(owner.initialize_freedom_continue(args[0]), "Clock refusal damaged normal Continue")
	check(owner.advance_freedom_flight(60.0, neutral, true) and owner.get_freedom_flight_state() == start, "Paused time changed clock/body")
	check(not owner.initialize_freedom_continue(args[2]) and owner.get_freedom_flight_state() == start, "Corrupt load replaced selected flight")
	var view := FlightView.new()
	root.add_child(view)
	check(view.initialize(owner, args[4]), "Production flight view failed: " + view.error)
	view.set_process(false)
	check(view.paused and not view.cockpit and view.ship.get_child_count() == 2 and view.exhaust != null, "Continue did not start safely paused with the actual model")
	check(view.terrain.bridge == owner and owner.get_freedom_flight_state() == start, "Terrain/view invented or advanced saved state")
	check(is_equal_approx(view.camera.near, 0.05) and view.camera.far == 200.0 and view.terrain_camera.far / view.terrain_camera.near <= 500001.0, "Native camera depth ranges are unsafe")
	check(view.camera.transform == view.terrain_camera.transform and view.camera.fov == view.terrain_camera.fov and view.camera.cull_mask == 3 and view.terrain_camera.cull_mask == 1, "Close terrain occlusion/camera registration was lost")
	check(view.camera.get_world_3d() == view.terrain_camera.get_world_3d() and view.camera.get_viewport() != view.terrain_camera.get_viewport(), "Depth passes lost the shared world or viewport isolation")
	check(view.exhaust.plumes.size() == 2 and view.exhaust.intensity == 0.0, "Neutral Continue displayed main thrust")
	var glass: MeshInstance3D = view.ship.find_child("HopperGlass", true, false)
	check(glass != null and glass.material_override != null and glass.material_override.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA and glass.material_override.albedo_color.a < 0.1, "Selected cockpit glass blocks native flight visibility")
	var commands := neutral.duplicate()
	commands[4] = 0.1
	commands[5] = 0.25
	commands[7] = 0.05
	var output := args[0].get_base_dir().path_join("godot-flight-trace.json")
	for n in 120:
		if n == 60:
			check(owner.save_freedom_as(output) and owner.initialize_freedom_continue(output), "Mid-trace Save As/Continue failed")
			check(owner.set_freedom_assistance(false), "Explicit Advanced piloting refused")
		check(owner.advance_freedom_flight(1.0 / 120.0, commands, false), "C++ flight rejected a real control step")
	var final: Dictionary = owner.get_freedom_flight_state()
	check(final.tick == "145" and not final.assistance and final.checksum != start.checksum, "Input did not advance/persist selected state")
	check(owner.save_freedom_as(output), "Flight Save As refused")
	check(FileAccess.get_file_as_bytes(output) == original[1], "Godot input/save trace differs from independent C++ trace bytes")
	check(view.exhaust.update_applied(final, 0.1, false) and is_equal_approx(view.exhaust.intensity, 0.25), "Exhaust ignored actual C++ gross main force")
	var phase: float = view.exhaust.phase
	check(view.exhaust.update_applied(final, 0.1, false) and view.exhaust.phase > phase and view.exhaust.plumes[0].visible, "Applied plume did not animate")
	phase = view.exhaust.phase
	check(view.exhaust.update_applied(final, 60.0, true) and view.exhaust.intensity == 0.0 and view.exhaust.phase == phase, "Paused exhaust changed phase or kept firing")
	var invalid_force := final.duplicate(true)
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false) and not view.exhaust.plumes[0].visible, "Truncated force buffer lit a plume")
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0, NAN])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false), "Nonfinite force lit a plume")
	# A different presentation cadence consumes exactly the same commands/ticks.
	var other: Variant = ClassDB.instantiate("FreedomBridge")
	check(other.initialize_freedom_continue(args[0]), "Cadence comparison could not Continue")
	for n in 60:
		if n == 30:
			check(other.set_freedom_assistance(false), "Cadence assistance refused")
		check(other.advance_freedom_flight(1.0 / 60.0, commands, false), "Two-tick presentation batch refused")
	check(other.get_freedom_flight_state() == final, "Presentation cadence changed authoritative state or applied propulsion")
	check(other.initialize_freedom_continue(args[0]), "Opposing-channel fixture could not Continue")
	var opposed := neutral.duplicate()
	opposed[2] = 1.0
	opposed[5] = start.positive_force_ratings[2] / start.negative_force_ratings[2]
	check(other.advance_freedom_flight(1.0 / 120.0, opposed, false), "Physical opposing firings refused")
	var gross: Dictionary = other.get_freedom_flight_state()
	check(absf(gross.applied_force_body[2]) < 0.00001 and gross.negative_force_body[2] > 0.0, "Gross opposition was erased or became net thrust")
	check(view.exhaust.update_applied(gross, 0.1, false) and view.exhaust.intensity > 0.0, "Zero net propulsion hid an actual firing main nozzle")
	check(other.initialize_freedom_continue(output) and other.get_freedom_flight_state().checksum == final.checksum, "Flight Save As did not resume exact state")
	for path in ["relative.json", "", args[0].get_base_dir(), args[0].get_base_dir() + "/missing/save.json"]:
		check(not owner.save_freedom_as(path) and owner.get_freedom_flight_state() == final, "Refused Save As changed selected flight")
	check(other.initialize_freedom_continue(args[3]) and other.get_freedom_flight_state().is_empty(), "Station selection retained stale saved flight")
	view.free()
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == original[i], "Native flight modified a source fixture")
	print("Saved native flight: %d failures; actual C++ controls and save trace, no GPU qualification" % failures)
	quit(0 if failures == 0 else 1)
