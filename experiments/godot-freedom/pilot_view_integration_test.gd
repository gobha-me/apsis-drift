extends SceneTree
## Actual inherited main view/camera methods with synthetic grouped meshes.
## No imported asset, human fit, GPU pixels or controller hardware qualified.
class HeadlessMain extends "res://main.gd":
	func _ready() -> void:
		set_process(false)

	func load_model(_filename: String, _explicit_path := "") -> Node3D:
		return Node3D.new() # Inspection exhibit only; no asset bootstrap.

const Layout = preload("res://cockpit_layout.gd")
const Pilot = preload("res://pilot_presentation.gd")
var failures := 0
var study: Variant
var head: Node3D
var body: Node3D

func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)

func group(parent: Node3D, title: String) -> Node3D:
	var node := Node3D.new()
	node.name = title
	parent.add_child(node)
	var mesh := MeshInstance3D.new()
	mesh.mesh = BoxMesh.new()
	node.add_child(mesh)
	return node

func check_mount() -> void:
	check(study.pilot_cockpit.transform.is_equal_approx(Layout.mount()), "View switch changed shared cabin mount")
	check(study.seated_pilot.get_parent() == study.pilot_cockpit and study.seated_pilot.transform == Transform3D.IDENTITY, "Pilot was reparented, rescaled or given duplicate cabin mount")
	check(study.seated_pilot.global_transform.is_equal_approx(study.ship.global_transform * Layout.mount()), "Pilot global transform disagrees with once-mounted cabin")
	check(body.visible and study.seated_pilot.visible, "View switch hid pilot body/model")

func check_eye(centered: bool) -> void:
	var look := Vector2.ZERO if centered else Vector2(study.head_angles)
	var expected: Transform3D = study.ship.global_transform * Layout.mount() * Transform3D(Basis.from_euler(Vector3(look.x, look.y, 0)), Layout.eye())
	check(study.camera.global_transform.is_equal_approx(expected), "Pilot camera does not use shared mounted eye and bounded look")
	check(study.camera.fov == 75.0 and study.pilot_view and not head.visible, "First-person mode did not mask only own head")
	check_mount()

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	study = HeadlessMain.new()
	root.add_child(study)
	study.data = {"planet": {"display_name": "Synthetic view fixture", "planet_seed": 42}}
	study.surface = Node3D.new()
	study.exhibit = Node3D.new()
	study.camera = Camera3D.new()
	study.label = Label.new()
	study.caption = Label.new()
	study.sun_light = DirectionalLight3D.new()
	study.scene_environment = Environment.new()
	for node in [study.surface, study.exhibit, study.camera, study.label, study.caption, study.sun_light]:
		study.add_child(node)
	study.ship = Node3D.new()
	study.surface.add_child(study.ship)
	study.pilot_cockpit = Node3D.new()
	study.pilot_cockpit.transform = Layout.mount()
	study.ship.add_child(study.pilot_cockpit)
	study.seated_pilot = Node3D.new()
	study.pilot_cockpit.add_child(study.seated_pilot)
	head = group(study.seated_pilot, "PilotHead")
	body = group(study.seated_pilot, "PilotBody")
	check(Pilot.valid(study.seated_pilot), "Synthetic pilot hierarchy fixture invalid")
	var transforms := [Transform3D.IDENTITY,
		Transform3D(Basis.from_euler(Vector3(0.37, -1.12, 0.48)), Vector3(123, -45, 67)),
		Transform3D(Basis.from_euler(Vector3(-0.8, 2.1, -0.67)), Vector3(-650, 91, 204))]
	for transform in transforms:
		study.ship.transform = transform
		study.set_view(1)
		check(head.visible and not study.pilot_view, "Flight exterior failed to restore head")
		check_mount()
		for cycle in 2:
			study.toggle_pilot()
			check(study.head_angles == Vector2.ZERO and study.chase_angles == Vector2.ZERO, "Entering cockpit retained stale look/orbit angles")
			check_eye(true)
			study.turn_head(Vector2(85, -95))
			study.update_head_camera()
			check_eye(false)
			study.toggle_pilot()
			check(head.visible and not study.pilot_view and study.mode == 1 and study.camera.fov == 55.0, "Leaving cockpit failed to restore head/exterior view")
			check(study.head_angles == Vector2.ZERO and study.chase_angles == Vector2.ZERO, "Leaving cockpit retained stale look/orbit angles")
			check_mount()
		study.toggle_pilot()
		study.turn_head(Vector2(-125, 72))
		study.update_head_camera()
		study.set_view(1) # Explicit reset/Home path bypasses toggle_pilot.
		check(head.visible and not study.pilot_view and study.head_angles == Vector2.ZERO, "Explicit flight view reset left own head hidden")
		for mode in [2, 3, 4]:
			study.set_view(1)
			study.toggle_pilot()
			check(not head.visible, "Inspection transition fixture did not begin first-person")
			study.set_view(mode)
			check(head.visible and not study.pilot_view and study.mode == mode, "Inspection view failed to restore original pilot head")
			check_mount()
			if mode == 3:
				check(study.camera.transform.is_equal_approx(Transform3D(Basis.IDENTITY, Layout.eye())), "Standalone cockpit inspection inherited ship mount twice")
		study.set_view(1)
		study.toggle_pilot()
		check_eye(true)
	print("Pilot view integration: %d failures; inherited view/toggle/camera paths, synthetic meshes, no asset-fit or GPU qualification" % failures)
	study.queue_free()
	quit(0 if failures == 0 else 1)
