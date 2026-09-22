extends SceneTree
## Actual C++ owner/tick plus inherited presentation wiring. Dummy renderer:
## transfer checks only; GPU day/terminator/night captures remain separate.
class HeadlessMain extends "res://main.gd":
	var errors: Array[String] = []
	func _ready() -> void:
		set_process(false)
	func fail(message: String) -> void:
		errors.append(message)

var failures := 0
var study: Variant

func check(ok: bool, reason: String) -> void:
	if not ok:
		failures += 1
		push_error(reason)

func refresh() -> Dictionary:
	var before: Dictionary = study.live_bridge.get_state()
	check(study.refresh_physical_lighting(before), "Main accepted authoritative lighting")
	check(study.live_bridge.get_state() == before, "Main lighting transfer did not advance flight")
	var sample: Dictionary = study.lighting_sample
	check(sample.tick == before.tick, "Main uses the same integer tick")
	check(study.sun_light.basis.z.is_equal_approx(sample.direction), "Main light rays travel away from star")
	check(study.near_sun_light.basis == study.sun_light.basis, "Near and far sun directions match")
	check(study.navigation_sky.get_shader_parameter("view_to_inertial") == sample.local_to_system, "Star background consumes C++ basis")
	check(study.navigation_sky.get_shader_parameter("sun_direction") == sample.direction, "Sky consumes same light vector")
	check(study.fill_light.light_energy == 0, "No independent bright night-side fill")
	study.world_view.refresh(study, before.altitude, before.planet_radius)
	check(study.world_view.near_environment.ambient_light_energy == study.scene_environment.ambient_light_energy, "Both depth layers share ambient")
	return sample

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 4:
		push_error("Expected standalone, physical42, physical43, procedural-home snapshots")
		quit(1)
		return
	study = HeadlessMain.new()
	root.add_child(study)
	study.snapshot_text = FileAccess.get_file_as_string(args[1])
	study.data = JSON.parse_string(study.snapshot_text)
	study.physical_lighting = true
	check(study.valid_snapshot(study.data), "Physical snapshot shape accepted")
	check(study.initialize_live_bridge(), "Physical canonical owner accepted by main startup")
	if study.live_bridge == null:
		study.free()
		quit(1)
		return
	check(study.live_bridge.enable_orbit_practice() and study.live_bridge.enable_streaming(), "Physical streamed thrust fixture")
	study.build_environment()
	study.navigation_sky = ShaderMaterial.new()
	study.navigation_sky.shader = preload("res://navigation_sky.gdshader")
	study.scene_environment.sky.sky_material = study.navigation_sky
	study.camera = Camera3D.new()
	study.add_child(study.camera)
	study.world_view = preload("res://world_view.gd").new()
	study.add_child(study.world_view)
	study.world_view.install(study)
	check(study.sun_light.light_cull_mask == 2 and study.near_sun_light.light_cull_mask == 1, "Distinct terrain and nearby-craft sunlight")
	var initial := refresh()
	var state: Dictionary = study.live_bridge.get_state()
	var latitude: float = state.latitude
	var longitude: float = state.longitude
	var east := Vector3(-sin(longitude), cos(longitude), 0)
	var up := Vector3(cos(latitude) * cos(longitude), cos(latitude) * sin(longitude), sin(latitude))
	var north := Vector3(-sin(latitude) * cos(longitude), -sin(latitude) * sin(longitude), cos(latitude))
	var toward: Vector3 = (east * initial.direction.x + up * initial.direction.y - north * initial.direction.z).normalized()
	var sublatitude := asin(toward.z)
	var sublongitude := atan2(toward.y, toward.x)
	# Fixed-epoch diagnostic relocation is not accelerated simulation time.
	check(study.live_bridge.set_survey_pose(sublatitude, sublongitude, 250000), "Day-side diagnostic relocation")
	var day := refresh()
	check(day.solar_elevation_sine > 0.99 and study.near_sun_light.light_energy > 1.7, "Day craft lit")
	var day_fog: float = study.scene_environment.fog_light_energy
	check(study.live_bridge.set_survey_pose(-sublatitude, wrapf(sublongitude + PI, -PI, PI), 250000), "Night-side diagnostic relocation")
	var night := refresh()
	check(night.solar_elevation_sine < -0.99 and study.near_sun_light.light_energy == 0, "Night craft correctly shadowed")
	check(is_equal_approx(study.sun_light.light_energy, 1.8), "Observer shadow does not extinguish terrain sunlight")
	check(study.scene_environment.fog_light_energy < day_fog, "No day-bright fog at night")
	check(study.live_bridge.initialize(study.snapshot_text) and study.live_bridge.enable_orbit_practice() and study.live_bridge.enable_streaming(), "Reset uses original physical owner")
	check(refresh() == initial, "Reset recovers exact initial lighting sample")
	check(study.errors.is_empty(), "No main presentation refusals")
	# Legacy refresh must remain a no-op, even with a physical-capable binary.
	study.physical_lighting = false
	var sun_before: Basis = study.sun_light.basis
	var ambient_before: float = study.scene_environment.ambient_light_energy
	check(study.live_bridge.initialize(FileAccess.get_file_as_string(args[0])), "Standalone startup preserved")
	check(study.refresh_physical_lighting(study.live_bridge.get_state()), "Legacy lighting no-op")
	check(study.sun_light.basis == sun_before and study.scene_environment.ambient_light_energy == ambient_before, "Legacy lighting not overwritten by new transfer")
	study.free()
	await process_frame
	print("physical lighting integration: %d failures" % failures)
	quit(0 if failures == 0 else 1)
