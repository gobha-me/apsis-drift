extends SceneTree
## Native read-only motion contract; optional display captures follow all refusals.
const Motion = preload("res://operating_motion_presentation.gd")
const Operating = preload("res://wayfarer_operating_view.gd")
const FIXTURES := {
	"sources/wayfarer-motion-proposal/source-poses.json": "9e0685b20363d6d93eefc8364faf764775cb29577a188269777833adae009d6f",
	"sources/operating-motion-fixtures-01/craft-combined.json": "485f9fa8d9030abd53e1db8a53dff830436e3133c726c7af1ddfc3daa37cbeab",
	"sources/operating-motion-fixtures-01/d1-recipe-source-poses.json": "c33dfdce9613fe978a96ebeae6346af2dc60d85dfd48c3d99ec9633fe311d6db"
}
var failures := 0
var checks := 0
var maximum_source_error := 0.0

func check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		push_error(message)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func identity() -> PackedFloat64Array:
	return PackedFloat64Array([1,0,0,0,1,0,0,0,1,0,0,0])

func fixture_pose() -> Dictionary:
	var result := {"craft_world_deltas": {}, "station_node_local": {}, "station_contact_deltas": {}}
	for id in Operating.GROUP_NODES:
		result.craft_world_deltas[id] = identity()
	for id in Motion.station_ids():
		result.station_node_local[id] = identity()
		result.station_contact_deltas[id] = identity()
	return result

func snapshots(view: Node3D) -> Dictionary:
	var result := {}
	for id in view.hardware.moving_nodes:
		result[id] = view.hardware.moving_nodes[id].transform
	for id in view.hardware.closure_nodes:
		result[id] = view.hardware.closure_nodes[id].transform
	return result

func source_columns(rows: Array) -> PackedFloat64Array:
	var columns := PackedFloat64Array()
	for column in 4:
		for row in 3:
			columns.append(rows[row][column])
	return columns

func check_source(actual: PackedFloat64Array, expected: Array, label: String) -> void:
	var columns := source_columns(expected)
	var error := 0.0
	for index in 12:
		error = maxf(error, absf(actual[index] - columns[index]))
	maximum_source_error = maxf(maximum_source_error, error)
	check(error <= 0.000005, "C++ matrix differs from independent source: " + label)

func check_applied(view: Node3D) -> void:
	for id in Operating.GROUP_NODES:
		check(view.hardware.moving_nodes[id].transform == Motion.native_transform(view.last_pose.craft_world_deltas[id]), "Craft world delta not applied once: " + id)
	for id in Motion.station_ids():
		check(view.hardware.closure_nodes[id].transform == Motion.native_transform(view.last_pose.station_node_local[id]), "D1 local pose not assigned below source parent: " + id)

func invalid_contracts() -> void:
	check(Motion.valid_matrix(identity()), "Rigid binary64 identity refused")
	for buffer in [null, [], PackedFloat32Array([1,0,0,0,1,0,0,0,1,0,0,0]), PackedFloat64Array(), PackedFloat64Array([1.0])]:
		check(not Motion.valid_matrix(buffer), "Wrong matrix buffer/type/dimension accepted")
	for index in [0, 4, 8, 11]:
		var invalid := identity()
		invalid[index] = NAN
		check(not Motion.valid_matrix(invalid), "Nonfinite component accepted")
	for value in [INF, -INF, 101.0]:
		var invalid := identity()
		invalid[11] = value
		check(not Motion.valid_matrix(invalid), "Unbounded matrix component accepted")
	for value in [-1.0, 0.0, 1.01]:
		var invalid := identity()
		invalid[0] = value
		check(not Motion.valid_matrix(invalid), "Reflected/singular/nonrigid basis accepted")
	for progress in [null, [], [0,0,0], [0,0,0,0,0], [true,0,0,0], ["0",0,0,0], [NAN,0,0,0], [0,INF,0,0], [0,0,-0.01,0], [0,0,0,1.01]]:
		check(not Motion.valid_progress(progress), "Invalid progress buffer accepted")
	check(Motion.valid_progress([0,0.425,0.775,1]) and Motion.valid_progress(PackedFloat64Array([0,0,0,0])), "Valid progress refused")
	var pose := fixture_pose()
	check(Motion.valid_native_pose(pose), "Complete static pose contract refused")
	for key in ["craft_world_deltas", "station_node_local", "station_contact_deltas"]:
		var changed := pose.duplicate(true)
		changed.erase(key)
		check(not Motion.valid_native_pose(changed), "Missing transform space accepted")
	var changed := pose.duplicate(true)
	changed.station_contact_deltas.station_d1_16 = PackedFloat64Array([1.0])
	check(not Motion.valid_native_pose(changed), "Late contact buffer accepted")
	changed = pose.duplicate(true)
	changed.craft_world_deltas.unlisted = identity()
	check(not Motion.valid_native_pose(changed), "Unlisted transform group accepted")
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0,1,3])
	check(Operating.surface_counts(arrays).x < 0, "Out-of-bounds triangle index accepted")
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0,1,2])
	arrays[Mesh.ARRAY_VERTEX][2] = Vector3(NAN,0,0)
	check(Operating.surface_counts(arrays).x < 0, "Nonfinite mesh vertex accepted")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() not in [3,4]:
		push_error("Expected prepared starter, operating and motion directories; optional capture directory")
		quit(1)
		return
	for path in args:
		if not path.is_absolute_path() or not DirAccess.dir_exists_absolute(path):
			push_error("Absolute existing prepared/capture directories required")
			quit(1)
			return
	if args.size() == 4 and (DisplayServer.get_name() == "headless" or not DirAccess.get_files_at(args[3]).is_empty() or not DirAccess.get_directories_at(args[3]).is_empty()):
		push_error("Real display and empty capture directory required")
		quit(1)
		return
	invalid_contracts()
	var sources := {}
	for filename in FIXTURES:
		var path: String = args[2].path_join(filename)
		check(FileAccess.get_sha256(path) == FIXTURES[filename], "Changed independent source fixture")
		sources[filename] = Operating.bounded_json(path)
	if failures != 0:
		finish()
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if owner == null:
		check(false, "Compiled Freedom bridge missing")
		finish()
		return
	check(owner.get_operating_motion_pose(0,0,0,0).is_empty(), "Pose query worked before recipe admission")
	check(owner.initialize_freedom_new_game("42"), "Actual seed42 New Game refused")
	var before: Dictionary = owner.get_freedom_flight_state()
	var actor: Dictionary = owner.get_freedom_walk_state()
	var temporary := ProjectSettings.globalize_path("user://motion-contract-%d-%d" % [OS.get_process_id(), Time.get_ticks_usec()])
	check(DirAccess.make_dir_recursive_absolute(temporary) == OK and owner.save_freedom_as(temporary.path_join("before.json")), "Before Save As failed")
	var recipe := Motion.verified_recipe(args[2])
	check(not recipe.is_empty() and owner.initialize_operating_motion(recipe), "Selected native motion recipe refused")
	var admitted: Dictionary = owner.get_operating_motion_pose(0.425,0.375,0.775,0.6052631578947368)
	check(Motion.valid_native_pose(admitted), "Complete native binary64 output missing")
	for invalid in ["{}", " ".repeat(128*1024+1), "é".repeat(65537), '{"schema":1,"schema":2}', "[".repeat(17)+"0"+"]".repeat(17)]:
		check(not owner.initialize_operating_motion(invalid) and not owner.get_last_error().is_empty(), "Invalid recipe request accepted")
		check(owner.get_operating_motion_pose(0.425,0.375,0.775,0.6052631578947368) == admitted, "Refused load replaced prior admitted recipe")
	for progress in [PackedFloat64Array([NAN,0,0,0]), PackedFloat64Array([0,INF,0,0]), PackedFloat64Array([0,0,-0.01,0]), PackedFloat64Array([0,0,0,1.01])]:
		check(owner.get_operating_motion_pose(progress[0],progress[1],progress[2],progress[3]).is_empty(), "Invalid C++ progress produced transforms")
	var view := Motion.new()
	root.add_child(view)
	if not view.initialize_motion(owner,args[0],args[1],args[2]):
		check(false, "Source-bound native motion initialization failed: " + view.error)
		view.free()
		finish()
		return
	var initial_visual := snapshots(view)
	for item in sources["sources/wayfarer-motion-proposal/source-poses.json"].source_poses:
		var progress := PackedFloat64Array([0,0,0,0])
		progress[["roof_transfer","inner_door","seat_boarding"].find(item.channel)] = item.progress
		check(view.set_progress(progress), "Independent craft sample refused")
		for id in Operating.GROUP_NODES:
			check_source(view.last_pose.craft_world_deltas[id], item.source_runtime_delta_rows[id], "%s/%s" % [item.channel,id])
		check_applied(view)
	for item in sources["sources/operating-motion-fixtures-01/craft-combined.json"].poses:
		check(view.set_progress(PackedFloat64Array([item.progress.roof_transfer,item.progress.inner_door,item.progress.seat_boarding,0])), "Combined source tuple refused")
		for id in Operating.GROUP_NODES:
			check_source(view.last_pose.craft_world_deltas[id], item.groups[id].godot_world_delta_rows, "combined/" + id)
		check_applied(view)
	for item in sources["sources/operating-motion-fixtures-01/d1-recipe-source-poses.json"].source_poses:
		check(view.set_progress(PackedFloat64Array([0,0,0,item.progress])), "D1 independent frame refused")
		for id in Motion.station_ids():
			check_source(view.last_pose.station_node_local[id], item.controls[id].native_local_godot_rows, "D1local/" + id)
			check_source(view.last_pose.station_contact_deltas[id], item.controls[id].canonical_contact_delta_rows, "D1contact/" + id)
		check_applied(view)
	check(view.set_progress(PackedFloat64Array([0,0,0,0])) and snapshots(view) == initial_visual, "Rest-pose-rest changed the source rest geometry")
	var last: Dictionary = view.last_pose.duplicate(true)
	var last_progress: PackedFloat64Array = view.last_progress.duplicate()
	for progress in [[true,0,0,0],[NAN,0,0,0],[0,0,0],[0,0,0,1.1]]:
		check(not view.set_progress(progress) and snapshots(view) == initial_visual and view.last_pose == last and view.last_progress == last_progress, "Invalid progress partially applied")
	for map_name in ["craft_world_deltas","station_node_local","station_contact_deltas"]:
		var changed := last.duplicate(true)
		var id: String = "seat_lock" if map_name == "craft_world_deltas" else "station_d1_16"
		var matrix: PackedFloat64Array = changed[map_name][id].duplicate()
		matrix[11] = NAN
		changed[map_name][id] = matrix
		check(not view.apply_native_pose(changed) and snapshots(view) == initial_visual and view.last_pose == last, "Late invalid matrix partially changed hardware")
	var lock: MeshInstance3D = view.hardware.moving_nodes.seat_lock
	var extras: Dictionary = lock.get_meta("extras").duplicate(true)
	lock.set_meta("extras", {})
	check(not view.apply_native_pose(admitted) and snapshots(view) == initial_visual, "Changed craft source binding accepted")
	lock.set_meta("extras", extras)
	var flat_parent := lock.get_parent()
	lock.reparent(view.hardware.moving_nodes.seat_carriage, false)
	check(not view.apply_native_pose(admitted) and snapshots(view) == initial_visual, "Nested craft world-delta group accepted")
	lock.reparent(flat_parent, false)
	var final_node: Node3D = view.hardware.closure_nodes.station_d1_16
	view.hardware.closure_nodes.erase("station_d1_16")
	check(not view.apply_native_pose(admitted) and snapshots(view).size() == 29 and view.last_pose == last, "Missing late D1 binding partially applied")
	view.hardware.closure_nodes.station_d1_16 = final_node
	check(snapshots(view) == initial_visual, "Missing D1 control refusal changed an earlier craft node")
	check(owner.get_freedom_flight_state() == before and owner.get_freedom_walk_state() == actor, "Read-only motion altered authoritative body/actor/tick")
	check(owner.save_freedom_as(temporary.path_join("after.json")) and FileAccess.get_file_as_bytes(temporary.path_join("before.json")) == FileAccess.get_file_as_bytes(temporary.path_join("after.json")), "Motion inspection changed serialized world/save")
	if args.size() == 4 and failures == 0:
		await captures(view,owner,args,temporary,before,actor)
	for name in ["before.json","after.json"]:
		DirAccess.remove_absolute(temporary.path_join(name))
	DirAccess.remove_absolute(temporary)
	view.free()
	finish()

func captures(view: Node3D, owner: Variant, args: PackedStringArray, temporary: String, before: Dictionary, actor: Dictionary) -> void:
	root.size = Vector2i(1280,720)
	var camera := Camera3D.new()
	camera.near = 0.025
	camera.far = 160.0
	camera.fov = 68.0
	view.add_child(camera)
	camera.current = true
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004,0.009,0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6,0.75,0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	view.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40,-30,0)
	sun.light_energy = 1.5
	view.add_child(sun)
	var cabin := OmniLight3D.new()
	cabin.omni_range = 10.0
	cabin.light_energy = 1.0
	cabin.position = before.body_basis * Vector3(0,1.9,0)
	view.add_child(cabin)
	var caption := Label.new()
	caption.position = Vector2(16,16)
	caption.add_theme_constant_override("outline_size",5)
	caption.add_theme_color_override("font_outline_color",Color.BLACK)
	view.add_child(caption)
	var cases := [
		{"id":"roof-0425","progress":PackedFloat64Array([.425,0,0,0]),"camera":Vector3(0,1.25,4.65),"target":Vector3(0,2.95,5.2)},
		{"id":"seat-withdraw-0575","progress":PackedFloat64Array([0,0,.575,0]),"camera":Vector3(0,1.6,.5),"target":Vector3(0,.9,-2.25)},
		{"id":"seat-turn-0775","progress":PackedFloat64Array([0,0,.775,0]),"camera":Vector3(0,1.6,.5),"target":Vector3(0,.9,-2.25)},
		{"id":"seat-restore-0975","progress":PackedFloat64Array([0,0,.975,0]),"camera":Vector3(0,1.6,.5),"target":Vector3(0,.9,-2.25)},
		{"id":"combined-off-knot","progress":PackedFloat64Array([.425,.375,.775,0]),"camera":Vector3(0,1.25,4.65),"target":Vector3(0,2.95,5.2)},
		{"id":"d1-gate-midpoint","progress":PackedFloat64Array([0,0,0,115.0/190.0]),"station_camera":Vector3(-21.9,1.7,0),"station_target":Vector3(-24,.1,0)},
		{"id":"d1-contact-context","progress":PackedFloat64Array([0,0,0,115.0/190.0]),"station_camera":Vector3(-18.9,1.65,0),"station_target":Vector3(-22,1.1,0)}
	]
	var images := []
	for item in cases:
		check(view.set_progress(item.progress), "Rendered C++ pose refused")
		if item.has("station_camera"):
			camera.position = view.hardware.station.transform * item.station_camera
			camera.look_at(view.hardware.station.transform * item.station_target,before.station_basis.y)
		else:
			camera.position = before.body_basis * item.camera
			camera.look_at(before.body_basis * item.target,before.body_basis.y)
		caption.text = "C++ source-local mechanism preview: " + item.id + "\nUnchanged actor/world; no boarding, pressure or clearance acceptance."
		for frame in 4:
			await process_frame
			await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		var filename: String = item.id + ".png"
		check(image != null and image.get_size() == Vector2i(1280,720) and image.save_png(args[3].path_join(filename)) == OK, "Native preview image failed")
		images.append({"id":item.id,"file":filename,"sha256":FileAccess.get_sha256(args[3].path_join(filename)),"progress":item.progress,"pose":view.last_pose,"camera":Operating.columns(camera.transform)})
	check(owner.get_freedom_flight_state() == before and owner.get_freedom_walk_state() == actor, "Rendered presentation advanced world")
	check(owner.save_freedom_as(args[3].path_join("unchanged-journey.json")) and FileAccess.get_file_as_bytes(args[3].path_join("unchanged-journey.json")) == FileAccess.get_file_as_bytes(temporary.path_join("before.json")), "Rendered presentation changed save bytes")
	var output := FileAccess.open(args[3].path_join("capture.json"),FileAccess.WRITE)
	var scripts := {}
	for filename in ["operating_motion_presentation.gd","operating_motion_test.gd","wayfarer_operating_view.gd","native_station_view.gd","bin/libapsis_freedom_bridge.so"]:
		scripts[filename] = FileAccess.get_sha256("res://" + filename)
	var assets := {}
	for filename in Operating.SELECTED_SHA256:
		assets[filename] = FileAccess.get_sha256(args[1].path_join(filename))
	if output != null:
		output.store_string(JSON.stringify({"schema":"apsis.operating-motion-native-proof/1","scope":Motion.SCOPE,"recipe_sha256":Motion.RECIPE_HASH,"fixtures_sha256":FIXTURES,"assets_sha256":assets,"scripts_sha256":scripts,"tick":before.tick,"checksum":before.checksum,"unchanged_save_sha256":FileAccess.get_sha256(args[3].path_join("unchanged-journey.json")),"renderer":RenderingServer.get_video_adapter_name(),"engine":Engine.get_version_info(),"checks":checks,"failures":failures,"maximum_source_component_error":maximum_source_error,"captures":images},"\t")+"\n")
	else:
		check(false,"Native proof receipt failed")

func finish() -> void:
	print("Operating motion read-only contract: %d failures; %d checks; source maximum %.12f" % [failures,checks,maximum_source_error])
	quit(0 if failures == 0 else 1)
