extends SceneTree
## Offline frame-addressed Godot viewport capture. Editorial cameras and optical
## shells are explicitly presentation, not demonstrated gameplay/weather physics.

const Plan = preload("res://film_plan.gd")
var study: Variant
var output := ""
var options: Dictionary = {}
var scene_assets: Dictionary = {}
var optical_shells: Array[MeshInstance3D] = []
var frame := 0
var report: Dictionary = {"schema_version": 1, "kind": "offline Godot cinematic; scripted cameras, not gameplay or a real-time benchmark", "shots": []}
var fade: ColorRect
var title: Label
var subtitle: Label
var brand: Label
var material_cache: Dictionary = {}
var initial: Dictionary
var failed := false


func _initialize() -> void:
	call_deferred("run")


func require_ok(condition: bool, message: String) -> bool:
	if not condition:
		push_error(message)
		failed = true
		quit(1)
	return condition


func run() -> void:
	for arg in OS.get_cmdline_user_args():
		var pair := arg.split("=", true, 1)
		if pair.size() == 2:
			options[pair[0]] = pair[1]
	if not require_ok(Plan.valid_options(options), "Invalid film frame rate, review flag, size or shot"):
		return
	output = options.get("--film-output", "")
	if not require_ok(not output.is_empty() and DirAccess.dir_exists_absolute(output), "Film output must be an existing empty directory"):
		return
	if not require_ok(DirAccess.get_files_at(output).is_empty(), "Refusing to overwrite a film capture directory"):
		return
	if not require_ok(DisplayServer.get_name() != "headless", "Film capture requires a GPU session"):
		return
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	if not require_ok(study.live_bridge != null and study.planet_stream != null, "Film needs --stream=true and a valid snapshot"):
		return
	study.set_process(false)
	study.set_process_unhandled_input(false)
	study.capture_path = ""
	study.label.get_parent().hide()
	study.ship.hide()
	initial = study.live_bridge.get_state()
	root.use_debanding = true
	root.msaa_3d = Viewport.MSAA_4X
	study.scene_environment.glow_enabled = true
	study.scene_environment.glow_intensity = 0.18
	study.scene_environment.glow_bloom = 0.04
	study.scene_environment.ssao_intensity = 0.8
	for kind in ["station", "ship", "cockpit"]:
		var model: Node3D = study.load_model("hero-%s-hero.glb" % kind)
		if not require_ok(model != null, "Hero GLB missing: " + kind):
			return
		study.add_child(model)
		scene_assets[kind] = model
		if kind == "ship":
			study.install_ship_interior(model, "hero")
		finish_materials(model)
		model.hide()
	study.add_cabin_lighting(scene_assets.cockpit)
	build_optics()
	if failed:
		return
	build_titles()
	var review: bool = options.get("--film-review", "false") == "true"
	var elapsed := 0.0
	for shot in Plan.SHOTS:
		if options.get("--film-shot", "all") not in ["all", shot.id]:
			elapsed += float(shot.seconds)
			continue
		print("FILM prepare " + shot.id)
		prepare_shot(shot.id)
		if failed:
			return
		pose_shot(shot.id, 0.5, elapsed)
		if shot.id in ["world", "cabin", "terrain"]:
			if not await prepare_terrain():
				return
		# After preparation the short camera move stays within that complete cover.
		# Film rendering is deliberately decoupled from worker completion timing.
		for warm in range(8):
			await process_frame
		var count: int = 3 if review else int(shot.seconds) * Plan.FPS
		var first := frame
		for i in range(count):
			var u := float(i) / float(count - 1)
			pose_shot(shot.id, u, elapsed + u * float(shot.seconds))
			update_titles(shot, u, review)
			await process_frame
			await RenderingServer.frame_post_draw
			var captured := root.get_texture().get_image()
			if not require_ok(captured.get_size() == study.render_size, "Film viewport size mismatch"):
				return
			var path := output.path_join("frame-%06d.png" % frame)
			if not require_ok(captured.save_png(path) == OK, "Could not save frame"):
				return
			frame += 1
			if i % Plan.FPS == 0:
				print("FILM %s %d/%d" % [shot.id, i + 1, count])
		report.shots.append({"id": shot.id, "first_frame": first, "frames": count,
			"seconds": shot.seconds, "stream": study.planet_stream.report(),
			"camera_start": serial_pose(Plan.sample(shot.id, 0, float(initial.planet_radius))),
			"camera_end": serial_pose(Plan.sample(shot.id, 1, float(initial.planet_radius)))})
		elapsed += float(shot.seconds)
	report.merge({"engine": Engine.get_version_info().string, "fps": Plan.FPS,
		"frames": frame, "review_only": review, "pixels": [study.render_size.x, study.render_size.y],
		"planet_seed": study.data.planet.planet_seed, "planet": study.data.planet.display_name,
		"terrain_generator_version": study.data.terrain_generator_version,
		"experimental_relief_version": study.data.get("experimental_relief_version", 0),
		"geometry": "original hero-tier GLBs; no master mesh changes",
		"atmosphere": "optical review shells, descriptor-gated; not authoritative weather",
		"capture": "PNG frames saved from Godot Vulkan viewport after frame_post_draw; loading frames excluded",
		"runtime_performance_claim": false})
	var file := FileAccess.open(output.path_join("render.json"), FileAccess.WRITE)
	if not require_ok(file != null, "Cannot save film report"):
		return
	file.store_string(JSON.stringify(report, "  ") + "\n")
	print("FILM complete: %d frames" % frame)
	quit()


func serial_pose(pose: Dictionary) -> Dictionary:
	var result := {}
	for key in pose:
		var v: Variant = pose[key]
		result[key] = [v.x, v.y, v.z] if v is Vector3 else ([v.x, v.y] if v is Vector2 else v)
	return result


func finish_materials(node: Node) -> void:
	if node is MeshInstance3D and node.mesh != null:
		for i in range(node.mesh.get_surface_count()):
			var source: Material = node.get_active_material(i)
			if not source is StandardMaterial3D or source.emission_enabled or source.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED:
				continue
			var id := source.get_instance_id()
			if not material_cache.has(id):
				var material := ShaderMaterial.new()
				material.shader = load("res://asset_finish.gdshader")
				material.set_shader_parameter("base_color", source.albedo_color)
				material.set_shader_parameter("metal", source.metallic)
				material.set_shader_parameter("rough", source.roughness)
				material_cache[id] = material
			node.set_surface_override_material(i, material_cache[id])
	for child in node.get_children():
		finish_materials(child)


func build_optics() -> void:
	var profile := Plan.atmosphere_profile(study.data.planet)
	if not require_ok(not profile.is_empty(), "Invalid atmospheric appearance descriptor"):
		return
	if not profile.enabled:
		return
	var radius: float = initial.planet_radius
	for clouds in [true, false]:
		var mesh := SphereMesh.new()
		mesh.radius = radius + (14000 if clouds else 65000)
		mesh.height = mesh.radius * 2
		mesh.radial_segments = 256
		mesh.rings = 128
		var instance := MeshInstance3D.new()
		instance.mesh = mesh
		instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var material := ShaderMaterial.new()
		material.shader = load("res://film_atmosphere.gdshader")
		material.set_shader_parameter("cloud_layer", clouds)
		material.set_shader_parameter("coverage", profile.coverage)
		var atmosphere_color: Color = profile.tint
		material.set_shader_parameter("tint", Color("c7c6c3").lerp(atmosphere_color, 0.15) if clouds else atmosphere_color.lightened(0.30))
		instance.material_override = material
		study.add_child(instance)
		optical_shells.append(instance)


func build_titles() -> void:
	var canvas := CanvasLayer.new()
	canvas.layer = 10
	study.add_child(canvas)
	var size: Vector2 = study.render_size
	var scale := size.x / 1920.0
	# 2.39:1 picture within the actual 16:9 4K delivery, no upscaling.
	var bar := (size.y - size.x / 2.39) / 2
	for y in [0.0, size.y - bar]:
		var rect := ColorRect.new()
		rect.color = Color.BLACK
		rect.position = Vector2(0, y)
		rect.size = Vector2(size.x, bar)
		canvas.add_child(rect)
	title = Label.new()
	title.position = Vector2(70 * scale, size.y - bar - 105 * scale)
	title.add_theme_font_size_override("font_size", int(29 * scale))
	title.add_theme_color_override("font_color", Color("d9e9ed"))
	title.add_theme_color_override("font_shadow_color", Color.BLACK)
	title.add_theme_constant_override("shadow_offset_x", 1)
	title.add_theme_constant_override("shadow_offset_y", 2)
	canvas.add_child(title)
	subtitle = Label.new()
	subtitle.position = title.position + Vector2(0, 43 * scale)
	subtitle.add_theme_font_size_override("font_size", int(16 * scale))
	subtitle.add_theme_color_override("font_color", Color("a7b7c0"))
	subtitle.add_theme_color_override("font_shadow_color", Color.BLACK)
	subtitle.add_theme_constant_override("shadow_offset_y", 2)
	canvas.add_child(subtitle)
	brand = Label.new()
	brand.text = "APSIS DRIFT   /   GODOT ENGINE     •     SCRIPTED TECH PREVIEW"
	brand.position = Vector2(70 * scale, (bar - 15 * scale) / 2)
	brand.add_theme_font_size_override("font_size", int(14 * scale))
	brand.add_theme_color_override("font_color", Color("718490"))
	canvas.add_child(brand)
	fade = ColorRect.new()
	fade.color = Color(0, 0, 0, 0)
	fade.size = size
	canvas.add_child(fade)


func prepare_shot(id: String) -> void:
	for model in scene_assets.values():
		model.hide()
	for shell in optical_shells:
		shell.visible = id == "world"
	study.surface.visible = id in ["world", "terrain", "cabin"]
	study.ship.hide()
	study.camera.near = 0.1
	study.camera.far = 10000
	study.scene_environment.background_mode = Environment.BG_COLOR
	study.scene_environment.background_color = Color("010207")
	study.scene_environment.fog_enabled = false
	study.scene_environment.ambient_light_energy = 0.13
	study.sun_light.light_energy = 2.2
	study.sun_light.rotation_degrees = Vector3(-28, -38, 0)
	study.sun_light.directional_shadow_max_distance = 500
	if id == "world":
		study.camera.near = 1000
		study.camera.far = float(initial.planet_radius) * 8
		study.scene_environment.ambient_light_energy = 0.025
		study.sun_light.rotation_degrees = Vector3(-18, -110, 0)
	elif id == "station":
		scene_assets.station.show()
	elif id in ["shuttle", "closing"]:
		scene_assets.ship.show()
	elif id in ["cabin", "terrain"]:
		study.scene_environment.background_mode = Environment.BG_SKY
		study.scene_environment.fog_enabled = true
		study.scene_environment.fog_density = 0.000012
		study.scene_environment.ambient_light_energy = 0.32
		study.sun_light.rotation_degrees = Vector3(-17, -55, 0)
		study.sun_light.directional_shadow_max_distance = 16000 if id == "terrain" else 1000
		study.camera.far = 100000
		var altitude := float(initial.altitude) + (2300.0 if id == "cabin" else 0.0)
		if not require_ok(study.live_bridge.set_survey_pose(initial.latitude, initial.longitude, altitude), "Invalid survey fixture"):
			return
		if id == "cabin":
			scene_assets.cockpit.show()
			study.sun_light.light_energy = 1.35
		else:
			study.camera.near = 1.0


func pose_shot(id: String, u: float, seconds: float) -> void:
	var pose := Plan.sample(id, u, float(initial.planet_radius))
	study.camera.position = pose.eye
	study.camera.fov = pose.fov
	if id == "cabin":
		study.camera.basis = Basis.from_euler(Vector3(pose.head.x, pose.head.y, 0))
	else:
		study.camera.look_at(pose.target)
	for shell in optical_shells:
		shell.position = Vector3(0, -float(initial.planet_radius) - float(initial.altitude), 0)
		shell.material_override.set_shader_parameter("phase", seconds)
		shell.material_override.set_shader_parameter("sun_direction", study.sun_light.global_basis.z)


func prepare_terrain() -> bool:
	var last_swaps: int = study.planet_stream.swaps
	var stable := 0
	var until := Time.get_ticks_msec() + 30000
	while Time.get_ticks_msec() < until:
		study.planet_stream.tick(1.0 / Plan.FPS, study.camera.position)
		if not require_ok(study.planet_stream.error.is_empty(), study.planet_stream.error):
			return false
		if not require_ok(study.planet_stream.nodes.size() <= 768, "Film preparation exceeded tile budget"):
			return false
		if study.planet_stream.is_ready and study.planet_stream.pending.is_empty() and not study.live_bridge.is_stream_busy() and study.planet_stream.swaps == last_swaps:
			stable += 1
		else:
			stable = 0
		last_swaps = study.planet_stream.swaps
		if stable >= 40:
			return true
		await process_frame
	return require_ok(false, "Film terrain preparation timed out")


func update_titles(shot: Dictionary, u: float, review: bool) -> void:
	title.text = shot.title
	subtitle.text = shot.detail
	var seconds := u * float(shot.seconds)
	var remaining := (1.0 - u) * float(shot.seconds)
	# Short dip-to-black joins protect cuts; timing is deterministic, not render speed.
	var opacity := minf(clampf(seconds / 0.65, 0, 1), clampf(remaining / 0.65, 0, 1))
	fade.color.a = 0 if review else 1.0 - opacity
	title.modulate.a = 1 if review else clampf((seconds - 0.6) / 0.6, 0, 1)
	subtitle.modulate.a = title.modulate.a
