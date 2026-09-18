extends SceneTree
## Run with --headless --script res://validate_test.gd -- SNAPSHOT.json.

func _initialize() -> void:
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1:
		push_error("Expected one snapshot filename")
		quit(1)
		return
	var input := FileAccess.open(arguments[0], FileAccess.READ)
	if input == null:
		push_error("Snapshot missing")
		quit(1)
		return
	var data: Variant = JSON.parse_string(input.get_as_text())
	var consumer := preload("res://main.gd").new()
	if not consumer.valid_snapshot(data):
		push_error("Valid C++ snapshot rejected")
		consumer.free()
		quit(1)
		return
	var bad_inputs: Array = [null, [], {}, {"schema_version": 999}]
	for size in [0, 1, 513.5, 514, INF, NAN]:
		var bad: Dictionary = data.duplicate(true)
		bad.samples = size
		bad_inputs.append(bad)
	for component in [INF, NAN, "0", 1000001]:
		var bad: Dictionary = data.duplicate(true)
		bad.vertices[0][0] = component
		bad_inputs.append(bad)
	var short_buffer: Dictionary = data.duplicate(true)
	short_buffer.colors.pop_back()
	bad_inputs.append(short_buffer)
	var short_vertex: Dictionary = data.duplicate(true)
	short_vertex.vertices[0] = [0, 0]
	bad_inputs.append(short_vertex)
	var broken_replay: Dictionary = data.duplicate(true)
	broken_replay.replay[1].tick = 9
	bad_inputs.append(broken_replay)
	var numeric_seed: Dictionary = data.duplicate(true)
	numeric_seed.planet.planet_seed = 42
	bad_inputs.append(numeric_seed)
	for key in ["seed_derivation_version", "terrain_generator_version", "lod"]:
		var bad: Dictionary = data.duplicate(true)
		bad[key] = 99
		bad_inputs.append(bad)
	var planet_version: Dictionary = data.duplicate(true)
	planet_version.planet.generator_version = 99
	bad_inputs.append(planet_version)
	for version in [-1, 0.5, 2, INF, NAN, "1"]:
		var bad: Dictionary = data.duplicate(true)
		bad.experimental_relief_version = version
		bad_inputs.append(bad)
	for bad in bad_inputs:
		if consumer.valid_snapshot(bad):
			push_error("Invalid snapshot accepted")
			consumer.free()
			quit(1)
			return
	consumer.turn_head(Vector2(1e12, -1e12))
	if consumer.head_angles != Vector2(1.05, -consumer.HEAD_YAW_LIMIT):
		push_error("Seated look escaped its yaw/pitch bounds")
		consumer.free()
		quit(1)
		return
	var bounded: Vector2 = consumer.head_angles
	consumer.turn_head(Vector2(NAN, INF))
	if consumer.head_angles != bounded:
		push_error("Nonfinite mouse input corrupted seated camera")
		consumer.free()
		quit(1)
		return
	consumer.turn_head(Vector2(-1e12, 1e12))
	if consumer.head_angles != Vector2(-1.22, consumer.HEAD_YAW_LIMIT):
		push_error("Reverse seated look escaped its yaw/pitch bounds")
		consumer.free()
		quit(1)
		return
	var layout = preload("res://cockpit_layout.gd")
	if not layout.valid(layout.spec()):
		push_error("Shared fit layout rejected")
		quit(1)
		return
	for bad in [null, {}, {"schema_version": 99}]:
		if layout.valid(bad):
			push_error("Invalid cockpit contract accepted")
			quit(1)
			return
	for key in ["pilot_eye_blender", "cabin_to_ship"]:
		for value in [[0, NAN, 0], [INF, 0, 0], [0, 0], ["0", 0, 0], [1000, 0, 0]]:
			var bad: Dictionary = layout.spec().duplicate(true)
			bad[key] = value
			if layout.valid(bad):
				push_error("Invalid cockpit anchor accepted")
				quit(1)
				return
	consumer.camera = Camera3D.new()
	consumer.ship = Node3D.new()
	consumer.add_child(consumer.camera)
	consumer.add_child(consumer.ship)
	consumer.pilot_view = true
	consumer.update_head_camera()
	var expected := Vector3(0, 1.6354, -3.005)
	if not consumer.camera.position.is_equal_approx(expected):
		push_error("Pilot camera does not occupy the assembled ship eye")
		quit(1)
		return
	consumer.free()
	print("Godot consumer: valid fixture, %d invalid fixtures and bounded/nonfinite head-look passed" % bad_inputs.size())
	quit()
