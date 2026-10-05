extends SceneTree
## Integration checks against the unchanged, staged Hero source dependencies.
const RESOURCES := ["res://actors/pixel_pilot.tscn", "res://trials/walk-04/pilot_presentation.gd", "res://trials/casual-male-01/pilot.gd", "res://trials/flight-pair-01/pilot.gd", "res://trials/casual-actions-01/pilot.gd"]
var checks := 0
var failures := 0

func check(value: bool, description: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error(description)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	# These optional, privately staged assets are absent from the normal native
	# project. Avoid preload so ordinary editor import can still parse this test.
	for path in RESOURCES:
		if not ResourceLoader.exists(path):
			push_error("Character test requires a staged film project: " + path)
			quit(2)
			return
	var Fixed: Variant = load(RESOURCES[0])
	var Directional: Variant = load(RESOURCES[1])
	var Casual: Variant = load(RESOURCES[2])
	var Suit: Variant = load(RESOURCES[3])
	var CasualActions: Variant = load(RESOURCES[4])
	var fixed: Variant = Fixed.instantiate()
	root.add_child(fixed)
	fixed.transform = Transform3D(Basis(Vector3.UP, 0.4), Vector3(3, 2, 1))
	var original: Transform3D = fixed.transform
	for family in ["idle", "walk", "press", "service"]:
		var count := 1 if family == "idle" else (8 if family == "walk" else 4)
		for frame in range(count):
			check(fixed.set_visual_pose(family, frame), "female fixed " + family)
			check(fixed.transform == original, "pose must retain caller root")
	for frame in range(4):
		check(fixed.set_visual_pose("press", frame, "quarter"), "female quarter press")
	check(not fixed.set_visual_pose("service", 0, "quarter"), "service has no quarter source")
	check(not fixed.set_visual_pose("walk", 8), "reject frame overflow")
	check(not fixed.set_visual_pose("idle", -1), "reject negative frame")
	check(fixed.set_visual_pose("service", 2), "working pose")
	check(fixed.anchor_available("right_grip") and fixed.right_grip.position.is_finite(), "service grip present")
	check(not fixed._card.no_depth_test, "female world depth enabled")
	check(is_equal_approx(fixed._card.pixel_size * 144, 1.78), "female metres")
	check(fixed.set_visual_pose("idle", 0) and not fixed.anchor_available("right_grip"), "idle hides service grip")
	var directional: Variant = Directional.new()
	root.add_child(directional)
	for heading in [0.0, PI / 2, PI, -PI / 2]:
		for frame in range(8):
			check(directional.present_movement(heading, Vector3(0, 1, 4), float(frame) * 1.36 / 8, true), "directional walk")
	check(not directional.present_movement(NAN, Vector3(0, 1, 4), 0, false), "nonfinite heading")
	check(not directional.present_movement(0, Vector3.ZERO, 0, false), "coincident camera")
	for frame in range(4):
		check(directional.present_contact("touch", frame, PI / 2), "touch source")
	check(directional.anchor_available("right_fingertip"), "touch fingertip authored")
	check(not directional.present_contact("touch", 4, PI / 2), "touch overflow")
	var casual: Variant = Casual.new()
	root.add_child(casual)
	check(casual.configure(), "male casual configure")
	for view in ["front", "right", "rear", "left"]:
		check(casual.present("idle", 0, view), "male idle")
	for view in ["right", "left"]:
		for frame in range(8):
			check(casual.present("walk_side", frame, view), "male sidewalk")
		for frame in range(4):
			check(casual.present("gesture", frame, view), "male greeting")
	for view in ["front", "rear"]:
		for frame in range(4):
			check(casual.present("walk_axial", frame, view), "male axialwalk")
	check(not casual.present("service", 0, "right"), "male service unsupported")
	check(not casual.present("gesture", 4, "right"), "male greeting overflow")
	check(not casual.card.no_depth_test, "male depth enabled")
	check(is_equal_approx(casual.card.pixel_size * 160, 1.85), "male metres")
	for identity in ["female", "male"]:
		var actions: Variant = CasualActions.new()
		root.add_child(actions)
		check(actions.configure(identity), "casual held actions configure")
		for frame in range(8):
			check(actions.present(frame), "casual held actions")
		check(not actions.present(8), "casual action overflow")
		check(not actions.present(0, "left"), "asymmetric casual source never mirrored")
		check(not actions.anchor_local("missing").is_finite(), "casual missing landmark")
		check(actions.card.billboard == BaseMaterial3D.BILLBOARD_DISABLED, "casual fixed interaction plane")
		check(not actions.card.no_depth_test, "casual actions depth enabled")
		var suit: Variant = Suit.new()
		root.add_child(suit)
		check(suit.configure(identity), "suit configure")
		for helmet in [false, true]:
			for view in ["front", "right", "rear", "left"]:
				check(suit.present("idle", 0, view, helmet), "suit idle")
			for family in ["walk_side", "actions"]:
				for view in ["right", "left"]:
					for frame in range(8):
						check(suit.present(family, frame, view, helmet), "suit held action/walk")
			for view in ["front", "rear"]:
				for frame in range(4):
					check(suit.present("walk_axial", frame, view, helmet), "suit axial")
		check(not suit.present("actions", 8, "right"), "suit overflow")
		check(not suit.anchor_local("unavailable").is_finite(), "missing anchor unavailable")
		check(not suit.card.no_depth_test, "suit depth enabled")
	print("REEL_CHARACTER_INTEGRATION_CHECKS checks=", checks, " failures=", failures)
	quit(0 if failures == 0 else 1)
