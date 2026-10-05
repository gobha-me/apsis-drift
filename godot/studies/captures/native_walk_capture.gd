extends SceneTree
## Actual fresh New Game/input traversal. No actor relocation or art substitution.
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
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or DisplayServer.get_name() == "headless":
		push_error("Expected prepared assets, absolute output directory and rendering display")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(args[1])
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	root.size = Vector2i(1280, 720)
	var shell := CaptureShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": args[0]}):
		push_error(shell.error)
		quit(1)
		return
	var view: Control = shell.current_view
	if view == null or not view is WalkView:
		push_error("The staged New Game did not select the walking view")
		quit(1)
		return
	# Keep rendering waits/input callbacks from advancing the controlled route.
	view.set_process(false)
	view.set_process_input(false)
	var selected_binding := binding_metadata(owner.get_freedom_craft_binding())
	var captures := []
	# Heading PI/2 views west; right=-1 at heading0 moves west. Looking and
	# movement are real command requests, never changes to the saved actor pose.
	for phase in ["hub", "workshop", "d1-edge", "return"]:
		var steps := 0
		var direction := -1.0
		if phase == "workshop":
			steps = 960
		elif phase == "d1-edge":
			steps = 640
		elif phase == "return":
			direction = 1.0
			steps = 1600
		for n in steps:
			if phase == "return" and owner.get_freedom_walk_state().foot_position_metres[0] >= -0.01:
				break
			if not view.advance_requested(1.0 / 120.0, PackedFloat64Array([0.0, direction, 0.0])):
				push_error(view.error)
				quit(1)
				return
		# Fresh hub captures the exact ordinary spawn, already facing D1. Later
		# turns advance the same actor/ship clock; camera yaw consumes C++ heading.
		if phase != "hub" and not view.advance_requested(1.0 / 120.0, PackedFloat64Array([0.0, 0.0, PI / 2.0])):
			quit(1)
			return
		# Pitch is the ordinary presentation-only head look. Inspect the real open
		# well from the supported stop without moving the authoritative actor.
		view.pitch = -0.85 if phase == "d1-edge" else 0.0
		view.update_view()
		var before_render: Dictionary = owner.get_freedom_walk_state()
		await process_frame
		await process_frame
		await RenderingServer.frame_post_draw
		if owner.get_freedom_walk_state() != before_render:
			push_error("Rendering wait advanced the authoritative actor")
			quit(1)
			return
		var picture := root.get_texture().get_image()
		var filename := "station-walk-%s.png" % phase
		if picture == null or picture.is_empty() or picture.save_png(args[1].path_join(filename)) != OK:
			quit(1)
			return
		var current: Dictionary = owner.get_freedom_walk_state()
		if (phase == "workshop" and current.foot_position_metres[0] > -15.0) or (phase == "d1-edge" and current.foot_position_metres[0] > -20.0) or current.tick != owner.get_freedom_flight_state().tick:
			push_error("Capture did not traverse the supported route with shared time")
			quit(1)
			return
		captures.append({"phase": phase, "file": filename, "sha256": FileAccess.get_sha256(args[1].path_join(filename)), "width": picture.get_width(), "height": picture.get_height(), "tick": current.tick, "actor_id": current.actor_id, "foot_position_metres": Array(current.foot_position_metres), "heading_radians": current.heading_radians, "look_pitch_radians": view.pitch, "camera_position": [view.camera.position.x, view.camera.position.y, view.camera.position.z], "craft_tick": owner.get_freedom_flight_state().tick})
	var sources := {}
	for name in ["studies/captures/native_walk_capture.gd", "scripts/native/native_walk_view.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_station_view.gd", "scripts/characters/hopper_presentation.gd", "bin/libapsis_freedom_bridge.so"]:
		sources[name] = FileAccess.get_sha256("res://" + name)
	var report := FileAccess.open(args[1].path_join("capture.json"), FileAccess.WRITE)
	if report == null:
		quit(1)
		return
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Fresh supported station walk, actual shared-clock actor input; no hatch/climb/boarding/seat or hardware performance acceptance", "seed": "42", "sources_sha256": sources, "station_sha256": WalkView.StationPresentation.STATION_HASH, "wayfarer_sha256": selected_binding.operating_model_sha256, "stowed_model_sha256": selected_binding.stowed_model_sha256, "operating_model_sha256": selected_binding.operating_model_sha256, "selected_binding": selected_binding, "render_path": "matched native walking view; presentation-only pitch; explicit shared-clock route commands", "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "captures": captures}, "\t") + "\n")
	report.close()
	shell.free()
	print("Native station walking captures: %d" % captures.size())
	quit(0)
