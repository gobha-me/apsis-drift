extends SceneTree
## Actual GDExtension streaming boundary, without a GPU or engine-owned terrain.
var failures := 0

func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func batch(bridge: Variant) -> Dictionary:
	var deadline := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < deadline:
		var result: Dictionary = bridge.poll_stream()
		if not result.is_empty():
			return result
		await create_timer(0.005).timeout
	check(false, "Streaming worker timeout")
	return {}

func run() -> void:
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		push_error("Live adapter missing")
		quit(1)
		return
	var args := OS.get_cmdline_user_args()
	if args.size() != 1:
		quit(1)
		return
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.initialize(FileAccess.get_file_as_string(args[0])), "Initialization rejected")
	var initial: Dictionary = bridge.get_state()
	check(bridge.enable_streaming(), "Enable stream rejected")
	check(not bridge.is_stream_busy(), "New stream already has a worker")
	check(bridge.get_state().checksum == initial.checksum, "Enabling rendering changed simulation")
	check(not bridge.request_stream(Vector3(NAN, 0, 0)), "Nonfinite observer accepted")
	check(bridge.stream_transforms(PackedFloat64Array([1, 2])).is_empty(), "Short anchor buffer accepted")
	check(bridge.stream_transforms(PackedFloat64Array([INF, 0, 0])).is_empty(), "Infinite anchor accepted")
	var large := PackedFloat64Array()
	large.resize(769 * 3)
	check(bridge.stream_transforms(large).is_empty(), "Oversize anchor buffer accepted")
	check(not bridge.set_survey_pose(NAN, 0, 3000), "Invalid survey pose accepted")
	check(bridge.get_state().checksum == initial.checksum, "Rejected request mutated flight")
	check(bridge.request_stream(Vector3.ZERO), "First stream request rejected")
	check(bridge.is_stream_busy(), "Pending cover reported idle")
	var first: Dictionary = await batch(bridge)
	check(not bridge.is_stream_busy(), "Consumed cover still reported busy")
	if not first.has("tiles"):
		push_error(str(first))
		quit(1)
		return
	check(first.tiles.size() <= 384 and first.tiles.size() >= 6, "Resident tile budget")
	var anchors := PackedFloat64Array()
	for tile in first.tiles:
		check(tile.vertices.size() == 1221 and tile.normals.size() == 1221, "Vertex/normal buffer dimensions")
		check(tile.colors.size() == 1221 and tile.uv.size() == 1221, "Color/UV buffer dimensions")
		check(tile.indices.size() == 7680, "Index buffer dimensions")
		for index in tile.indices:
			check(index >= 0 and index < 1221, "Invalid triangle index")
		anchors.append_array(tile.anchor)
	var placed: Array = bridge.stream_transforms(anchors)
	check(placed.size() == first.tiles.size(), "Transform buffer size")
	for transform in placed:
		check(transform.origin.is_finite() and transform.basis.is_finite(), "Nonfinite tile placement")
	check(bridge.get_state().checksum == initial.checksum, "Streaming changed authoritative flight")
	# Actual C++ flight crosses longitude wrap, with rendering at the local origin.
	check(bridge.set_survey_pose(0.0, PI - 0.000001, 10000.0), "Dateline fixture rejected")
	for tick in range(2400):
		check(bridge.advance(1.0 / 120, 1), "Dateline flight step rejected")
	var crossed: Dictionary = bridge.get_state()
	check(crossed.longitude < 0, "Continuous C++ flight did not cross longitude wrap")
	check(crossed.position == Vector3.ZERO and crossed.tick == 2400, "Floating origin changed ticks/position")
	check(bridge.set_survey_pose(0.0, PI / 4 - 0.000001, 10000.0), "Cube-edge fixture rejected")
	for tick in range(2400):
		check(bridge.advance(1.0 / 120, 1), "Cube-edge flight step rejected")
	check(bridge.get_state().longitude > PI / 4, "Continuous flight did not cross the cube-face edge")
	for latitude in [-PI / 2, PI / 2]:
		check(bridge.set_survey_pose(latitude, 0, 10000), "Polar fixture rejected")
		var polar: Array = bridge.stream_transforms(anchors)
		for transform in polar:
			check(transform.origin.is_finite() and transform.basis.is_finite(), "Polar transform invalid")
	# Orbital cover is a lower-detail selection of the same planet, not another asset.
	check(bridge.request_stream(Vector3(0, initial.planet_radius, initial.planet_radius)), "Orbital request rejected")
	var orbit: Dictionary = await batch(bridge)
	check(orbit.has("tiles") and orbit.tiles.size() <= 384, "Orbital cover invalid")
	# Reset while a different cover is being generated; worker ownership must
	# end before the old world is released, and the new session starts cleanly.
	check(bridge.request_stream(Vector3.ZERO), "Reset-race request rejected")
	check(bridge.initialize(FileAccess.get_file_as_string(args[0])), "Reset during generation rejected")
	check(bridge.get_state() == initial, "Reset retained old stream/flight state")
	check(bridge.enable_streaming() and bridge.request_stream(Vector3.ZERO), "Restart stream rejected")
	var restarted: Dictionary = await batch(bridge)
	check(restarted.has("tiles") and restarted.generated == first.tiles.size(), "Restart reused stale frontend ownership")
	print("Actual C++ stream boundary: %d failures; surface %d tiles / orbit %d tiles; dateline/cube-edge flight, polar placement and worker reset passed" %
		[failures, first.tiles.size(), orbit.get("tiles", []).size()])
	quit(0 if failures == 0 else 1)
