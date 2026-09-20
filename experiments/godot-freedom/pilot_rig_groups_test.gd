extends SceneTree
## Import partition normalization, synthetic geometry; not anatomical fit.
const Pilot = preload("res://pilot_presentation.gd")
var failures := 0

func check(value: bool, message: String) -> void:
	if not value:
		failures += 1
		push_error(message)

func fixture() -> Node3D:
	var model := Node3D.new()
	for title in ["PilotHead", "PilotBody"]:
		var group := Node3D.new()
		group.name = title
		model.add_child(group)
	var skeleton := Skeleton3D.new()
	skeleton.name = "SharedRig"
	skeleton.add_bone("root")
	model.add_child(skeleton)
	for title in ["PilotHeadMesh__skin", "PilotBodyMesh__skin"]:
		var mesh := MeshInstance3D.new()
		mesh.name = title
		mesh.mesh = BoxMesh.new()
		mesh.skin = Skin.new()
		mesh.skin.add_bind(0, Transform3D.IDENTITY)
		skeleton.add_child(mesh)
		mesh.skeleton = NodePath("..")
	return model

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var model := fixture()
	check(not Pilot.normalize_rig_groups(model), "Off-tree rig must refuse")
	root.add_child(model)
	model.transform = Transform3D(Basis.from_euler(Vector3(0.2, -0.7, 0.3)), Vector3(45, -18, 12))
	var head := model.get_node("PilotHead") as Node3D
	var body := model.get_node("PilotBody") as Node3D
	head.position = Vector3(0, 0.1, 0)
	body.position = Vector3(0.05, 0, 0)
	var skeleton := model.get_node("SharedRig") as Skeleton3D
	var head_mesh := skeleton.get_child(0) as MeshInstance3D
	var body_mesh := skeleton.get_child(1) as MeshInstance3D
	var head_skin := head_mesh.skin
	var body_skin := body_mesh.skin
	var transforms := [head_mesh.global_transform, body_mesh.global_transform]
	check(not Pilot.valid(model), "Imported misplaced skins unexpectedly satisfy static partition")
	check(Pilot.normalize_rig_groups(model) and Pilot.valid(model), "Tagged skin partition normalization failed")
	check(head_mesh.get_parent() == head and body_mesh.get_parent() == body, "Meshes assigned wrong visibility group")
	check(head_mesh.skin == head_skin and body_mesh.skin == body_skin, "Normalization replaced Skin resources")
	check(head_mesh.get_node(head_mesh.skeleton) == skeleton and body_mesh.get_node(body_mesh.skeleton) == skeleton, "Normalization changed shared skeleton identity")
	check(head_mesh.global_transform.is_equal_approx(transforms[0]) and body_mesh.global_transform.is_equal_approx(transforms[1]), "Normalization moved geometry")
	Pilot.set_first_person(model, true)
	check(not head_mesh.is_visible_in_tree() and body_mesh.is_visible_in_tree() and skeleton.is_visible_in_tree(), "Own-head masking hid body/shared rig")
	Pilot.set_first_person(model, false)
	check(head_mesh.is_visible_in_tree(), "Exterior view did not restore own head")
	check(Pilot.normalize_rig_groups(model), "Normalization is not idempotent")
	model.free()
	for defect in ["untagged", "wrong_skeleton", "missing_group", "nested_skeleton", "static_tag", "empty_mesh", "cyclic_group", "nonfinite_group", "singular_group", "mesh_root", "head_is_skeleton"]:
		model = fixture()
		root.add_child(model)
		skeleton = model.get_node("SharedRig")
		head_mesh = skeleton.get_child(0)
		body_mesh = skeleton.get_child(1)
		match defect:
			"untagged": body_mesh.name = "UnownedSkin"
			"wrong_skeleton": body_mesh.skeleton = NodePath("../..")
			"missing_group": model.get_node("PilotBody").name = "MissingBody"
			"nested_skeleton": skeleton.reparent(model.get_node("PilotHead"))
			"static_tag": body_mesh.skin = null
			"empty_mesh": body_mesh.mesh = null
			"cyclic_group": model.get_node("PilotHead").reparent(head_mesh)
			"nonfinite_group": model.get_node("PilotHead").position.x = NAN
			"singular_group": model.get_node("PilotBody").basis = Basis(Vector3.ZERO, Vector3.UP, Vector3.BACK)
			"head_is_skeleton":
				model.get_node("PilotHead").free()
				skeleton.name = "PilotHead"
			"mesh_root":
				var wrapper := MeshInstance3D.new()
				wrapper.mesh = BoxMesh.new()
				root.add_child(wrapper)
				model.reparent(wrapper)
				model = wrapper
		var parent := head_mesh.get_parent()
		var transform := head_mesh.transform
		check(not Pilot.normalize_rig_groups(model), "Malformed partition accepted: " + defect)
		check(head_mesh.get_parent() == parent and head_mesh.transform == transform, "Preflight refusal partially moved earlier mesh: " + defect)
		model.free()
	print("PILOT RIG GROUPS: ", failures, " failures; synthetic import hierarchy only")
	quit(0 if failures == 0 else 1)
