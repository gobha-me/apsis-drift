extends SceneTree
## Real C++ saved selections and main-thread mesh residency. Dummy/headless
## rendering is not visible-feature, GPU performance or controller acceptance.
const StreamView = preload("res://scripts/world/planet_stream.gd")

class PlacementBridge extends RefCounted:
	var owner: Variant
	var batch: Dictionary
	var transforms: Array
	func get_stream_identity() -> Dictionary: return owner.get_stream_identity()
	func poll_stream() -> Dictionary:
		var result := batch
		batch = {}
		return result
	func stream_transforms(_anchors: PackedFloat64Array) -> Array: return transforms
	func get_last_error() -> String: return "Declared placement refusal"

class MorphBridge extends RefCounted:
	var owner: Variant
	var batch: Dictionary
	var polls := 0
	var requests := 0
	func get_stream_identity() -> Dictionary: return owner.get_stream_identity()
	func poll_stream() -> Dictionary:
		polls += 1
		var result := batch
		batch = {}
		return result
	func request_stream(_observer: Vector3) -> bool:
		requests += 1
		return true
	func stream_transforms(positions: PackedFloat64Array) -> Array:
		var result: Array = []
		for _index in range(0, positions.size(), 3): result.append(Transform3D.IDENTITY)
		return result
	func get_last_error() -> String: return "Declared morph fixture refusal"

class EmptySurfaceStream extends StreamView:
	func _create_mesh(_arrays: Array) -> ArrayMesh: return ArrayMesh.new()

var failures := 0

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failures += 1
	return value

func _initialize() -> void:
	call_deferred("run")

func select_continue(owner: Variant, path: String) -> bool:
	if not check(owner.stage_freedom_continue(path), "Continue staging refused: " + str(owner.get_last_error())):
		return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Terrain selection commit refused")

func wait_ready(terrain: Node3D, expected: Dictionary) -> bool:
	var deadline := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline:
		terrain.tick(0.01, Vector3.ZERO)
		if not check(terrain.error.is_empty(), "Native terrain consumer refused: " + terrain.error):
			return false
		if terrain.is_ready:
			return check(terrain.identity == expected and terrain.nodes.size() >= 6 and terrain.nodes.size() <= 384 and terrain.pending.is_empty(), "Ready mesh cover differs from selected world/policy")
		await create_timer(0.005).timeout
	return check(false, "Selected terrain worker/mesh upload timeout")

func check_cover_replacements(accepted: Node3D, owner: Variant) -> void:
	# Reuse admitted C++ mesh buffers for successful and refused placement.
	var source_ids: Array = accepted.nodes.keys()
	var source_node: MeshInstance3D = accepted.nodes[source_ids[0]]
	var arrays: Array = source_node.mesh.surface_get_arrays(0)
	var incoming := {"id": "incoming", "anchor": accepted.anchors[source_ids[0]],
		"vertices": arrays[Mesh.ARRAY_VERTEX], "normals": arrays[Mesh.ARRAY_NORMAL],
		"colors": arrays[Mesh.ARRAY_COLOR], "uv": arrays[Mesh.ARRAY_TEX_UV],
		"indices": arrays[Mesh.ARRAY_INDEX]}
	for failure in ["valid", "short", "second-type", "second-basis", "second-origin", "empty-surface"]:
		var terrain: Node3D = EmptySurfaceStream.new() if failure == "empty-surface" else StreamView.new()
		root.add_child(terrain)
		var fake := PlacementBridge.new()
		fake.owner = owner
		fake.batch = {"identity": owner.get_stream_identity(), "generated": 1,
			"tiles": [{"id": "retained"}, incoming]}
		fake.transforms = [Transform3D(Basis.IDENTITY, Vector3(123, 456, 789)), Transform3D.IDENTITY, Transform3D.IDENTITY]
		match failure:
			"short": fake.transforms.pop_back()
			"second-type": fake.transforms[1] = "unsupported transform"
			"second-basis": fake.transforms[1] = Transform3D(Basis(Vector3(NAN, 0, 0), Vector3.UP, Vector3.BACK), Vector3.ZERO)
			"second-origin": fake.transforms[1] = Transform3D(Basis.IDENTITY, Vector3(0, INF, 0))
		terrain.bridge = fake
		terrain.identity = owner.get_stream_identity()
		for index in 2:
			var id := "retained" if index == 0 else "outgoing"
			var original: MeshInstance3D = accepted.nodes[source_ids[index]]
			var node := MeshInstance3D.new()
			node.mesh = original.mesh
			node.transform = original.transform
			terrain.add_child(node)
			terrain.nodes[id] = node
			terrain.anchors[id] = accepted.anchors[source_ids[index]]
		terrain.resident_ids = ["retained", "outgoing"]
		terrain.is_ready = true
		terrain.swaps = 3
		terrain.generated_total = 7
		terrain.retired_total = 2
		terrain.last_report = {"generated": 7, "accepted": true}
		var before: Array = [terrain.nodes.retained.transform, terrain.nodes.outgoing.transform]
		var outgoing: MeshInstance3D = terrain.nodes.outgoing
		terrain.tick(0.0, Vector3.ZERO)
		if failure == "valid":
			check(terrain.error.is_empty() and terrain.is_ready and terrain.resident_ids == ["retained", "incoming"] and terrain.pending.is_empty(), "Valid same-owner replacement did not commit")
			check(terrain.nodes.retained.visible and terrain.nodes.incoming.visible and terrain.nodes.retained.transform == fake.transforms[0] and terrain.nodes.incoming.transform == fake.transforms[2], "Committed replacement lost its complete placement")
			check(not terrain.nodes.has("outgoing") and not outgoing.visible and outgoing.is_queued_for_deletion() and outgoing.transform == fake.transforms[1], "Outgoing cover retired before complete placement")
			check(terrain.swaps == 4 and terrain.generated_total == 8 and terrain.retired_total == 3 and terrain.last_report == {"identity": owner.get_stream_identity(), "generated": 1}, "Successful replacement did not commit counters/report exactly once")
			var depth: Vector2 = terrain.depth_range(Transform3D.IDENTITY, 100000.0)
			check(depth != Vector2.ZERO, "Swapped actual mesh cover has no clip depth")
			for placed: MeshInstance3D in terrain.nodes.values():
				var actual_arrays: Array = placed.mesh.surface_get_arrays(0)
				for vertex: Vector3 in actual_arrays[Mesh.ARRAY_VERTEX]:
					check(-(placed.transform * vertex).z <= depth.y, "Same-tick replacement vertex escapes clip depth")
			terrain.free()
			continue
		check(not terrain.error.is_empty() and terrain.is_ready and terrain.resident_ids == ["retained", "outgoing"], "Refused cover changed accepted residency: " + failure)
		check(terrain.swaps == 3 and terrain.generated_total == 7 and terrain.retired_total == 2 and terrain.last_report == {"generated": 7, "accepted": true}, "Refused cover committed counters/report: " + failure)
		for index in 2:
			var id := "retained" if index == 0 else "outgoing"
			check(terrain.nodes.has(id) and terrain.nodes[id].visible and not terrain.nodes[id].is_queued_for_deletion() and terrain.nodes[id].transform == before[index], "Refused cover partially moved or retired accepted geometry: " + failure)
		check(not terrain.nodes.has("incoming") if failure == "empty-surface" else terrain.nodes.has("incoming") and not terrain.nodes.incoming.visible, "Refused incoming geometry was admitted or exposed: " + failure)
		var fault: String = terrain.error
		fake.transforms = [Transform3D.IDENTITY, Transform3D.IDENTITY, Transform3D.IDENTITY]
		terrain.tick(1.0, Vector3.ZERO)
		check(terrain.error == fault and terrain.swaps == 3 and terrain.nodes.retained.transform == before[0], "Placement fault resumed without explicit reset: " + failure)
		terrain.free()


# Lifecycle mock: admitted arrays are deliberately reused under declared tile
# keys, not claimed to be the real parent/child world's matching geometry.
# The C++ streaming contract separately checks the actual retained tile pair.
const MORPH_PARENT := "0:10:513:511"
const MORPH_CHILDREN := ["0:11:1026:1022", "0:11:1027:1022", "0:11:1026:1023", "0:11:1027:1023"]
const MORPH_COMMON := "1:1:0:0"

func morph_fixture(accepted: Node3D, owner: Variant, refining: bool) -> Dictionary:
	var source: MeshInstance3D = accepted.nodes.values()[0]
	var arrays: Array = source.mesh.surface_get_arrays(0)
	var positions := PackedFloat32Array()
	var normals := PackedFloat32Array()
	var colors := PackedFloat32Array()
	for index in arrays[Mesh.ARRAY_VERTEX].size():
		var vertex: Vector3 = arrays[Mesh.ARRAY_VERTEX][index]
		var normal: Vector3 = arrays[Mesh.ARRAY_NORMAL][index]
		var color: Color = arrays[Mesh.ARRAY_COLOR][index]
		positions.append_array(PackedFloat32Array([vertex.x, vertex.y, vertex.z - 100000.0, 0.0]))
		normals.append_array(PackedFloat32Array([normal.x, normal.y, normal.z, 0.0]))
		colors.append_array(PackedFloat32Array([color.r, color.g, color.b, 1.0]))
	var terrain: Node3D = StreamView.new()
	root.add_child(terrain)
	var fake := MorphBridge.new()
	fake.owner = owner
	terrain.bridge = fake
	terrain.identity = owner.get_stream_identity()
	var old_ids: Array = [MORPH_COMMON, MORPH_PARENT] if refining else [MORPH_COMMON] + MORPH_CHILDREN
	var final_ids: Array = [MORPH_COMMON] + MORPH_CHILDREN if refining else [MORPH_COMMON, MORPH_PARENT]
	for id in old_ids:
		var node := MeshInstance3D.new()
		node.mesh = source.mesh
		var material := ShaderMaterial.new()
		material.shader = load("res://shaders/terrain.gdshader")
		material.set_shader_parameter("morph_active", false)
		material.set_shader_parameter("morph_fine_weight", 1.0)
		node.material_override = material
		terrain.add_child(node)
		terrain.nodes[id] = node
		terrain.anchors[id] = PackedFloat64Array([0, 0, 0])
	terrain.resident_ids = old_ids.duplicate()
	terrain.is_ready = true
	terrain.swaps = 3
	terrain.last_report = {"accepted": true}
	var tiles: Array = []
	for id in final_ids:
		var tile := {"id": id}
		if not old_ids.has(id):
			tile.merge({"anchor": PackedFloat64Array([0, 0, 0]),
				"vertices": arrays[Mesh.ARRAY_VERTEX], "normals": arrays[Mesh.ARRAY_NORMAL],
				"colors": arrays[Mesh.ARRAY_COLOR], "uv": arrays[Mesh.ARRAY_TEX_UV],
				"indices": arrays[Mesh.ARRAY_INDEX]})
		tiles.append(tile)
	var entries: Array = []
	for id in MORPH_CHILDREN:
		entries.append({"id": id, "coarse_id": MORPH_PARENT, "refining": refining,
			"position_elevation": positions, "normals": normals, "colors": colors})
	fake.batch = {"identity": owner.get_stream_identity(), "generated": 4 if refining else 1,
		"tiles": tiles, "morph": {"version": 1, "draw_ids": [MORPH_COMMON] + MORPH_CHILDREN, "entries": entries}}
	return {"terrain": terrain, "bridge": fake, "old_ids": old_ids, "final_ids": final_ids,
		"base_arrays": var_to_bytes(arrays), "base_vertices": var_to_bytes(arrays[Mesh.ARRAY_VERTEX]),
		"coarse_position": Vector3(positions[0], positions[1], positions[2])}

func stage_mock_morph(fixture: Dictionary) -> bool:
	var terrain: Node3D = fixture.terrain
	# At most five new base nodes and four endpoint uploads. The count bound
	# permits completion even when the existing soft upload budget stops early.
	for iteration in 16:
		terrain.tick(0.0, Vector3.ZERO, false)
		if not check(terrain.error.is_empty(), "Declared morph staging refused: " + terrain.error): return false
		if terrain.morph_active: return true
	return check(false, "Declared bounded morph did not finish staging")

func check_morph_lifecycle(accepted: Node3D, owner: Variant) -> void:
	for refining in [true, false]:
		var fixture := morph_fixture(accepted, owner, refining)
		var terrain: Node3D = fixture.terrain
		var fake: MorphBridge = fixture.bridge
		var retired: Array = []
		for id in fixture.old_ids:
			if id != MORPH_COMMON: retired.append(terrain.nodes[id])
		if not stage_mock_morph(fixture): terrain.free(); continue
		check(terrain.morph_active and terrain.morph_progress == 0.0 and terrain.swaps == 3 and terrain.resident_ids == fixture.old_ids, "Morph start silently committed final residency")
		check(terrain.report().morph_active and terrain.report().morph_progress == 0.0, "Morph report disagrees with presenter phase")
		for id in MORPH_CHILDREN:
			var node: MeshInstance3D = terrain.nodes[id]
			check(node.visible and not terrain.nodes[MORPH_PARENT].visible and var_to_bytes(node.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]) == fixture.base_vertices, "Common fine draw cover changed base vertices or exposed coarse duplicate")
			var bounds: AABB = node.mesh.custom_aabb
			var coarse: Vector3 = fixture.coarse_position
			check(coarse.x >= bounds.position.x and coarse.x <= bounds.end.x and coarse.y >= bounds.position.y and coarse.y <= bounds.end.y and coarse.z >= bounds.position.z and coarse.z <= bounds.end.z, "Swept custom AABB omits declared coarse endpoint")
		var depth: Vector2 = terrain.depth_range(Transform3D.IDENTITY, 100000.0)
		check(depth != Vector2.ZERO and depth.y >= -fixture.coarse_position.z, "Depth clipping ignored morph's swept bounds")
		var calls := [fake.polls, fake.requests]
		terrain.tick(0.125, Vector3.ZERO, false)
		check(terrain.morph_active and terrain.morph_progress == 0.0 and [fake.polls, fake.requests] == calls, "Paused morph advanced or polled another cover")
		terrain.tick(0.125, Vector3.ZERO, true)
		check(terrain.morph_active and is_equal_approx(terrain.morph_progress, 0.5) and [fake.polls, fake.requests] == calls, "Half morph changed stream demand or phase")
		terrain.tick(0.125, Vector3.ZERO, true)
		var final_ids: Array = terrain.resident_ids.duplicate()
		final_ids.sort()
		var expected: Array = fixture.final_ids.duplicate()
		expected.sort()
		check(not terrain.morph_active and terrain.pending.is_empty() and final_ids == expected and terrain.swaps == 4, "Completed refine/coarsen did not commit final cover exactly once")
		for node: MeshInstance3D in retired:
			check(not node.visible and node.is_queued_for_deletion(), "Completed morph retained outgoing visible geometry")
		var completed_calls := [fake.polls, fake.requests]
		var completed_children := terrain.get_child_count()
		terrain.tick(1.0, Vector3.ZERO)
		check([fake.polls, fake.requests] == completed_calls and terrain.get_child_count() == completed_children and terrain.swaps == 4, "Same-frame retirement admitted a third cover before queued nodes left")
		check(var_to_bytes(accepted.nodes.values()[0].mesh.surface_get_arrays(0)) == fixture.base_arrays, "Morph mutated shared admitted source mesh arrays")
		terrain.free()
	# Reset before all uploads and while actively paused: neither can leak
	# endpoint resources or resume stale progress into the next world.
	for active in [false, true]:
		var fixture := morph_fixture(accepted, owner, true)
		var terrain: Node3D = fixture.terrain
		if active:
			if not stage_mock_morph(fixture): terrain.free(); continue
		else:
			terrain.tick(0.0, Vector3.ZERO, false)
			check(not terrain.pending.is_empty() and not terrain.morph_active, "Four-child fixture skipped its bounded staging phase")
		var nodes: Array = terrain.nodes.values().duplicate()
		terrain.reset_world()
		check(not terrain.morph_active and terrain.morph_progress == 0.0 and terrain.nodes.is_empty() and terrain.pending.is_empty() and terrain.anchors.is_empty() and terrain.resident_ids.is_empty(), "Reset retained morph state or geometry")
		for node: MeshInstance3D in nodes: check(not node.visible and node.is_queued_for_deletion(), "Reset left staged/active geometry visible")
		var reset_calls := [fixture.bridge.polls, fixture.bridge.requests]
		terrain.tick(1.0, Vector3.ZERO)
		check([fixture.bridge.polls, fixture.bridge.requests] == reset_calls and terrain.nodes.is_empty() and terrain.get_child_count() == nodes.size(), "Same-frame world reset uploaded behind queued old covers")
		terrain.free()

func check_morph_refusals(accepted: Node3D, owner: Variant) -> void:
	for damage in ["length", "nonfinite", "wetness", "ancestry", "owner", "draw-roster"]:
		var fixture := morph_fixture(accepted, owner, true)
		var terrain: Node3D = fixture.terrain
		var fake: MorphBridge = fixture.bridge
		match damage:
			"length":
				var broken: PackedFloat32Array = fake.batch.morph.entries[0].normals.duplicate()
				broken.resize(1)
				fake.batch.morph.entries[0].normals = broken
			"nonfinite":
				var broken: PackedFloat32Array = fake.batch.morph.entries[0].position_elevation.duplicate()
				broken[0] = NAN
				fake.batch.morph.entries[0].position_elevation = broken
			"wetness":
				var broken: PackedFloat32Array = fake.batch.morph.entries[0].normals.duplicate()
				broken[3] = 1.01
				fake.batch.morph.entries[0].normals = broken
			"ancestry": fake.batch.morph.entries[0].coarse_id = "1:10:513:511"
			"owner": fake.batch.identity.generation = "999999"
			"draw-roster": fake.batch.morph.draw_ids.pop_back()
		var accepted_nodes: Array = []
		for id in fixture.old_ids:
			var node: MeshInstance3D = terrain.nodes[id]
			accepted_nodes.append([node, node.mesh, node.material_override, node.transform])
		for iteration in 16:
			terrain.tick(0.0, Vector3.ZERO, false)
			if not terrain.error.is_empty(): break
		check(not terrain.error.is_empty() and not terrain.morph_active and terrain.resident_ids == fixture.old_ids and terrain.swaps == 3 and terrain.last_report == {"accepted": true}, "Malformed morph changed accepted cover: " + damage)
		for entry in accepted_nodes:
			var node: MeshInstance3D = entry[0]
			check(node.visible and not node.is_queued_for_deletion() and node.mesh == entry[1] and node.material_override == entry[2] and node.transform == entry[3] and not node.material_override.get_shader_parameter("morph_active") and node.material_override.get_shader_parameter("morph_fine_weight") == 1.0, "Malformed morph mutated accepted base/material/placement: " + damage)
		for id in terrain.nodes:
			if not fixture.old_ids.has(id): check(not terrain.nodes[id].visible, "Refused incoming morph geometry was exposed: " + damage)
		terrain.free()


func check_camera_depth(terrain: Node3D, state: Dictionary) -> void:
	var relation := Camera3D.new()
	root.add_child(relation)
	var radius := float(state.planet_radius)
	# Only the camera is moved for a 10km inspection; selected C++ state/mesh
	# placement stays untouched. Construct the reference-sphere tangent point
	# independently, then ask Camera3D itself whether its far plane clips it.
	var centre := Vector3(0, -radius - float(state.altitude), 0)
	relation.position = Vector3(0, 10000.0 - float(state.altitude), 0)
	var radial := radius + 10000.0
	var tangent_y := radius * radius / radial
	var limb := centre + Vector3(sqrt(radius * radius - tangent_y * tangent_y), tangent_y, 0)
	relation.look_at(limb, Vector3.UP)
	relation.near = 0.2
	relation.far = 100000.0
	check(not relation.is_position_in_frustum(limb), "Declared old 100km camera did not reproduce limb clipping")
	var depth: Vector2 = terrain.depth_range(relation.transform, 100000.0)
	check(depth != Vector2.ZERO and depth.y > 100000.0 and depth.y <= 1000000000.0 and depth.y / depth.x <= 500001.0, "Actual cover depth escaped its retained ratio/cap")
	relation.near = depth.x
	relation.far = depth.y
	check(relation.is_position_in_frustum(limb), "Actual-camera far plane still clips the reference limb")
	# Check actual submitted vertices independently of the AABB-bound algorithm,
	# including C++ relief/skirts, a translated eye and a rolled camera.
	var target := Vector3.ZERO
	var maximum := 0.0
	for node: MeshInstance3D in terrain.nodes.values():
		var arrays: Array = node.mesh.surface_get_arrays(0)
		for vertex: Vector3 in arrays[Mesh.ARRAY_VERTEX]:
			var point: Vector3 = node.transform * vertex
			if point.distance_squared_to(relation.position) > maximum:
				maximum = point.distance_squared_to(relation.position)
				target = point
	relation.look_at(target, Vector3.UP)
	relation.rotate_object_local(Vector3.BACK, 0.73)
	depth = terrain.depth_range(relation.transform, 100000.0)
	relation.near = depth.x
	relation.far = depth.y
	check(relation.is_position_in_frustum(target), "Actual submitted distant vertex was clipped after camera attitude change")
	var inverse := relation.transform.affine_inverse()
	for node: MeshInstance3D in terrain.nodes.values():
		var arrays: Array = node.mesh.surface_get_arrays(0)
		for vertex: Vector3 in arrays[Mesh.ARRAY_VERTEX]:
			var actual: Vector3 = inverse * (node.transform * vertex)
			check(-actual.z <= relation.far, "A submitted relief/skirt vertex escapes actual camera far depth")
	# Every guard is pure: accepted placement/residency must survive refusal.
	var before := [terrain.resident_ids.duplicate(), terrain.swaps, terrain.last_report.duplicate(true)]
	for limit in [NAN, INF, -INF, -1.0, 99999.0, 1000000001.0]:
		check(terrain.depth_range(relation.transform, limit) == Vector2.ZERO, "Invalid camera clip envelope accepted")
	for invalid in [Transform3D(Basis.IDENTITY, Vector3(NAN, 0, 0)), Transform3D(Basis.IDENTITY, Vector3(0, INF, 0)), Transform3D(Basis(Vector3.ZERO, Vector3.UP, Vector3.BACK), Vector3.ZERO)]:
		check(terrain.depth_range(invalid, 100000.0) == Vector2.ZERO, "Invalid/nonfinite camera relation accepted")
	var source: MeshInstance3D = terrain.nodes.values()[0]
	terrain.nodes["invalid-depth-node"] = null
	check(terrain.depth_range(relation.transform, 100000.0) == Vector2.ZERO, "Malformed geometry owner accepted")
	terrain.nodes.erase("invalid-depth-node")
	var original := source.transform
	source.transform = Transform3D(Basis.IDENTITY, Vector3(0, 0, INF))
	check(terrain.depth_range(relation.transform, 100000.0) == Vector2.ZERO, "Nonfinite geometry placement accepted")
	source.transform = original
	var saved_nodes: Dictionary = terrain.nodes
	terrain.nodes = {}
	for index in 769: terrain.nodes[index] = source
	check(terrain.depth_range(relation.transform, 100000.0) == Vector2.ZERO, "Oversized double-cover buffer accepted")
	terrain.nodes = saved_nodes
	check([terrain.resident_ids, terrain.swaps, terrain.last_report] == before and source.transform == original, "Depth refusal mutated accepted cover")
	relation.free()

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"):
		quit(1)
		return
	if not check(DirAccess.make_dir_recursive_absolute(args[1]) == OK, "Terrain test output unavailable"):
		quit(1)
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(owner.stage_freedom_new_game("42"), "New Game staging refused"):
		quit(1)
		return
	var pending: Dictionary = owner.get_pending_freedom_start()
	check(pending.flight_state.get("terrain_policy") == 3 and owner.get_stream_identity().is_empty(), "Pending New Game did not keep selected terrain off the active world")
	if not check(owner.commit_pending_freedom_start(pending.candidate_id), "New Game commit refused"):
		quit(1)
		return
	var first_identity: Dictionary = owner.get_stream_identity()
	var first_state: Dictionary = owner.get_freedom_flight_state()
	check(StreamView.valid_identity(first_identity) and first_identity.terrain_policy == 3 and first_identity.system_id == first_state.system_id and first_identity.body_id == first_state.planet_id, "Selected stream is not owned by the saved world")
	for damage in ["version", "generation", "policy", "body", "extra"]:
		var invalid: Dictionary = first_identity.duplicate(true)
		match damage:
			"version": invalid.schema_version = 2
			"generation": invalid.generation = "18446744073709551616"
			"policy": invalid.terrain_policy = 4
			"body": invalid.body_version = 999
			"extra": invalid["unowned"] = true
		check(not StreamView.valid_identity(invalid), "Malformed stream identity accepted: " + damage)
	var source := args[1].path_join("selected.json")
	check(owner.save_freedom_as(source), "Selected terrain Save As refused")
	var source_hash := FileAccess.get_sha256(source)
	var source_text := FileAccess.get_file_as_string(source)
	check(source_text.contains('"terrain_policy": 3'), "Selected terrain was not explicit in saved bytes")
	# Bad recipe metadata must refuse before replacing the active C++ world,
	# stream generation or its presentation cover.
	var damaged := args[1].path_join("unsupported-policy.json")
	var file := FileAccess.open(damaged, FileAccess.WRITE)
	if not check(file != null, "Invalid selection fixture cannot be written"):
		quit(1)
		return
	file.store_string(source_text.replace('"terrain_policy": 3', '"terrain_policy": 4'))
	file.close()
	check(not owner.stage_freedom_continue(damaged) and owner.get_pending_freedom_start().is_empty(), "Unknown saved terrain policy staged")
	check(owner.get_stream_identity() == first_identity and owner.get_freedom_flight_state() == first_state and FileAccess.get_sha256(source) == source_hash, "Refused selection changed active terrain/state/source bytes")
	var terrain := StreamView.new()
	terrain.bridge = owner
	root.add_child(terrain)
	# A fault can already be latched before the first tick installs an
	# identity. Identity initialization must not silently resume uploads.
	terrain.error = "Latched first-tick terrain fault"
	terrain.tick(0.01, Vector3.ZERO)
	check(terrain.identity == first_identity and terrain.error == "Latched first-tick terrain fault" and terrain.nodes.is_empty() and not terrain.is_ready, "Initial stream identity cleared a latched fault")
	terrain.reset_world()
	check(terrain.error.is_empty() and terrain.identity.is_empty(), "Explicit terrain reset did not clear acknowledged fault")
	if not await wait_ready(terrain, first_identity):
		terrain.free()
		quit(1)
		return
	check_cover_replacements(terrain, owner)
	check_morph_lifecycle(terrain, owner)
	check_morph_refusals(terrain, owner)
	check_camera_depth(terrain, first_state)
	check(owner.get_freedom_flight_state() == first_state and FileAccess.get_sha256(source) == source_hash, "Presentation refusals changed the admitted world or source bytes")
	var old_nodes: Array = terrain.nodes.values().duplicate()
	# Place an old-generation batch in the pending queue, then replace the
	# real C++ world. The next tick must retire it before any upload occurs.
	terrain.pending = [{"id": "stale-selected-payload"}]
	terrain.upload_index = 0
	terrain.error = "Latched old-world terrain fault"
	if not select_continue(owner, args[0]):
		terrain.free()
		quit(1)
		return
	var legacy_identity: Dictionary = owner.get_stream_identity()
	check(legacy_identity.generation != first_identity.generation and legacy_identity.terrain_policy == 1 and owner.get_freedom_flight_state().terrain_policy == 1, "Legacy Continue silently migrated terrain")
	terrain.tick(0.01, Vector3.ZERO)
	check(terrain.identity == legacy_identity and terrain.pending.is_empty() and terrain.nodes.is_empty() and not terrain.is_ready and terrain.error == "Latched old-world terrain fault", "World/policy transition retained old geometry or cleared a latched fault")
	for node: MeshInstance3D in old_nodes:
		check(is_instance_valid(node) and not node.visible and node.is_queued_for_deletion(), "Retired old terrain cover remains visible")
	terrain.reset_world()
	check(terrain.error.is_empty() and terrain.identity.is_empty(), "Explicit selection reset did not clear acknowledged fault")
	if not await wait_ready(terrain, legacy_identity):
		terrain.free()
		quit(1)
		return
	if not select_continue(owner, source):
		terrain.free()
		quit(1)
		return
	var restored: Dictionary = owner.get_stream_identity()
	check(restored.terrain_policy == 3 and restored.generation != legacy_identity.generation and owner.get_freedom_flight_state().terrain_policy == 3 and FileAccess.get_sha256(source) == source_hash, "Selected Continue lost its saved terrain choice")
	if not await wait_ready(terrain, restored):
		terrain.free()
		quit(1)
		return
	check(not terrain.nodes.has("stale-selected-payload"), "Retired payload aliased another recipe's mesh cache")
	var after := args[1].path_join("selected-after-stream.json")
	check(owner.save_freedom_as(after) and FileAccess.get_sha256(after) == source_hash, "Terrain streaming changed complete saved world bytes")
	terrain.free()
	owner = null
	await process_frame
	print("Native selected terrain: %d failures" % failures)
	quit(1 if failures else 0)
