extends SceneTree
## Private demo-reel capture. C++ owns walking; hardware studies are editorial.
const Operating = preload("res://scripts/ships/wayfarer_operating_view.gd")
const Walk = preload("res://scripts/native/native_walk_view.gd")
const FPS := 24
const SHOT_SECONDS := 14
const SHOT_FRAMES := FPS * SHOT_SECONDS
const SHOTS := ["berth-reveal", "wayfarer-reveal", "hub-walk", "d1-approach", "roof-study", "ladder-study", "inner-study", "seat-study", "d1-closure", "homeward-walk"]

var owner: Variant
var view: Node3D
var camera: Camera3D
var label: Label
var output_dir := ""
var review := false
var initial_flight: Dictionary
var initial_actor: Dictionary
var pictures: Array = []
var chapters: Array = []

func _initialize() -> void:
	call_deferred("run")

func fail(message: String) -> void:
	push_error(message)
	quit(1)

func state_receipt() -> Dictionary:
	var actor: Dictionary = owner.get_freedom_walk_state()
	var flight: Dictionary = owner.get_freedom_flight_state()
	return {"tick": actor.tick, "flight_tick": flight.tick, "checksum": flight.checksum, "actor_id": actor.actor_id, "station_id": actor.station_id, "craft_id": actor.craft_id, "foot_position_metres": Array(actor.foot_position_metres), "heading_radians": actor.heading_radians, "dropped_seconds": actor.dropped_seconds}

func save_checkpoint(name: String) -> Dictionary:
	var filename := name + ".json"
	var path := output_dir.path_join(filename)
	if FileAccess.file_exists(path) or not owner.save_freedom_as(path):
		return {"error": "Journey Save As failed: " + str(owner.get_last_error())}
	return {"file": filename, "sha256": FileAccess.get_sha256(path), "state": state_receipt()}

func walking_shot(shot: int) -> bool:
	return shot == 2 or shot == 3 or shot == 9

func advance_walking(shot: int, frame: int) -> bool:
	var direction := 0.0
	if shot == 2 and frame < 8 * FPS:
		direction = 1.0
	elif shot == 3 and frame < 3 * FPS:
		direction = 1.0
	for tick in 5:
		if shot == 9:
			var previous: Dictionary = owner.get_freedom_walk_state()
			direction = -1.0 if previous.foot_position_metres[0] < -0.02 else 0.0
		if not owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([direction, 0.0, PI / 2.0])):
			fail("Real C++ walking failed: " + str(owner.get_last_error()))
			return false
	var actor: Dictionary = owner.get_freedom_walk_state()
	var flight: Dictionary = owner.get_freedom_flight_state()
	if not Walk.valid_state(actor) or actor.tick != flight.tick or actor.dropped_seconds != 0.0:
		fail("Actor/body shared clock or projected walking state is invalid")
		return false
	view.station.transform = Transform3D(actor.station_basis, actor.station_position)
	view.ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	return true

func point_camera(start: Vector3, finish: Vector3, target: Vector3, fraction: float, station_frame: bool) -> void:
	var transform: Transform3D = view.station.transform if station_frame else view.ship.transform
	camera.position = transform * start.lerp(finish, smoothstep(0.0, 1.0, fraction))
	camera.look_at(transform * target, transform.basis.y)

func set_frame(shot: int, frame: int) -> bool:
	var fraction := float(frame) / float(SHOT_FRAMES - 1)
	if walking_shot(shot):
		if not advance_walking(shot, frame):
			return false
		var actor: Dictionary = owner.get_freedom_walk_state()
		var pitch := 0.0
		if shot == 3:
			pitch = lerpf(0.0, -0.65, smoothstep(0.28, 0.62, fraction))
		elif shot == 9:
			# Looking back along the return path is presentation framing only.
			camera.transform = Transform3D(actor.station_basis * Basis(Vector3.UP, -PI / 2.0), actor.actor_eye_position)
			return true
		camera.transform = Transform3D(actor.station_basis * Basis(Vector3.UP, actor.heading_radians) * Basis(Vector3.RIGHT, pitch), actor.actor_eye_position)
		return true
	match shot:
		0:
			point_camera(Vector3(32, -24, 48), Vector3(20, -18, 48), Vector3(-4, -3, 0), fraction, true)
		1:
			point_camera(Vector3(10, 4, 13), Vector3(5, 3, 11), Vector3(0, 1, 1), fraction, false)
		4:
			var knot := clampi(int(floor((float(frame) / FPS - 2.0) * 2.0)), 0, 20)
			if not view.set_channel("roof_transfer", float(knot) / 20.0):
				return false
			point_camera(Vector3(0, 1.25, 4.65), Vector3(0.13, 1.3, 4.7), Vector3(0, 2.95, 5.2), fraction, false)
		5:
			var knot := 20 - clampi(int(floor((float(frame) / FPS - 2.0) * 2.0)), 0, 20)
			if not view.set_channel("roof_transfer", float(knot) / 20.0):
				return false
			point_camera(Vector3(0, 1.6, 4.65), Vector3(0.16, 1.75, 4.65), Vector3(0, 2.2, 5.6), fraction, false)
		6:
			var knot := clampi(int(floor((float(frame) / FPS - 2.0) * 2.0)), 0, 20)
			if not view.set_channel("inner_door", float(knot) / 20.0):
				return false
			point_camera(Vector3(0, 1.5, 5), Vector3(0.15, 1.5, 4.7), Vector3(0, 1, 2), fraction, false)
		7:
			var knot := clampi(int(floor((float(frame) / FPS - 2.0) * 2.0)), 0, 20)
			if not view.set_channel("seat_boarding", float(knot) / 20.0):
				return false
			point_camera(Vector3(0, 1.6, 0.5), Vector3(0.22, 1.65, 0.35), Vector3(0, 0.9, -2.25), fraction, false)
		8:
			# Authored station-local knots are qualified for this local interpolation.
			var progress := smoothstep(0.14, 0.86, fraction) * 160.0 / 190.0
			if not view.set_station_progress(progress):
				return false
			point_camera(Vector3(-21.9, 1.7, 0), Vector3(-21.5, 1.9, 0.15), Vector3(-24, 0.1, 0), fraction, true)
	return true

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3 or args.size() > 4 or (args.size() == 4 and args[3] != "review"):
		fail("Expected absolute prepared starter, operating, empty output directory; optional review")
		return
	for path in args.slice(0, 3):
		if not path.is_absolute_path() or not DirAccess.dir_exists_absolute(path):
			fail("Capture paths must be existing absolute directories")
			return
	output_dir = args[2]
	var directory := DirAccess.open(output_dir)
	if not directory.get_files().is_empty() or not directory.get_directories().is_empty() or DisplayServer.get_name() == "headless":
		fail("Capture requires an empty output directory and rendering display")
		return
	review = args.size() == 4
	if GDExtensionManager.load_extension("res://bin/freedom.gdextension") != OK:
		fail("Cannot load the existing authoritative bridge")
		return
	owner = ClassDB.instantiate("FreedomBridge")
	if owner == null or not owner.initialize_freedom_new_game("42"):
		fail("Cannot initialize C++ New Game")
		return
	initial_flight = owner.get_freedom_flight_state()
	initial_actor = owner.get_freedom_walk_state()
	if not Walk.valid_state(initial_actor) or initial_actor.tick != "0":
		fail("Expected fresh valid station walker")
		return
	root.size = Vector2i(1920, 1080)
	view = Operating.new()
	root.add_child(view)
	if not view.initialize(owner, args[0], args[1]):
		fail(view.error)
		return
	camera = Camera3D.new()
	camera.near = 0.025
	camera.far = 500.0
	camera.fov = 68.0
	view.add_child(camera)
	camera.current = true
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004, 0.009, 0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6, 0.75, 0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	view.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, -30, 0)
	sun.light_energy = 1.5
	view.add_child(sun)
	var cabin_light := OmniLight3D.new()
	cabin_light.omni_range = 10
	cabin_light.light_energy = 1.0
	cabin_light.position = initial_flight.body_basis * Vector3(0, 1.9, 0)
	view.add_child(cabin_light)
	label = Label.new()
	label.position = Vector2(24, 24)
	label.add_theme_font_size_override("font_size", 20)
	label.add_theme_color_override("font_shadow_color", Color.BLACK)
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(label)
	var started := Time.get_ticks_usec()
	for shot in SHOTS.size():
		if not view.set_pose("rest") or not view.set_station_progress(0.0):
			fail("Initial hardware state refused")
			return
		if shot == 5 and not view.set_pose("transfer_deployed"):
			fail("Qualified transfer endpoint refused")
			return
		var snapshot := state_receipt()
		var chapter := {"id": SHOTS[shot], "start_frame": shot * SHOT_FRAMES, "frames": SHOT_FRAMES, "seconds": SHOT_SECONDS, "authority": "C++ shared-tick station walking" if walking_shot(shot) else "editorial frozen-world camera; source-bound hardware study", "start_state": snapshot, "samples": []}
		label.text = "C++ station locomotion · " + SHOTS[shot] if walking_shot(shot) else ("Qualified hardware study · admitted pose steps" if shot >= 4 and shot <= 7 else "Editorial camera · " + SHOTS[shot])
		if shot == 8:
			label.text = "Qualified D1 closure study · attached station hardware"
		if walking_shot(shot):
			chapter["start_save"] = save_checkpoint(SHOTS[shot] + "-start")
			if chapter.start_save.has("error"):
				fail(chapter.start_save.error)
				return
		for frame in SHOT_FRAMES:
			if not set_frame(shot, frame):
				fail("Frame refused: " + SHOTS[shot] + " / " + str(frame) + " / " + view.error)
				return
			var sample := frame == 0 or frame == SHOT_FRAMES / 2 or frame == SHOT_FRAMES - 1
			if review and not sample:
				continue
			await process_frame
			await RenderingServer.frame_post_draw
			var picture := root.get_texture().get_image()
			var absolute_frame := shot * SHOT_FRAMES + frame
			var filename := "frame-%06d.png" % absolute_frame
			var path := output_dir.path_join(filename)
			if picture == null or picture.get_width() != 1920 or picture.get_height() != 1080 or FileAccess.file_exists(path) or picture.save_png(path) != OK:
				fail("Frame image failed: " + filename)
				return
			pictures.append({"frame": absolute_frame, "file": filename, "sha256": FileAccess.get_sha256(path)})
			if sample:
				var moving := {}
				for id in view.moving_nodes:
					moving[id] = Operating.columns(view.moving_nodes[id].transform)
				var closure := {}
				for id in view.closure_nodes:
					closure[id] = Operating.columns(view.closure_nodes[id].transform)
				chapter.samples.append({"frame": absolute_frame, "state": state_receipt(), "camera": Operating.columns(camera.transform), "ship": Operating.columns(view.ship.transform), "station": Operating.columns(view.station.transform), "operating_transforms": moving, "closure_transforms": closure})
			if frame % FPS == 0:
				print("Station reel %s frame %d/%d" % [SHOTS[shot], frame, SHOT_FRAMES])
		chapter["end_state"] = state_receipt()
		if not walking_shot(shot) and chapter.end_state != snapshot:
			fail("Editorial study changed C++ world, actor or clock")
			return
		if walking_shot(shot):
			if int(chapter.end_state.tick) - int(snapshot.tick) != SHOT_SECONDS * 120:
				fail("Walking did not advance exactly one shared120Hz clock")
				return
			chapter["end_save"] = save_checkpoint(SHOTS[shot] + "-end")
			if chapter.end_save.has("error"):
				fail(chapter.end_save.error)
				return
		chapters.append(chapter)
	var final_actor: Dictionary = owner.get_freedom_walk_state()
	if abs(final_actor.foot_position_metres[0]) > 0.02 or final_actor.tick != "5040":
		fail("Actual walking path failed to return to the hub with expected shared tick")
		return
	var inputs := {}
	for filename in ["prepared.json", "wayfarer-operating-02.json", "wayfarer-operating-02.glb", "contact.json", "station-closure.json", "qualification.json", "provenance.json", "station-d1-clearance-01.glb"]:
		inputs[filename] = FileAccess.get_sha256(args[1].path_join(filename))
	var sources := {}
	for filename in ["studies/captures/reel_station_capture.gd", "scripts/ships/wayfarer_operating_view.gd", "scripts/native/native_walk_view.gd", "scripts/native/native_station_view.gd", "bin/libapsis_freedom_bridge.so"]:
		sources[filename] = FileAccess.get_sha256("res://" + filename)
	var starter_inputs := {}
	for filename in ["prepared.json", "station-reference.glb", "station-reference.json", "station-reference-presentation.json", "hopper-wayfarer-01.glb", "hopper-wayfarer-01.json", "authoring-lineage.json"]:
		starter_inputs[filename] = FileAccess.get_sha256(args[0].path_join(filename))
	var report := {"schema": "apsis.reel-station-capture/1", "review_only": review, "fps": FPS, "width": 1920, "height": 1080, "timeline_frames": SHOTS.size() * SHOT_FRAMES, "captured_frames": pictures.size(), "seconds": SHOTS.size() * SHOT_SECONDS, "capture_wall_seconds": (Time.get_ticks_usec() - started) / 1000000.0, "scope": "Actual C++ New Game walking and shared clock; independent editorial cameras and discrete admitted hardware poses; no completed boarding, seating, takeoff or continuous departure claim", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "operating_inputs_sha256": inputs, "starter_inputs_sha256": starter_inputs, "sources_sha256": sources, "station_model_sha256": Operating.STATION_HASH, "initial_state": chapters[0].start_state, "final_state": state_receipt(), "chapters": chapters, "images": pictures}
	var file := FileAccess.open(output_dir.path_join("render.json"), FileAccess.WRITE)
	if file == null:
		fail("Cannot write source/capture receipt")
		return
	file.store_string(JSON.stringify(report, "\t") + "\n")
	print("Station reel complete: %d source frames, tick %s" % [pictures.size(), final_actor.tick])
	quit(0)
