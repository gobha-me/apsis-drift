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


func check_exhaust_inputs(state: Dictionary) -> void:
	# Validate source ownership and every gross buffer before importing a model.
	check(MainExhaust.valid_applied(state, 0.0), "Actual selected force state refused")
	for key in ["positive_force_body", "negative_force_body", "positive_force_ratings", "negative_force_ratings"]:
		for size in [0, 2, 4, 1000]:
			var malformed := state.duplicate(true)
			var buffer := PackedFloat64Array()
			buffer.resize(size)
			malformed[key] = buffer
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong buffer dimensions: " + key)
		for bad in [NAN, INF, -INF, -1.0]:
			for axis in 3:
				var malformed := state.duplicate(true)
				malformed[key][axis] = bad
				check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted invalid component: " + key)
		for bad_type in [null, [], Vector3.ZERO, PackedFloat32Array([1.0, 1.0, 1.0])]:
			var malformed := state.duplicate(true)
			malformed[key] = bad_type
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong buffer type: " + key)
	for key in ["positive_force_ratings", "negative_force_ratings"]:
		for axis in 3:
			var malformed := state.duplicate(true)
			malformed[key][axis] = 0.0
			check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted zero force rating")
	for key in ["mode", "frame_id", "attached"]:
		var malformed := state.duplicate(true)
		malformed.erase(key)
		check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted missing owner identity: " + key)
	for bad_elapsed in [NAN, INF, -INF, -0.01, 60.01]:
		check(not MainExhaust.valid_applied(state, bad_elapsed), "Exhaust accepted invalid elapsed time")
	for wrong_owner in [{"mode": "freedom_station"}, {"frame_id": "1"}, {"frame_id": 2}, {"attached": 0}]:
		var malformed := state.duplicate(true)
		malformed.merge(wrong_owner, true)
		check(not MainExhaust.valid_applied(malformed, 0.0), "Exhaust accepted wrong physical owner")


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
	check_exhaust_inputs(start)
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
	check(view.exhaust.plumes.size() == 2 and view.exhaust.withdrawal_plumes.size() == 2 and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0, "Neutral Continue displayed thrust or lost source outlets")
	var structural_skin: MeshInstance3D = view.ship.find_child("HopperStructure", true, false)
	check(structural_skin != null and structural_skin.transform == Transform3D.IDENTITY, "Qualified skin lost its body-local identity")
	var skin_arrays: Array = structural_skin.mesh.surface_get_arrays(MainExhaust.SKIN_SURFACE)
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(skin_arrays[Mesh.ARRAY_VERTEX].to_byte_array())
	check(hash.finish().hex_encode() == "19a2e4799f007e26bf0de87b2e0472b3416e3ce622d40e1c2a0e468e3f1f0ca2", "Imported skin positions differ from the qualified source")
	check(skin_arrays[Mesh.ARRAY_INDEX].size() == 631083, "Imported skin lost retained source triangles")
	hash.start(HashingContext.HASH_SHA256)
	hash.update(skin_arrays[Mesh.ARRAY_INDEX].to_byte_array())
	# Godot reverses each source triangle's winding for its clockwise convention.
	check(hash.finish().hex_encode() == "53d2c00fb625e9de091092a75f0470ffdf068782eb8af9f9f6bdec4511599465", "Imported skin topology differs from the qualified source")
	var coating: StandardMaterial3D = structural_skin.get_surface_override_material(MainExhaust.SKIN_SURFACE)
	check(coating != null and coating.next_pass is ShaderMaterial, "Source-conformed aperture pass is unavailable")
	var emitted_meshes := []
	for i in 2:
		var plume: MeshInstance3D = view.exhaust.withdrawal_plumes[i]
		check(plume.basis == Basis(Vector3.RIGHT, Vector3.DOWN, Vector3.FORWARD), "Withdrawal plume acquired a trigonometric axis tilt")
		var vertices: PackedVector3Array = plume.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
		var recorded_vertices := []
		for vertex in vertices:
			check(vertex.is_finite() and float(vertex.x) * float(vertex.x) + float(vertex.z) * float(vertex.z) <= MainExhaust.WITHDRAWAL_RADIUS * MainExhaust.WITHDRAWAL_RADIUS, "Generated plume vertex exceeded the qualified radial envelope")
			check(float(plume.position.y) - float(vertex.y) >= MainExhaust.WITHDRAWAL_ANCHORS[i].y, "Generated plume base rounded inward")
			recorded_vertices.append([vertex.x, vertex.y, vertex.z])
		emitted_meshes.append({"position": [plume.position.x, plume.position.y, plume.position.z], "vertices": recorded_vertices})
	var mesh_record := FileAccess.open(args[0].get_base_dir().path_join("native-withdrawal-mesh.json"), FileAccess.WRITE)
	check(mesh_record != null, "Generated plume numeric record could not be saved")
	if mesh_record != null:
		mesh_record.store_string(JSON.stringify(emitted_meshes, "\t", true, true))
		mesh_record.close()
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
	check(is_equal_approx(view.exhaust.withdrawal_intensity, 0.1) and view.exhaust.withdrawal_plumes[0].visible, "Withdrawal exhaust ignored independent C++ gross negative-Y force")
	var phase: float = view.exhaust.phase
	check(view.exhaust.update_applied(final, 0.1, false) and view.exhaust.phase > phase and view.exhaust.plumes[0].visible, "Applied plume did not animate")
	phase = view.exhaust.phase
	check(view.exhaust.update_applied(final, 60.0, true) and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.phase == phase, "Paused exhaust changed phase or kept firing")
	var invalid_force := final.duplicate(true)
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false) and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible, "Truncated force buffer lit a plume")
	invalid_force.negative_force_body = PackedFloat64Array([0.0, 0.0, NAN])
	check(not view.exhaust.update_applied(invalid_force, 0.1, false), "Nonfinite force lit a plume")
	phase = view.exhaust.phase
	var attached_force := final.duplicate(true)
	attached_force.attached = true
	check(view.exhaust.update_applied(attached_force, 0.1, false) and not view.exhaust.plumes[0].visible and not view.exhaust.withdrawal_plumes[0].visible and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.phase == phase, "Attached craft displayed firing or advanced exhaust phase")
	check(not view.exhaust.update_applied({}, 0.1, false) and not view.exhaust.plumes[0].visible and view.exhaust.phase == phase, "Missing owner retained firing or changed exhaust phase")
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
	check(view.exhaust.withdrawal_intensity == 0.0 and not view.exhaust.withdrawal_plumes[0].visible, "Main firing lit the vertical outlets")
	check(other.initialize_freedom_continue(args[0]), "Vertical opposition fixture could not Continue")
	opposed.fill(0.0)
	opposed[1] = 0.25
	opposed[4] = 0.25 * start.positive_force_ratings[1] / start.negative_force_ratings[1]
	check(other.advance_freedom_flight(1.0 / 120.0, opposed, false), "Physical opposing vertical firings refused")
	gross = other.get_freedom_flight_state()
	check(absf(gross.applied_force_body[1]) < 0.00001 and gross.negative_force_body[1] > 0.0, "Gross vertical opposition was erased")
	check(view.exhaust.update_applied(gross, 0.1, false) and view.exhaust.withdrawal_intensity > 0.0 and view.exhaust.withdrawal_plumes[0].visible and view.exhaust.intensity == 0.0, "Zero net vertical force hid the firing outlet or lit main thrust")
	check(other.advance_freedom_flight(1.0 / 120.0, neutral, false), "Neutral coast after opposition refused")
	check(view.exhaust.update_applied(other.get_freedom_flight_state(), 0.1, false) and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0, "Coasting velocity or gravity was presented as exhaust")
	check(other.initialize_freedom_continue(output) and other.get_freedom_flight_state().checksum == final.checksum, "Flight Save As did not resume exact state")
	for path in ["relative.json", "", args[0].get_base_dir(), args[0].get_base_dir() + "/missing/save.json"]:
		check(not owner.save_freedom_as(path) and owner.get_freedom_flight_state() == final, "Refused Save As changed selected flight")
	check(other.initialize_freedom_continue(args[3]) and other.get_freedom_flight_state().is_empty(), "Station selection retained stale saved flight")
	view.free()
	for i in 4:
		check(FileAccess.get_file_as_bytes(args[i]) == original[i], "Native flight modified a source fixture")
	print("Saved native flight: %d failures; actual C++ controls and save trace, no GPU qualification" % failures)
	quit(0 if failures == 0 else 1)
