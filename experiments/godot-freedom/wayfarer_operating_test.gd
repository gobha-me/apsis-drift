extends SceneTree
## Explicit operating-model contract. No player boarding or C++ world mutation.
const Operating = preload("res://wayfarer_operating_view.gd")
var failures := 0

func commit_fixture_new_game(owner: Variant, seed: String) -> bool:
	if not owner.stage_freedom_new_game(seed): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

func check(condition: bool, message: String) -> void:
	if not condition:
		push_error(message)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func snapshots(nodes: Dictionary) -> Dictionary:
	var result := {}
	for id in nodes:
		result[id] = nodes[id].transform
	return result

func check_edited_receipt_rejected(directory: String) -> void:
	# Never modify prepared/source assets. Even a self-consistent edited receipt
	# must refuse before parsing or importing the substituted asset.
	var receipt: Dictionary = Operating.bounded_json(directory.path_join("prepared.json"))
	var temporary := ProjectSettings.globalize_path("user://operating-identity-negative-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()])
	if DirAccess.make_dir_recursive_absolute(temporary) != OK:
		check(false, "Could not create isolated identity control directory")
		return
	var copied := true
	for name in receipt.files:
		if DirAccess.copy_absolute(directory.path_join(name), temporary.path_join(name)) != OK:
			copied = false
	if copied:
		var output := FileAccess.open(temporary.path_join("prepared.json"), FileAccess.WRITE)
		output.store_string(JSON.stringify(receipt))
		output.close()
		check(Operating.valid_prepared(temporary), "Exact isolated identity fixture refused")
		for name in Operating.SELECTED_SHA256:
			var path: String = temporary.path_join(name)
			var edited := FileAccess.open(path, FileAccess.READ_WRITE)
			edited.seek_end()
			edited.store_8(32)
			edited.close()
			var changed := receipt.duplicate(true)
			changed.files[name] = FileAccess.get_sha256(path)
			output = FileAccess.open(temporary.path_join("prepared.json"), FileAccess.WRITE)
			output.store_string(JSON.stringify(changed))
			output.close()
			check(changed.files[name] != receipt.files[name] and FileAccess.get_sha256(path) == changed.files[name] and not Operating.valid_prepared(temporary), "Edited asset plus matching receipt accepted: " + name)
			check(DirAccess.copy_absolute(directory.path_join(name), path) == OK, "Could not restore isolated identity control")
	else:
		check(false, "Could not copy isolated identity control assets")
	for name in receipt.files.keys() + ["prepared.json"]:
		var path: String = temporary.path_join(name)
		if FileAccess.file_exists(path):
			check(DirAccess.remove_absolute(path) == OK, "Could not remove isolated identity control file")
	check(DirAccess.remove_absolute(temporary) == OK, "Could not remove isolated identity control directory")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path():
		push_error("Expected prepared starter and operating directories")
		quit(1)
		return
	check(not Operating.integral(NAN, -1, 2) and not Operating.integral(INF, -1, 2) and not Operating.integral(0.5, -1, 2) and not Operating.integral("1", -1, 2) and Operating.integral(1.0, -1, 2), "Numeric metadata conversion is not finite integral and bounded")
	check(Operating.metadata_matches(1.0, 1) and not Operating.metadata_matches(1.0000001, 1) and not Operating.metadata_matches(INF, 1), "Float-imported identity metadata accepted fractional or nonfinite values")
	check(Operating.canonical_uint64("0") and Operating.canonical_uint64("18446744073709551615") and not Operating.canonical_uint64("18446744073709551616") and not Operating.canonical_uint64("01") and not Operating.canonical_uint64("-1"), "Unsigned identity conversion accepted an overflow or noncanonical decimal")
	check(not Operating.valid_transform([[1,0,0],[0,1,0],[0,0,1],[NAN,0,0]]) and not Operating.valid_transform([[0,0,0],[0,0,0],[0,0,0],[0,0,0]]), "Invalid transformation accepted")
	check(not Operating.valid_glb_container(args[1].path_join("prepared.json")) and not Operating.valid_glb_container(args[1].path_join("missing.glb")), "Invalid GLB magic/path accepted")
	var duplicate := Node3D.new()
	var first := Node3D.new()
	var second := Node3D.new()
	first.name = "Duplicate"
	second.name = "Duplicate"
	var branch := Node3D.new()
	branch.name = "Branch"
	duplicate.add_child(first)
	duplicate.add_child(branch)
	branch.add_child(second)
	check(Operating.unique_nodes(duplicate).is_empty(), "Duplicate cross-parent runtime name accepted")
	duplicate.free()
	var empty_mesh := MeshInstance3D.new()
	check(not Operating.valid_mesh_buffers(empty_mesh), "Missing mesh accepted before visualization")
	empty_mesh.free()
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2])
	check(Operating.surface_counts(arrays) == Vector2i(3, 1), "Valid independent triangle buffer refused")
	for indices in [PackedInt32Array([-1,1,2]), PackedInt32Array([0,1,3]), PackedInt32Array([0,1])]:
		arrays[Mesh.ARRAY_INDEX] = indices
		check(Operating.surface_counts(arrays).x < 0, "Negative/out-of-range/truncated index buffer accepted")
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0,1,2])
	arrays[Mesh.ARRAY_VERTEX][0] = Vector3(NAN, 0, 0)
	check(Operating.surface_counts(arrays).x < 0, "Nonfinite vertex accepted")
	arrays[Mesh.ARRAY_VERTEX][0] = Vector3.ZERO
	arrays[Mesh.ARRAY_TEX_UV] = PackedVector2Array([Vector2.ZERO, Vector2.ZERO])
	check(Operating.surface_counts(arrays).x < 0, "UV buffer count mismatch accepted")
	var spec: Variant = Operating.bounded_json(args[1].path_join(Operating.MANIFEST))
	var closure: Variant = Operating.bounded_json(args[1].path_join("station-closure.json"))
	if not Operating.valid_spec(spec) or not Operating.valid_closure(closure) or not Operating.valid_prepared(args[1]):
		push_error("Prepared operating package failed contract qualification")
		quit(1)
		return
	check_edited_receipt_rejected(args[1])
	for mutation in 19:
		var bad: Dictionary = spec.duplicate(true)
		match mutation:
			0: bad["extra"] = true
			1: bad.source.sha256 = "0".repeat(64)
			2: bad.groups[1].id = bad.groups[0].id
			3: bad.groups[0].runtime_node += "_guess"
			4: bad.groups[0].source_rig = "Unknown rig"
			5: bad.groups[0].runtime_rest_transform[3][0] = 1.0
			6: bad.groups[0].source_objects.append(bad.groups[1].source_objects[0])
			7: bad.poses.rest["ladder_upper"][3][0] = INF
			8: bad.channels[0].samples[1].progress = 0.0
			9: bad.channels[0].samples[0].transforms.erase("seat_entry_arm")
			10: bad.model.file = "../unexpected.glb"
			11: bad.anchors.pilot_eye[1] = 9.0
			12: bad.screens[0]["size"][0] = 0.0
			13: bad.roster.included[1].source_object = bad.roster.included[0].source_object
			14: bad.gear_preview["HopperGear00"][0][3][0] = NAN
			15: bad.derivative_corrections.operations[0].parameters.inward_metres = 0.1
			16: bad.derivative_corrections.operations[0].source_objects[0].local_translation_metres[0] = INF
			17: bad.derivative_corrections.operations[0].source_objects[0].operating_rest_local_transform[3][0] += 1.0
			18: bad.exporter.path = "../unexpected.py"
		check(not Operating.valid_spec(bad), "Malformed operating manifest accepted: " + str(mutation))
	for mutation in 8:
		var bad: Dictionary = closure.duplicate(true)
		match mutation:
			0: bad.schema_version = 1.1
			1: bad.closure_source_sha256 = "0".repeat(64)
			2: bad.bindings[1].runtime_node = bad.bindings[0].runtime_node
			3: bad.bindings[0].runtime_node = bad.bindings[0].source_object
			4: bad.bindings[0].knots[1].progress = bad.bindings[0].knots[0].progress
			5: bad.bindings[0].knots[0].transform[3][0] = NAN
			6: bad.attached_closed_progress = 1.0
			7: bad.bindings[0].expected_extras = {}
		check(not Operating.valid_closure(bad), "Malformed station closure accepted: " + str(mutation))
	for mutation in 11:
		var bad: Array = closure.corrections.duplicate(true)
		match mutation:
			0: bad[1].id = bad[0].id
			1: bad[0].filename = "../other.glb"
			2: bad[0].model_sha256 = "bad hash"
			3: bad[0].replacement_points_metres[0][0] += 1.0
			4: bad[1].bore_centre_metres[0] += 0.1
			5: bad[1].bore_depth_interval_metres[0] = NAN
			6: bad[2].surface_count = 1.5
			7: bad[2]["extra"] = true
			8: bad[2].expected_extras = {}
			9: bad[1].runtime_parent = "Unqualified parent"
			10: bad[0].rest_transform[3][0] = 1.0
		check(not Operating.valid_station_corrections(bad), "Malformed station mesh replacement accepted: " + str(mutation))
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not commit_fixture_new_game(owner, "42"):
		push_error(str(owner.get_last_error()))
		quit(1)
		return
	var before: Dictionary = owner.get_freedom_flight_state()
	check(Operating.valid_owner_state(before), "Real C++ attached D1 world rejected")
	for field in ["station_position", "body_basis", "target_port", "attached", "tick", "station_id", "universe_seed"]:
		var bad: Dictionary = before.duplicate(true)
		match field:
			"station_position": bad[field] = Vector3(NAN,0,0)
			"body_basis": bad[field] = Basis(Vector3.ZERO,Vector3.ZERO,Vector3.ZERO)
			"target_port": bad[field] = 1.5
			"attached": bad[field] = 1
			"tick": bad[field] = "01"
			"station_id": bad[field] = "0"
			"universe_seed": bad[field] = "18446744073709551616"
		check(not Operating.valid_owner_state(bad), "Malformed world/frame state accepted: " + field)

	var actor: Dictionary = owner.get_freedom_walk_state()
	var ports: Dictionary = owner.get_freedom_station_geometry()
	var view := Operating.new()
	root.add_child(view)
	if not view.initialize(owner, args[0], args[1]):
		push_error("Actual operating model import failed: " + view.error)
		view.free()
		quit(1)
		return
	check(view.station.port_markers.is_empty() and view.station.get_child_count() == 1 and view.station.get_child(0).name == "OriginStationAsset", "Operating inspection retained generated debug overlays or removed its imported station")
	check(view.station.geometry == ports and owner.get_freedom_station_geometry() == ports, "Inspection helper cleanup changed authoritative C++ ports")
	check(view.moving_nodes.size() == 13 and view.closure_nodes.size() == 17 and view.station.camera == null, "Missing actual hardware groups or old inspection camera")
	var station_nodes := Operating.unique_nodes(view.station.get_child(0))
	var mesh_snapshot := {}
	for correction in closure.corrections:
		var node: MeshInstance3D = station_nodes[correction.runtime_node]
		mesh_snapshot[correction.id] = {"node": node, "mesh": node.mesh, "material": node.mesh.surface_get_material(0), "transform": node.transform, "parent": node.get_parent(), "extras": node.get_meta("extras", {}).duplicate(true), "visible": node.visible}
	var late_invalid: Array = closure.corrections.duplicate(true)
	late_invalid[2].runtime_parent = "ChangedParent"
	check(not view.apply_station_correction(view.station.get_child(0), args[1], late_invalid), "Late invalid station mesh binding accepted")
	for snapshot in mesh_snapshot.values():
		check(snapshot.node.mesh == snapshot.mesh, "Late station binding failure partially installed an earlier replacement")
	check(view.apply_station_correction(view.station.get_child(0), args[1], closure.corrections), "Exact station mesh replacement refused")
	for snapshot in mesh_snapshot.values():
		var node: MeshInstance3D = snapshot.node
		check(node.mesh != snapshot.mesh and node.mesh.surface_get_material(0) == snapshot.material and node.transform == snapshot.transform and node.get_parent() == snapshot.parent and node.get_meta("extras", {}) == snapshot.extras and node.visible == snapshot.visible, "Replacement changed station node/material/frame identity")
	check(Operating.unique_nodes(view.station.get_child(0)).size() == station_nodes.size(), "Station replacements added duplicate visible geometry")

	check(view.ship.transform == Transform3D(before.body_basis, Vector3.ZERO) and view.station.transform == Transform3D(before.station_basis, before.station_position), "Read-only operating preview changed C++ registration")
	for name in Operating.POSES:
		check(view.set_pose(name), "Qualified operating pose refused: " + name)
		for id in Operating.GROUP_NODES:
			check(view.moving_nodes[id].transform.is_equal_approx(Operating.pose(spec.poses[name][id])), "World delta not applied exactly once: " + name + "/" + id)
	for channel in spec.channels:
		for sample in channel.samples:
			check(view.set_channel(channel.id, sample.progress), "Exact qualified channel sample refused")
			for id in Operating.GROUP_NODES:
				check(view.moving_nodes[id].transform.is_equal_approx(Operating.pose(sample.transforms[id])), "Qualified channel sample changed its complete world delta")
	check(view.set_pose("rest"), "Rest reset refused")
	var rest := snapshots(view.moving_nodes)
	var selected := view.selected_pose
	check(not view.set_pose("unknown") and not view.set_channel("unknown", 0.5) and snapshots(view.moving_nodes) == rest and view.selected_pose == selected, "Unknown pose/channel partially changed model")
	for name in Operating.CHANNELS:
		for progress in [0.425, 0.675, NAN, INF, -0.05, 1.01]:
			check(not view.set_channel(name, progress) and snapshots(view.moving_nodes) == rest and view.selected_pose == selected, "Non-grid, nonfinite or out-of-range channel progress changed state")
	var missing_channel: Dictionary = view.specification.channels[0]
	var qualified_samples: Array = missing_channel.samples
	var missing_samples := qualified_samples.duplicate(true)
	missing_samples.remove_at(8)
	missing_channel.samples = missing_samples
	check(not view.set_channel(missing_channel.id, 0.4) and snapshots(view.moving_nodes) == rest and view.selected_pose == selected, "Missing admitted sample was approximated or changed state")
	missing_channel.samples = qualified_samples
	var malformed: Dictionary = spec.poses.rest.duplicate(true)
	malformed["seat_lock"][3][0] = NAN
	check(not view.apply_poses(malformed) and snapshots(view.moving_nodes) == rest, "Late invalid world delta partially changed sibling nodes")
	for progress in [0.0, 0.2, 0.5, closure.attached_closed_progress]:
		check(view.set_station_progress(progress), "Qualified D1 closure progress refused")
	var closed := snapshots(view.closure_nodes)
	check(not view.set_station_progress(NAN) and not view.set_station_progress(1.0) and snapshots(view.closure_nodes) == closed, "Attached closure bypassed release interlock or changed nodes on refusal")
	check(view.set_station_progress(1.0, true), "Explicit release-hardware inspection refused")
	check(view.set_station_progress(0.0), "D1 open reset refused")
	var binding: Dictionary = closure.bindings[0]
	var bound: Node3D = view.closure_nodes[binding.id]
	var old_extras: Dictionary = bound.get_meta("extras").duplicate(true)
	var wrong: Dictionary = old_extras.duplicate(true)
	var key: String = binding.expected_extras.keys()[0]
	wrong[key] = "wrong identity"
	bound.set_meta("extras", wrong)
	check(not view.bind_station_model(view.station.get_child(0), closure), "Changed D1 runtime identity accepted")
	bound.set_meta("extras", old_extras)
	check(view.bind_station_model(view.station.get_child(0), closure), "Exact D1 identity restoration failed")
	for item in closure.bindings:
		for identity_key in item.expected_extras:
			if not (item.expected_extras[identity_key] is float or item.expected_extras[identity_key] is int):
				continue
			var node: Node3D = view.closure_nodes[item.id]
			var exact: Dictionary = node.get_meta("extras").duplicate(true)
			var fractional := exact.duplicate(true)
			fractional[identity_key] = float(item.expected_extras[identity_key]) + 0.0000001
			node.set_meta("extras", fractional)
			check(not view.bind_station_model(view.station.get_child(0), closure), "Fractional float-imported D1 identity accepted before integer conversion")
			node.set_meta("extras", exact)
			break
	var nested: Node3D = view.moving_nodes.seat_lift
	var flat_parent: Node = nested.get_parent()
	var flat_owner: Node = nested.owner
	nested.owner = null
	nested.reparent(view.moving_nodes.seat_carriage, false)
	check(not view.bind_operating_model(view.ship.get_child(0), spec), "Nested complete world-delta group accepted double transform")
	nested.reparent(flat_parent, false)
	nested.owner = flat_owner
	var wrapper := Node3D.new()
	wrapper.position.x = 1.0
	flat_parent.add_child(wrapper)
	var original_owners := {}
	for id in view.moving_nodes:
		var node: Node3D = view.moving_nodes[id]
		original_owners[id] = node.owner
		node.owner = null
		node.reparent(wrapper, false)
	check(not view.bind_operating_model(view.ship.get_child(0), spec), "Sibling world-delta groups accepted an extra transformed ancestor")
	for id in view.moving_nodes:
		var node: Node3D = view.moving_nodes[id]
		node.reparent(flat_parent, false)
		node.owner = original_owners[id]
	wrapper.free()
	var model: Node3D = view.ship.get_child(0)
	model.position.x = 1.0
	check(not view.bind_operating_model(model, spec), "Changed operating scene-root transform accepted")
	model.transform = Transform3D.IDENTITY
	var op: Node3D = view.moving_nodes.roof_port
	var op_rest := op.transform
	op.position.x = 1.0
	check(not view.bind_operating_model(view.ship.get_child(0), spec), "Changed operating rest transform accepted")
	op.transform = op_rest
	var op_name := op.name
	op.name = "MissingRoofPort"
	check(not view.bind_operating_model(view.ship.get_child(0), spec), "Missing operating group accepted")
	op.name = op_name
	var original: Dictionary = op.get_meta("extras").duplicate(true)
	op.set_meta("extras", {"operating_group": "roof_port", "source_rig": original.source_rig, "source_sha256": "0".repeat(64)})
	check(not view.bind_operating_model(view.ship.get_child(0), spec), "Changed operating source identity accepted")
	op.set_meta("extras", original)
	check(owner.get_freedom_flight_state() == before and owner.get_freedom_walk_state() == actor, "Mechanism inspection mutated authoritative actor, body or clock")
	view.free()
	print("Wayfarer operating read-only contract: %d failures" % failures)
	quit(0 if failures == 0 else 1)
