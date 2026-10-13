extends SceneTree
## Real C++ saved selections and main-thread mesh residency. Dummy/headless
## rendering is not visible-feature, GPU performance or controller acceptance.
const StreamView = preload("res://scripts/world/planet_stream.gd")
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
