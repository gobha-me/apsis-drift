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
	var invalid_marker := Navigation.new()
	root.add_child(invalid_marker)
	invalid_marker.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
	invalid_marker.size = Vector2(640, 450)
	var original_caption: Rect2 = invalid_marker.label_bounds(Vector2(100, 100))
	for rectangle: Rect2 in [Rect2(), Rect2(Vector2(NAN, 0), Vector2(100, 100)), Rect2(Vector2.ZERO, Vector2(INF, 100)), Rect2(0, 0, -100, 100), Rect2(0, 0, 100, -100), Rect2(0, 0, 0, 100), Rect2(700, 500, 100, 100)]:
		invalid_marker.exclusion_rect = rectangle
		check(invalid_marker.label_bounds(Vector2(100, 100)) == original_caption, "Invalid/offscreen exclusion displaced or hid a caption")
	for scale in [0.0, -1.0, NAN, INF]:
		invalid_marker.render_scale = scale
		check(not invalid_marker.label_bounds(Vector2(100, 100)).has_area(), "Invalid caption scale reached rectangle placement")
	invalid_marker.render_scale = 1.0
	check(not invalid_marker.label_bounds(Vector2(NAN, 100)).has_area(), "Nonfinite cue point reached caption placement")
	invalid_marker.free()
	for invalid in [-1.0, NAN, INF]:
		check(Navigation.range_text(invalid).is_empty(), "Invalid target range became a visible number")
	check(Navigation.range_text(83.4) == "83.4 m" and Navigation.range_text(1000.0) == "1.0 km" and Navigation.range_text(12345.0) == "12.3 km", "Target range lost actual metre/kilometre units")
	for bounds in [Vector2.ZERO, Vector2(-1, 720), Vector2(1280, 56), Vector2(NAN, 720), Vector2(1280, INF)]:
		check(Navigation.project(camera, Vector3(0, 0, -10), bounds).is_empty(), "Invalid marker dimensions accepted")
	for inset in [0.0, -1.0, NAN, INF, 1000.0]:
		check(Navigation.project(camera, Vector3.FORWARD, Vector2(1280, 720), inset).is_empty(), "Invalid marker inset accepted")
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
	check(marker.label_text == "HOME", "Default home marker label changed")
	marker.update_cue(camera, forward, false)
	check(absf(marker.render_scale * root.size.x / marker.size.x - 1.0) < 0.0001, "Marker changed physical size under viewport stretch")
	marker.update_cue(camera, forward, true)
	check(marker.cue.is_empty() and marker.mouse_filter == Control.MOUSE_FILTER_IGNORE, "Attached marker persisted or intercepted input")
	var ship_marker := Navigation.new()
	ship_marker.label_text = "SHIP"
	root.add_child(ship_marker)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	ship_marker.update_cue(camera, Vector3.ZERO, false)
	var expected := Navigation.project(camera, Vector3.ZERO, ship_marker.size, Navigation.INSET * ship_marker.render_scale)
	check(not expected.is_empty() and ship_marker.cue == expected and ship_marker.cue.in_view and ship_marker.mouse_filter == Control.MOUSE_FILTER_IGNORE, "Ship marker lost actual origin projection or intercepted input")
	check(marker.label_text == "HOME", "Ship marker relabelled the home marker")
	for bounds in [Vector2(320, 180), Vector2(800, 450), Vector2(1280, 720)]:
		ship_marker.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT)
		ship_marker.size = bounds
		for scale in [0.5, 1.0, 2.0]:
			ship_marker.render_scale = scale
			for label in ["HOME", "D1", "D2", "SHIP", "RETURN"]:
				ship_marker.label_text = label
				for point in [Vector2(1, 1), Vector2(bounds.x - 1, 1), Vector2(1, bounds.y - 1), bounds - Vector2.ONE]:
					var backing: Rect2 = ship_marker.label_bounds(point)
					var measured := ThemeDB.fallback_font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, maxi(1, roundi(18 * scale))).x
					check(Rect2(Vector2.ZERO, bounds).encloses(backing) and backing.size.x >= measured + 6 * scale, "Measured return/home/ship label clipped at viewport edge")
	# Live target names/ranges must remain readable at every viewport edge.
	ship_marker.display_name = "Origin Station"
	ship_marker.detail_text = "12.3 km\nBeyond horizon"
	for bounds in [Vector2(640, 450), Vector2(1280, 720)]:
		ship_marker.size = bounds
		for scale in [1.0, 2.0]:
			ship_marker.render_scale = scale
			for point in [Vector2.ONE, Vector2(bounds.x - 1, 1), Vector2(1, bounds.y - 1), bounds - Vector2.ONE]:
				var rect := ship_marker.label_bounds(point)
				check(Rect2(Vector2.ZERO, bounds).encloses(rect) and rect.size.y >= 68 * scale, "Named target/range/horizon caption clipped at viewport edge")
	check(ship_marker.caption_lines() == PackedStringArray(["Origin Station", "12.3 km", "Beyond horizon"]), "Tracking caption lost target identity, distance or horizon warning")
	# Exclude one real UI rectangle without changing the projected cue, edge
	# arrow or supplied target/range strings. Projection remains camera-owned.
	camera.position = Vector3.ZERO
	camera.rotation = Vector3.ZERO
	for bounds in [Vector2(640, 450), Vector2(1280, 720)]:
		ship_marker.size = bounds
		var viewport := Rect2(Vector2.ZERO, bounds)
		for scale in [0.5, 1.0, 2.0]:
			ship_marker.render_scale = scale
			for point in [Vector3(0, 0, -10), Vector3(100, 0, -10), Vector3(-100, 0, -10), Vector3(0, 100, -10), Vector3(100, 0, 10)]:
				var projected: Dictionary = Navigation.project(camera, point, bounds, Navigation.INSET * scale)
				ship_marker.cue = projected.duplicate(true)
				ship_marker.exclusion_rect = Rect2()
				var previous: Rect2 = ship_marker.label_bounds(projected.position)
				ship_marker.exclusion_rect = Rect2(Vector2.ZERO, bounds * 0.6)
				var placed: Rect2 = ship_marker.label_bounds(projected.position)
				check(placed.has_area() and viewport.encloses(placed) and not placed.intersects(ship_marker.exclusion_rect), "Caption overlapped the status card or escaped the viewport")
				check(placed == ship_marker.label_bounds(projected.position), "Repeated exclusion layout changed deterministic placement")
				check(placed != previous if previous.intersects(ship_marker.exclusion_rect) else placed == previous, "Exclusion moved a clear caption or retained an overlapping one")
				check(ship_marker.cue == projected and ship_marker.caption_lines() == PackedStringArray(["Origin Station", "12.3 km", "Beyond horizon"]), "Caption avoidance changed the true cue or target/range text")
				ship_marker.exclusion_rect = viewport
				check(not ship_marker.label_bounds(projected.position).has_area() and ship_marker.cue == projected, "Fully blocked viewport lost its cue or retained a caption backing")
			# A caption as wide as the viewport can still move above/below the
			# card; left/right candidates must not truncate or shrink it.
			ship_marker.display_name = "Long known target name ".repeat(30)
			ship_marker.exclusion_rect = Rect2(0, 0, bounds.x, bounds.y * 0.3)
			var wide: Rect2 = ship_marker.label_bounds(Vector2(10, 10))
			check(wide.has_area() and wide.size.x == bounds.x and viewport.encloses(wide) and not wide.intersects(ship_marker.exclusion_rect), "Full-width caption did not use the available vertical space")
			ship_marker.display_name = "Origin Station"
	ship_marker.exclusion_rect = Rect2()
	ship_marker.display_name = ""
	ship_marker.detail_text = ""
	check(ship_marker.caption_lines() == PackedStringArray([ship_marker.label_text]), "Cleared tracking text leaked into an ordinary marker")
	check(marker.label_text == "HOME", "Return label changed separate home marker")
	ship_marker.free()
	marker.free()
	camera.free()
	print("Home navigation: %d failures" % failures)
	quit(0 if failures == 0 else 1)
