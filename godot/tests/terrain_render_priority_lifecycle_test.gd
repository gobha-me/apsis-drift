extends SceneTree
## Actual C++ covers and unmodified StreamView mesh/material creation. No mocks.
const StreamView = preload("res://scripts/world/planet_stream.gd")
var checks := 0
var failures := 0
var material_signals := {}
var phases: Array = []


func check(value: bool, reason: String) -> bool:
	checks += 1
	if not value:
		failures += 1
		push_error(reason)
	return value


func _initialize() -> void:
	call_deferred("run")


func audit(terrain: Node3D, label: String) -> void:
	check(not terrain.nodes.is_empty() and terrain.nodes.size() <= 768, "Empty or oversized actual cover: " + label)
	for id in terrain.nodes:
		var node: MeshInstance3D = terrain.nodes[id]
		if not check(node.mesh is ArrayMesh and node.mesh.get_surface_count() == 1 and node.material_override is ShaderMaterial, "Actual upload missing mesh/material: " + id):
			continue
		var material: ShaderMaterial = node.material_override
		check(material.render_priority == StreamView.tile_render_priority(id), "Uploaded material priority differs from actual tile key: " + id + "/" + label)
		check(material_signals.get(material.get_instance_id()) == material.render_priority, "Material-created signal saw a different/unassigned priority: " + id)
		check(material.shader == load("res://shaders/terrain.gdshader") and material.get_shader_parameter("planet_tile") == true, "Upload lost physical terrain shader: " + id)
		check(node.transform.is_finite(), "Nonfinite real tile placement: " + id)
		var arrays: Array = node.mesh.surface_get_arrays(0)
		check(arrays[Mesh.ARRAY_VERTEX].size() == 1221 and arrays[Mesh.ARRAY_NORMAL].size() == 1221 and arrays[Mesh.ARRAY_COLOR].size() == 1221 and arrays[Mesh.ARRAY_TEX_UV].size() == 1221, "Actual C++ vertex attribute boundary changed: " + id)
		var indices := Array(arrays[Mesh.ARRAY_INDEX])
		check(indices.size() == 7680 and indices.min() == 0 and indices.max() < 1221, "Actual C++ index buffer out of bounds: " + id)
	phases.append({"name": label, "nodes": terrain.nodes.size(), "resident": terrain.resident_ids.size(), "swaps": terrain.swaps, "morph_active": terrain.morph_active})


func materials(terrain: Node3D) -> Dictionary:
	var result := {}
	for id in terrain.nodes:
		result[id] = terrain.nodes[id].material_override
	return result


func retained(terrain: Node3D, before: Dictionary) -> int:
	var count := 0
	for id in terrain.nodes:
		if before.has(id):
			count += 1
			check(terrain.nodes[id].material_override == before[id], "Cover replacement recreated retained material: " + id)
	return count


func wait_cover(terrain: Node3D, observer: Vector3, previous: int, stop_at_morph := false) -> bool:
	var deadline := Time.get_ticks_msec() + 60000
	while Time.get_ticks_msec() < deadline:
		# Explicit demand only; no automatic camera requests or simulation ticks.
		terrain.tick(0.0, observer)
		if not check(terrain.error.is_empty(), "Real cover refused: " + terrain.error): return false
		if stop_at_morph and terrain.morph_active: return true
		if terrain.swaps > previous and terrain.pending.is_empty():
			await process_frame
			return check(not stop_at_morph, "Requested replacement omitted real morph endpoints")
		await create_timer(0.005).timeout
	return check(false, "Real cover upload exceeded 60 seconds")


func transition(terrain: Node3D, owner: Variant, observer: Vector3, label: String, refining: bool) -> bool:
	var before := materials(terrain)
	var swaps: int = terrain.swaps
	if not check(owner.request_stream(observer), "Real transition request refused: " + label): return false
	if not await wait_cover(terrain, observer, swaps, true): return false
	audit(terrain, label + "-start")
	check(retained(terrain, before) > 0, "Transition did not retain real materials: " + label)
	var directions := 0
	for id in terrain.morph_entries:
		if terrain.morph_entries[id].refining == refining: directions += 1
		var arrays: Array = terrain.nodes[id].mesh.surface_get_arrays(0)
		check(arrays[Mesh.ARRAY_CUSTOM0] is PackedFloat32Array and arrays[Mesh.ARRAY_CUSTOM0].size() == 4 * 1221, "Real morph mesh lacks C++ correspondence: " + id)
	check(directions > 0, "Requested real refine/coarsen direction absent: " + label)
	var active := materials(terrain)
	terrain.tick(StreamView.MORPH_SECONDS * 0.5, observer)
	check(terrain.morph_active and terrain.morph_progress == 0.5 and terrain.swaps == swaps, "Half morph committed residency early")
	audit(terrain, label + "-half")
	check(retained(terrain, active) == active.size(), "Morph replaced material instances")
	terrain.tick(StreamView.MORPH_SECONDS * 0.5, observer)
	await process_frame
	check(not terrain.morph_active and terrain.swaps == swaps + 1 and terrain.pending.is_empty(), "Real morph failed to commit once")
	audit(terrain, label + "-complete")
	check(terrain.last_report.reused > 0, "Real replacement did not exercise cover reuse")
	if refining:
		check(terrain.last_report.generated > 0, "Refinement did not generate new real tiles")
	retained(terrain, before)
	return true


func exercise(terrain: Node3D, owner: Variant, output: String) -> void:
	var state: Dictionary = owner.get_freedom_flight_state()
	var orbit := Vector3(0.0, state.planet_radius, state.planet_radius)
	check(owner.request_stream(orbit), "Initial real orbital request refused")
	if not await wait_cover(terrain, orbit, 0): return
	audit(terrain, "initial")
	var accepted := materials(terrain)
	var initial_ids: Array = terrain.resident_ids.duplicate()
	var swaps: int = terrain.swaps
	check(owner.request_stream(orbit), "Unchanged real cover request refused")
	for frame in 3:
		terrain.tick(0.0, orbit)
		await process_frame
	check(not owner.is_stream_busy() and terrain.swaps == swaps and terrain.resident_ids == initial_ids, "Identical request changed accepted cover")
	check(retained(terrain, accepted) == accepted.size(), "Identical request recreated materials")
	if not await transition(terrain, owner, Vector3.ZERO, "refine", true): return
	if not await transition(terrain, owner, orbit, "coarsen", false): return
	accepted = materials(terrain)
	initial_ids = terrain.resident_ids.duplicate()
	swaps = terrain.swaps
	var report: Dictionary = terrain.last_report.duplicate(true)
	check(owner.request_stream(Vector3.ZERO), "Rollback transition request refused")
	if not await wait_cover(terrain, Vector3.ZERO, swaps, true): return
	audit(terrain, "rollback-active")
	# A real invalid public tick input triggers production rollback. No fake
	# bridge packets, substituted meshes, manual morph state or world edits.
	terrain.tick(NAN, Vector3.ZERO)
	check(terrain.error == "Invalid terrain presentation clock or observer" and not terrain.morph_active, "Invalid clock failed to roll back real morph")
	check(terrain.resident_ids == initial_ids and terrain.swaps == swaps and terrain.last_report == report, "Rollback changed accepted cover/report")
	check(retained(terrain, accepted) == accepted.size(), "Rollback lost accepted materials")
	for id in terrain.nodes:
		check(terrain.nodes[id].visible == initial_ids.has(id), "Rollback exposed unaccepted tile: " + id)
		check(terrain.nodes[id].material_override.get_shader_parameter("morph_active") == false, "Rollback left morph material enabled: " + id)
	audit(terrain, "rollback-complete")
	check(owner.get_freedom_flight_state() == state, "Presentation lifecycle changed authoritative C++ state")
	check(owner.save_freedom_as(output), "Post-lifecycle canonical Save As refused")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if not check(args.size() == 2, "Expected authentic system import and new output Save As path"):
		quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Actual C++ bridge unavailable"):
		quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(owner.stage_freedom_continue(args[0]), "Actual Continue staging refused"):
		quit(1); return
	var candidate: Dictionary = owner.get_pending_freedom_start()
	if not check(owner.commit_pending_freedom_start(candidate.candidate_id), "Actual Continue commit refused"):
		quit(1); return
	var terrain := StreamView.new()
	terrain.bridge = owner
	terrain.terrain_material_created.connect(func(material: ShaderMaterial) -> void:
		material_signals[material.get_instance_id()] = material.render_priority)
	root.add_child(terrain)
	await exercise(terrain, owner, args[1])
	terrain.free()
	owner = null
	await process_frame
	print("Terrain priority real lifecycle phases: " + JSON.stringify(phases))
	print("Terrain priority real lifecycle: %d checks, %d failures" % [checks, failures])
	quit(1 if failures else 0)
