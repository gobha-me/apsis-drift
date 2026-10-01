extends SceneTree
## Frozen C++ world, registered source room; editorial camera/shutter only.
const Flight = preload("res://native_flight_view.gd")
const FPS := 24
const SECONDS := 20
const INPUT_HASHES := {"station-observation-commons.glb":"a983bd2130ead14beea61f64a530fbaa5c7609b16a58ad9dfe7a86592be6ab80", "station-observation-commons.json":"d87195e54c40952d16785ae5d9dc570cb3f26483abee138e25b392367d4b949f", "station-commons.json":"ca85bc97c01965266e918f609e1ed01de20cbb05cb6dbbe15c7b5adf7334852c", "station-observation-checks.json":"8b1c4f5eecef060fe0911c2106c57ed158dd01730b3870d809a15a8bc0dc5105"}
const NONEXPORTED_ROOTS := ["KIT05 commons area", "KIT05 commons area.001", "KIT05 commons area.002"]
var bridge: Variant
var view: Variant
var hidden := []
var expected := {}
var output := ""
var model: Node3D
var player: AnimationPlayer
var review := false
var observations: Array[Dictionary] = []

func fail(message: String) -> void:
	push_error(message)
	quit(1)
func _initialize() -> void:
	call_deferred("run")

static func source_frame(seconds: float) -> int:
	if not is_finite(seconds) or seconds < 0 or seconds >= SECONDS:
		return -1
	return int(round(lerpf(1.0, 100.0, clampf((seconds - 1.0) / 6.0, 0.0, 1.0))))

static func eye_local(seconds: float) -> Vector3:
	if source_frame(seconds) < 0:
		return Vector3(NAN, NAN, NAN)
	return Vector3(-1.30,1.37,-2.0).lerp(Vector3(-2.15,1.24,-2.0), smoothstep(0.0, 12.0, seconds))

static func contracts_valid() -> bool:
	for value in [NAN, INF, -INF, -1.0, 20.0]:
		if source_frame(value) != -1 or eye_local(value).is_finite():
			return false
	for frame in range(FPS * SECONDS):
		var seconds := float(frame) / FPS
		if source_frame(seconds) not in range(1,101) or not eye_local(seconds).is_finite():
			return false
	return source_frame(0.0) == 1 and source_frame(7.0) == 100 and eye_local(19.0).is_equal_approx(Vector3(-2.15,1.24,-2.0))

func set_pose(seconds: float) -> bool:
	var eye := eye_local(seconds)
	if not eye.is_finite():
		return false
	var target_y := lerpf(1.37, 1.24, smoothstep(0.0,12.0,seconds))
	player.seek(float(source_frame(seconds))/24.0, true)
	view.camera.global_position = model.to_global(eye)
	view.camera.look_at(model.to_global(Vector3(-4.81,target_y,-2.0)), model.global_basis.y)
	view.camera.fov = 64.0
	view.terrain_camera.transform = view.camera.transform
	view.terrain_camera.fov = view.camera.fov
	return view.camera.transform.is_finite() and view.camera.transform.is_equal_approx(view.terrain_camera.transform)

func matrices(value: Transform3D) -> Array:
	return [[value.basis.x.x,value.basis.x.y,value.basis.x.z], [value.basis.y.x,value.basis.y.y,value.basis.y.z], [value.basis.z.x,value.basis.z.y,value.basis.z.z], [value.origin.x,value.origin.y,value.origin.z]]
func row_transform(rows: Array) -> Transform3D:
	return Transform3D(Basis(Vector3(rows[0][0], rows[1][0], rows[2][0]), Vector3(rows[0][1], rows[1][1], rows[2][1]), Vector3(rows[0][2], rows[1][2], rows[2][2])), Vector3(rows[0][3], rows[1][3], rows[2][3]))
func hide_old_room(node: Node) -> void:
	if expected.has(str(node.name)):
		if node is Node3D:
			node.hide()
			hidden.append(str(node.name))
	for child in node.get_children():
		hide_old_room(child)
func animation_player(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node
	for child in node.get_children():
		var found := animation_player(child)
		if found != null:
			return found
	return null
func run() -> void:
	var args := OS.get_cmdline_user_args()
	if not contracts_valid():
		fail("Invalid camera/frame sampler contracts")
		return
	if args == PackedStringArray(["--check-only"]):
		print("OBSERVATION_CONTRACTS_PASS finite, bounds,480samples, exact paused view endpoints")
		quit()
		return
	if args.size() not in [2,3] or (args.size() == 3 and args[2] != "review") or DisplayServer.get_name() == "headless":
		fail("Prepared native assets, empty absolute output and real raster display required")
		return
	review = args.size() == 3
	output = args[1]
	if not args[0].is_absolute_path() or not output.is_absolute_path() or not DirAccess.dir_exists_absolute(args[0]) or not DirAccess.dir_exists_absolute(output) or not DirAccess.get_files_at(output).is_empty() or not DirAccess.get_directories_at(output).is_empty():
		fail("Output is not empty")
		return
	for name in INPUT_HASHES:
		if FileAccess.get_sha256("res://observation-source/" + name) != INPUT_HASHES[name]:
			fail("Stage unchanged observation source: " + name)
			return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	bridge = ClassDB.instantiate("FreedomBridge")
	if bridge == null or not bridge.initialize_freedom_new_game("42"):
		fail("Actual C++ New Game unavailable")
		return
	var initial: Dictionary = bridge.get_freedom_flight_state()
	root.size = Vector2i(1920, 1080)
	view = Flight.new()
	root.add_child(view)
	view.set_process(false)
	view.set_process_unhandled_input(false)
	if not view.initialize(bridge, args[0]):
		fail(view.error)
		return
	for child in view.get_children():
		if child is Control and not child is SubViewportContainer:
			child.hide()
	var commons: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://observation-source/station-commons.json"))
	for item in commons.source_mapping:
		# These exact source collection roots are absent from BOTH immutable GLBs;
		# All 232 exported room objects must still bind before replacement.
		if item.source_object in NONEXPORTED_ROOTS:
			continue
		expected[str(item.source_object).validate_node_name()] = true
	hide_old_room(view.station)
	if hidden.size() != expected.size() or expected.size() != 232:
		fail("Exact room replacement binding refused: " + str(hidden.size()) + "/" + str(expected.size()))
		return
	var descriptor: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://observation-source/station-observation-commons.json"))
	if FileAccess.get_sha256("res://observation-source/station-observation-commons.glb") != descriptor.glb_sha256:
		fail("Changed observation source")
		return
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file("res://observation-source/station-observation-commons.glb", state) != OK:
		fail("Observation source import refused")
		return
	model = doc.generate_scene(state, 24.0)
	if model == null:
		fail("Observation scene missing")
		return
	player = animation_player(model)
	if player == null or not player.has_animation("Observation shutter cycle"):
		fail("Source shutter animation binding missing")
		return
	view.scene.add_child(model)
	var convert := Transform3D(Basis(Vector3.RIGHT, Vector3(0,0,-1), Vector3(0,1,0)), Vector3.ZERO)
	var local_to_source := row_transform(commons.source_to_local_matrix).affine_inverse()
	# asset_offset_metres is a binary64 array; convert once to the native scene's local vector.
	var offset: PackedFloat64Array = view.station_geometry.asset_offset_metres
	var room_to_station := Transform3D(Basis.IDENTITY, Vector3(offset[0],offset[1],offset[2])) * convert * local_to_source * convert.affine_inverse()
	model.transform = view.station.transform * room_to_station
	if not model.transform.is_finite() or absf(model.basis.determinant() - 1.0) > 0.00001:
		fail("Nonrigid or nonfinite observation registration")
		return
	Flight.set_ship_layer(model)
	player.play("Observation shutter cycle")
	player.pause()
	if not set_pose(0.0):
		fail("Camera source pose refused")
		return
	var caption := Label.new()
	caption.position = Vector2(24,24)
	caption.text = "Observation study / frozen C++ world"
	caption.add_theme_color_override("font_outline_color",Color.BLACK)
	caption.add_theme_constant_override("outline_size",6)
	caption.add_theme_font_size_override("font_size",24)
	root.add_child(caption)
	var warmed := 0
	while warmed < 600 and not view.terrain.is_ready and view.terrain.error.is_empty():
		view.terrain.tick(0.04, view.camera.position)
		await process_frame
		warmed += 1
	if not view.terrain.is_ready or not view.terrain.error.is_empty():
		fail("Actual C++ terrain cover unavailable: " + view.terrain.error)
		return
	var frames: Array[int] = []
	if review:
		for number in [0,72,168,288,479]:
			frames.append(number)
	else:
		for number in range(FPS * SECONDS):
			frames.append(number)
	for number in frames:
		var seconds := float(number)/FPS
		if not set_pose(seconds):
			fail("Camera/source shutter pose refused")
			return
		await process_frame
		await RenderingServer.frame_post_draw
		var snapshot: Dictionary = bridge.get_freedom_flight_state()
		if snapshot.tick != initial.tick or snapshot.checksum != initial.checksum or not bridge.get_freedom_walk_state().foot_position_metres == PackedFloat64Array([0.0,0.0,0.0]):
			fail("Editorial scene changed authoritative C++ state")
			return
		var image := root.get_texture().get_image()
		var name := "frame-%06d.png" % number
		if image == null or image.get_size() != Vector2i(1920,1080) or image.save_png(output.path_join(name)) != OK:
			fail("Capture failed")
			return
		if review or number % FPS == 0 or number == FPS * SECONDS - 1:
			observations.append({"frame":number,"seconds":seconds,"source_frame":source_frame(seconds),"camera":matrices(view.camera.transform),"tick":snapshot.tick,"checksum":snapshot.checksum,"png_sha256":FileAccess.get_sha256(output.path_join(name))})
		if number % 120 == 0:
			print("OBSERVATION_CAPTURE ",number,"/",FPS * SECONDS)
	var final: Dictionary = bridge.get_freedom_flight_state()
	for name in INPUT_HASHES:
		if FileAccess.get_sha256("res://observation-source/" + name) != INPUT_HASHES[name]:
			fail("Observation source changed during recording")
			return
	if not bridge.save_freedom_as(output.path_join("unchanged-journey.json")):
		fail("C++ Save As proof failed")
		return
	var result := {"schema":"apsis.reel-observation/1", "scope":"Optional frozen C++ seed42 observation derivative, registered room replacement; camera and source shutter presentation only; no actor/pressure/collision admission or celestial backdrop", "renderer":RenderingServer.get_video_adapter_name(), "fps":FPS,"seconds":SECONDS,"frames":frames.size(),"review":review,"capture_sha256":FileAccess.get_sha256("res://reel_observation_capture.gd"),"inputs_sha256":INPUT_HASHES,"unchanged_save_sha256":FileAccess.get_sha256(output.path_join("unchanged-journey.json")), "initial_tick":initial.tick, "final_tick":final.tick, "initial_checksum":initial.checksum, "final_checksum":final.checksum, "room_hidden_source_nodes":hidden, "source_mapping_size":expected.size(), "nonexported_source_collection_roots":NONEXPORTED_ROOTS, "room_transform":matrices(model.transform), "room_to_station":matrices(room_to_station), "camera_transform":matrices(view.camera.transform), "terrain":view.terrain.report(),"observations":observations,"reserved_caption_pixels_bottom":220,"source_checks_sha256":INPUT_HASHES["station-observation-checks.json"]}
	FileAccess.open(output.path_join("receipt.json"),FileAccess.WRITE).store_string(JSON.stringify(result,"\t"))
	print("OBSERVATION_CAPTURE_PASS ",frames.size(),"frames frozen tick",final.tick)
	quit()
