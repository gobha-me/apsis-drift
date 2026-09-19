extends SceneTree
const Pilot = preload("res://pilot_presentation.gd")
var failures := 0

func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func group(parent: Node, title: String) -> Node3D:
	var node := Node3D.new()
	node.name = title
	parent.add_child(node)
	var mesh := MeshInstance3D.new()
	mesh.mesh = BoxMesh.new()
	node.add_child(mesh)
	return node

func _initialize() -> void:
	check(not Pilot.valid(null), "Null pilot accepted")
	Pilot.set_first_person(null, true)
	var model := Node3D.new()
	check(not Pilot.valid(model), "Empty pilot accepted")
	var head := group(model, "PilotHead")
	check(not Pilot.valid(model), "Head-only pilot accepted")
	var body := group(model, "PilotBody")
	check(Pilot.valid(model), "Separate body/head groups rejected")
	for first_person in [true, false, true, false]:
		Pilot.set_first_person(model, first_person)
		check(head.visible == not first_person and body.visible and model.visible, "Pilot view switch hid body or failed to restore head")
	model.remove_child(body)
	head.add_child(body)
	check(not Pilot.valid(model), "Head contains body: own-head masking would hide body")
	head.remove_child(body)
	model.add_child(body)
	var duplicate_container := Node3D.new()
	model.add_child(duplicate_container)
	group(duplicate_container, "PilotHead")
	check(not Pilot.valid(model), "Ambiguous head groups accepted")
	duplicate_container.free()
	var orphan := MeshInstance3D.new()
	orphan.mesh = BoxMesh.new()
	model.add_child(orphan)
	check(not Pilot.valid(model), "Ungrouped geometry could leave a helmet visible in first person")
	orphan.free()
	var head_mesh: MeshInstance3D = head.get_child(0)
	head_mesh.mesh = null
	check(not Pilot.valid(model), "Empty head mesh counted as a usable asset")
	var cabin := Node3D.new()
	for path in ["", "relative.glb", "/not-a-pilot.txt"]:
		check(Pilot.load_into(cabin, path) == null and cabin.get_child_count() == 0, "Malformed pilot path changed cabin")
	model.free()
	cabin.free()
	print("Pilot visibility contracts: %d failures; mesh hierarchy only, not asset fit" % failures)
	quit(0 if failures == 0 else 1)
