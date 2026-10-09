extends SceneTree
## Render-only synthetic fixture: no ship, world, save or simulation advancement.
const Exhaust = preload("res://scripts/native/native_main_exhaust.gd")
var output := ""
var records: Array = []

func _initialize() -> void:
	call_deferred("run")

func stats(image: Image) -> Dictionary:
	var pixel_signal := 0
	var visible_signal := 0
	for y in image.get_height():
		for x in image.get_width():
			var c := image.get_pixel(x, y)
			if c.b - c.r > 0.015:
				pixel_signal += 1
				if c.a > 0.01: visible_signal += 1
	return {"width": image.get_width(), "height": image.get_height(), "signal_pixels": pixel_signal, "signal_with_alpha": visible_signal}

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or not args[0].is_absolute_path() or DisplayServer.get_name() == "headless":
		push_error("Expected absolute output directory and rendering display")
		quit(1)
		return
	output = args[0]
	DirAccess.make_dir_recursive_absolute(output)
	root.size = Vector2i(640, 480)
	var container := SubViewportContainer.new()
	root.add_child(container)
	container.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	var viewport := SubViewport.new()
	viewport.size = root.size
	viewport.transparent_bg = true
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	container.add_child(viewport)
	var camera := Camera3D.new()
	camera.position = Vector3(12, 8, 20)
	camera.look_at_from_position(camera.position, Vector3(0, 1.23, 10), Vector3.UP)
	camera.near = 0.05
	camera.far = 200.0
	viewport.add_child(camera)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_CLEAR_COLOR
	camera.environment = environment
	var exhaust := Exhaust.new()
	viewport.add_child(exhaust)
	var state := {"mode": "freedom_flight", "frame_id": "2", "attached": false, "positive_force_body": PackedFloat64Array([0, 0, 0]), "negative_force_body": PackedFloat64Array([0, 0, 360000]), "positive_force_ratings": PackedFloat64Array([112000, 208000, 72000]), "negative_force_ratings": PackedFloat64Array([112000, 144000, 360000])}
	for mode in ["default_mix", "premultiplied"]:
		if mode == "premultiplied":
			var compositor := ShaderMaterial.new()
			compositor.shader = preload("res://shaders/native_close_composite.gdshader")
			container.material = compositor
		for strength in [1.0, 0.1, 0.0]:
			state.negative_force_body[2] = 360000.0 * strength
			exhaust.update_applied(state, 0.0, false)
			await process_frame
			await process_frame
			await RenderingServer.frame_post_draw
			var raw := viewport.get_texture().get_image()
			var final := root.get_texture().get_image()
			for item in [["raw", raw], ["composite", final]]:
				var path := output.path_join(mode + "-" + str(strength) + "-" + item[0] + ".png")
				if item[1].save_png(path) != OK: quit(1); return
				records.append({"mode": mode, "intensity": strength, "attachment": item[0], "file": path.get_file(), "sha256": FileAccess.get_sha256(path), "stats": stats(item[1])})
	var report := FileAccess.open(output.path_join("capture.json"), FileAccess.WRITE)
	report.store_string(JSON.stringify({"scope": "Synthetic exhaust component through transparent SubViewport; same camera, geometry, applied-force inputs and phase0; canvas compositing is the only candidate change", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "shader_sha256": FileAccess.get_sha256("res://shaders/native_main_exhaust.gdshader"), "compositor_sha256": FileAccess.get_sha256("res://shaders/native_close_composite.gdshader"), "script_sha256": FileAccess.get_sha256("res://studies/captures/native_exhaust_alpha_capture.gd"), "license": "BSD-3-Clause", "attachments": records}, "\t") + "\n")
	report.close()
	container.free()
	# Matching raw attachments isolate the compositor. Low firing must remain
	# visible after compositing, while zero applied propulsion remains dark.
	if records[2].sha256 != records[8].sha256 or records[9].stats.signal_pixels <= records[3].stats.signal_pixels or records[11].stats.signal_pixels != 0:
		push_error("Exhaust compositor regression: low thrust lost or idle plume shown")
		quit(1)
		return
	print(JSON.stringify(records))
	quit(0)
