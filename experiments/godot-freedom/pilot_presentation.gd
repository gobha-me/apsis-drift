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

static func load_into(cabin: Node3D, path: String) -> Node3D:
	if not is_instance_valid(cabin) or not path.is_absolute_path() or path.get_extension().to_lower() != "glb":
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	var model := document.generate_scene(state) as Node3D
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
