extends SceneTree
## Isolated native art-direction review; never installed into gameplay.
## BSD-3-Clause. Original assets only. Geometry and C++ state are unchanged.
const Layout = preload("res://cockpit_layout.gd")
var study: Variant
var output := ""
var material_slots: Array = []
var cabin_lights: Array = []
var task_lights: Array = []
var bounce_lights: Array = []
var fill: DirectionalLight3D
var frames_by_view: Dictionary = {}
var receipt: Array = []
const PROFILES := [
	{"id": "baseline", "sun": 1.8, "fill": 0.35, "ambient": 0.08, "cabin": 0.45, "cabin_color": "91bfd5", "task": 0.0},
	{"id": "utility", "sun": 1.8, "fill": 0.25, "ambient": 0.10, "cabin": 0.65, "cabin_color": "c7d6dd", "task": 0.12},
	{"id": "cinematic", "sun": 2.1, "fill": 0.16, "ambient": 0.085, "cabin": 0.48, "cabin_color": "a8c4d2", "task": 0.24},
]

func _initialize() -> void:
	call_deferred("run")

func fail(message: String) -> void:
	push_error(message)
	quit(1)

func wait_frames(count: int) -> void:
	for i in count:
		await process_frame

func collect(node: Node) -> void:
	if node is MeshInstance3D:
		for i in node.mesh.get_surface_count():
			var material: Material = node.get_active_material(i)
			if material is StandardMaterial3D:
				material_slots.append({"node": node, "index": i, "original": material})
	if node is OmniLight3D:
		cabin_lights.append(node)
	for child in node.get_children():
		collect(child)

func apply_profile(profile: Dictionary) -> void:
	study.sun_light.light_energy = profile.sun
	fill.light_energy = profile.fill
	study.scene_environment.ambient_light_energy = profile.ambient
	study.world_view.near_environment.ambient_light_energy = profile.ambient
	for lamp in cabin_lights:
		lamp.light_color = Color(profile.cabin_color)
		lamp.light_energy = profile.cabin
	for lamp in task_lights:
		lamp.light_energy = profile.task
	for lamp in bounce_lights:
		lamp.light_energy = 0.065 if profile.id == "cinematic" else 0.0
	for slot in material_slots:
		slot.node.set_surface_override_material(slot.index, null)
		if profile.id == "baseline":
			continue
		var original: StandardMaterial3D = slot.original
		var kind := original.resource_name.to_lower()
		if kind not in ["ivory", "paint", "steel", "black"]:
			continue
		var changed: StandardMaterial3D = original.duplicate()
		match kind:
			"ivory":
				changed.metallic = 0.12
				changed.roughness = 0.58
			"paint":
				changed.metallic = 0.18
				changed.roughness = 0.52
			"steel":
				changed.metallic = 0.85
				changed.roughness = 0.33
			"black":
				changed.metallic = 0.08
				changed.roughness = 0.72
		slot.node.set_surface_override_material(slot.index, changed)

func capture(profile: Dictionary, view: String) -> bool:
	var state: Dictionary = study.live_bridge.get_state()
	study.world_view.refresh(study, state.altitude, state.planet_radius)
	await wait_frames(24)
	await RenderingServer.frame_post_draw
	var frame := root.get_texture().get_image()
	if frame.get_size() != study.render_size:
		fail("Asset review viewport mismatch")
		return false
	var name: String = profile.id + "-" + view
	if frame.save_png(output.path_join(name + ".png")) != OK:
		fail("Cannot write asset review capture")
		return false
	if not frames_by_view.has(view):
		frames_by_view[view] = {}
	frames_by_view[view][profile.id] = frame
	var metadata := {"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Original code-authored assets rendered in Godot", "script": get_script().resource_path.get_file(), "kind": "Paused art-direction inspection, not gameplay or performance proof", "profile": profile, "view": view, "pixels": [frame.get_width(), frame.get_height()], "geometry_modified": false, "flight_state_modified_by_profile": false, "surfaces_inspected": material_slots.size()}
	var sidecar := FileAccess.open(output.path_join(name + ".json"), FileAccess.WRITE)
	if sidecar == null:
		fail("Cannot write review provenance")
		return false
	sidecar.store_string(JSON.stringify(metadata, "  ") + "\n")
	receipt.append(metadata)
	print("ASSET LOOK " + name)
	return true

func run() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--review-dir="):
			output = arg.trim_prefix("--review-dir=")
	if output.is_empty() or not DirAccess.dir_exists_absolute(output) or not DirAccess.get_files_at(output).is_empty():
		fail("Asset look review requires an existing empty --review-dir")
		return
	study = load("res://main.tscn").instantiate()
	study.options["--asset-materials"] = "baseline"
	root.add_child(study)
	if study.options.get("--asset-materials") != "baseline":
		fail("Art-direction comparison requires --asset-materials=baseline")
		return
	if study.live_bridge == null or study.world_view == null:
		fail("Asset look review requires streamed thrust mode")
		return
	study.live_paused = true
	study.pause_menu.hide_menu()
	if not study.live_bridge.set_survey_pose(0.25, 0.4, 650000):
		fail("Inspection pose rejected")
		return
	await wait_frames(90)
	study.set_process(false)
	study.set_process_unhandled_input(false)
	study.label.get_parent().hide()
	study.exhibit.hide()
	# This compares lighting on the same geometry, not terrain residency or flight.
	study.planet_stream.hide()
	collect(study.ship)
	for child in study.get_children():
		if child is DirectionalLight3D and child != study.sun_light:
			fill = child
	if fill == null or material_slots.is_empty() or cabin_lights.size() != 2:
		fail("Unexpected asset/light topology")
		return
	var kinds: Dictionary = {}
	for slot in material_slots:
		kinds[slot.original.resource_name] = true
	print("ASSET MATERIALS " + str(kinds.keys()))
	for side in [-1, 1]:
		var light := OmniLight3D.new()
		# Existing roof-practical strip midpoint; no floating fictional fixture.
		light.position = Vector3(side * 1.13, 2.20, 0.20)
		light.light_color = Color("ffcf9a")
		light.omni_range = 2.4
		light.shadow_enabled = true
		study.pilot_cockpit.add_child(light)
		task_lights.append(light)
		# Art-review proxy for nearby display/practical bounce, not a new lamp.
		var bounce := OmniLight3D.new()
		bounce.position = Vector3(side * 0.72, 0.73, -0.31)
		bounce.light_color = Color("d9bba0")
		bounce.omni_range = 0.85
		bounce.shadow_enabled = true
		study.pilot_cockpit.add_child(bounce)
		bounce_lights.append(bounce)
	for profile in PROFILES:
		apply_profile(profile)
		study.camera.fov = 75
		study.camera.transform = study.ship.transform * Layout.mount() * Transform3D(Basis.IDENTITY, study.pilot_eye)
		if not await capture(profile, "cockpit-straight"):
			return
		study.camera.transform = study.ship.transform * Layout.mount() * Transform3D(Basis.from_euler(Vector3(-0.20, 0, 0)), study.pilot_eye)
		if not await capture(profile, "cockpit"):
			return
		study.camera.fov = 48
		study.camera.position = study.ship.transform * Vector3(12, 8, -15.2)
		study.camera.look_at(study.ship.position + study.ship.basis.y, study.ship.basis.y)
		if not await capture(profile, "exterior"):
			return
	for view in frames_by_view:
		var baseline: Image = frames_by_view[view].baseline
		var alternative: Image = frames_by_view[view].cinematic
		var comparison := Image.create(baseline.get_width() * 2, baseline.get_height(), false, Image.FORMAT_RGBA8)
		baseline.convert(Image.FORMAT_RGBA8)
		alternative.convert(Image.FORMAT_RGBA8)
		comparison.blit_rect(baseline, Rect2i(Vector2i.ZERO, baseline.get_size()), Vector2i.ZERO)
		comparison.blit_rect(alternative, Rect2i(Vector2i.ZERO, alternative.get_size()), Vector2i(baseline.get_width(), 0))
		if comparison.save_png(output.path_join("baseline-left-cinematic-right-" + view + ".png")) != OK:
			fail("Cannot write side-by-side comparison")
			return
	var metadata := FileAccess.open(output.path_join("review.json"), FileAccess.WRITE)
	if metadata == null:
		fail("Cannot write review receipt")
		return
	metadata.store_string(JSON.stringify({"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Original code-authored assets; native Godot renders and lossless image blits", "geometry_modified": false, "profiles": receipt, "comparisons": "Baseline left, cinematic right; same poses/exposure/geometry"}, "  ") + "\n")
	quit(0)
