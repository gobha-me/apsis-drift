extends SceneTree
## Explicit rendered review, separate from the headless contract runner.
class CaptureShell extends "res://native_start_shell.gd":
	func _ready() -> void:
		pass


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not DirAccess.dir_exists_absolute(args[1]):
		push_error("Expected absolute prepared assets and existing capture directory")
		quit(1)
		return
	if DisplayServer.get_name() == "headless":
		push_error("Rendered station capture requires a rendering display driver")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	if not bridge.initialize_freedom_new_game("42"):
		quit(1)
		return
	var shell := CaptureShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	shell.bridge = bridge
	shell.selected = bridge.get_freedom_start()
	shell.assets_root = args[0]
	shell.build_view(shell.dock_geometry(shell.selected))
	var captures := []
	var cases := [
		["overview", 100.0, 0.65, 0.3, Vector2i(1920, 1080)],
		["front", 100.0, 0.0, 0.0, Vector2i(640, 360)],
		["rear", 100.0, PI, 0.0, Vector2i(640, 360)],
		["side", 100.0, PI / 2, 0.0, Vector2i(640, 360)],
		["above", 100.0, 0.0, 1.4, Vector2i(640, 360)],
		["ports_below", 30.0, 0.0, -1.4, Vector2i(1920, 1080)],
		["distant", 500.0, 0.65, 0.3, Vector2i(640, 360)],
		["too_close", 2.0, 0.0, 0.0, Vector2i(640, 360)],
	]
	for item in cases:
		root.size = item[4]
		shell.station_view.inspect(item[1], item[2], item[3])
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		var viewport: SubViewport = shell.station_view.get_viewport()
		if viewport.size != Vector2i(shell.size) or not shell.station_view.valid_dimensions(viewport.size):
			push_error("Station viewport failed resize registration")
			quit(1)
			return
		var filename: String = item[0] + ".png"
		if image == null or image.is_empty() or image.save_png(args[1].path_join(filename)) != OK:
			push_error("Station rendered capture failed")
			quit(1)
			return
		captures.append({"file": filename, "sha256": FileAccess.get_sha256(args[1].path_join(filename)),
			"width": image.get_width(), "height": image.get_height(), "logical_viewport": [viewport.size.x, viewport.size.y], "range_metres": item[1], "yaw": item[2], "pitch": item[3],
			"ports": [shell.station_view.port_cue(1), shell.station_view.port_cue(2)]})
	var report := FileAccess.open(args[1].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	var sources := {}
	for path in ["native_station_capture.gd", "native_start_shell.gd", "native_station_view.gd", "native_host_sky.gdshader", "bin/libapsis_freedom_bridge.so"]:
		sources[path] = FileAccess.get_sha256("res://" + path)
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Rendered station inspection; no gameplay or hardware-performance acceptance", "station_sha256": shell.station_view.STATION_HASH, "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "start": shell.selected, "renderer": RenderingServer.get_video_adapter_name(), "display": DisplayServer.get_name(), "engine": Engine.get_version_info(), "sources_sha256": sources, "captures": captures}, "\t") + "\n")
	shell.free()
	print("Station rendered captures: %d" % captures.size())
	quit(0)
