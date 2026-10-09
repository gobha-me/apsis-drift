extends SceneTree
## Actual authored boarding playthrough; C++ owns every route tick and pose.
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const WalkCapture = preload("res://studies/captures/native_walk_capture.gd")
class CaptureShell extends "res://scripts/native/native_start_shell.gd":
	func _ready() -> void:
		pass

var owner: Variant
var shell: Control
var output := ""
var captures: Array = []
var failed := false


func _initialize() -> void:
	call_deferred("run")


func freeze_view() -> void:
	var view: Control = shell.current_view
	view.set_process(false)
	view.set_process_input(false)
	view.set_process_unhandled_input(false)
	view.set_process_unhandled_key_input(false)
	if view is FlightView:
		view.player_input.set_process_input(false)
		view.player_input.set_process_unhandled_input(false)
		view.controls_menu.set_process_input(false)


func fail(message: String) -> void:
	failed = true
	push_error(message)


func walk_ticks(count: int, controls: PackedFloat64Array) -> bool:
	var view: Control = shell.current_view
	if not view is WalkView:
		fail("Walking presentation is required for route advancement")
		return false
	if view.paused: view.resume_requested()
	if view.paused:
		fail("The neutral walking view could not resume")
		return false
	for tick in count:
		if not view.advance_requested(1.0 / 120.0, controls):
			fail(view.error)
			return false
	return true


func snapshot(label: String) -> void:
	freeze_view()
	var before_walk: Dictionary = owner.get_freedom_walk_state()
	var before_flight: Dictionary = owner.get_freedom_flight_state()
	var before_boarding: Dictionary = owner.get_freedom_boarding_state()
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	if owner.get_freedom_walk_state() != before_walk or owner.get_freedom_flight_state() != before_flight or owner.get_freedom_boarding_state() != before_boarding:
		fail("Rendering waits advanced the authoritative journey")
		return
	var image := root.get_texture().get_image()
	var filename := "boarding-%s.png" % label
	if image == null or image.is_empty() or image.save_png(output.path_join(filename)) != OK:
		fail("Could not save boarding capture " + label)
		return
	var view: Control = shell.current_view
	var camera: Camera3D = view.camera
	var row := {"label": label, "file": filename, "sha256": FileAccess.get_sha256(output.path_join(filename)), "width": image.get_width(), "height": image.get_height(), "tick": before_flight.tick, "checksum": before_flight.checksum, "view": "walk" if view is WalkView else "flight", "camera_position": [camera.position.x, camera.position.y, camera.position.z], "camera_basis_columns": [[camera.basis.x.x, camera.basis.x.y, camera.basis.x.z], [camera.basis.y.x, camera.basis.y.y, camera.basis.y.z], [camera.basis.z.x, camera.basis.z.y, camera.basis.z.z]]}
	if not before_walk.is_empty():
		row["foot_position_metres"] = Array(before_walk.foot_position_metres)
		row["eye_position_metres"] = Array(before_walk.eye_position_metres)
	if not before_boarding.is_empty():
		row["boarding_state"] = before_boarding.state
		row["route_phase"] = before_boarding.phase
		row["route_progress"] = before_boarding.progress
		row["eye_craft"] = Array(before_boarding.eye_craft)
	captures.append(row)


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or DisplayServer.get_name() == "headless":
		fail("Expected prepared assets, absolute output directory and rendering display")
		quit(1)
		return
	output = args[1]
	if DirAccess.make_dir_recursive_absolute(output) != OK:
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	owner = ClassDB.instantiate("FreedomBridge")
	root.size = Vector2i(1280, 720)
	shell = CaptureShell.new()
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": args[0]}):
		fail(shell.error)
		quit(1)
		return
	freeze_view()
	if not walk_ticks(2960, PackedFloat64Array([0.0, -1.0, 0.0])) or not walk_ticks(1, PackedFloat64Array([0.0, 0.0, PI / 2.0])):
		quit(1)
		return
	var original_model: Node3D = shell.current_view.staged_model
	var entry: Dictionary = owner.get_freedom_walk_state()
	await snapshot("d1-entry")
	shell.current_view.board_requested()
	if owner.get_freedom_boarding_state().get("state") != "boarding":
		fail("Nearby Board action did not begin: " + str(owner.get_last_error()))
		quit(1)
		return
	var elapsed := 0
	for point in [["approach", 60], ["hatch", 120], ["ladder", 300], ["cabin", 660], ["seat-moving", 960], ["seated", 1440]]:
		if not walk_ticks(int(point[1]) - elapsed, PackedFloat64Array([0.0, 0.0, PI / 2.0])):
			quit(1)
			return
		elapsed = point[1]
		if point[0] == "seated":
			shell.switch_journey_view()
			freeze_view()
			if not shell.current_view is FlightView or shell.current_view.staged_model != original_model:
				fail("Seat handoff replaced the session's ship model")
				quit(1)
				return
			# Resume only presentation controls to expose the cockpit; processing
			# remains disabled, so rendering never advances the flight clock.
			shell.current_view.toggle_pause()
			shell.current_view.update_view(0.0)
		await snapshot(point[0])
		if failed: quit(1); return
	shell.current_view.unboard_requested()
	shell.switch_journey_view()
	freeze_view()
	if not shell.current_view is WalkView or shell.current_view.staged_model != original_model:
		fail("Unboard did not reuse the same ship model")
		quit(1)
		return
	elapsed = 0
	for point in [["unboard-cabin", 720], ["unboard-ladder", 1140], ["station-return", 1440]]:
		if not walk_ticks(int(point[1]) - elapsed, PackedFloat64Array([0.0, 0.0, PI / 2.0])):
			quit(1)
			return
		elapsed = point[1]
		await snapshot(point[0])
		if failed: quit(1); return
	var returned: Dictionary = owner.get_freedom_walk_state()
	if returned.foot_position_metres != entry.foot_position_metres or returned.heading_radians != entry.heading_radians:
		fail("Reverse route did not restore the original supported actor")
		quit(1)
		return
	var sources := {}
	for name in ["studies/captures/native_boarding_capture.gd", "scripts/native/native_walk_view.gd", "scripts/native/native_flight_view.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_station_view.gd", "scripts/characters/hopper_presentation.gd", "bin/libapsis_freedom_bridge.so"]:
		sources[name] = FileAccess.get_sha256("res://" + name)
	var report := FileAccess.open(output.path_join("capture.json"), FileAccess.WRITE)
	if report == null: quit(1); return
	report.store_string(JSON.stringify({"schema_version": 1, "scope": "Playable authored boarding and reverse unboard on one authoritative C++ session; body proxy and corridor are authored gameplay approximations, not anatomical or continuous-collision certification", "seed": "42", "sources_sha256": sources, "selected_binding": WalkCapture.binding_metadata(owner.get_freedom_craft_binding()), "station_sha256": WalkView.StationPresentation.STATION_HASH, "render_path": "Actual NewGame42, public supported walk2960ticks plus one look tick, Board and Unboard; render waits freeze time; same model and session throughout", "asset_package": "freedom-starter-01", "licenses": ["LicenseRef-Apsis-Station-Kit-Output", "LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"], "license_records": "assets/native/freedom-starter-01/licenses", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "captures": captures}, "\t") + "\n")
	report.close()
	shell.free()
	print("Native authored boarding captures: %d" % captures.size())
	quit(0)
