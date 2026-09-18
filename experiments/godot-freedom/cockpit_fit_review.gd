extends SceneTree
## Native design inspection, not gameplay or ergonomic certification.
const Layout = preload("res://cockpit_layout.gd")
var study: Variant
var cabin: Node3D
var exterior: Node3D
var exterior_cabin: Node3D
var dummies: Dictionary = {}
var original: Dictionary = {}
var title: Label
var note: Label
var output := ""
var poses: Array = []

func glazing_vertices(node: Node, result: Dictionary) -> void:
	if node is MeshInstance3D and str(node.name).replace("_", " ").begins_with("Cell glazing"):
		var points: Array = []
		for surface in range(node.mesh.get_surface_count()):
			for vertex in node.mesh.surface_get_arrays(surface)[Mesh.ARRAY_VERTEX]:
				points.append(node.global_transform * vertex)
		result[str(node.name)] = points
	for child in node.get_children():
		glazing_vertices(child, result)

func _initialize() -> void:
	call_deferred("run")

func require_ok(ok: bool, message: String) -> bool:
	if not ok:
		push_error(message)
		quit(1)
	return ok

func remember(node: Node) -> void:
	if node is Node3D:
		original[node.get_instance_id()] = {"position": node.position, "visible": node.visible}
	for child in node.get_children():
		remember(child)

func restore(node: Node) -> void:
	if node is Node3D and original.has(node.get_instance_id()):
		var saved: Dictionary = original[node.get_instance_id()]
		node.position = saved.position
		node.visible = saved.visible
	for child in node.get_children():
		restore(child)

func adjust(node: Node, pose: Dictionary) -> void:
	if node is Node3D:
		if str(node.name).begins_with("SeatMoving"):
			node.position += Layout.converted(pose.seat_delta)
		if str(node.name).begins_with("PedalMoving"):
			node.position += Layout.converted(pose.pedal_delta)
	for child in node.get_children():
		adjust(child, pose)

func cutaway(node: Node) -> void:
	if node is MeshInstance3D:
		var part := str(node.name).replace("_", " ")
		if part.begins_with("Cell pressure skin") or part.begins_with("Cell glazing") or part.begins_with("Cell inner") or part.begins_with("Cell structural frame") or part.begins_with("Rescue stencil"):
			node.hide()
		if node.position.x > 0.5 and (part.begins_with("Secondary console") or part.begins_with("POWER")):
			node.hide()
	for child in node.get_children():
		cutaway(child)

func aim(eye: Array, target: Array, fov: float) -> void:
	study.camera.position = Layout.converted(eye)
	study.camera.look_at(Layout.converted(target))
	study.camera.fov = fov

func save_frame(name: String) -> bool:
	for i in range(12):
		await process_frame
	await RenderingServer.frame_post_draw
	var captured := root.get_texture().get_image()
	if not require_ok(captured.get_size() == study.render_size, "Fit review viewport mismatch"):
		return false
	if not require_ok(captured.save_png(output.path_join(name + ".png")) == OK, "Cannot save fit review"):
		return false
	print("FIT REVIEW " + name)
	return true

func run() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--fit-output="):
			output = arg.trim_prefix("--fit-output=")
	if not require_ok(not output.is_empty() and DirAccess.dir_exists_absolute(output) and DirAccess.get_files_at(output).is_empty(), "Fit review needs an existing empty output directory"):
		return
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	study.set_process(false)
	study.set_process_unhandled_input(false)
	study.capture_path = ""
	study.surface.hide()
	study.exhibit.hide()
	study.label.get_parent().hide()
	study.scene_environment.fog_enabled = false
	study.scene_environment.ambient_light_energy = 0.65
	study.sun_light.rotation_degrees = Vector3(-38, -28, 0)
	study.sun_light.light_energy = 1.25
	study.camera.near = 0.025
	study.camera.far = 10000
	root.msaa_3d = Viewport.MSAA_4X
	root.use_debanding = true
	cabin = study.load_model("hero-cockpit-hero.glb")
	exterior = study.load_model("hero-ship-hero.glb")
	if not require_ok(cabin != null and exterior != null, "Missing fit assets"):
		return
	study.add_child(cabin)
	study.add_child(exterior)
	study.add_cabin_lighting(cabin)
	exterior_cabin = study.install_ship_interior(exterior, "hero")
	var interior_windows: Dictionary = {}
	var exterior_windows: Dictionary = {}
	glazing_vertices(cabin, interior_windows)
	# Do not collect the mounted duplicate (hidden) interior envelope here.
	for child in exterior.get_children():
		if child != exterior_cabin:
			glazing_vertices(child, exterior_windows)
	if not require_ok(interior_windows.size() == 5 and exterior_windows.size() == 5, "GLB glazing topology mismatch"):
		return
	for key in interior_windows:
		if not require_ok(exterior_windows.has(key) and exterior_windows[key].size() == interior_windows[key].size(), "GLB glazing identity mismatch"):
			return
		for i in range(interior_windows[key].size()):
			if not require_ok((Layout.mount() * interior_windows[key][i]).distance_to(exterior_windows[key][i]) < 0.00002, "GLB interior/exterior window mismatch"):
				return
	print("FIT shared exported glazing: 5 panels matched within 0.02 mm")
	var report: Variant = JSON.parse_string(FileAccess.get_file_as_string(study.assets_path.path_join("cockpit-fit-report.json")))
	if not require_ok(report is Dictionary and report.get("revision") == 5 and report.get("poses") is Array and report.poses.size() == 3, "Invalid fit pose report"):
		return
	poses = report.poses
	for pose in poses:
		var dummy: Node3D = study.load_model("cockpit-fit-%s.glb" % pose.name)
		if not require_ok(dummy != null, "Missing mannequin"):
			return
		study.add_child(dummy)
		dummies[pose.name] = dummy
		dummy.hide()
	remember(cabin)
	remember(exterior)
	var canvas := CanvasLayer.new()
	study.add_child(canvas)
	var scale: float = study.render_size.x / 1920.0
	title = Label.new()
	title.position = Vector2(35, 25) * scale
	title.add_theme_font_size_override("font_size", int(28 * scale))
	title.add_theme_color_override("font_shadow_color", Color.BLACK)
	title.add_theme_constant_override("shadow_offset_y", 2)
	canvas.add_child(title)
	note = Label.new()
	note.position = Vector2(35, 1010) * scale
	note.add_theme_font_size_override("font_size", int(20 * scale))
	note.add_theme_color_override("font_shadow_color", Color.BLACK)
	note.add_theme_constant_override("shadow_offset_y", 2)
	canvas.add_child(note)
	for pose in poses:
		restore(cabin)
		cabin.show()
		exterior.hide()
		adjust(cabin, pose)
		study.camera.position = Layout.converted(pose.eye)
		study.camera.basis = Basis.from_euler(Vector3(-0.12, 0, 0))
		study.camera.fov = 75
		title.text = "APSIS DRIFT / FLIGHT CELL 05 / CONTROLS %d / %.2f m PILOT EYE" % [Layout.spec().get("control_layout_revision", 1), pose.stature]
		note.text = "Physical glazing shared with exterior / adjusted seat and pedals / static instrument art\nSynthetic fit fixture, not population coverage or ergonomic certification"
		if not await save_frame("pilot-" + pose.name): return
		cutaway(cabin)
		dummies[pose.name].show()
		aim([2.8, 0.65, 1.75], [0, 0.15, 0.80], 56)
		title.text = "APSIS DRIFT / %.2f m SUITED FIT / CUTAWAY" % pose.stature
		note.text = "Primary controls at armrests / nearby flight switches / service panels require unstrapping\nShell, frame and near-side secondary console removed for inspection / synthetic suited design dummy"
		if not await save_frame("fit-" + pose.name): return
		if pose.name == "medium":
			aim([3.5, -0.10, 1.20], [0, 0.17, 0.80], 44)
			title.text = "APSIS DRIFT / SEATED REACH AND PEDALS / SIDE CUTAWAY"
			if not await save_frame("fit-side"): return
		dummies[pose.name].hide()
	cabin.hide()
	restore(exterior)
	exterior.show()
	var medium: Node3D = dummies.medium
	medium.transform = Layout.mount()
	medium.show()
	aim([6.5, 10.0, 4.7], [0, 3.6, 1.6], 47)
	title.text = "APSIS DRIFT / ONE FLIGHT CELL / REAL GLAZING"
	note.text = "Same window vertices and pilot location inside and outside / translation only, no scale correction\nCockpit revision 5 / optional suit mannequin shown for fit review"
	if not await save_frame("assembled-exterior"): return
	cutaway(exterior)
	aim([6.5, 5.5, 4.4], [0, 3.7, 1.35], 48)
	title.text = "APSIS DRIFT / ASSEMBLED HULL AND PILOT / CUTAWAY"
	note.text = "Pressure skins removed to expose seat, pedals and control layout inside the unchanged-size craft\nCamera-fed viewports remain an alternative; this candidate uses actual transparent openings"
	if not await save_frame("assembled-cutaway"): return
	restore(exterior)
	aim([18, 24, 12], [0, 0, 1], 43)
	title.text = "APSIS DRIFT / SHUTTLE WITH SHARED FLIGHT CELL"
	note.text = "Revised nose and canopy around the same human-scale interior / stowed flight gear\nDesign review only: no landing, walking, adjustable-seat UI or interactive instruments implemented"
	if not await save_frame("shuttle"): return
	var receipt := {"schema_version": 1, "geometry_revision": 5, "control_layout_revision": Layout.spec().get("control_layout_revision", 1), "pixels": [study.render_size.x, study.render_size.y], "poses": poses, "cabin_to_ship": Layout.spec().cabin_to_ship, "images": 10, "exported_glazing_match": "5 GLB panels, vertex mismatch below 0.02 mm", "kind": "native Godot static fit inspection, not gameplay or certified ergonomics"}
	var file := FileAccess.open(output.path_join("review.json"), FileAccess.WRITE)
	if not require_ok(file != null, "Cannot write fit receipt"): return
	file.store_string(JSON.stringify(receipt, "  ") + "\n")
	quit()
