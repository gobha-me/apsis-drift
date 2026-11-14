extends SceneTree
## Opt-in rendered comparison of an authentic system import and its independent
## canonical save. Run on an isolated display; never hide the game window.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const INPUTS = ["system-import.json", "canonical-before.json"]
var error := ""


static func valid_dimensions(width: int, height: int) -> bool:
	return Vector2i(width, height) in [Vector2i(800, 450), Vector2i(1920, 1080), Vector2i(3840, 2160)]


static func valid_image(image: Image, expected: Vector2i) -> bool:
	if image == null or image.is_empty() or image.get_size() != expected or not valid_dimensions(expected.x, expected.y):
		return false
	# Refuse blank/uniform readback without claiming that colour variation proves
	# correct terrain, scale or recognizable relief.
	var first := image.get_pixel(0, 0)
	for y in [0, expected.y >> 1, expected.y - 1]:
		for x in [0, expected.x >> 1, expected.x - 1]:
			if image.get_pixel(x, y) != first: return true
	return false


func require(value: bool, reason: String) -> bool:
	if not value:
		if error.is_empty(): error = reason
		push_error(reason)
	return value


static func coordinates(value: Vector3) -> Array:
	return [value.x, value.y, value.z]


static func camera_record(camera: Camera3D) -> Dictionary:
	var size := camera.get_viewport().get_visible_rect().size
	return {"position_metres": coordinates(camera.global_position),
		"basis": [coordinates(camera.global_basis.x), coordinates(camera.global_basis.y), coordinates(camera.global_basis.z)],
		"fov_degrees": camera.fov, "keep_aspect": camera.keep_aspect,
		"projection": camera.projection, "near_metres": camera.near, "far_metres": camera.far,
		"viewport": [size.x, size.y]}


func settle(view: Control, owner: Variant) -> bool:
	# An accepted old cover is insufficient: service the requested camera cover,
	# including its pending uploads/morph. Only presentation time may advance.
	var near_mode: int = view.near_viewport.render_target_update_mode
	var far_mode: int = view.far_viewport.render_target_update_mode
	view.near_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	view.far_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	var requested := false
	var ready := false
	var started := Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < 60000 and view.error.is_empty() and view.terrain.error.is_empty():
		view.terrain.tick(1.0 / 60.0, view.camera.position)
		if not requested and not owner.is_stream_busy() and view.terrain.pending.is_empty() and not view.terrain.morph_active:
			if not require(owner.request_stream(view.camera.position), "Camera terrain request refused"):
				break
			requested = true
		elif requested and not owner.is_stream_busy() and view.terrain.pending.is_empty() and not view.terrain.morph_active and view.terrain.is_ready:
			ready = true
			break
		await process_frame
	view.near_viewport.render_target_update_mode = near_mode
	view.far_viewport.render_target_update_mode = far_mode
	if not require(ready, "Camera terrain did not settle within 60 seconds: " + view.error + view.terrain.error):
		return false
	# Refresh depth ranges after the completed physical mesh cover, still paused.
	view.update_view(0.0)
	return true


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if not require(args.size() == 5, "Expected absolute fixture/assets/new output directories, width and height"):
		quit(1); return
	if not require(args[3].is_valid_int() and args[4].is_valid_int() and str(args[3].to_int()) == args[3] and str(args[4].to_int()) == args[4] and valid_dimensions(args[3].to_int(), args[4].to_int()), "Unsupported capture dimensions"):
		quit(1); return
	for index in 3:
		if not require(args[index].is_absolute_path(), "Capture directories must be absolute"):
			quit(1); return
	if not require(DisplayServer.get_name() != "headless", "Capture requires an isolated rendering display"):
		quit(1); return
	var hashes := {}
	for name in INPUTS:
		var path: String = args[0].path_join(name)
		if not require(FileAccess.file_exists(path), "Missing authoritative fixture: " + name):
			quit(1); return
		hashes[name] = FileAccess.get_sha256(path)
	if not require(FileAccess.file_exists(args[1].path_join("prepared.json")), "Prepared native assets are required"):
		quit(1); return
	if not require(not DirAccess.dir_exists_absolute(args[2]) and not FileAccess.file_exists(args[2]), "Capture output must be new"):
		quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not require(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"):
		quit(1); return
	if not require(DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Cannot create capture output"):
		quit(1); return
	root.size = Vector2i(args[3].to_int(), args[4].to_int())
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var captures: Array = []
	var images: Array[Image] = []
	var canonical_state := {}
	var cameras := {}
	for name in INPUTS:
		if not require(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join(name), "assets": args[1]}), "Continue refused: " + shell.error):
			break
		var view: Control = shell.current_view
		if not require(view is FlightView and view.paused and NativeEnvironment.valid(owner.get_freedom_flight_state()), "Expected a paused, valid physical flight"):
			break
		view.set_process(false)
		view.set_process_input(false)
		view.set_process_unhandled_input(false)
		view.controls_menu.hide_menu()
		view.cockpit = false
		view.update_view(0.0)
		var before: Dictionary = owner.get_freedom_flight_state()
		if not captures.is_empty() and not require(before == canonical_state, "System import differs from independently opened canonical state"):
			break
		canonical_state = before.duplicate(true)
		if not await settle(view, owner):
			break
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		if not require(owner.get_freedom_flight_state() == before and view.paused, "Rendering changed authoritative state"):
			break
		var camera := {"near": camera_record(view.camera), "terrain": camera_record(view.terrain_camera)}
		if not captures.is_empty() and not require(camera == cameras, "Imported and canonical camera projections differ"):
			break
		cameras = camera
		var copy: String = args[2].path_join(name.get_basename() + "-resaved.json")
		if not require(owner.save_freedom_as(copy) and FileAccess.get_sha256(copy) == hashes["canonical-before.json"], "Canonical Save As differs after capture"):
			break
		var picture := root.get_texture().get_image()
		var image_name: String = name.get_basename() + ".png"
		if not require(valid_image(picture, root.size) and picture.save_png(args[2].path_join(image_name)) == OK, "Capture image unavailable, uniform or wrong size"):
			break
		images.append(picture)
		captures.append({"source": name, "image": image_name,
			"image_sha256": FileAccess.get_sha256(args[2].path_join(image_name)),
			"canonical_save_sha256": FileAccess.get_sha256(copy),
			"tick": before.tick, "checksum": before.checksum,
			"universe_seed": before.universe_seed, "system_id": before.system_id, "planet_id": before.planet_id,
			"frame_id": before.frame_id, "frame_version": before.frame_version, "terrain_policy": before.terrain_policy,
			"altitude_metres": before.altitude, "planet_radius_metres": before.planet_radius,
			"star_direction": coordinates(before.star_direction), "lighting": before.lighting,
			"camera": camera, "terrain": view.terrain.report()})
	for name in INPUTS:
		require(FileAccess.get_sha256(args[0].path_join(name)) == hashes[name], "Capture modified source fixture: " + name)
	var pixels_equal := images.size() == 2 and images[0].get_format() == images[1].get_format() and images[0].get_data() == images[1].get_data()
	require(pixels_equal, "Same-tick import/canonical pixel buffers differ or are unavailable; inspect retained images")
	var source_hashes := {}
	for path in ["studies/captures/native_frame_import_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_flight_view.gd", "scripts/native/native_environment.gd", "scripts/world/planet_stream.gd", "shaders/terrain.gdshader", "shaders/native_navigation_sky.gdshader", "shaders/native_planet_optics.gdshaderinc", "bin/libapsis_freedom_bridge.so"]:
		source_hashes[path] = FileAccess.get_sha256("res://" + path)
	var report := {"schema": "apsis.native-frame-import-capture/1", "passed": error.is_empty() and pixels_equal,
		"scope": "Frozen system-import/canonical appearance at one actual fixture pose; not a journey, relief, hardware or frame-rate qualification",
		"display": DisplayServer.get_name(), "renderer": RenderingServer.get_video_adapter_name(),
		"engine": Engine.get_version_info(), "viewport": [root.size.x, root.size.y],
		"sources_sha256": source_hashes, "fixture_sha256": hashes,
		"prepared_assets_sha256": FileAccess.get_sha256(args[1].path_join("prepared.json")),
		"licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"],
		"license_records": "assets/native/freedom-starter-01/licenses", "captures": captures,
		"pixels_equal": pixels_equal, "error": error}
	var output := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
	if require(output != null, "Capture report unavailable"):
		output.store_string(JSON.stringify(report, "\t") + "\n")
		output.close()
	shell.free()
	owner = null
	await process_frame
	print("Native frame import capture: " + ("PASS" if error.is_empty() else "FAIL"))
	quit(0 if error.is_empty() else 1)
