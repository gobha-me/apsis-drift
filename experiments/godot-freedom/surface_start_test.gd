extends SceneTree
## Native startup-only survey; legacy replay/model fixtures remain unchanged.
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func _initialize() -> void:
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var snapshot := FileAccess.get_file_as_string(arguments[0])
	var source: Dictionary = JSON.parse_string(snapshot)
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(not bridge.enable_surface_practice() and bridge.get_state().is_empty(), "Uninitialized survey accepted")
	check(bridge.initialize(snapshot), "Snapshot initialization failed")
	var legacy: Dictionary = bridge.get_state()
	check(not legacy.has("surface_start_reference"), "Legacy fixture acquired native survey metadata")
	check(bridge.enable_surface_practice(), "Native surface practice failed")
	var original: Dictionary = bridge.get_state()
	var reference: Dictionary = original.get("surface_start_reference", {})
	check(not reference.is_empty(), "Surface reference metadata missing")
	if not reference.is_empty():
		check(reference.sample_count == 289 and reference.grid_half_extent_metres == 2000 and reference.grid_spacing_metres == 250, "Survey dimensions changed")
		check(reference.latitude == source.frame.latitude_radians and reference.longitude == source.frame.longitude_radians, "Practice moved reference region")
		check(reference.planet_seed == source.planet.planet_seed, "Practice changed seed")
		check(original.altitude == reference.maximum_sampled_elevation_metres + 300, "Survey altitude margin missing")
		check(original.clearance == original.altitude - reference.center_elevation_metres, "Clearance reports neighborhood instead of local ground")
	check(original.tick == 0 and original.speed == 0 and original.assist, "Practice is not a stationary assisted start")
	check(not bridge.enable_surface_practice() and bridge.get_state() == original, "Repeated enable teleported existing lab")
	check(not bridge.enable_thrust_flight() and bridge.get_state() == original, "Legacy lab enable replaced surveyed session")
	for malformed in ["{}", "[]", "null", "not-json"]:
		check(not bridge.initialize(malformed) and bridge.get_state() == original, "Malformed reset replaced authoritative state")
	check(bridge.enable_streaming(), "Stream setup failed")
	check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array([0, 0, 0, 0, 0, 0, 0]), true), "First physics tick failed")
	var moved: Dictionary = bridge.get_state()
	check(moved.tick == 1 and not bridge.enable_surface_practice() and bridge.get_state() == moved, "Survey repaired ongoing flight")
	for attempt in 3:
		check(bridge.initialize(snapshot) and bridge.enable_surface_practice(), "Explicit reset failed")
		check(bridge.get_state() == original, "Explicit reset changed repeatable start")
	check(bridge.initialize(snapshot) and bridge.enable_thrust_flight(), "Standalone lab fixture failed")
	check(bridge.get_state().altitude == legacy.altitude and not bridge.get_state().has("surface_start_reference"), "Standalone lab fixture was silently relocated")
	check(bridge.initialize(snapshot) and bridge.advance(1.0 / 120, 1), "Legacy tick failed")
	moved = bridge.get_state()
	check(not bridge.enable_surface_practice() and bridge.get_state() == moved, "Late switch replaced legacy flight")
	check(bridge.initialize(snapshot) and bridge.enable_streaming(), "Inspection setup failed")
	check(bridge.set_survey_pose(source.frame.latitude_radians, source.frame.longitude_radians, 30000), "Inspection relocation failed")
	moved = bridge.get_state()
	check(not bridge.enable_surface_practice() and bridge.get_state() == moved, "Startup accepted a modified inspection state")
	print("Surface-start bridge: %d failures; altitude %.3f m, clearance %.3f m" % [failures, original.altitude, original.clearance])
	quit(0 if failures == 0 else 1)
