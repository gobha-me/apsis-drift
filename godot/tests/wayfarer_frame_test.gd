extends SceneTree
## Compare the actual C++ frame with the admitted mesh and sampled gear poses.
var failures := 0


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func vector(value: Array) -> Vector3:
	return Vector3(value[0], value[1], value[2])


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		quit(1)
		return
	var model_path := args[0].path_join("hopper-wayfarer-01.glb")
	var descriptor := args[0].path_join("hopper-wayfarer-01.json")
	if FileAccess.get_sha256(model_path) != "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8" or FileAccess.get_sha256(descriptor) != "17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7":
		push_error("Wayfarer source-bound measurement bytes changed")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	var frame: Dictionary = JSON.parse_string(bridge.get_wayfarer_frame())
	check(frame.frame_id == "2" and frame.descriptor_version == 1 and frame.name == "wayfarer-v1", "Wrong C++ frame identity")
	check(bridge.get_freedom_start().is_empty(), "Descriptor query created a game")
	var spec: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(descriptor))
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	check(document.append_from_file(model_path, state) == OK, "Wayfarer import refused")
	var model: Node3D = document.generate_scene(state)
	if model == null:
		quit(1)
		return
	root.add_child(model)
	var bounds := AABB()
	var first := true
	for node: MeshInstance3D in model.find_children("*", "MeshInstance3D", true, false):
		var box: AABB = node.global_transform * node.mesh.get_aabb()
		bounds = box if first else bounds.merge(box)
		first = false
	for axis in 3:
		check(frame.hull_min_mm[axis] / 1000.0 <= bounds.position[axis] and frame.hull_max_mm[axis] / 1000.0 >= bounds.end[axis], "C++ stowed hull excludes actual model")
		check(bounds.position[axis] - frame.hull_min_mm[axis] / 1000.0 < 0.0011 and frame.hull_max_mm[axis] / 1000.0 - bounds.end[axis] < 0.0011, "C++ envelope is not outward-rounded to millimetres")
	# The archived descriptor's sample zero is the fully deployed authored pose.
	for key in spec.gear_preview:
		var node: Node3D = model.find_child(key, true, false)
		var pose: Array = spec.gear_preview[key][0]
		node.transform = Transform3D(Basis(vector(pose[0]), vector(pose[1]), vector(pose[2])), vector(pose[3]))
	var names := ["HopperGear05", "HopperGear16", "HopperGear27"]
	check(frame.supports.size() == names.size(), "Support count differs from actual feet")
	for index in names.size():
		var foot: MeshInstance3D = model.find_child(names[index], true, false)
		var box: AABB = foot.global_transform * foot.mesh.get_aabb()
		var support: Dictionary = frame.supports[index]
		var contact := vector(support.contact_mm) / 1000.0
		check(abs(contact.x - box.get_center().x) < 0.00001 and abs(contact.z - box.get_center().z) < 0.00001 and abs(contact.y - box.position.y) < 0.00001, "C++ contact differs from the deployed visual sole")
		check(abs(support.half_width_mm / 1000.0 - box.size.x / 2) < 0.00001 and abs(support.half_length_mm / 1000.0 - box.size.z / 2) < 0.00001, "C++ contact patch differs from deployed pad")
		check(contact.y + support.stroke_mm / 1000.0 < frame.hull_min_mm[1] / 1000.0, "Compressed support intersects stowed hull")
	model.free()
	print("Wayfarer physical frame: %d failures; actual source hull/deployed feet and pure C++ descriptor" % failures)
	quit(0 if failures == 0 else 1)
