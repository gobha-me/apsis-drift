extends SceneTree
## Opt-in named-pose hardware images; no continuous mechanism or actor boarding claim.
const Operating = preload("res://wayfarer_operating_view.gd")

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not args[2].is_absolute_path() or not DirAccess.dir_exists_absolute(args[2]) or DisplayServer.get_name() == "headless":
		push_error("Expected prepared starter, operating and existing capture directories with rendering display")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not owner.initialize_freedom_new_game("42"):
		push_error(str(owner.get_last_error()))
		quit(1)
		return
	var initial: Dictionary = owner.get_freedom_flight_state()
	var initial_actor: Dictionary = owner.get_freedom_walk_state()
	root.size = Vector2i(1280, 720)
	var view := Operating.new()
	root.add_child(view)
	if not view.initialize(owner, args[0], args[1]):
		push_error(view.error)
		view.free()
		quit(1)
		return
	var camera := Camera3D.new()
	camera.near = 0.025
	camera.far = 160.0
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
	cabin_light.position = initial.body_basis * Vector3(0, 1.9, 0)
	view.add_child(cabin_light)
	var label := Label.new()
	label.position = Vector2(16, 16)
	label.add_theme_color_override("font_shadow_color", Color.BLACK)
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	view.add_child(label)
	var cases := [
		{"id": "roof-sealed", "pose": "rest", "station": 0.0, "camera": Vector3(0,1.25,4.65), "target": Vector3(0,2.95,5.2)},
		{"id": "roof-open", "pose": "roof_open", "station": 0.0, "camera": Vector3(0,1.25,4.65), "target": Vector3(0,2.95,5.2)},
		{"id": "ladder-deployed", "pose": "transfer_deployed", "station": 0.0, "camera": Vector3(0,1.6,4.65), "target": Vector3(0,2.2,5.6)},
		{"id": "inner-open", "pose": "inner_open", "station": 0.0, "camera": Vector3(0,1.5,5.0), "target": Vector3(0,1.0,2.0)},
		{"id": "seat-flight", "pose": "rest", "station": 0.0, "camera": Vector3(0,1.6,0.5), "target": Vector3(0,0.9,-2.25)},
		{"id": "seat-boarding", "pose": "seat_boarding", "station": 0.0, "camera": Vector3(0,1.6,0.5), "target": Vector3(0,0.9,-2.25)},
		{"id": "d1-open", "pose": "rest", "station": 0.0, "station_camera": Vector3(-21.9,1.7,0.0), "station_target": Vector3(-24.0,0.1,0.0)},
		{"id": "d1-attached-closed", "pose": "rest", "station": 160.0/190.0, "station_camera": Vector3(-21.9,1.7,0.0), "station_target": Vector3(-24.0,0.1,0.0)},
		{"id": "d1-inboard-open", "pose": "rest", "station": 0.0, "station_camera": Vector3(-18.9,1.65,0.0), "station_target": Vector3(-22.0,1.1,0.0)},
		{"id": "d1-inboard-closed", "pose": "rest", "station": 160.0/190.0, "station_camera": Vector3(-18.9,1.65,0.0), "station_target": Vector3(-22.0,1.1,0.0)}
	]
	var images := []
	for item in cases:
		if not view.set_pose(item.pose) or not view.set_station_progress(item.station):
			push_error("Qualified inspection state refused")
			quit(1)
			return
		if item.has("station_camera"):
			camera.position = view.station.transform * item.station_camera
			camera.look_at(view.station.transform * item.station_target, initial.station_basis.y)
		else:
			camera.position = initial.body_basis * item.camera
			camera.look_at(initial.body_basis * item.target, initial.body_basis.y)
		label.text = "Operating hardware inspection: " + item.id + "\nC++ actor and shared world remain unchanged; boarding is subsequent work."
		for frame in 4:
			await process_frame
			await RenderingServer.frame_post_draw
		var picture := root.get_texture().get_image()
		var filename: String = item.id + ".png"
		var path: String = args[2].path_join(filename)
		if picture == null or picture.is_empty() or picture.save_png(path) != OK:
			push_error("Operating inspection capture failed")
			quit(1)
			return
		var transforms := {}
		for id in view.moving_nodes:
			transforms[id] = Operating.columns(view.moving_nodes[id].transform)
		var station_transforms := {}
		for id in view.closure_nodes:
			station_transforms[id] = Operating.columns(view.closure_nodes[id].transform)
		images.append({"id": item.id, "file": filename, "sha256": FileAccess.get_sha256(path), "width": picture.get_width(), "height": picture.get_height(), "pose": item.pose, "station_progress": item.station, "camera_position": [camera.position.x,camera.position.y,camera.position.z], "group_transforms": transforms, "station_transforms": station_transforms})
	if owner.get_freedom_flight_state() != initial or owner.get_freedom_walk_state() != initial_actor:
		push_error("Inspection altered authoritative body, actor or clock")
		quit(1)
		return
	var inputs := {}
	for filename in ["prepared.json", "wayfarer-operating-02.json", "wayfarer-operating-02.glb", "contact.json", "station-closure.json", "qualification.json", "provenance.json", "station-d1-clearance-01.glb"]:
		inputs[filename] = FileAccess.get_sha256(args[1].path_join(filename))
	var scripts := {}
	for filename in ["wayfarer_operating_view.gd", "wayfarer_operating_capture.gd", "native_station_view.gd", "bin/libapsis_freedom_bridge.so"]:
		scripts[filename] = FileAccess.get_sha256("res://" + filename)
	var output := FileAccess.open(args[2].path_join("capture.json"), FileAccess.WRITE)
	if output == null:
		quit(1)
		return
	output.store_string(JSON.stringify({"schema_version": 1, "scope": "Source-bound operating hardware inspection; unchanged C++ seed42 actor/body/tick; no actual boarding, seating or hardware-performance acceptance", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name(), "world_tick": initial.tick, "world_checksum": initial.checksum, "station_model_sha256": Operating.STATION_HASH, "inputs_sha256": inputs, "sources_sha256": scripts, "captures": images}, "\t") + "\n")
	view.free()
	print("Wayfarer operating rendered captures: %d" % images.size())
	quit(0)
