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
	check(pending.flight_state.get("terrain_policy") == 2 and owner.get_stream_identity().is_empty(), "Pending New Game did not keep selected terrain off the active world")
	if not check(owner.commit_pending_freedom_start(pending.candidate_id), "New Game commit refused"):
		quit(1)
		return
	var first_identity: Dictionary = owner.get_stream_identity()
	var first_state: Dictionary = owner.get_freedom_flight_state()
	check(StreamView.valid_identity(first_identity) and first_identity.terrain_policy == 2 and first_identity.system_id == first_state.system_id and first_identity.body_id == first_state.planet_id, "Selected stream is not owned by the saved world")
	for damage in ["version", "generation", "policy", "body", "extra"]:
		var invalid: Dictionary = first_identity.duplicate(true)
		match damage:
			"version": invalid.schema_version = 2
			"generation": invalid.generation = "18446744073709551616"
			"policy": invalid.terrain_policy = 3
			"body": invalid.body_version = 999
			"extra": invalid["unowned"] = true
		check(not StreamView.valid_identity(invalid), "Malformed stream identity accepted: " + damage)
	var source := args[1].path_join("selected.json")
	check(owner.save_freedom_as(source), "Selected terrain Save As refused")
	var source_hash := FileAccess.get_sha256(source)
	var source_text := FileAccess.get_file_as_string(source)
	check(source_text.contains('"terrain_policy": 2'), "Selected terrain was not explicit in saved bytes")
	# Bad recipe metadata must refuse before replacing the active C++ world,
	# stream generation or its presentation cover.
	var damaged := args[1].path_join("unsupported-policy.json")
	var file := FileAccess.open(damaged, FileAccess.WRITE)
	if not check(file != null, "Invalid selection fixture cannot be written"):
		quit(1)
		return
	file.store_string(source_text.replace('"terrain_policy": 2', '"terrain_policy": 3'))
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
	check(restored.terrain_policy == 2 and restored.generation != legacy_identity.generation and owner.get_freedom_flight_state().terrain_policy == 2 and FileAccess.get_sha256(source) == source_hash, "Selected Continue lost its saved terrain choice")
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
