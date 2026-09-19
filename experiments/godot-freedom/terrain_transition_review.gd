extends SceneTree
## Stationary inspection fixtures, not flight proof or a frame-rate benchmark.
var study: Variant

func _initialize() -> void:
	call_deferred("run")

func settle(after_swap: int) -> bool:
	var deadline := Time.get_ticks_msec() + 45000
	var stable := 0
	var previous := -1
	while Time.get_ticks_msec() < deadline:
		await process_frame
		var stream: Node = study.planet_stream
		if stream.is_ready and stream.pending.is_empty() and not study.live_bridge.is_stream_busy() and stream.swaps > after_swap and stream.swaps == previous:
			stable += 1
		else:
			stable = 0
		previous = stream.swaps
		# Allow asynchronous generation and complete cover replacement, not just
		# the first frame of an old cover after a survey relocation.
		if stable > 240:
			return true
	return false

func run() -> void:
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	study.live_paused = true
	study.pause_menu.hide_menu()
	study.player_input.needs_neutral = false
	if study.pilot_view:
		study.toggle_pilot()
	var folder: String = study.options.get("--review-dir", "")
	if folder.is_empty():
		push_error("Terrain review needs an existing --review-dir")
		quit(1)
		return
	for altitude in [2500, 30000, 85000, 180000, 650000, 85000]:
		var before: int = study.planet_stream.swaps
		if not study.live_bridge.set_survey_pose(0.25, 0.4, altitude):
			quit(1)
			return
		if not await settle(before):
			push_error("Terrain cover did not settle")
			quit(1)
			return
		await RenderingServer.frame_post_draw
		var name := folder + "/terrain-%d" % altitude
		var frame := root.get_texture().get_image()
		if frame.save_png(name + ".png") != OK:
			quit(1)
			return
		var metadata := FileAccess.open(name + ".json", FileAccess.WRITE)
		metadata.store_string(JSON.stringify({"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Original code-authored assets rendered in Godot", "inspection_relocation": true, "altitude_metres": altitude, "flight_state": study.live_bridge.get_state(), "stream": study.planet_stream.report(), "viewport": [frame.get_width(), frame.get_height()]}, "\t"))
		print("Terrain review captured %d m / %d resident tiles" % [altitude, study.planet_stream.resident_ids.size()])
	quit(0)
