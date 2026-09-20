extends SceneTree
## Generated rig fixtures by default; optional isolated GLBs, never game assets.
const Motion = preload("res://pilot_motion.gd")
var failures := 0

func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

static func bone(skeleton: Skeleton3D, name: String, parent: int, position: Vector3) -> int:
	var id := skeleton.get_bone_count()
	skeleton.add_bone(name)
	skeleton.set_bone_parent(id, parent)
	var local := Transform3D(Basis.IDENTITY, position)
	if parent >= 0:
		local = skeleton.get_bone_global_pose(parent).affine_inverse()*local
	skeleton.set_bone_rest(id, local)
	skeleton.set_bone_pose(id, local)
	return id

static func fixture() -> Node3D:
	var scene := Node3D.new()
	var skeleton := Skeleton3D.new()
	skeleton.name = "Skeleton"
	scene.add_child(skeleton)
	for name in ["pelvis", "eye.L", "eye.R", "foot.L", "foot.R"]:
		bone(skeleton, name, -1, Vector3(0, 1, 0))
	for side in ["L", "R"]:
		var sign_value := -1.0 if side == "L" else 1.0
		var shoulder := Vector3(sign_value*0.2, 1.3, 0.05)
		var elbow := Vector3(sign_value*0.26, 1.0, 0.04)
		var wrist := Vector3(sign_value*0.35, 0.85, -0.18)
		var points := [shoulder, shoulder.lerp(elbow, 0.4), elbow, elbow.lerp(wrist, 0.5), wrist]
		var parent := 0 # Fixed pelvis ancestor, not an independent arm root.
		for i in Motion.CHAIN.size():
			parent = bone(skeleton, Motion.CHAIN[i]+"."+side, parent, points[i])
		var pivot := Node3D.new()
		pivot.name = "PROVISIONAL control pivot " + side
		pivot.position = Vector3(sign_value*0.35, 0.65, -0.12)
		scene.add_child(pivot)
	return scene

func demand(pitch := 0.0, roll := 0.0, yaw := 0.0, heave := 0.0) -> Dictionary:
	return {"pitch": pitch, "roll": roll, "yaw": yaw, "heave": heave}

func verify_pose(driver: Node, unchanged: Dictionary, rest: Array) -> void:
	var skeleton: Skeleton3D = driver._skeleton
	for side in ["L", "R"]:
		var arm: Dictionary = driver._arms[side]
		var shoulder := skeleton.get_bone_global_pose(arm.ids[0]).origin
		var elbow := skeleton.get_bone_global_pose(arm.ids[2]).origin
		var wrist := skeleton.get_bone_global_pose(arm.ids[4])
		var desired: Transform3D = skeleton.global_transform.affine_inverse()*arm.pivot.global_transform*arm.wrist_from_pivot
		check(absf(shoulder.distance_to(elbow)-arm.upper) < 0.00001, "Upper arm stretched")
		check(absf(elbow.distance_to(wrist.origin)-arm.lower) < 0.00001, "Lower arm stretched")
		check(wrist.origin.distance_to(desired.origin) < 0.00001, "Wrist left its control reference")
		check((wrist.basis.x-desired.basis.x).length() < 0.00002 and (wrist.basis.y-desired.basis.y).length() < 0.00002, "Wrist orientation lost grip reference")
		for id in arm.ids:
			check(Motion._rigid(skeleton.get_bone_global_pose(id)), "Arm gained scale/shear")
	for id in unchanged:
		check(skeleton.get_bone_pose(id) == unchanged[id], "Non-arm local pose changed")
	for i in rest.size():
		check(skeleton.get_bone_rest(i) == rest[i], "Rest skeleton mutated")

func exercise(scene: Node3D, label: String) -> void:
	var driver := Motion.new()
	scene.add_child(driver)
	check(driver.configure(scene), label + " configure: " + driver.diagnostics().last_error)
	if not driver.diagnostics().configured:
		return
	var skeleton: Skeleton3D = driver._skeleton
	var unchanged := {}
	var rest := []
	var arm_ids := []
	for side in ["L", "R"]:
		arm_ids.append_array(driver._arms[side].ids)
	for i in skeleton.get_bone_count():
		rest.append(skeleton.get_bone_rest(i))
		if i not in arm_ids:
			unchanged[i] = skeleton.get_bone_pose(i)
	check(driver.update_controls(demand(), true, 0.0), "Neutral zero-delta rejected")
	verify_pose(driver, unchanged, rest)
	for key in Motion.KEYS:
		for sign_value in [-1.0, 1.0]:
			driver.reset_neutral()
			var input := demand()
			input[key] = sign_value
			check(driver.update_controls(input, true, 0.15), "Single semantic input rejected")
			verify_pose(driver, unchanged, rest)
			var idle_side := "R" if key in ["pitch", "roll"] else "L"
			check(driver._arms[idle_side].pivot.transform.is_equal_approx(driver._arms[idle_side].pivot_local), "Semantic input moved wrong control")
	for mask in 16:
		driver.reset_neutral()
		var input := demand()
		for i in 4:
			input[Motion.KEYS[i]] = 1.0 if mask & (1 << i) else -1.0
		check(driver.update_controls(input, true, 0.15), "Combined corner target refused: " + label)
		verify_pose(driver, unchanged, rest)
		# A camera is never queried; moving the entire presentation root is also
		# not a new demand or a reason to move the body within the seat.
		var wrist_before := skeleton.get_bone_global_pose(driver._arms.L.ids[4])
		var old_transform := scene.transform
		scene.transform = Transform3D(Basis(Vector3.UP, 0.4), Vector3(4, 3, 2))*old_transform
		check(driver.update_controls(input, true, 0.0), "Root transform invalidated local solve")
		check(skeleton.get_bone_global_pose(driver._arms.L.ids[4]).origin.distance_to(wrist_before.origin) < 0.00001, "World/camera frame contaminated arm target")
		scene.transform = old_transform
		check(driver.update_controls({}, false, 0.15), "Inactive return rejected")
		check(driver.diagnostics().blend == Vector4.ZERO, "Pause/focus/disconnect gate did not return neutral")
		verify_pose(driver, unchanged, rest)
	for input in [{}, {"pitch": "bad", "roll": 0, "yaw": 0, "heave": 0}, demand(NAN), demand(INF), demand(1.01)]:
		check(driver.update_controls(demand(1), true, 0.15), "Invalid-input fixture setup failed")
		check(not driver.update_controls(input, true, 0.01), "Malformed semantic demand accepted")
		check(driver.diagnostics().blend == Vector4.ZERO, "Malformed demand retained active gesture")
	for delta in [-0.1, NAN, INF, 0.25001]:
		check(not driver.update_controls(demand(), true, delta), "Invalid delta accepted")
	check(driver.update_thrust_axes(PackedFloat64Array([0.3, 0.2, 0.5, -0.25, 0.75, 0.8, -1]), true, 0.15), "Valid resolved axes rejected")
	check(driver.diagnostics().blend == Vector4(0.5, 0.75, -0.25, -1), "Resolved semantic axis mapping changed")
	for axes in [[], PackedFloat32Array([0, 0, 0, 0, 0, 0, 0]), PackedFloat64Array(), PackedFloat64Array([0, 0, 0, 0, 0, 0, 0, 0]), PackedFloat64Array([-0.1, 0, 0, 0, 0, 0, 0]), PackedFloat64Array([0, 0, 0, 0, 0, INF, 0])]:
		check(not driver.update_thrust_axes(axes, true, 0.01), "Malformed resolved axes accepted")
		check(driver.diagnostics().blend == Vector4.ZERO, "Bad resolved axes retained gesture")
	driver.reset_neutral()
	check(driver.update_controls(demand(0.7, -0.4, 0.2, -0.9), true, 0.12), "Chunking fixture setup failed")
	var one_step := skeleton.get_bone_global_pose(driver._arms.L.ids[4])
	var one_blend: Vector4 = driver.diagnostics().blend
	driver.reset_neutral()
	for i in 12:
		check(driver.update_controls(demand(0.7, -0.4, 0.2, -0.9), true, 0.01), "Chunked update failed")
	check(driver.diagnostics().blend.is_equal_approx(one_blend) and skeleton.get_bone_global_pose(driver._arms.L.ids[4]).is_equal_approx(one_step), "Bounded blend depends on presentation chunking")
	var camera := Camera3D.new()
	scene.add_child(camera)
	camera.position = Vector3(-2, 1, 3)
	camera.rotation = Vector3(0.4, -0.7, 0.2)
	var before_camera := skeleton.get_bone_global_pose(driver._arms.L.ids[4])
	check(driver.update_controls(demand(0.7, -0.4, 0.2, -0.9), true, 0.0), "Camera independence update failed")
	check(skeleton.get_bone_global_pose(driver._arms.L.ids[4]).is_equal_approx(before_camera), "Camera moved arm pose")
	camera.free()
	var pivot: Node3D = driver._arms.L.pivot
	var original: Transform3D = driver._arms.L.pivot_local
	driver._arms.L.pivot_local = Transform3D(original.basis, original.origin+Vector3(4, 0, 0))
	check(not driver.update_controls(demand(1), true, 0.15), "Unreachable target stretched rig")
	driver._arms.L.pivot_local = original
	driver.reset_neutral()
	check(pivot.transform == original, "Neutral reset failed")
	verify_pose(driver, unchanged, rest)
	print("Pilot motion fixture: ", label, "; segment lengths and grip references checked")
	# Teardown cannot create an uncaught null access or keep
	# the controls displaced. This also covers an asset reload invalidation.
	check(driver.update_controls(demand(1), true, 0.15), "Teardown fixture setup failed")
	if not driver._ancestor_poses.is_empty():
		var ancestor: int = driver._ancestor_poses.keys()[0]
		var ancestor_neutral := skeleton.get_bone_pose(ancestor)
		var moved := ancestor_neutral
		moved.origin += Vector3(0.01, 0, 0)
		skeleton.set_bone_pose(ancestor, moved)
		var stored_moved := skeleton.get_bone_pose(ancestor)
		check(not driver.update_controls(demand(1), true, 0.01), "Moving torso ancestor admitted")
		check(skeleton.get_bone_pose(ancestor) == stored_moved, "Refusal overwrote independent torso pose")
		skeleton.set_bone_pose(ancestor, ancestor_neutral)
	var detached: Node3D = driver._arms.L.pivot
	var original_parent := detached.get_parent()
	original_parent.remove_child(detached)
	check(not driver.update_controls(demand(1), true, 0.01), "Detached control admitted")
	original_parent.add_child(detached)
	driver._arms.L.pivot.free()
	check(not driver.update_controls(demand(1), true, 0.01), "Removed control admitted")
	check(driver.diagnostics().blend == Vector4.ZERO, "Removed control retained demand")
	skeleton.add_bone("changed-rig")
	check(not driver.update_controls(demand(1), true, 0.01), "Changed rig admitted")
	skeleton.free()
	check(not driver.update_controls(demand(1), true, 0.01), "Freed rig admitted")

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy":
		push_error("Requires --headless --audio-driver Dummy")
		quit(1)
		return
	check(Motion.solve_elbow(Vector3.ZERO, Vector3(0.3, 0.3, 0), 0.3, 0.3, Vector3.UP).has("elbow"), "Reachable pure solve failed")
	for target in [Vector3.ZERO, Vector3(1, 0, 0), Vector3(NAN, 0, 0)]:
		check(Motion.solve_elbow(Vector3.ZERO, target, 0.3, 0.3, Vector3.UP).is_empty(), "Invalid pure solve accepted")
	check(Motion.solve_elbow(Vector3.ZERO, Vector3(0.4, 0, 0), 0.3, 0.3, Vector3.RIGHT).is_empty(), "Undefined bend plane accepted")
	for malformed in ["missing", "ambiguous", "chain", "scale"]:
		var scene := fixture()
		root.add_child(scene)
		if malformed == "missing":
			scene.get_node("PROVISIONAL control pivot L").free()
		elif malformed == "ambiguous":
			scene.add_child(Skeleton3D.new())
		elif malformed == "chain":
			var skeleton: Skeleton3D = scene.get_node("Skeleton")
			skeleton.set_bone_parent(skeleton.find_bone("wrist.L"), -1)
		else:
			scene.scale = Vector3(1, 2, 1)
		var driver := Motion.new()
		scene.add_child(driver)
		check(not driver.configure(scene), "Malformed rig admitted: " + malformed)
		scene.free()
	for invalidation in ["removed", "detached", "scaled"]:
		var scene := fixture()
		root.add_child(scene)
		var driver := Motion.new()
		scene.add_child(driver)
		check(driver.configure(scene), "Neutral invalidation setup rejected")
		var pivot: Node3D = driver._arms.L.pivot
		if invalidation == "removed":
			pivot.free()
		elif invalidation == "detached":
			scene.remove_child(pivot)
		else:
			scene.scale = Vector3(1, 2, 1)
		for active in [true, false]:
			check(not driver.update_controls(demand(), active, 0.0), "Invalid neutral/inactive hierarchy admitted: " + invalidation)
			check(driver.diagnostics().blend == Vector4.ZERO, "Invalid neutral hierarchy retained demand")
		if invalidation == "detached":
			pivot.free()
		scene.free()
	var synthetic := fixture()
	root.add_child(synthetic)
	exercise(synthetic, "synthetic")
	synthetic.free()
	var competing := fixture()
	root.add_child(competing)
	var animation_player := AnimationPlayer.new()
	competing.add_child(animation_player)
	var library := AnimationLibrary.new()
	var animation := Animation.new()
	animation.length = 1.0
	library.add_animation("competing", animation)
	animation_player.add_animation_library("", library)
	var animation_driver := Motion.new()
	competing.add_child(animation_driver)
	animation_player.play("competing")
	check(not animation_driver.configure(competing), "Playing animation admitted at configure")
	animation_player.pause()
	check(animation_driver.configure(competing), "Paused animation rejected")
	check(animation_driver.update_controls(demand(1), true, 0.15), "Animation conflict setup failed")
	animation_player.play("competing")
	check(not animation_driver.update_controls(demand(1), true, 0.01), "Competing animation admitted during update")
	check(animation_driver.diagnostics().blend == Vector4.ZERO, "Animation conflict retained gesture")
	competing.free()
	for argument in OS.get_cmdline_user_args():
		if not argument.begins_with("--pilot-glb="):
			continue
		var path := argument.trim_prefix("--pilot-glb=")
		if not path.is_absolute_path() or not FileAccess.file_exists(path):
			check(false, "Invalid optional pilot path")
			continue
		var document := GLTFDocument.new()
		var state := GLTFState.new()
		if document.append_from_file(path, state) != OK:
			check(false, "Could not read optional pilot GLB")
			continue
		var scene := document.generate_scene(state)
		root.add_child(scene)
		var players := scene.find_children("*", "AnimationPlayer", true, false)
		check(players.size() == 1, "Expected one imported AnimationPlayer")
		if players.size() == 1:
			var player: AnimationPlayer = players[0]
			player.play("Scene")
			player.seek(0.0, true)
			player.pause()
			await process_frame
			exercise(scene, path.get_file())
		scene.free()
	print("Pilot semantic motion: %d failures; presentation rig proof only" % failures)
	quit(0 if failures == 0 else 1)
