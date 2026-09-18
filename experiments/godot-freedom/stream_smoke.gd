extends SceneTree
## GPU integration test: scripted camera approach and explicit survey relocations.
## This is NOT a recording of a player flying an orbit or circumnavigation.

var study: Variant
var peak_nodes := 0
var peak_children := 0
var failed := false
var deadline := 0


func _initialize() -> void:
	deadline = Time.get_ticks_msec() + 90000
	call_deferred("start")


func check(condition: bool, message: String) -> bool:
	if not condition:
		failed = true
		push_error(message)
		quit(1)
	return condition


func settle(previous_swaps: int) -> bool:
	while not failed:
		await process_frame
		var stream: Variant = study.planet_stream
		if stream.is_ready and stream.swaps > previous_swaps and stream.pending.is_empty():
			# Let queued nodes leave the tree before checking actual scene ownership.
			await process_frame
			return check(stream.get_child_count() == stream.resident_ids.size(),
				"Retired tile nodes remain in the scene")
	return false


func start() -> void:
	study = load("res://main.tscn").instantiate()
	if not check(study.get_script() != null, "Study script failed to load"):
		return
	root.add_child(study)
	if not check(study.planet_stream != null, "Use --stream=true for the stream smoke"):
		return
	var capture: String = study.capture_path
	study.capture_path = ""
	study.set_view(4)
	if not await settle(0):
		return
	var initial: Dictionary = study.live_bridge.get_state()
	var orbital_tiles: int = study.planet_stream.resident_ids.size()
	var visited: Array = []
	for position in [Vector3(0, 250000, 400000), Vector3(0, 15000, 25000), Vector3(0, 3000, 8000)]:
		var before: int = study.planet_stream.swaps
		study.camera.position = position
		study.camera.near = maxf(0.15, position.y * 0.0001)
		study.camera.far = maxf(100000, position.length() * 8)
		study.camera.look_at(Vector3(0, -200, -5500))
		if not await settle(before):
			return
		visited.append({"inspection_camera_metres": [position.x, position.y, position.z],
			"resident_tiles": study.planet_stream.resident_ids.size()})
	# Cube-face edge, longitude wrap, both poles, then return to the same seed/site.
	# Arrays preserve doubles; Vector2 would round a pole beyond the legal latitude.
	for coordinates in [[0.0, PI / 4], [0.0, PI - 0.000001],
		[PI / 2, 0.0], [-PI / 2, 0.0], [initial.latitude, initial.longitude]]:
		var before: int = study.planet_stream.swaps
		var altitude: float = float(initial.altitude) if coordinates == [initial.latitude, initial.longitude] else 10000.0
		if not check(study.live_bridge.set_survey_pose(coordinates[0], coordinates[1], altitude),
			"Survey relocation rejected: " + str(study.live_bridge.get_last_error())):
			return
		if not await settle(before):
			return
		visited.append({"survey_latitude": coordinates[0], "survey_longitude": coordinates[1],
			"resident_tiles": study.planet_stream.resident_ids.size()})
	var stream: Variant = study.planet_stream
	if not check(stream.retired_total > 0 and stream.resident_ids.size() > orbital_tiles,
		"Approach did not refine and retire terrain"):
		return
	study.capture_evidence = {
		"kind": "scripted inspection; survey relocation is not simulated flight",
		"route": visited, "orbital_tiles": orbital_tiles, "peak_tile_nodes": peak_nodes,
		"peak_tile_children": peak_children, "swaps": stream.swaps,
		"retired_tiles": stream.retired_total, "final_children": stream.get_child_count(),
	}
	print("GPU streaming smoke passed: " + JSON.stringify(study.capture_evidence))
	if capture.is_empty():
		quit()
	else:
		study.capture_path = capture
		study.capture_frames = 0
		study.frame_times.clear()
		study.scene_environment.background_mode = Environment.BG_SKY
		study.scene_environment.fog_enabled = true
		study.scene_environment.fog_density = 0.000018
		study.scene_environment.ambient_light_energy = 0.40


func _process(_delta: float) -> bool:
	if Time.get_ticks_msec() > deadline and not failed:
		check(false, "Streaming smoke timed out")
	if study != null and study.planet_stream != null:
		var stream: Variant = study.planet_stream
		peak_nodes = maxi(peak_nodes, stream.nodes.size())
		peak_children = maxi(peak_children, stream.get_child_count())
		if not failed:
			check(stream.nodes.size() <= 768 and stream.get_child_count() <= 768,
				"Old/new tile covers exceeded their combined cap")
			check(stream.resident_ids.size() <= 384, "Resident tile budget exceeded")
	return false
