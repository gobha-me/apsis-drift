extends SceneTree
## Explicit rendered review, separate from the headless contract runner.
const WalkView = preload("res://scripts/native/native_walk_view.gd")
class CaptureShell extends "res://scripts/native/native_start_shell.gd":
	func _ready() -> void:
		pass


static func binding_metadata(binding: Dictionary) -> Dictionary:
	var result := {}
	for key in ["profile", "hardware_known", "operating_model_sha256", "stowed_model_sha256", "frame_sha256", "contact_sha256", "operating_atlas_surface"]:
		result[key] = binding[key]
	return result


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
	root.size = Vector2i(1920, 1080)
	var shell := CaptureShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not shell.select_start(bridge, {"mode": "new_game", "value": "42", "assets": args[0]}):
		push_error(shell.error)
		quit(1)
		return
	var view: Control = shell.current_view
	if view == null or not view is WalkView:
		push_error("The staged New Game did not select the walking view")
		quit(1)
		return
	view.set_process(false)
	view.set_process_input(false)
	# A station-local inspection camera is presentation only; the actual actor
	# remains at its ordinary spawn and the matched craft remains installed.
	view.camera.current = false
	view.hud_scroll.hide()
	var station: Node3D = view.station
	station.set_process_unhandled_input(false)
	station.camera = Camera3D.new()
	station.camera.name = "CaptureStationInspectionCamera"
	station.camera.near = 0.05
	station.camera.far = 2000.0
	station.camera.fov = 75.0
	station.add_child(station.camera)
	station.camera.current = true
	var initial_actor: Dictionary = bridge.get_freedom_walk_state()
	var initial_flight: Dictionary = bridge.get_freedom_flight_state()
	var selected_binding := binding_metadata(bridge.get_freedom_craft_binding())
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
		if not station.inspect(item[1], item[2], item[3]):
			push_error("Station capture camera request refused")
			quit(1)
			return
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		if bridge.get_freedom_walk_state() != initial_actor or bridge.get_freedom_flight_state() != initial_flight:
			push_error("Station inspection changed the actor or shared C++ clock")
			quit(1)
			return
		var image := root.get_texture().get_image()
		var viewport: SubViewport = station.get_viewport()
		if viewport.size != Vector2i(view.size) or not station.valid_dimensions(viewport.size):
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
			"ports": [station.port_cue(1), station.port_cue(2)]})
	var report := FileAccess.open(args[1].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	var sources := {}
	for path in ["studies/captures/native_station_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_station_view.gd", "scripts/native/native_walk_view.gd", "scripts/native/native_flight_view.gd", "scripts/characters/hopper_presentation.gd", "bin/libapsis_freedom_bridge.so"]:
		sources[path] = FileAccess.get_sha256("res://" + path)
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Rendered station inspection of staged matched starting assembly; actor and C++ clock unchanged; no gameplay or hardware-performance acceptance", "station_sha256": station.STATION_HASH, "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "start": initial_actor, "selected_binding": selected_binding, "operating_model_sha256": selected_binding.operating_model_sha256, "wayfarer_sha256": selected_binding.operating_model_sha256, "stowed_model_sha256": selected_binding.stowed_model_sha256, "render_path": "matched native walking view; station-local inspection camera", "actor_pose": "unchanged ordinary New Game station actor; camera does not relocate actor", "tick": initial_actor.tick, "craft_tick": initial_flight.tick, "renderer": RenderingServer.get_video_adapter_name(), "display": DisplayServer.get_name(), "engine": Engine.get_version_info(), "sources_sha256": sources, "captures": captures}, "\t") + "\n")
	shell.free()
	print("Station rendered captures: %d" % captures.size())
	quit(0)
