extends SceneTree
const Navigation = preload("res://scripts/native/home_navigation.gd")
var failures := 0

func check(value: bool, reason: String) -> void:
	if not value:
		push_error(reason)
		failures += 1

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if DisplayServer.get_name() != "headless" or AudioServer.get_driver_name() != "Dummy": quit(1); return
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.current = true
	# Invalid geometry refuses before any projection or drawing.
	for bounds in [Vector2.ZERO, Vector2(-1, 720), Vector2(1280, 56), Vector2(NAN, 720), Vector2(1280, INF)]:
		check(Navigation.project(camera, Vector3(0, 0, -10), bounds).is_empty(), "Invalid marker dimensions accepted")
	for point in [Vector3.ZERO, Vector3(NAN, 0, -10), Vector3(0, INF, -10), Vector3(1.0e30, 0, -10)]:
		check(Navigation.project(camera, point, Vector2(1280, 720)).is_empty(), "Invalid/coincident marker target accepted")
	check(Navigation.project(null, Vector3.FORWARD, Vector2(1280, 720)).is_empty(), "Missing camera accepted")
	camera.basis = Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO)
	check(Navigation.project(camera, Vector3.FORWARD, Vector2(1280, 720)).is_empty(), "Collapsed camera accepted")
	camera.basis = Basis.IDENTITY
	for pixels in [Vector2i(1280, 720), Vector2i(800, 450), Vector2i(1920, 1080)]:
		root.size = pixels
		for frame in 2: await process_frame
		var bounds := Vector2(pixels)
		var middle := Navigation.project(camera, Vector3(0, 0, -10), bounds)
		check(middle.in_view and not middle.behind and middle.position.distance_to(bounds * 0.5) < 0.01, "Forward marker missed screen centre")
		for point in [Vector3(0.5, 0, -10), Vector3(-0.5, 0, -10), Vector3(0, 0.5, -10)]:
			var cue := Navigation.project(camera, point, bounds)
			check(cue.in_view and not cue.behind and cue.position.distance_to(camera.unproject_position(point) / camera.get_viewport().get_visible_rect().size * bounds) < 0.01, "Visible marker diverged from actual camera projection")
		for point in [Vector3(100, 0, -10), Vector3(-100, 0, -10), Vector3(0, 100, -10), Vector3(0, -100, -10), Vector3(100, 100, -10), Vector3(100, 0, 0), Vector3(-100, 0, 0), Vector3(100, 0, 10), Vector3(-100, 0, 10), Vector3(0, 0, 10)]:
			var cue := Navigation.project(camera, point, bounds)
			check(not cue.is_empty() and not cue.in_view and cue.position.is_finite() and Rect2(Vector2.ZERO, bounds).has_point(cue.position), "Off-screen marker escaped viewport")
			check(cue.behind == (point.z > 0) and absf(cue.direction.length() - 1) < 0.0001, "Behind/edge marker lost direction")
			if point.x > 0: check(cue.position.x > bounds.x * 0.5, "Right-hand target reflected to left edge")
			if point.x < 0: check(cue.position.x < bounds.x * 0.5, "Left-hand target reflected to right edge")
		# Different render-pixel/logical UI dimensions must map correctly.
		var logical := bounds * 0.5
		var scaled := Navigation.project(camera, Vector3(0.5, 0, -10), logical)
		check(scaled.position.distance_to(camera.unproject_position(Vector3(0.5, 0, -10)) / camera.get_viewport().get_visible_rect().size * logical) < 0.01, "Marker confused render pixels and logical UI")
	camera.position = Vector3(10, 5, 20)
	camera.rotation.y = PI * 0.5
	var forward: Vector3 = camera.position - camera.basis.z * 100
	check(Navigation.project(camera, forward, Vector2(root.size)).in_view, "Rotated/translated camera lost forward home")
	check(Navigation.globe_occludes(Vector3(-20, 0, 0), Vector3(20, 0, 0), Vector3.ZERO, 10), "Globe-crossing sightline not disclosed")
	check(not Navigation.globe_occludes(Vector3(-20, 11, 0), Vector3(20, 11, 0), Vector3.ZERO, 10), "Clear sightline became blocked")
	check(not Navigation.globe_occludes(Vector3(20, 0, 0), Vector3(30, 0, 0), Vector3.ZERO, 10), "Globe behind camera blocked target")
	for radius in [NAN, INF, -1.0, 0.0]: check(not Navigation.globe_occludes(Vector3(-20, 0, 0), Vector3(20, 0, 0), Vector3.ZERO, radius), "Invalid globe geometry used")
	var marker := Navigation.new()
	root.add_child(marker)
	marker.update_cue(camera, forward, true)
	check(marker.cue.is_empty() and marker.mouse_filter == Control.MOUSE_FILTER_IGNORE, "Attached marker persisted or intercepted input")
	marker.free()
	camera.free()
	print("Home navigation: %d failures" % failures)
	quit(0 if failures == 0 else 1)
