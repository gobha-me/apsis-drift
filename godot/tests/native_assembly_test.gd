extends SceneTree
const Hopper = preload("res://scripts/characters/hopper_presentation.gd")
const Operating = preload("res://scripts/ships/wayfarer_operating_view.gd")
const Exhaust = preload("res://scripts/native/native_main_exhaust.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
var failures := 0
func digest(raw: PackedByteArray) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(raw)
	return context.finish().hex_encode()
func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1
func _initialize() -> void:
	call_deferred("run")
func triangle() -> ArrayMesh:
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK, Vector3.BACK, Vector3.BACK])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh
func replacement_fixture() -> Node3D:
	var model := Node3D.new()
	for name in Hopper.STOWED_ROOTS:
		var node := MeshInstance3D.new()
		node.name = name
		node.mesh = triangle()
		node.set_meta("extras", {"source_member_count": 56 if name == "WFStowedResidual" else 1, "static_stow_prototype_not_admitted": true, "source_sha256": Operating.SOURCE_HASH})
		model.add_child(node)
	return model
func legacy_binding() -> Dictionary:
	# Explicit synthetic legacy binding exercises the common asset consumer,
	# independently of the C++ session tests; it grants no session authority.
	return {"profile": "legacy-original", "hardware_known": false, "operating_model_sha256": Exhaust.MODEL_SHA256, "stowed_model_sha256": "", "frame_sha256": "", "contact_sha256": "", "craft_world_deltas": [], "replacement_world_delta": Transform3D.IDENTITY, "operating_atlas_surface": 18}

func check_surface_boundaries() -> void:
	var valid := triangle().surface_get_arrays(0)
	check(Operating.surface_counts(valid) == Vector2i(3, 1), "Tiny triangle surface refused")
	var bad := valid.duplicate(true)
	bad[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(NAN, 0, 0), Vector3.RIGHT, Vector3.UP])
	check(Operating.surface_counts(bad).x < 0, "Nonfinite mesh vertex accepted")
	bad = valid.duplicate(true)
	bad[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 3])
	check(Operating.surface_counts(bad).x < 0, "Out-of-bound triangle index accepted")
	bad[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1])
	check(Operating.surface_counts(bad).x < 0, "Incomplete triangle accepted")
	bad = valid.duplicate(true)
	bad[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK])
	check(Operating.surface_counts(bad).x < 0, "Wrong normal dimensions accepted")

func check_legacy_assets(assets: String) -> void:
	var selected := legacy_binding()
	var legacy := Hopper.load_selected_asset(assets, selected)
	check(legacy != null, "Fixed legacy asset consumer refused")
	if legacy != null:
		check(legacy.valid_installed() and legacy.exterior_surface == 18 and legacy.replacement_nodes.is_empty(), "Legacy silently selected stowed assembly")
		var exhaust := Exhaust.new()
		check(exhaust.bind_skin(legacy, 18) and exhaust.valid_bound_skin(), "Legacy atlas18 exhaust refused")
		exhaust.free()
		legacy.free()
	var temporary := ProjectSettings.globalize_path("user://legacy-substitute-%d" % Time.get_ticks_usec())
	check(DirAccess.make_dir_recursive_absolute(temporary) == OK, "Legacy substitute fixture directory unavailable")
	var raw := "fabricated model matching descriptor".to_utf8_buffer()
	var descriptor: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(assets.path_join(Hopper.CONTRACT)))
	descriptor.model_sha256 = digest(raw)
	var file := FileAccess.open(temporary.path_join(Hopper.CONTRACT), FileAccess.WRITE)
	file.store_string(JSON.stringify(descriptor)); file.close()
	file = FileAccess.open(temporary.path_join("hopper-wayfarer-01.glb"), FileAccess.WRITE)
	file.store_buffer(raw); file.close()
	check(Hopper.load_selected_asset(temporary, selected) == null, "Self-consistent foreign legacy descriptor/model bypassed independent source pins")
	DirAccess.remove_absolute(temporary.path_join(Hopper.CONTRACT))
	DirAccess.remove_absolute(temporary.path_join("hopper-wayfarer-01.glb"))
	DirAccess.remove_absolute(temporary)

func run() -> void:
	check_surface_boundaries()
	# Low-level invented nodes test binding boundaries before real imports.
	var fixture := replacement_fixture()
	check(Hopper.valid_replacement(fixture), "Complete tiny14-root fixture refused")
	var last: Node3D = fixture.get_child(13)
	last.position.x = 1
	check(not Hopper.valid_replacement(fixture), "Nonidentity final root accepted")
	last.position = Vector3.ZERO
	var name := last.name
	last.name = "foreign"
	check(not Hopper.valid_replacement(fixture), "Missing last root accepted")
	last.name = name
	fixture.get_child(0).name = "WFStowedResidual"
	check(not Hopper.valid_replacement(fixture), "Duplicate root name accepted")
	fixture.get_child(0).name = "WFStowed0"
	last.transform = Transform3D(Basis.IDENTITY, Vector3(INF, 0, 0))
	check(not Hopper.valid_replacement(fixture), "Nonfinite root transform accepted")
	last.transform = Transform3D.IDENTITY
	last.set_meta("extras", {"source_member_count": 55, "static_stow_prototype_not_admitted": true, "source_sha256": Operating.SOURCE_HASH})
	check(not Hopper.valid_replacement(fixture), "Wrong residual membership accepted")
	fixture.free()
	for bad in [null, {}, {"profile": "wayfarer-stowed-01"}, {"profile": "legacy-original"}]:
		check(not Hopper.valid_binding(bad), "Malformed selected binding accepted")
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		push_error("native_assembly needs prepared starter root with operating/stowed children")
		quit(1)
		return
	check_legacy_assets(args[0])
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		check(false, "Native C++ bridge unavailable")
		quit(1)
		return
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.stage_freedom_new_game("42"), "C++ pending starter refused")
	var pending: Dictionary = bridge.get_pending_freedom_start()
	if pending.is_empty(): quit(1); return
	var binding: Dictionary = pending.craft_binding
	check(Hopper.valid_binding(binding), "C++ selected binding refused")
	for field in ["profile", "frame_sha256", "contact_sha256", "stowed_model_sha256", "hardware_known", "operating_atlas_surface"]:
		var changed := binding.duplicate(true)
		changed[field] = null
		check(not Hopper.valid_binding(changed), "Malformed binding accepted: " + field)
	var changed := binding.duplicate(true)
	changed.craft_world_deltas[12] = Transform3D(Basis.IDENTITY, Vector3(NAN, 0, 0))
	check(not Hopper.valid_binding(changed), "Nonfinite final C++ delta accepted")
	changed = binding.duplicate(true)
	changed.replacement_world_delta = Transform3D.IDENTITY
	check(not Hopper.valid_binding(changed), "Wrong replacement delta accepted")
	var model := Hopper.load_selected_asset(args[0], binding)
	check(model != null, "Actual complete selected Wayfarer load failed")
	if model == null: quit(1); return
	check(model.valid_installed() and model.replacement_nodes.size() == 14 and model.exterior_surface == 14, "Selected14-root binding incomplete")
	var nodes := Operating.unique_nodes(model.get_child(0))
	check(not nodes.has("WFOpSeatLift"), "Old complete seat lift survived")
	for node in model.replacement_nodes:
		check(node.get_parent() == model.get_child(0) and node.transform == binding.replacement_world_delta, "Replacement failed flat once-delta")
	check(model.gear_nodes.size() == 30 and model.specification.screens.size() == 3 and model.specification.gear_preview_samples == 21, "Eye/screens/gear calibration incomplete")
	check(model.specification.pilot_eye == [0.0, 1.3650000095367432, -2.490000009536743], "Calibrated pilot eye changed")
	var skin: MeshInstance3D = nodes.HopperStructure
	var arrays := skin.mesh.surface_get_arrays(14)
	check(digest(arrays[Mesh.ARRAY_VERTEX].to_byte_array()) == "19a2e4799f007e26bf0de87b2e0472b3416e3ce622d40e1c2a0e468e3f1f0ca2", "Atlas source POSITION changed")
	check(digest(arrays[Mesh.ARRAY_INDEX].to_byte_array()) == "53d2c00fb625e9de091092a75f0470ffdf068782eb8af9f9f6bdec4511599465", "Atlas imported winding changed")
	check((skin.mesh.surface_get_format(14) & Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES) == 0, "Atlas was lossy packed")
	var exhaust := Exhaust.new()
	check(exhaust.bind_skin(model, model.exterior_surface), "Selected atlas14 exhaust refused")
	check(model.valid_installed() and exhaust.valid_bound_skin(), "Aperture override changed source material")
	var coating: Material = skin.get_surface_override_material(14)
	skin.set_surface_override_material(14, null)
	check(not exhaust.valid_bound_skin(), "Removed exhaust coating accepted")
	skin.set_surface_override_material(14, coating)
	var aperture: Material = coating.next_pass
	coating.next_pass = ShaderMaterial.new()
	check(not exhaust.valid_bound_skin(), "Late aperture substitution accepted")
	coating.next_pass = aperture
	check(exhaust.valid_bound_skin(), "Restored exhaust binding refused")
	var original_material: Material = skin.mesh.surface_get_material(14)
	skin.mesh.surface_set_material(14, StandardMaterial3D.new())
	check(not model.valid_installed(), "Late atlas replacement accepted")
	skin.mesh.surface_set_material(14, original_material)
	var pose: Transform3D = model.replacement_nodes[13].transform
	model.replacement_nodes[13].transform = Transform3D.IDENTITY
	check(not model.valid_installed(), "Late last-root delta accepted")
	model.replacement_nodes[13].transform = pose
	check(model.valid_installed(), "Restored complete candidate refused")
	exhaust.free()
	skin.set_surface_override_material(14, null)
	var flight := FlightView.new()
	check(flight.stage(bridge, args[0], pending, model), "Genuine pending flight projection staging refused")
	check(flight.ready_to_commit(), "Complete detached flight/exhaust refused")
	var flight_coating: Material = skin.get_surface_override_material(14)
	skin.set_surface_override_material(14, null)
	check(not flight.ready_to_commit(), "Flight accepted late exhaust override removal")
	skin.set_surface_override_material(14, flight_coating)
	check(flight.ready_to_commit(), "Flight refused restored exhaust")
	flight.free()
	bridge.discard_pending_freedom_start(pending.candidate_id)
	print("Native selected assembly checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)
