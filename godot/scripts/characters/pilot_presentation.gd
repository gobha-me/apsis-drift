extends RefCounted
## Opt-in seated asset study. Cabin coordinates, no physics or crew state.

static func meshes(node: Node) -> int:
	var count := 1 if node is MeshInstance3D and node.mesh != null and node.mesh.get_surface_count() > 0 else 0
	for child in node.get_children():
		count += meshes(child)
	return count

static func groups(node: Node, name_to_find: String, result: Array[Node3D]) -> void:
	if str(node.name) == name_to_find and node is Node3D:
		result.append(node)
	for child in node.get_children():
		groups(child, name_to_find, result)

static func valid(model: Node3D) -> bool:
	if not is_instance_valid(model):
		return false
	var heads: Array[Node3D] = []
	var bodies: Array[Node3D] = []
	groups(model, "PilotHead", heads)
	groups(model, "PilotBody", bodies)
	if heads.size() != 1 or bodies.size() != 1:
		return false
	if heads[0].is_ancestor_of(bodies[0]) or bodies[0].is_ancestor_of(heads[0]):
		return false # Hiding own head must never hide the whole body.
	var head_count := meshes(heads[0])
	var body_count := meshes(bodies[0])
	return head_count > 0 and body_count > 0 and meshes(model) == head_count + body_count

static func rigid_group_transform(value: Transform3D) -> bool:
	if not value.is_finite():
		return false
	var basis := value.basis
	return absf(basis.x.length_squared()-1.0) < 0.0001 and absf(basis.y.length_squared()-1.0) < 0.0001 and absf(basis.z.length_squared()-1.0) < 0.0001 and absf(basis.x.dot(basis.y)) < 0.0001 and absf(basis.x.dot(basis.z)) < 0.0001 and absf(basis.y.dot(basis.z)) < 0.0001 and basis.determinant() > 0.9998

static func normalize_rig_groups(model: Node3D) -> bool:
	# Godot imports skinned GLB meshes below their shared Skeleton3D rather than
	# retaining the authored visibility groups. Only the explicitly tagged rig
	# study may restore that partition; never infer anatomy from mesh positions.
	if not is_instance_valid(model) or not model.is_inside_tree() or model is MeshInstance3D:
		return false
	var heads: Array[Node3D] = []
	var bodies: Array[Node3D] = []
	groups(model, "PilotHead", heads)
	groups(model, "PilotBody", bodies)
	var skeletons := model.find_children("*", "Skeleton3D", true, false)
	if heads.size() != 1 or bodies.size() != 1 or skeletons.size() != 1:
		return false
	var head := heads[0]
	var body := bodies[0]
	var skeleton: Skeleton3D = skeletons[0]
	if head == skeleton or body == skeleton or head.is_ancestor_of(body) or body.is_ancestor_of(head) or head.is_ancestor_of(skeleton) or body.is_ancestor_of(skeleton):
		return false
	for node in [head, body, skeleton]:
		if not rigid_group_transform(node.global_transform):
			return false
	var moves := []
	var head_skins := 0
	var body_skins := 0
	for node in model.find_children("*", "MeshInstance3D", true, false):
		var mesh: MeshInstance3D = node
		var tag_head := str(mesh.name).begins_with("PilotHeadMesh__")
		var tag_body := str(mesh.name).begins_with("PilotBodyMesh__")
		if mesh.is_ancestor_of(head) or mesh.is_ancestor_of(body) or mesh.is_ancestor_of(skeleton):
			return false
		if mesh.skin != null:
			if mesh.mesh == null or mesh.mesh.get_surface_count() == 0:
				return false
			if not (tag_head or tag_body) or mesh.get_node_or_null(mesh.skeleton) != skeleton:
				return false
			if not mesh.global_transform.is_finite():
				return false
			moves.append({"mesh": mesh, "group": head if tag_head else body})
			head_skins += 1 if tag_head else 0
			body_skins += 1 if tag_body else 0
		elif tag_head or tag_body or not (head.is_ancestor_of(mesh) or body.is_ancestor_of(mesh)):
			return false
	if head_skins == 0 or body_skins == 0:
		return false
	# Preflight completed. Reparent keeps global geometry and the existing Skin
	# resource. Explicitly restore each path to the SAME shared skeleton.
	for move in moves:
		var mesh: MeshInstance3D = move.mesh
		if mesh.get_parent() != move.group:
			mesh.reparent(move.group, true)
		mesh.skeleton = mesh.get_path_to(skeleton)
	return valid(model)

static func load_into(cabin: Node3D, path: String, rig_study := false) -> Node3D:
	if not is_instance_valid(cabin) or not path.is_absolute_path() or path.get_extension().to_lower() != "glb":
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var model := document.generate_scene(state) as Node3D
	if rig_study:
		if model == null or not cabin.is_inside_tree():
			if is_instance_valid(model):
				model.free()
			return null
		cabin.add_child(model)
		if normalize_rig_groups(model):
			return model
		model.free()
		return null
	if not valid(model):
		if is_instance_valid(model):
			model.free()
		return null
	# Blender export already supplies the axis conversion. Never add the ship's
	# cabin mount twice or rescale the existing interior to fit a character.
	cabin.add_child(model)
	return model
static func set_first_person(model: Node3D, first_person: bool) -> void:
	if not valid(model):
		return
	var heads: Array[Node3D] = []
	groups(model, "PilotHead", heads)
	heads[0].visible = not first_person
	# Body, gloves and boots remain visible; this is the local pilot only.
