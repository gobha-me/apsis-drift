extends Node3D
## Native presentation of C++ terrain; recorded or optional live C++ flight.
## No Godot-owned flight physics, collision, persistence or substitute generator.

var data: Dictionary
var camera: Camera3D
var surface: Node3D
var ship: Node3D
var exhibit: Node3D
var label: Label
var caption: Label
var mode := 1
var replay_time := 0.0
var replay_running := false
var capture_path := ""
var capture_evidence: Dictionary = {}
var capture_frames := 0
var frame_times: Array[float] = []
var load_started := Time.get_ticks_usec()
var load_ms := 0.0
var options: Dictionary = {}
var assets_path := ""
var last_frame_usec := 0
var scene_environment: Environment
var sun_light: DirectionalLight3D
var render_size := Vector2i.ZERO
var live_bridge: Variant = null
var live_paused := false
var window_focused := true
var clear_live_input := false
var snapshot_text := ""
var pilot_cockpit: Node3D
var pilot_view := false
var head_angles := Vector2.ZERO # pitch/yaw; same centerline as recenter and look release
const CockpitLayout = preload("res://cockpit_layout.gd")
const AssetMaterials = preload("res://asset_materials.gd")
var pilot_eye := CockpitLayout.eye()
const HEAD_YAW_LIMIT := 2.44 # 140 degrees: shoulders remain in the seat
const FLIGHT_KEYS := [KEY_W, KEY_S, KEY_A, KEY_D, KEY_Q, KEY_E, KEY_SPACE, KEY_CTRL]
var focus_blocked_keys: Dictionary = {}
var streaming_requested := false
var planet_stream: Node3D
var player_input: Node
var pause_menu: CanvasLayer
var flight_displays: Node3D
var flight_plan_menu: CanvasLayer
var flight_plan := 0 # Presentation-only selection; never part of flight state.
var guidance_elapsed := 0.0
var flight_guidance: Dictionary = {}
var guidance_overlay: Control
var navigation_sky: ShaderMaterial
var debug_visible := true
var debug_backdrop: ColorRect
var chase_angles := Vector2.ZERO
var chase_distance := 36.0
var chase_follow := preload("res://chase_camera.gd").new()
var live_presentation := false
var world_view: Node


func fail(message: String) -> void:
	push_error(message)
	get_tree().quit(1)


func finite_number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))


func valid_snapshot(value: Variant) -> bool:
	if not value is Dictionary:
		return false
	if value.get("schema_version") != 1 or value.get("terrain_generator_version") != 1:
		return false
	if value.get("seed_derivation_version") != 1:
		return false
	var relief: Variant = value.get("experimental_relief_version", 0)
	if not finite_number(relief) or relief < 0 or relief > 1 or relief != int(relief):
		return false
	if not finite_number(value.get("lod")) or value.lod != int(value.lod) or value.lod < 0 or value.lod > 10:
		return false
	var n: Variant = value.get("samples")
	if not finite_number(n) or n != int(n) or n < 2 or n > 513:
		return false
	if not value.get("vertices") is Array or not value.get("colors") is Array:
		return false
	if value.vertices.size() != int(n) * int(n) or value.colors.size() != value.vertices.size():
		return false
	for i in range(value.vertices.size()):
		if not value.vertices[i] is Array or value.vertices[i].size() != 3:
			return false
		if not value.colors[i] is Array or value.colors[i].size() != 3:
			return false
		for component in value.vertices[i]:
			if not finite_number(component) or abs(component) > 1000000:
				return false
		for component in value.colors[i]:
			if not finite_number(component) or component < 0 or component > 255:
				return false
	if not value.get("frame") is Dictionary or value.frame.get("axes") != "east,up,-north":
		return false
	if value.frame.get("units") != "metres" or not finite_number(value.frame.get("altitude_metres")):
		return false
	if not value.get("planet") is Dictionary or not value.planet.get("display_name") is String:
		return false
	if value.planet.get("schema_version") != 1 or value.planet.get("generator_version") != 1:
		return false
	if not value.planet.get("planet_id") is String:
		return false
	if not value.planet.get("planet_seed") is String or not finite_number(value.get("span_metres")):
		return false
	if value.span_metres < 64 or value.span_metres > 262144 or value.get("simulation_hz") != 120:
		return false
	if not value.get("replay") is Array or value.replay.size() != 301:
		return false
	for i in range(value.replay.size()):
		var pose: Variant = value.replay[i]
		if not pose is Dictionary or pose.get("tick") != i * 4 or not pose.get("checksum") is String:
			return false
		if not finite_number(pose.get("heading")) or not pose.get("position") is Array:
			return false
		if pose.position.size() != 3:
			return false
		for component in pose.position:
			if not finite_number(component) or abs(component) > 1000000:
				return false
	return true


func _ready() -> void:
	for argument in OS.get_cmdline_user_args():
		var pair := argument.split("=", true, 1)
		if pair.size() == 2:
			options[pair[0]] = pair[1]
	var snapshot_path: String = options.get("--snapshot", "")
	assets_path = options.get("--assets", "")
	if options.get("--asset-materials", "tuned") not in ["baseline", "tuned"]:
		fail("Asset materials must be baseline or tuned")
		return
	if not pilot_eye.is_finite():
		fail("Invalid cockpit eye anchor")
		return
	if snapshot_path.is_empty():
		fail("Supply --snapshot=/absolute/path/to/snapshot.json after --")
		return
	var input := FileAccess.open(snapshot_path, FileAccess.READ)
	if input == null or input.get_length() > 96 * 1024 * 1024:
		fail("Snapshot missing or larger than 96 MiB")
		return
	snapshot_text = input.get_as_text()
	var decoded: Variant = JSON.parse_string(snapshot_text)
	if not valid_snapshot(decoded):
		fail("Invalid snapshot schema, dimensions, identity, buffers or replay")
		return
	data = decoded
	if options.has("--validate-only"):
		print("Godot snapshot consumer validation passed")
		get_tree().quit()
		return
	if assets_path.is_empty():
		fail("Supply --assets=/absolute/path/to/assets/visual after --")
		return
	if DisplayServer.get_name() == "headless":
		fail("Rendering requires a graphics session; use --validate-only=true for headless validation")
		return
	streaming_requested = options.get("--stream", "false") == "true"
	if options.get("--flight-model", "legacy") not in ["legacy", "thrust"]:
		fail("Flight model must be legacy or thrust")
		return
	if options.get("--live", "false") == "true" or streaming_requested:
		if not ClassDB.class_exists("FreedomBridge"):
			GDExtensionManager.load_extension("res://bin/freedom.gdextension")
		if not ClassDB.class_exists("FreedomBridge"):
			fail("Live adapter missing; build with APSIS_DRIFT_GODOT_LIVE=ON")
			return
		live_bridge = ClassDB.instantiate("FreedomBridge")
		if not live_bridge.initialize(snapshot_text):
			fail("Live C++ initialization failed: " + str(live_bridge.get_last_error()))
			return
	if options.get("--flight-model", "legacy") == "thrust":
		if live_bridge == null or options.get("--controls", "true") != "true" or options.has("--capture"):
			fail("Thrust flight requires --live=true or --stream=true and interactive controls")
			return
		if not live_bridge.enable_surface_practice():
			fail("Thrust flight initialization failed: " + str(live_bridge.get_last_error()))
			return
	capture_path = options.get("--capture", "")
	if options.has("--render-size"):
		if options["--render-size"] not in ["1920x1080", "3840x2160"]:
			fail("Render size must be 1920x1080 or 3840x2160")
			return
		render_size = Vector2i(3840, 2160) if options["--render-size"] == "3840x2160" else Vector2i(1920, 1080)
		get_window().content_scale_mode = Window.CONTENT_SCALE_MODE_VIEWPORT
		get_window().content_scale_size = render_size
	camera = Camera3D.new()
	surface = Node3D.new()
	exhibit = Node3D.new()
	label = Label.new()
	caption = Label.new()
	add_child(surface)
	add_child(exhibit)
	build_environment()
	if streaming_requested:
		start_stream()
	else:
		build_terrain()
	ship = load_model("hero-ship-near.glb")
	if ship == null:
		return
	surface.add_child(ship)
	pilot_cockpit = install_ship_interior(ship)
	ship.position = Vector3.ZERO if streaming_requested else vector(data.replay[0].position)
	ship.rotation.y = float(data.replay[0].heading) - PI / 2.0
	add_child(camera)
	get_viewport().msaa_3d = Viewport.MSAA_4X
	camera.near = 0.15
	camera.far = 100000
	camera.fov = 55
	camera.make_current()
	build_overlay()
	live_presentation = live_bridge != null and options.get("--flight-model", "legacy") == "thrust"
	if live_presentation:
		debug_visible = options.get("--debug", "false") == "true"
		flight_displays = preload("res://flight_displays.gd").new()
		pilot_cockpit.add_child(flight_displays)
		navigation_sky = preload("res://navigation_sky.gd").material(live_bridge.get_sky_catalog(), Color(data.planet.palette.atmosphere).lightened(0.22), float(data.planet.atmosphere.pressure_millibars))
		scene_environment.sky.sky_material = navigation_sky
		world_view = preload("res://world_view.gd").new()
		add_child(world_view)
		world_view.install(self)
	if capture_path.is_empty() and options.get("--controls", "true") == "true":
		setup_player_controls()
	set_view(int(options.get("--view", "1")))
	if options.get("--pilot", "false") == "true" and live_bridge != null and mode == 1:
		toggle_pilot()
	var look: String = options.get("--look", "front")
	if look not in ["front", "left", "right", "floor", "aft", "underside"]:
		fail("Unknown inspection look preset")
		return
	if mode == 3 or pilot_view:
		match look:
			"left": head_angles = Vector2(-0.24, 1.25)
			"right": head_angles = Vector2(-0.24, -1.25)
			"floor": head_angles = Vector2(-1.10, 0.32)
			"aft": head_angles = Vector2(-0.14, HEAD_YAW_LIMIT)
		update_head_camera()
	elif look == "underside" and mode == 1:
		camera.position = ship.position + ship.basis * Vector3(15, -11, -18)
		camera.look_at(ship.position)
	replay_running = options.get("--replay", "false") == "true" and mode == 1 and live_bridge == null
	load_ms = (Time.get_ticks_usec() - load_started) / 1000.0
	last_frame_usec = Time.get_ticks_usec()
	print("%s: C++ world loaded in %.1f ms; %d terrain vertices" % [
		"LIVE C++ STUDY" if live_bridge != null else "READ-ONLY STUDY", load_ms, data.vertices.size()])
	set_process(true)
	if player_input != null and options.get("--start-paused", "false") == "true":
		set_player_paused(true, "Connect your controller, review the bindings, then resume when ready.")


func vector(values: Array) -> Vector3:
	return Vector3(float(values[0]), float(values[1]), float(values[2]))


func setup_player_controls() -> void:
	player_input = preload("res://player_input.gd").new()
	player_input.thrust_mode = options.get("--flight-model", "legacy") == "thrust"
	player_input.persist = options.get("--controls-persist", "true") == "true"
	add_child(player_input)
	pause_menu = preload("res://pause_menu.gd").new()
	pause_menu.controls = player_input
	pause_menu.rotational_coasting = live_bridge != null and live_bridge.get_state().get("flight_model", "") == "thrust-lab-2"
	add_child(pause_menu)
	flight_plan_menu = preload("res://flight_plan_menu.gd").new()
	flight_plan_menu.setup(player_input)
	add_child(flight_plan_menu)
	flight_plan_menu.plan_selected.connect(select_flight_plan)
	var guidance_layer := CanvasLayer.new()
	guidance_layer.layer = 8
	add_child(guidance_layer)
	guidance_overlay = preload("res://guidance_screen.gd").new()
	guidance_overlay.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	guidance_overlay.position = Vector2(-440, 24)
	guidance_overlay.size = Vector2(420, 385)
	guidance_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	guidance_layer.add_child(guidance_overlay)
	guidance_overlay.hide()
	player_input.pause_requested.connect(func():
		if flight_plan_menu.active:
			flight_plan_menu.close()
		else:
			set_player_paused(not pause_menu.panel.visible))
	pause_menu.guidance_requested.connect(func():
		set_player_paused(false)
		flight_plan_menu.open(flight_plan))
	player_input.safety_pause.connect(func(reason: String): set_player_paused(true, reason))
	player_input.camera_requested.connect(func():
		if live_bridge != null and mode == 1:
			toggle_pilot())
	player_input.recenter_requested.connect(func():
		head_angles = Vector2.ZERO
		if pilot_view or mode == 3:
			update_head_camera())
	pause_menu.resumed.connect(func(): set_player_paused(false))
	pause_menu.reset_flight.connect(reset_live_flight)
	pause_menu.debug_changed.connect(func(value: bool): debug_visible = value)
	pause_menu.camera_distance_changed.connect(set_chase_distance)
	pause_menu.practice_requested.connect(func(reentry: bool):
		if not live_bridge.start_practice(reentry):
			pause_menu.controls.status = str(live_bridge.get_last_error())
			return
		player_input.assist = false
		player_input.needs_neutral = true
		head_angles = Vector2.ZERO
		chase_angles = Vector2.ZERO
		chase_follow.reset(live_bridge.get_state().body_basis)
		flight_guidance = {}
		guidance_elapsed = 1.0
		pause_menu.controls.status = "Practice start loaded (relocated, unsaved). Assist OFF. Resume when ready."
		flight_displays.elapsed = 1.0)


func select_flight_plan(value: int) -> void:
	if value < 0 or value > 3:
		return
	flight_plan = value
	flight_guidance = {}
	guidance_elapsed = 1.0
	if is_instance_valid(flight_displays):
		flight_displays.elapsed = 1.0
	if is_instance_valid(guidance_overlay):
		guidance_overlay.hide()


func plan_shortcut_available(pad: bool) -> bool:
	for action in player_input.bindings:
		var binding: Dictionary = player_input.bindings[action]["pad" if pad else "key"]
		if pad and binding.kind == "button" and int(binding.code) in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN, JOY_BUTTON_DPAD_LEFT, JOY_BUTTON_DPAD_RIGHT]:
			return false
		if not pad and int(binding.code) == KEY_G:
			return false
	return true


func set_chase_distance(value: float) -> void:
	if is_finite(value):
		chase_distance = clampf(value, 22, 100)


func orbit_camera(relative: Vector2) -> void:
	if not relative.is_finite():
		return
	chase_angles.y -= relative.x * 0.003
	chase_angles.x = clampf(chase_angles.x - relative.y * 0.003, -1.15, 1.15)


func set_player_paused(paused: bool, reason := "") -> void:
	# A queued GUI accept must not resume through the focus-safety pause.
	if not paused and not window_focused:
		return
	live_paused = paused
	if paused and is_instance_valid(flight_plan_menu):
		flight_plan_menu.close()
	player_input.set_enabled(not paused)
	if paused:
		head_angles = Vector2.ZERO
		pause_menu.sync_view_controls(debug_visible, chase_distance)
		pause_menu.show_menu(reason)
	else:
		pause_menu.hide_menu()


func reset_live_flight() -> void:
	select_flight_plan(0)
	if live_bridge != null and live_bridge.initialize(snapshot_text):
		if options.get("--flight-model", "legacy") == "thrust" and not live_bridge.enable_surface_practice():
			fail("Thrust reset failed: " + str(live_bridge.get_last_error()))
			return
	if streaming_requested:
		start_stream()
		ship.position = live_bridge.get_state().position
		if is_instance_valid(flight_displays):
			flight_displays.elapsed = 1.0
		set_view(1)
		toggle_pilot()
		if player_input != null:
			player_input.assist = true
			player_input.needs_neutral = true


func start_stream() -> void:
	if planet_stream != null:
		planet_stream.visible = false
		planet_stream.queue_free()
	if not live_bridge.enable_streaming():
		fail("C++ terrain stream failed: " + str(live_bridge.get_last_error()))
		return
	planet_stream = load("res://planet_stream.gd").new()
	planet_stream.bridge = live_bridge
	planet_stream.render_layer = 2 if options.get("--flight-model", "legacy") == "thrust" else 1
	surface.add_child(planet_stream)


func load_model(filename: String) -> Node3D:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var result := document.append_from_file(assets_path.path_join(filename), state)
	if result != OK:
		fail("Could not import existing asset: " + filename)
		return null
	var scene := document.generate_scene(state) as Node3D
	if scene == null:
		fail("GLB generated no scene: " + filename)
	elif filename.begins_with("hero-ship-") or filename.begins_with("hero-cockpit-"):
		AssetMaterials.apply(scene, filename.begins_with("hero-cockpit-"), options.get("--asset-materials", "tuned"))
	return scene


func install_ship_interior(hull: Node3D, tier: String = "near") -> Node3D:
	var cabin := load_model("hero-cockpit-%s.glb" % tier)
	if cabin == null:
		return null
	hull.add_child(cabin)
	cabin.transform = CockpitLayout.mount()
	CockpitLayout.hide_duplicate_envelope(cabin)
	add_cabin_lighting(cabin)
	return cabin


func build_environment() -> void:
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	scene_environment = environment
	var sky := Sky.new()
	var sky_material := ProceduralSkyMaterial.new()
	sky_material.sky_top_color = Color("243e60")
	sky_material.sky_horizon_color = Color("a9bcc2")
	sky_material.ground_bottom_color = Color("a9bcc2")
	sky_material.ground_horizon_color = Color("a9bcc2")
	sky_material.sky_curve = 0.12
	sky.sky_material = sky_material
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color("809bbb")
	environment.ambient_light_energy = 0.40
	environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	environment.ssao_enabled = true
	environment.ssao_radius = 0.45
	environment.ssao_intensity = 1.4
	environment.fog_light_color = Color("a1b8c7")
	environment.fog_light_energy = 0.35
	environment.fog_density = 0.000018
	environment.fog_sky_affect = 0.0
	world.environment = environment
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun_light = sun
	sun.rotation_degrees = Vector3(-24, -40, 0)
	sun.light_color = Color("ffe2bd")
	sun.light_energy = 1.8
	sun.light_cull_mask = 3
	sun.layers = 3
	sun.shadow_enabled = true
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
	sun.directional_shadow_max_distance = 18000
	add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-35, 140, 0)
	fill.light_color = Color("779bcc")
	fill.light_energy = 0.35
	fill.light_cull_mask = 3
	fill.layers = 3
	add_child(fill)


func build_terrain() -> void:
	var builder := SurfaceTool.new()
	builder.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in range(data.vertices.size()):
		var color: Array = data.colors[i]
		builder.set_color(Color(float(color[0]) / 255, float(color[1]) / 255, float(color[2]) / 255))
		builder.add_vertex(vector(data.vertices[i]))
	var n := int(data.samples)
	for row in range(n - 1):
		for column in range(n - 1):
			var a := row * n + column
			# Godot front faces are clockwise; preserve an upward-facing surface.
			for index in [a, a + n, a + 1, a + 1, a + n, a + n + 1]:
				builder.add_index(index)
	builder.generate_normals()
	var terrain := MeshInstance3D.new()
	terrain.layers = 2 if options.get("--flight-model", "legacy") == "thrust" else 1
	terrain.mesh = builder.commit()
	var normals: PackedVector3Array = terrain.mesh.surface_get_arrays(0)[Mesh.ARRAY_NORMAL]
	if normals[int(normals.size() / 2)].y <= 0:
		fail("Terrain bridge produced an inverted surface")
		return
	var material := ShaderMaterial.new()
	material.shader = load("res://terrain.gdshader")
	material.set_shader_parameter("altitude_origin", float(data.frame.altitude_metres))
	terrain.material_override = material
	surface.add_child(terrain)


func build_overlay() -> void:
	var ui_scale := float(render_size.x) / 1920.0 if render_size != Vector2i.ZERO else 1.0
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var top := ColorRect.new()
	debug_backdrop = top
	top.color = Color(0.015, 0.025, 0.045, 0.86)
	top.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE)
	top.offset_bottom = (145 if streaming_requested else 106) * ui_scale
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	canvas.add_child(top)
	label.grow_vertical = Control.GROW_DIRECTION_END
	label.add_theme_font_size_override("font_size", int(26 * ui_scale))
	label.add_theme_color_override("font_color", Color("b9e3ec"))
	canvas.add_child(label)
	label.position = Vector2(42, 24) * ui_scale
	caption.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	caption.offset_left = 42 * ui_scale
	caption.offset_top = -100 * ui_scale
	caption.add_theme_font_size_override("font_size", int(20 * ui_scale))
	caption.add_theme_color_override("font_shadow_color", Color.BLACK)
	caption.add_theme_constant_override("shadow_offset_x", 2)
	caption.add_theme_constant_override("shadow_offset_y", 2)
	canvas.add_child(caption)


func set_view(next: int) -> void:
	mode = clampi(next, 1, 4)
	pilot_view = false
	head_angles = Vector2.ZERO
	if pilot_cockpit != null:
		pilot_cockpit.visible = true
	ship.visible = true
	camera.fov = 55
	camera.near = 0.15
	camera.far = 100000
	replay_running = false
	for child in exhibit.get_children():
		exhibit.remove_child(child)
		child.queue_free()
	surface.visible = mode == 1 or mode == 4
	sun_light.directional_shadow_max_distance = 30 if mode == 3 else (18000 if mode == 4 else 1500)
	scene_environment.fog_enabled = surface.visible
	scene_environment.ambient_light_energy = 0.40
	scene_environment.background_mode = Environment.BG_COLOR if mode == 2 else Environment.BG_SKY
	scene_environment.background_color = Color("050812")
	var view_name := ""
	if mode == 1:
		view_name = "SHUTTLE / SURFACE"
		camera.position = ship.position + ship.basis * Vector3(16, 8, -19)
		camera.look_at(ship.position + Vector3(0, 1, 0))
	elif mode == 2:
		view_name = "STATION / ASSET INSPECTION"
		var station := load_model("hero-station-near.glb")
		if station != null:
			exhibit.add_child(station)
		camera.position = Vector3(180, 160, 230)
		camera.look_at(Vector3(0, 10, 0))
	elif mode == 3:
		view_name = "COCKPIT / SEATED INSPECTION"
		var cockpit := load_model("hero-cockpit-near.glb")
		if cockpit != null:
			exhibit.add_child(cockpit)
			add_cabin_lighting(cockpit)
		camera.fov = 75
		update_head_camera()
	else:
		view_name = "TERRAIN / TRUE-SCALE C++ SNAPSHOT"
		camera.position = Vector3(7000, 3400, 9500)
		camera.look_at(Vector3(-2000, -200, -5500))
		if streaming_requested:
			var state: Dictionary = live_bridge.get_state()
			var radius: float = state.planet_radius
			camera.position = Vector3(0, radius * 0.6, radius * 1.6)
			camera.near = 1000
			camera.far = radius * 8
			camera.look_at(Vector3(0, -radius - float(state.altitude), 0))
			scene_environment.background_mode = Environment.BG_COLOR
			scene_environment.background_color = Color("02040a")
			scene_environment.fog_enabled = false
			scene_environment.ambient_light_energy = 0.10
			view_name = "PLANET / STREAMED ORBITAL INSPECTION"
	label.text = "APSIS DRIFT    /    FREEDOM STUDY 03\n" + view_name
	caption.text = "%s  |  planet seed %s  |  1 unit = 1 metre  |  generator v1\n" % [data.planet.display_name, data.planet.planet_seed]
	caption.text += "1 ship   2 station   3 cockpit   4 terrain   /   RMB + WASD/QE: inspect   Shift: fast\n"
	caption.text += "R: recorded C++ flight (ship view)   Home: reset view   Esc: exit   /   NOT A PLAYABLE BUILD"
	if int(data.get("experimental_relief_version", 0)) == 1:
		label.text += " / EXPERIMENTAL C++ RELIEF 1"
	if mode == 3:
		caption.text = "SEATED CABIN REVIEW / RMB: look around (bounded) / Home: centre / 1-4: change view\n"
		caption.text += "Closed cabin, pilot-facing switches; instruments remain static concept art\n"
		caption.text += "Translation locked to pilot eye; no walking or landing simulation yet"
	if live_bridge != null:
		caption.text = "LIVE C++ FLIGHT EXPERIMENT / no landing, collision or persistence integration\n"
		caption.text += "W/S thrust   A/D turn   Q/E strafe   Space/Ctrl rise/fall   V pause   Backspace reset flight\n"
		caption.text += "C cockpit/chase   1 flight   2/3/4 inspect   RMB: seated look / external camera   /   16 m clearance clamp"
	if streaming_requested:
		label.text = "APSIS DRIFT / STREAMING STUDY 04\n" + view_name
		caption.text = "C++ PLANET STREAM / bounded tiles / camera-relative metres / same terrain recipe\n"
		caption.text += "1 surface flight   4 orbital inspection   C cockpit/chase   RMB + WASD/QE: inspect\n"
		caption.text += "W/S thrust   A/D turn   Space/Ctrl altitude   V pause / no landing or orbital dynamics integration"


func add_cabin_lighting(cabin: Node3D) -> void:
	for side in [-1, 1]:
		var light := OmniLight3D.new()
		light.position = Vector3(side * 1.15, 2.10, 0.55)
		light.light_color = Color("91bfd5")
		light.light_energy = 0.45
		light.omni_range = 3.0
		cabin.add_child(light)


func toggle_pilot() -> void:
	if pilot_cockpit == null:
		pilot_cockpit = install_ship_interior(ship)
		if pilot_cockpit == null:
			return
	pilot_view = not pilot_view
	pilot_cockpit.visible = true
	ship.visible = true
	head_angles = Vector2.ZERO
	chase_angles = Vector2.ZERO
	camera.fov = 75 if pilot_view else 55
	if not pilot_view:
		chase_follow.reset(ship.basis)
		set_view(1)
	else:
		update_head_camera()


func turn_head(relative: Vector2) -> void:
	if not relative.is_finite():
		return
	head_angles.x = clampf(head_angles.x - relative.y * 0.003, -1.22, 1.05)
	head_angles.y = clampf(head_angles.y - relative.x * 0.003, -HEAD_YAW_LIMIT, HEAD_YAW_LIMIT)


func update_head_camera() -> void:
	var head := Transform3D(Basis.from_euler(Vector3(head_angles.x, head_angles.y, 0)), pilot_eye)
	camera.transform = ship.transform * CockpitLayout.mount() * head if pilot_view else head


func _unhandled_input(event: InputEvent) -> void:
	if player_input != null and (not window_focused or pause_menu.panel.visible):
		return
	if player_input != null and player_input.thrust_mode and event.is_pressed() and not event.is_echo():
		var open_pad: bool = event is InputEventJoypadButton and event.device == player_input.device and event.button_index == JOY_BUTTON_DPAD_UP and plan_shortcut_available(true)
		var open_key: bool = event is InputEventKey and event.physical_keycode == KEY_G and not event.ctrl_pressed and not event.alt_pressed and not event.meta_pressed and plan_shortcut_available(false)
		if open_pad or open_key:
			flight_plan_menu.toggle(flight_plan)
			get_viewport().set_input_as_handled()
			return
	if live_presentation and event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_F3:
		debug_visible = not debug_visible
		get_viewport().set_input_as_handled()
		return
	if live_presentation and not pilot_view and event is InputEventMouseButton and event.pressed:
		if event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
			chase_distance = clampf(chase_distance + (-3 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 3), 22, 100)
			return
	# Legacy asset-inspection shortcuts must not fire beneath player bindings.
	if player_input != null and live_bridge != null and event is InputEventKey:
		return
	if event is InputEventKey and not event.pressed:
		focus_blocked_keys.erase(event.physical_keycode)
	if event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_ESCAPE:
				if player_input == null:
					get_tree().quit()
			KEY_1, KEY_2, KEY_3, KEY_4: set_view(event.keycode - KEY_0)
			KEY_HOME: set_view(mode)
			KEY_R:
				if mode == 1 and live_bridge == null:
					replay_running = not replay_running
			KEY_V:
				if player_input == null:
					live_paused = not live_paused
			KEY_C:
				if player_input == null and live_bridge != null and mode == 1:
					toggle_pilot()
			KEY_BACKSPACE:
				if live_bridge != null and live_bridge.initialize(snapshot_text):
					if streaming_requested:
						start_stream()
					live_paused = false
					ship.position = live_bridge.get_state().position
					set_view(1)
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		if pilot_view or mode == 3:
			turn_head(event.relative)
		else:
			if live_presentation:
				orbit_camera(event.relative)
			else:
				camera.rotation.y -= event.relative.x * 0.003
				camera.rotation.x = clampf(camera.rotation.x - event.relative.y * 0.003, -1.5, 1.5)


func _process(delta: float) -> void:
	if data.is_empty() or ship == null:
		return
	var command := {"axes": PackedFloat64Array([0, 0, 0, 0]), "look": Vector2.ZERO}
	if player_input != null:
		command = player_input.sample()
		if command.recenter:
			head_angles = Vector2.ZERO
		if pause_menu.panel.visible:
			return
		if window_focused and (pilot_view or mode == 3):
			head_angles.x = clampf(head_angles.x - command.look.y * delta, -1.25, 1.05)
			head_angles.y = clampf(head_angles.y - command.look.x * delta, -HEAD_YAW_LIMIT, HEAD_YAW_LIMIT)
		elif window_focused and live_presentation and mode == 1:
			if player_input.looking:
				chase_angles.x = clampf(chase_angles.x - command.look.y * delta, -1.15, 1.15)
				chase_angles.y -= command.look.x * delta
			elif not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
				chase_angles = chase_angles.lerp(Vector2.ZERO, 1.0 - exp(-delta * 5))
	if mode == 3:
		update_head_camera()
	if Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) and not pilot_view and mode != 3 and not live_presentation:
		var motion := Vector3(
			float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A)),
			float(Input.is_physical_key_pressed(KEY_E)) - float(Input.is_physical_key_pressed(KEY_Q)),
			float(Input.is_physical_key_pressed(KEY_S)) - float(Input.is_physical_key_pressed(KEY_W)))
		var speed := 12.0 if mode != 4 else 3000.0
		if streaming_requested and mode == 4:
			var state: Dictionary = live_bridge.get_state()
			var center := Vector3(0, -float(state.planet_radius) - float(state.altitude), 0)
			var altitude := camera.position.distance_to(center) - float(state.planet_radius)
			speed = clampf(altitude * 0.6, 100, float(state.planet_radius) * 0.4)
			camera.near = clampf(altitude * 0.0001, 0.15, 1000)
			camera.far = maxf(100000, maxf(altitude, camera.position.length()) * 8)
		if Input.is_physical_key_pressed(KEY_SHIFT):
			speed *= 10
		camera.position += camera.basis * motion.limit_length() * delta * speed
	if replay_running and mode == 1:
		replay_time = fmod(replay_time + delta, 10.0)
		var index := int(replay_time * 30)
		var next_position := vector(data.replay[index].position).lerp(
			vector(data.replay[index + 1].position), fmod(replay_time * 30, 1.0))
		camera.position += next_position - ship.position
		ship.position = next_position
	if live_bridge != null and mode == 1:
		if not live_paused and window_focused and (planet_stream == null or planet_stream.is_ready):
			var buttons := 0
			if not Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT) and not clear_live_input:
				for bit in range(FLIGHT_KEYS.size()):
					var held := Input.is_physical_key_pressed(FLIGHT_KEYS[bit])
					if focus_blocked_keys.has(FLIGHT_KEYS[bit]):
						if not held:
							focus_blocked_keys.erase(FLIGHT_KEYS[bit])
						continue
					if held:
						buttons |= 1 << bit
			clear_live_input = false
			var advanced: bool
			if player_input != null:
				if player_input.thrust_mode:
					advanced = live_bridge.advance_thrust(delta, command.thrust_axes, player_input.assist)
				else:
					advanced = live_bridge.advance_analog(delta, command.axes)
			else:
				advanced = live_bridge.advance(delta, buttons)
			if not advanced:
				fail("C++ flight step rejected: " + str(live_bridge.get_last_error()))
				return
		var state: Dictionary = live_bridge.get_state()
		if streaming_requested:
			# A ground-level near plane plus a planet-wide far plane degenerates
			# Godot's float frustum extraction. Grow the range with altitude.
			camera.far = maxf(100000, float(state.altitude) * 8)
			var atmosphere := exp(-maxf(0, float(state.altitude)) / 18000.0)
			scene_environment.fog_density = 0.000018 * atmosphere
			scene_environment.background_mode = Environment.BG_SKY if atmosphere > 0.01 else Environment.BG_COLOR
		camera.position += state.position - ship.position
		ship.position = state.position
		if state.flight_model in ["thrust-lab-1", "thrust-lab-2"]:
			ship.basis = state.body_basis
		else:
			ship.rotation.y = float(state.heading) - PI / 2.0
		if pilot_view:
			update_head_camera()
		elif live_presentation:
			var orbit_basis := Basis.from_euler(Vector3(chase_angles.x - 0.12, chase_angles.y, 0))
			# Track translation immediately; attitude follows independently instead
			# of mounting the camera on a rigid boom attached to the ship's nose.
			var follow_basis: Basis = chase_follow.follow(ship.basis, delta)
			camera.position = ship.position + follow_basis * (orbit_basis * Vector3(0, chase_distance * 0.20, chase_distance))
			if streaming_requested:
				var camera_clearance: float = live_bridge.camera_clearance(camera.position - ship.position)
				if is_finite(camera_clearance) and camera_clearance < 2:
					camera.position.y += 2 - camera_clearance
			camera.look_at(ship.position + follow_basis.y * 1.2, follow_basis.y)
		if live_presentation:
			guidance_elapsed += delta
			if flight_plan != 0 and guidance_elapsed >= 0.2:
				flight_guidance = live_bridge.get_flight_guidance(flight_plan)
				guidance_elapsed = 0.0
			state["guidance"] = flight_guidance
			flight_displays.refresh(state, delta)
			guidance_overlay.guidance = flight_guidance
			guidance_overlay.visible = flight_plan != 0 and not pilot_view and not flight_plan_menu.active
			guidance_overlay.queue_redraw()
			navigation_sky.set_shader_parameter("radius", state.planet_radius)
			navigation_sky.set_shader_parameter("altitude", maxf(0, state.altitude))
			navigation_sky.set_shader_parameter("view_to_inertial", state.view_to_inertial)
			scene_environment.background_mode = Environment.BG_SKY
			scene_environment.ambient_light_energy = 0.08 + 0.32 * exp(-maxf(0, state.altitude) / 18000)
		if not streaming_requested and not state.inside_patch:
			live_paused = true
		label.text = "APSIS DRIFT / LIVE C++ FLIGHT / STUDY %s%s\n" % [
			"04" if streaming_requested else "03", " / LIVE INSTRUMENTS" if pilot_view and live_presentation else ""]
		label.text += "TICK %d | %.1f m/s | CLEARANCE %.1f m | %s" % [state.tick, state.speed, state.clearance,
			"PAUSED" if live_paused or not window_focused else "120 Hz"]
		if int(data.get("experimental_relief_version", 0)) == 1:
			label.text += " / RELIEF EXPERIMENT 1"
		if not streaming_requested and not state.inside_patch:
			label.text += " | PATCH EDGE: RESET IN PAUSE MENU" if player_input != null else " | PATCH EDGE: BACKSPACE TO RESET"
		if player_input != null:
			var family: String = "pad" if player_input.last_device == "pad" else "key"
			caption.text = "ASSISTED SURFACE FLIGHT  |  %s / %s: forward / reverse  |  %s: cockpit / chase\n" % [player_input.binding_label("forward", family), player_input.binding_label("backward", family), player_input.binding_label("camera", family)]
			caption.text += "Esc / Start: controls & pause  |  Pitch / roll / landing not implemented"
			caption.text += "  |  Hold %s: look; release: center" % player_input.binding_label("look_hold", family)
			if player_input.thrust_mode:
				label.text += "\n%.0f km/h | CLIMB %+.1f m/s | MAIN %.0f%% / RETRO %.0f%% | q %.1f kPa" % [state.speed * 3.6, state.climb_rate, state.main_thrust * 100, state.retro_thrust * 100, state.dynamic_pressure / 1000]
				label.text += "\nASSIST: %s | THRUST xyz %+.1f / %+.1f / %+.1f m/s² | DROPPED %.2f s%s" % ["ON" if player_input.assist else "OFF", state.rcs_acceleration.x, state.rcs_acceleration.y, state.rcs_acceleration.z, state.dropped_seconds, " | FLOOR GUARD" if state.floor_guard else ""]
				caption.text = "THRUST LAB %s | %s: main / %s: weak retro | %s: assist | Esc / Start: pause\n" % [str(state.flight_model).trim_prefix("thrust-lab-"), player_input.binding_label("forward", family), player_input.binding_label("backward", family), player_input.binding_label("assist", family)]
				if state.flight_model == "thrust-lab-2":
					label.text += " | ROTATION: " + ("STABILIZED" if state.assist else "COAST")
				caption.text += "Hold %s: head-look (release centers) | Direct analog roll | No landing / collision / fuel yet" % player_input.binding_label("look_hold", family)
			if player_input.needs_neutral:
				caption.text += "  |  RELEASE CONTROLS TO ARM"
	if planet_stream != null and mode in [1, 4]:
		if mode == 4:
			var state: Dictionary = live_bridge.get_state()
			var center := Vector3(0, -float(state.planet_radius) - float(state.altitude), 0)
			var altitude := camera.position.distance_to(center) - float(state.planet_radius)
			camera.near = clampf(altitude * 0.0001, 0.15, 1000)
			camera.far = maxf(100000, maxf(altitude, camera.position.length()) * 8)
			var atmosphere := exp(-maxf(0, altitude) / 18000.0)
			scene_environment.background_mode = Environment.BG_SKY if atmosphere > 0.01 else Environment.BG_COLOR
			scene_environment.fog_enabled = atmosphere > 0.01
			scene_environment.fog_density = 0.000018 * atmosphere
			scene_environment.ambient_light_energy = 0.10 + 0.30 * atmosphere
		planet_stream.tick(delta, camera.position)
		if not planet_stream.error.is_empty():
			fail(planet_stream.error)
			return
		if mode == 4:
			label.text = "APSIS DRIFT / STREAMING STUDY 04\nPLANET / ORBIT-TO-SURFACE INSPECTION"
		label.text += "\n%d resident tiles | %d replacements | %d retired%s" % [
			planet_stream.resident_ids.size(), planet_stream.swaps, planet_stream.retired_total,
			" / PREPARING PLANET" if not planet_stream.is_ready else ""]
	if live_presentation:
		var view_state: Dictionary = live_bridge.get_state()
		world_view.refresh(self, view_state.altitude, view_state.planet_radius)
		debug_backdrop.visible = debug_visible
		label.visible = debug_visible or not pilot_view
		if not debug_visible:
			var state: Dictionary = live_bridge.get_state()
			var status: Dictionary = preload("res://flight_status.gd").describe(state)
			label.text = "%s  |  %s\nALT %s  |  GROUND CLEARANCE %s  |  %.0f m/s\n%s  |  VERTICAL %+.0f m/s" % [data.planet.display_name.to_upper(), status.environment, preload("res://flight_status.gd").distance(state.altitude), preload("res://flight_status.gd").distance(state.clearance), state.speed, status.trajectory, state.climb_rate]
			caption.text = "" if pilot_view else "X / Square: cockpit · Hold L3: orbit camera · D-pad up / G: guidance · Start: controls"
			if state.floor_guard:
				caption.text = "TEST FLOOR GUARD — COLLISION / LANDING NOT IMPLEMENTED"
			elif player_input != null and player_input.needs_neutral:
				caption.text = "RELEASE CONTROLS TO ARM"
			elif planet_stream != null and not planet_stream.is_ready:
				caption.text = "PREPARING PLANET"
			elif state.clearance < 300 and state.climb_rate < -15:
				caption.text = "TERRAIN — DESCENDING"
			elif state.dynamic_pressure > 50000:
				caption.text = "HIGH ATMOSPHERIC LOAD — %.0f kPa (damage not simulated)" % (state.dynamic_pressure / 1000)
	if not capture_path.is_empty():
		if planet_stream != null and (not planet_stream.is_ready or not planet_stream.pending.is_empty()):
			last_frame_usec = Time.get_ticks_usec()
			return
		var now := Time.get_ticks_usec()
		var wall_ms := (now - last_frame_usec) / 1000.0
		last_frame_usec = now
		capture_frames += 1
		if capture_frames > 60:
			frame_times.append(wall_ms)
		if capture_frames == 180:
			await RenderingServer.frame_post_draw
			var captured := get_viewport().get_texture().get_image()
			if render_size != Vector2i.ZERO and captured.get_size() != render_size:
				fail("Actual capture dimensions differ from requested render size")
				return
			var result := captured.save_png(capture_path)
			if result != OK:
				fail("PNG capture failed")
				return
			frame_times.sort()
			var report := {
				"schema_version": 1, "kind": "windowed viewport capture; not a gameplay benchmark",
				"engine": Engine.get_version_info().string, "view": mode,
				"renderer": RenderingServer.get_current_rendering_method(),
				"viewport": [get_viewport().get_visible_rect().size.x, get_viewport().get_visible_rect().size.y],
				"capture_pixels": [captured.get_width(), captured.get_height()],
				"load_ms": load_ms, "sample_frames": frame_times.size(),
				"frame_p50_ms": frame_times[int(frame_times.size() / 2)],
				"frame_p95_ms": frame_times[int(frame_times.size() * 0.95)],
				"frame_max_ms": frame_times.back(),
				"draw_calls": Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
				"render_primitives": Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
				"godot_static_memory_bytes": Performance.get_monitor(Performance.MEMORY_STATIC),
				"godot_video_memory_bytes": Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED),
				"vram_note": "Godot estimate, not process peak VRAM; excludes some driver allocations",
				"snapshot_seed": data.planet.planet_seed,
				"replay_running": replay_running, "replay_seconds": replay_time,
				"live_cpp": live_bridge != null,
				"pilot_view": pilot_view,
				"asset_design_version": 3,
				"head_angles_radians": [head_angles.x, head_angles.y],
				"terrain_material": "presentation-only mineral layers; no displacement",
				"terrain_generator_version": data.terrain_generator_version,
				"experimental_relief_version": data.get("experimental_relief_version", 0),
			}
			var report_file := FileAccess.open(capture_path + ".json", FileAccess.WRITE)
			if planet_stream != null:
				report["planet_stream"] = planet_stream.report()
			if not capture_evidence.is_empty():
				report["scripted_inspection"] = capture_evidence
			if live_bridge != null:
				var live_state: Dictionary = live_bridge.get_state()
				report["live_state"] = {"tick": live_state.tick, "checksum": live_state.checksum,
					"speed_metres_per_second": live_state.speed, "clearance_metres": live_state.clearance,
					"dropped_seconds": live_state.dropped_seconds}
			if report_file == null:
				fail("Could not write capture report")
				return
			report_file.store_string(JSON.stringify(report, "  ") + "\n")
			print("Captured " + capture_path + " with adjacent measurement report")
			get_tree().quit()


func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_WINDOW_FOCUS_OUT:
		window_focused = false
		if player_input != null:
			player_input.focused = false
			set_player_paused(true, "Window focus lost. Resume explicitly when ready.")
		clear_live_input = true
		for key in FLIGHT_KEYS:
			if Input.is_physical_key_pressed(key):
				focus_blocked_keys[key] = true
	elif what == NOTIFICATION_WM_WINDOW_FOCUS_IN:
		window_focused = true
		if player_input != null:
			player_input.focused = true
			player_input.needs_neutral = true
