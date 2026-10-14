extends SceneTree
## Real native capture/constraint/release/Save As trace against independent C++.
const FlightView = preload("res://scripts/native/native_flight_view.gd")
var failures := 0


func check(ok: bool, message: String) -> void:
	if not ok:
		push_error(message)
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func check_port_camera(view: Control, owner: Variant, source: Dictionary) -> void:
	# Synthetic presentation observations exercise a sub-millimetre boundary;
	# no provider pose, physical separation or simulation clock is rewritten.
	var camera_mode: bool = view.cockpit
	var look: Vector2 = view.look_offset
	view.cockpit = false
	view.look_offset = Vector2(0.23, -0.17)
	var look_basis := Basis(Vector3.UP, -view.look_offset.x) * Basis(Vector3.RIGHT, -view.look_offset.y)
	for rotation in [Basis.IDENTITY, Basis(Vector3(1, 2, 3).normalized(), 0.63)]:
		view.state = source.duplicate(true)
		view.state.body_basis = source.body_basis * rotation
		var previous_height := -INF
		var points := {}
		for distance in [0.0, 99.999, 100.0, 100.001, 125.0, 150.0, 175.0, 199.999, 200.0, 200.001, 1000.0]:
			view.state.docking.separation = distance
			view.update_view(0.0)
			var local_eye: Vector3 = look_basis.inverse() * view.state.body_basis.inverse() * view.camera.position
			check(local_eye.is_finite() and absf(local_eye.x - 17.0) < 0.0001 and absf(local_eye.z - 28.0) < 0.0001, "Exterior camera lost its body/look reference")
			check(local_eye.y >= -7.0001 and local_eye.y <= 9.0001 and local_eye.y >= previous_height - 0.0001, "Exterior camera height overshot or reversed during departure")
			check(view.camera.transform == view.terrain_camera.transform, "Port transition split the registered cameras")
			previous_height = local_eye.y
			points[distance] = view.camera.transform
		check(points[99.999].origin.distance_to(points[100.001].origin) < 0.001, "Exterior camera jumps at the old100m port cutoff")
		check(points[199.999].origin.distance_to(points[200.001].origin) < 0.001, "Exterior camera jumps at the far endpoint")
		check(absf((look_basis.inverse() * view.state.body_basis.inverse() * points[0.0].origin).y + 7.0) < 0.0001 and absf((look_basis.inverse() * view.state.body_basis.inverse() * points[1000.0].origin).y - 9.0) < 0.0001, "Existing close/far exterior viewpoints changed")
		var distances: Array = points.keys()
		distances.reverse()
		for distance in distances:
			view.state.docking.separation = distance
			view.update_view(0.25)
			check(view.camera.transform == points[distance], "Port camera depends on history, cadence or approach direction")
		view.state.docking.clear()
		view.update_view(0.0)
		check(view.camera.transform == points[1000.0], "Unavailable port changed the normal exterior camera")
	view.state = source.duplicate(true)
	view.cockpit = camera_mode
	view.look_offset = look
	view.update_view(0.0)
	check(owner.get_freedom_flight_state() == source, "Camera-only observation checks mutated authoritative flight")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 5:
		push_error("Expected approach, attached, trace, far saves and prepared assets")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	check(not owner.select_freedom_port(1) and not owner.capture_freedom_port() and not owner.release_freedom_port(), "Uninitialized port actions accepted")
	var originals: Array[PackedByteArray] = []
	for path in args.slice(0, 4):
		originals.append(FileAccess.get_file_as_bytes(path))
	check(owner.initialize_freedom_continue(args[3]), "Far approach could not Continue")
	var far: Dictionary = owner.get_freedom_flight_state()
	check(not far.attached and not far.docking.ready and not owner.capture_freedom_port(), "Distant capture relocated the craft")
	check(owner.get_freedom_flight_state() == far, "Failed distant capture changed clock/pose/target")
	check(owner.initialize_freedom_continue(args[0]), "Approach Continue refused")
	var start: Dictionary = owner.get_freedom_flight_state()
	for separation in [null, "100", 100, [], NAN, INF, -INF, -0.001]:
		var invalid_observation := start.duplicate(true)
		invalid_observation.docking.separation = separation
		check(not FlightView.valid_state(invalid_observation), "Invalid port separation accepted before presentation")
	var missing_separation := start.duplicate(true)
	missing_separation.docking.erase("separation")
	check(not FlightView.valid_state(missing_separation), "Missing port separation accepted before presentation")
	for ordinal in [-1, 0, 3, 4294967297, 9223372036854775807]:
		check(not owner.select_freedom_port(ordinal) and owner.get_freedom_flight_state() == start, "Malformed target changed selected owner")
	var corrupt := JSON.parse_string(FileAccess.get_file_as_string(args[0])) as Dictionary
	corrupt.docking.attached = true
	var corrupt_path := args[0].get_base_dir().path_join("bad-attachment.json")
	var file := FileAccess.open(corrupt_path, FileAccess.WRITE)
	file.store_string(JSON.stringify(corrupt))
	file.close()
	check(not owner.initialize_freedom_continue(corrupt_path) and owner.get_freedom_flight_state() == start, "Claimed attachment replaced real approach")
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	check(owner.advance_freedom_flight(60.0, neutral, true) and owner.get_freedom_flight_state() == start, "Paused approach advanced")
	var view := FlightView.new()
	root.add_child(view)
	view.set_process(false)
	check(view.initialize(owner, args[4]), "Physical station/ship view failed: " + view.error)
	check(view.station != null and view.station.camera == null and view.station.port_markers.size() == 2, "Close view lost real station or introduced inspection camera")
	check(view.station.transform == Transform3D(start.station_basis, start.station_position), "Rendered station differs from C++ same-tick projection")
	check(not view.capture_button.disabled and view.release_button.disabled, "Ready approach UI disagrees with capture gate")
	check_port_camera(view, owner, start)
	view.port_command("capture_freedom_port")
	var captured: Dictionary = owner.get_freedom_flight_state()
	check(captured.attached and captured.tick == start.tick and captured.target_port == 1, "UI capture did not apply the actual port constraint")
	check(view.capture_button.disabled and not view.release_button.disabled and view.port_buttons[0].disabled, "Attachment UI permits ambiguous recapture/target")
	check(captured.negative_force_body == PackedFloat64Array([0.0, 0.0, 0.0]), "Capture retained firing exhaust")
	var path := args[0].get_base_dir().path_join("native-attached.json")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[1], "Native capture Save As differs from C++ attachment bytes")
	check(owner.initialize_freedom_continue(path) and owner.get_freedom_flight_state() == captured, "Attached Continue changed canonical state")
	check(not owner.select_freedom_port(2) and not owner.capture_freedom_port() and owner.get_freedom_flight_state() == captured, "Attached actions changed port or pose")
	var invalid := neutral.duplicate()
	invalid[5] = NAN
	check(not owner.advance_freedom_flight(1.0 / 120.0, invalid, true) and owner.get_freedom_flight_state() == captured, "Paused attachment ignored nonfinite command")
	var commands := neutral.duplicate()
	commands[4] = 0.25
	commands[5] = 0.1
	check(not owner.advance_freedom_flight(1.0 / 120.0, commands, false) and owner.get_freedom_flight_state() == captured, "Attached propulsion mutated body/time")
	for n in 120:
		check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Constrained clock refused")
	var constrained: Dictionary = owner.get_freedom_flight_state()
	check(constrained.tick == "145" and constrained.attached and constrained.negative_force_body == PackedFloat64Array([0.0, 0.0, 0.0]), "Station co-motion became propulsion or lost shared clock")
	view.state = constrained
	view.update_view(0.0)
	view.port_command("release_freedom_port")
	var released: Dictionary = owner.get_freedom_flight_state()
	for key in ["tick", "checksum", "position_metres", "velocity_metres_per_second", "orientation_wxyz", "angular_velocity_body"]:
		check(released[key] == constrained[key], "Release changed canonical " + key)
	check(not released.attached and released.target_port == 1 and not view.capture_button.disabled and view.release_button.disabled, "Release lost approach target or physical gate")
	for n in 120:
		check(owner.advance_freedom_flight(1.0 / 120.0, commands, false), "Released normal flight controls refused")
	var final: Dictionary = owner.get_freedom_flight_state()
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[2], "Native lifecycle trace differs from independent C++ save bytes")
	check(final.tick == "265" and not final.attached and final.negative_force_body[2] > 0.0, "Released actuator/time state is absent")
	var other: Variant = ClassDB.instantiate("FreedomBridge")
	check(other.initialize_freedom_continue(args[0]) and other.capture_freedom_port(), "Cadence lifecycle start refused")
	for n in 60:
		check(other.advance_freedom_flight(1.0 / 60.0, neutral, false), "Cadence constrained advance refused")
	check(other.release_freedom_port(), "Cadence release refused")
	for n in 60:
		check(other.advance_freedom_flight(1.0 / 60.0, commands, false), "Cadence free advance refused")
	check(other.get_freedom_flight_state() == final, "Presentation cadence changed lifecycle state")
	# The first actual departure tick uses only negative-Y applied propulsion.
	check(other.initialize_freedom_continue(args[1]) and other.release_freedom_port(), "First-withdrawal attached fixture refused")
	commands.fill(0.0)
	commands[4] = 0.25
	check(other.advance_freedom_flight(1.0 / 120.0, commands, false), "First real withdrawal tick refused")
	var withdrawal: Dictionary = other.get_freedom_flight_state()
	check(not withdrawal.attached and withdrawal.negative_force_body[1] > 0.0 and withdrawal.negative_force_body[2] == 0.0, "Withdrawal tick did not preserve independent gross channels")
	check(view.exhaust.update_applied(withdrawal, 1.0 / 120.0, false) and is_equal_approx(view.exhaust.withdrawal_intensity, 0.25) and view.exhaust.withdrawal_plumes[0].visible and view.exhaust.withdrawal_plumes[1].visible and view.exhaust.intensity == 0.0, "First withdrawal tick left the source-bound exhaust dark or lit main thrust")
	# Ordinary Rise/withdrawal braking uses the same real positive-Y actuator.
	check(other.set_freedom_assistance(false), "Manual Rise comparison refused")
	commands.fill(0.0)
	commands[1] = 0.25
	check(other.advance_freedom_flight(1.0 / 120.0, commands, false), "Real Rise/braking tick refused")
	var lift: Dictionary = other.get_freedom_flight_state()
	check(lift.positive_force_body[1] > 0.0 and lift.negative_force_body[1] == 0.0, "Rise did not preserve independent positive-Y force")
	check(view.exhaust.update_applied(lift, 1.0 / 120.0, false) and is_equal_approx(view.exhaust.lift_intensity, 0.25) and view.exhaust.lift_plumes[0].visible and view.exhaust.lift_plumes[1].visible and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.intensity == 0.0, "Real Rise/braking did not light only the existing lower outlets")
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == originals[i], "Lifecycle modified source save")
	view.free()
	print("Native port lifecycle: %d failures" % failures)
	quit(0 if failures == 0 else 1)
