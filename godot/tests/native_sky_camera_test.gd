extends SceneTree
## Analytic raster calibration, not a generated world or simulation fixture.
const PIXELS := Vector2i(3840, 2160)
const RADIUS := 6000000.0
const ALTITUDE := 2.0
const FOV := 75.0
var failures := 0

func check(ok: bool, message: String) -> bool:
	if not ok:
		failures += 1
		push_error(message)
	return ok

static func horizon(radius: float, altitude: float, offset: float, pixels: Vector2i, fov: float) -> float:
	for value in [radius, altitude, offset, fov]:
		if not is_finite(value): return NAN
	var height := altitude + offset
	if radius <= 0 or radius > 1e12 or height < 0 or height > 1e9 or fov <= 0 or fov >= 180 or pixels.x <= 0 or pixels.y <= 0 or pixels.x > 8192 or pixels.y > 8192: return NAN
	return pixels.y * 0.5 + pixels.y / (2.0 * tan(deg_to_rad(fov) * 0.5)) * sqrt(height * (2.0 * radius + height)) / radius

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 1 or not args[0].is_absolute_path(): quit(2); return
	# Invalid/nonfinite analytic inputs refuse before constructing a GPU scene.
	for value in [NAN, INF, -1.0, 0.0]:
		check(is_nan(horizon(value, ALTITUDE, 0, PIXELS, FOV)), "Invalid radius accepted")
	for value in [NAN, INF, 0.0, 180.0]:
		check(is_nan(horizon(RADIUS, ALTITUDE, 0, PIXELS, value)), "Invalid field of view accepted")
	for pixels in [Vector2i.ZERO, Vector2i(-1, 2160), Vector2i(3840, 8193)]:
		check(is_nan(horizon(RADIUS, ALTITUDE, 0, pixels, FOV)), "Invalid dimensions accepted")
	check(is_nan(horizon(RADIUS, ALTITUDE, NAN, PIXELS, FOV)) and is_nan(horizon(RADIUS, ALTITUDE, -3, PIXELS, FOV)), "Invalid camera height accepted")
	if failures > 0: quit(1); return
	if not check(DisplayServer.get_name() == "X11" and RenderingServer.get_current_rendering_method() == "gl_compatibility" and AudioServer.get_driver_name() == "Dummy", "Calibration requires X11 Compatibility and Dummy audio"): quit(1); return
	root.size = PIXELS
	var scene := Node3D.new()
	root.add_child(scene)
	var material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = FileAccess.get_file_as_string("res://shaders/native_navigation_sky.gdshader")
	material.shader = shader
	for field in ["radius", "altitude", "scale_height", "sea_density", "atmosphere_edge", "sun_angular_radius", "sun_visibility"]:
		material.set_shader_parameter(field, {"radius": RADIUS, "altitude": ALTITUDE, "sun_angular_radius": 0.03, "sun_visibility": 1.0}.get(field, 0.0))
	material.set_shader_parameter("atmosphere_tint", Color.BLACK)
	material.set_shader_parameter("sun_direction", Vector3(0, 0, -1))
	material.set_shader_parameter("sun_color", Color.WHITE)
	var sky := Sky.new()
	sky.sky_material = material
	sky.radiance_size = Sky.RADIANCE_SIZE_32
	sky.process_mode = Sky.PROCESS_MODE_REALTIME
	var environment := Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	environment.tonemap_exposure = 1.0
	environment.glow_enabled = false
	environment.fog_enabled = false
	var world := WorldEnvironment.new()
	world.environment = environment
	scene.add_child(world)
	var camera := Camera3D.new()
	camera.keep_aspect = Camera3D.KEEP_HEIGHT
	camera.fov = FOV
	camera.near = 0.05
	camera.far = 1000.0
	scene.add_child(camera)
	camera.current = true
	var rows: Array = []
	for offset in [0.0, 9.0]:
		camera.position = Vector3(0, offset, 0)
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		if not check(image.get_size() == PIXELS, "Actual raster dimensions changed"): break
		var filename := "camera-y-%d.png" % int(offset)
		check(image.save_png(args[0].path_join(filename)) == OK, "Image write failed")
		var x: int = PIXELS.x / 2
		var last := -1
		for y in PIXELS.y:
			var color := image.get_pixel(x, y)
			if minf(color.r, minf(color.g, color.b)) >= 0.9: last = y
		if not check(last >= PIXELS.y / 2 and last + 1 < PIXELS.y, "Solar clip edge unavailable"): break
		var dark := image.get_pixel(x, last + 1)
		check(maxf(dark.r, maxf(dark.g, dark.b)) <= 0.1, "Solar edge is not a binary ground clip")
		var expected := horizon(RADIUS, ALTITUDE, offset, PIXELS, FOV)
		var measured := last + 1.0 # Midpoint between bright/dark pixel centres.
		check(absf(measured - expected) <= 1.0, "Sky horizon misses actual camera height by more than one pixel")
		var projection := camera.get_camera_projection()
		var columns: Array = []
		for column in [projection.x, projection.y, projection.z, projection.w]: columns.append([column.x, column.y, column.z, column.w])
		var axes: Array = []
		for axis in [camera.basis.x, camera.basis.y, camera.basis.z]: axes.append([axis.x, axis.y, axis.z])
		rows.append({"image": filename, "sha256": FileAccess.get_sha256(args[0].path_join(filename)), "camera_position": [camera.position.x, camera.position.y, camera.position.z], "camera_basis": axes, "fov": camera.fov, "keep_aspect": camera.keep_aspect, "projection_columns": columns, "viewport": [image.get_width(), image.get_height()], "internal_scale": root.scaling_3d_scale, "last_bright_pixel_centre_y": last + 0.5, "first_dark_pixel_centre_y": last + 1.5, "measured_edge_y": measured, "expected_edge_y": expected, "error_pixels": measured - expected})
	check(rows.size() == 2, "Both camera cases must complete")
	var report := FileAccess.open(args[0].path_join("sky-result.json"), FileAccess.WRITE)
	if check(report != null, "Calibration report write failed"):
		report.store_string(JSON.stringify({"pass": failures == 0, "scope": "Two airless analytic sky camera cases; no generated world, simulation or hardware qualification", "cpu_invalid_cases_rejected": 13, "radius": RADIUS, "craft_altitude": ALTITUDE, "sun_direction": [0, 0, -1], "sun_angular_radius": 0.03, "sun_visibility": 1.0, "cases": rows, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_current_rendering_method(), "adapter": RenderingServer.get_video_adapter_name(), "shader_sha256": FileAccess.get_sha256("res://shaders/native_navigation_sky.gdshader"), "image_license": "BSD-3-Clause; generated from project shader without external assets"}, "  ") + "\n")
		report.close()
	scene.queue_free()
	for frame in 3:
		await process_frame
		await RenderingServer.frame_post_draw
	print("Native sky camera: %d failures" % failures)
	quit(0 if failures == 0 else 1)
