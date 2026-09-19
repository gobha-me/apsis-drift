extends "res://asset_look_review.gd"
## Native material/practical qualification. No gameplay defaults are changed.
## BSD-3-Clause. Night is a controlled light fixture, not a day/night system.
const FIXTURES := [
	{"id": "surface-day", "altitude": 4000.0, "sun": 1.8, "fill": 0.35, "ambient": 0.28, "sky_scale": 1.0},
	{"id": "surface-night", "altitude": 4000.0, "sun": 0.0, "fill": 0.0, "ambient": 0.018, "sky_scale": 0.008},
	{"id": "orbit", "altitude": 250000.0, "sun": 1.8, "fill": 0.35, "ambient": 0.08, "sky_scale": 1.0},
]
var fixture: Dictionary
var material_groups: Dictionary = {}
var atmosphere_tint: Color

func settle() -> bool:
	var deadline := Time.get_ticks_msec() + 45000
	var stable := 0
	var previous := -1
	while Time.get_ticks_msec() < deadline:
		await process_frame
		var stream: Node = study.planet_stream
		if stream.is_ready and stream.pending.is_empty() and not study.live_bridge.is_stream_busy() and stream.swaps == previous:
			stable += 1
		else:
			stable = 0
		previous = stream.swaps
		if stable > 180:
			return true
	return false

func apply_profile(profile: Dictionary) -> void:
	super.apply_profile(profile)
	# A/B environmental illumination is identical. No grade can fake grouping.
	study.sun_light.light_energy = fixture.sun
	fill.light_energy = fixture.fill
	study.scene_environment.ambient_light_energy = fixture.ambient
	study.world_view.near_environment.ambient_light_energy = fixture.ambient
	study.navigation_sky.set_shader_parameter("atmosphere_tint", atmosphere_tint * fixture.sky_scale)
	material_groups.clear()
	if profile.id == "baseline":
		return
	for slot in material_slots:
		var original: StandardMaterial3D = slot.original
		var kind := original.resource_name.to_lower()
		var cabin: bool = study.pilot_cockpit.is_ancestor_of(slot.node)
		var group := ""
		var metallic := original.metallic
		var roughness := original.roughness
		if kind in ["ivory", "paint"]:
			group = "painted ceramic / coated hull"
			metallic = 0.05 if kind == "ivory" else 0.10
			roughness = 0.68 if kind == "ivory" else 0.60
		elif kind == "steel":
			group = "exposed machined metal"
			metallic = 0.90
			roughness = 0.36
		elif kind == "black" and not cabin:
			# Export batches by material: this includes thermal panels, pressure
			# jackets and substrate. Do not claim per-component semantic control.
			group = "shared dark exterior / thermal material"
			metallic = 0.08
			roughness = 0.88
		elif kind == "panel" and not cabin:
			group = "shared exterior panel / nozzle material"
			metallic = 0.65
			roughness = 0.48
		elif kind == "black" or (kind == "panel" and cabin):
			group = "dark nonreflective equipment"
			metallic = 0.08
			roughness = 0.72
		if group.is_empty():
			continue
		var changed: StandardMaterial3D = original.duplicate()
		changed.metallic = metallic
		changed.roughness = roughness
		slot.node.set_surface_override_material(slot.index, changed)
		material_groups[group] = int(material_groups.get(group, 0)) + 1

func pose(view: String, orbit_offset := 0.0) -> void:
	if view == "cockpit":
		study.camera.fov = 75
		study.camera.transform = study.ship.transform * Layout.mount() * Transform3D(Basis.from_euler(Vector3(-0.20, 0, 0)), study.pilot_eye)
	else:
		study.camera.fov = 48
		var offset := Basis(Vector3.UP, orbit_offset) * Vector3(12, 8, -15.2)
		study.camera.position = study.ship.transform * offset
		study.camera.look_at(study.ship.position + study.ship.basis.y, study.ship.basis.y)

func run() -> void:
	var installed := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--review-dir="):
			output = arg.trim_prefix("--review-dir=")
		if arg.begins_with("--installed-materials="):
			installed = arg.trim_prefix("--installed-materials=")
	if output.is_empty() or not DirAccess.dir_exists_absolute(output) or not DirAccess.get_files_at(output).is_empty():
		fail("Environment review requires an existing empty --review-dir")
		return
	if not installed.is_empty():
		await installed_review(installed)
		return
	study = load("res://main.tscn").instantiate()
	study.options["--asset-materials"] = "baseline"
	root.add_child(study)
	if study.options.get("--asset-materials") != "baseline":
		fail("Environment comparison requires --asset-materials=baseline")
		return
	if study.live_bridge == null or study.world_view == null:
		fail("Environment review requires streamed thrust mode")
		return
	study.live_paused = true
	study.pause_menu.hide_menu()
	# Focus changes during parallel GPU reviews may show the child menu again.
	# Hide its CanvasLayer too; never let a menu invalidate an art capture.
	study.pause_menu.hide()
	# Main intentionally stops terrain uploads while the menu is visible.
	# Keep this paused inspection free of focus-induced menu state each frame.
	process_frame.connect(func(): study.pause_menu.hide_menu())
	study.label.get_parent().hide()
	study.set_process_unhandled_input(false)
	collect(study.ship)
	for child in study.get_children():
		if child is DirectionalLight3D and child != study.sun_light:
			fill = child
	if fill == null or material_slots.is_empty() or cabin_lights.size() != 2:
		fail("Unexpected asset/light topology")
		return
	atmosphere_tint = Color(study.data.planet.palette.atmosphere).lightened(0.22)
	for side in [-1, 1]:
		var light := OmniLight3D.new()
		light.position = Vector3(side * 1.13, 2.20, 0.20)
		light.light_color = Color("ffcf9a")
		light.omni_range = 2.4
		light.shadow_enabled = true
		study.pilot_cockpit.add_child(light)
		task_lights.append(light)
		var bounce := OmniLight3D.new()
		bounce.position = Vector3(side * 0.72, 0.73, -0.31)
		bounce.light_color = Color("d9bba0")
		bounce.omni_range = 0.85
		bounce.shadow_enabled = true
		study.pilot_cockpit.add_child(bounce)
		bounce_lights.append(bounce)
	for setup in FIXTURES:
		fixture = setup
		study.set_process(true)
		if not study.live_bridge.set_survey_pose(0.25, 0.4, fixture.altitude):
			fail("Inspection pose rejected")
			return
		# Force a request for the current relocated state before waiting for cover.
		await wait_frames(30)
		if not await settle():
			fail("Terrain cover did not settle")
			return
		study.set_process(false)
		for template in [PROFILES[0], PROFILES[2]]:
			var profile: Dictionary = template.duplicate(true)
			profile["environment_fixture"] = fixture
			apply_profile(profile)
			profile["material_groups"] = material_groups.duplicate()
			profile["terrain"] = study.planet_stream.report()
			profile["survey_state"] = study.live_bridge.get_state()
			profile["night_note"] = "Controlled primary-light occlusion + authored ambient/sky attenuation; not a implemented planetary day/night cycle"
			for view in ["cockpit", "exterior"]:
				pose(view)
				if not await capture(profile, fixture.id + "-" + view):
					return
			if fixture.id == "surface-day" and profile.id == "cinematic":
				for i in 8:
					pose("exterior", (i - 3.5) * 0.012)
					if not await capture(profile, "surface-motion-%02d" % i):
						return
	var metadata := FileAccess.open(output.path_join("environment-review.json"), FileAccess.WRITE)
	if metadata == null:
		fail("Cannot write review receipt")
		return
	metadata.store_string(JSON.stringify({"license": "BSD-3-Clause; Apsis Drift contributors", "source": "Original code-authored assets rendered in native Godot", "script": "asset_environment_review.gd", "geometry_changed": false, "terrain_visible": true, "environment_same_within_each_ab_pair": true, "motion_note": "Eight nearby camera poses of a paused ship; not simulated flight, a continuous-rate clip, or performance evidence", "captures": receipt}, "  ") + "\n")
	quit(0)

func installed_review(mode: String) -> void:
	if mode not in ["baseline", "tuned"]:
		fail("Installed review materials must be baseline or tuned")
		return
	study = load("res://main.tscn").instantiate()
	study.options["--asset-materials"] = mode
	root.add_child(study)
	if study.options.get("--asset-materials") != mode or study.live_bridge == null or study.world_view == null:
		fail("Installed review needs matching materials and streamed thrust mode")
		return
	study.live_paused = true
	study.pause_menu.hide_menu()
	study.pause_menu.hide()
	process_frame.connect(func(): study.pause_menu.hide_menu())
	study.label.get_parent().hide()
	study.set_process_unhandled_input(false)
	if not study.live_bridge.set_survey_pose(0.25, 0.4, 4000):
		fail("Installed review pose rejected")
		return
	await wait_frames(30)
	if not await settle():
		fail("Installed review terrain did not settle")
		return
	study.set_process(false)
	collect(study.ship)
	if cabin_lights.size() != 2:
		fail("Installed review unexpectedly changed practical light count")
		return
	var overrides := 0
	for slot in material_slots:
		if slot.original.get_meta("apsis_material_response_revision", 0) == 1:
			overrides += 1
	if overrides != (7 if mode == "tuned" else 0):
		fail("Installed default material override count differs from tested near assets")
		return
	var profile := {"id": "installed-" + mode, "material_response_revision": 1 if mode == "tuned" else 0, "override_count": overrides, "additional_review_lights": 0, "cabin_lights": cabin_lights.size(), "production_light_values_unmodified": true, "ambient": study.scene_environment.ambient_light_energy, "sun": study.sun_light.light_energy, "terrain": study.planet_stream.report(), "survey_state": study.live_bridge.get_state()}
	for view in ["cockpit", "exterior"]:
		pose(view)
		if not await capture(profile, "surface-day-" + view):
			return
	print("Installed material smoke: %s / %d overrides / unchanged 2 cabin lights" % [mode, overrides])
	quit(0)
