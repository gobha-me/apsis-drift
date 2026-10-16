extends SceneTree
const Status = preload("res://scripts/native/native_status.gd")
var failed := 0

func check(value: bool, reason: String) -> void:
	if not value: push_error(reason); failed += 1

func _initialize() -> void:
	# Invalid data/layout is checked before any world, model or rendering stage.
	for dimensions in [Vector2.ZERO, Vector2(-1, 720), Vector2(INF, 720), Vector2(1280, NAN)]:
		check(Status.layout_bounds(dimensions, Vector2(1280, 720)) == Rect2() and Status.layout_bounds(Vector2(1280, 720), dimensions) == Rect2(), "Invalid HUD dimensions accepted")
	check(Status.rated_rise_text({}).is_empty(), "Unavailable mode invented rated thrust")
	for invalid in [null, true, false, NAN, INF, -INF, "unknown", [], {}]:
		check(Status.rated_rise_text({"rated_rise_margin": invalid}) == "Rated RISE unavailable", "Malformed rated thrust became a reading")
	check("+12.5 m/s²" in Status.rated_rise_text({"rated_rise_margin": 12.5}) and "-12.5 m/s²" in Status.rated_rise_text({"rated_rise_margin": -12.5}), "Rated thrust lost sign or units")
	var position := {"latitude": PI / 6.0, "longitude": -PI / 4.0}
	var position_before := position.duplicate(true)
	check(Status.surface_position_text(position) == "Lat +30.00000° · Lon -45.00000°", "Surface position lost axis, sign or radian-to-degree units")
	for field in ["latitude", "longitude"]:
		for invalid in [null, true, false, NAN, INF, -INF, "unknown", [], {}]:
			var bad_position := position.duplicate(true)
			bad_position[field] = invalid
			check(Status.surface_position_text(bad_position) == "Surface position unavailable", "Malformed surface coordinate became a position: " + field)
	for bad_position in [{}, {"latitude": 0.0}, {"longitude": 0.0}, {"latitude": PI / 2.0 + 0.0000001, "longitude": 0.0}, {"latitude": -PI / 2.0 - 0.0000001, "longitude": 0.0}, {"latitude": 0.0, "longitude": PI + 0.0000001}, {"latitude": 0.0, "longitude": -PI - 0.0000001}]:
		check(Status.surface_position_text(bad_position) == "Surface position unavailable", "Missing or out-of-range surface coordinates accepted")
	check(Status.surface_position_text({"latitude": 0.0, "longitude": 0.0}) == "Lat +0.00000° · Lon +0.00000°", "Equator or prime meridian lost")
	check("+90.00000°" in Status.surface_position_text({"latitude": PI / 2.0, "longitude": PI}) and "-90.00000°" in Status.surface_position_text({"latitude": -PI / 2.0, "longitude": -PI}), "Polar or date-line boundaries refused")
	check(position == position_before, "Position formatting mutated the supplied observation")
	var snapshot := {"altitude": 900.0, "surface_speed": 1.9, "radial_rate": 1.9, "air_density": 1.0685, "attached": false, "surface": {}, "surface_walk": {}, "jump": {"phase": "idle", "selected": "next"}, "resources": {"selected": true, "fraction": 0.9, "jump_charges": 3}, "chart": {"rows": [{"name": "Known home", "current": true, "system_id": "here"}, {"name": "Known neighbor", "current": false, "system_id": "next"}]}}
	var original := snapshot.duplicate(true)
	check("Atmosphere" in Status.summary(snapshot) and "Reference altitude 0.90 km" in Status.summary(snapshot) and "Known neighbor" in Status.summary(snapshot), "Reference height, modeled air or selected target lost")
	check(not "Coasting" in Status.summary(snapshot), "Powered or assisted flight granted coasting")
	for field in ["altitude", "surface_speed", "radial_rate", "air_density"]:
		for bad in [NAN, INF, -INF, "unknown"]:
			var damaged := snapshot.duplicate(true)
			damaged[field] = bad
			check("unavailable" in Status.summary(damaged), "Invalid observation accepted: " + field)
	for field in ["surface", "surface_walk", "resources", "jump", "chart"]:
		var damaged := snapshot.duplicate(true)
		damaged[field] = null
		check("unavailable" in Status.summary(damaged), "Wrong semantic group shape accepted")
	for bad in [-0.1, 1.1, NAN]:
		var damaged := snapshot.duplicate(true)
		damaged.resources.fraction = bad
		check("unavailable" in Status.summary(damaged), "Invalid fuel fraction accepted")
	for bad in [-1, 4, 1.5]:
		var damaged := snapshot.duplicate(true)
		damaged.resources.jump_charges = bad
		check("unavailable" in Status.summary(damaged), "Invalid charge count accepted")
	var variant := snapshot.duplicate(true)
	variant.air_density = 0.0
	check("Space" in Status.summary(variant), "Vacuum observation lost")
	variant.attached = true
	check("Docked" in Status.summary(variant), "Attached mode lost")
	variant.attached = false
	variant.surface = {"landed": true}
	check("Landed" in Status.summary(variant), "Actual anchor lost")
	variant.surface_walk = {"eye_position": Vector3(0, 2, 30)}
	check("On foot" in Status.summary(variant) and not "Reference altitude" in Status.summary(variant) and not "Jump target" in Status.summary(variant), "Ground view retained ship motion or navigation")
	for row in [["terrain_unresolved", "Ground support unavailable"], ["water", "Water or shoreline"], ["slope", "Ground too steep"], ["step", "Step or drop too large"], ["craft_obstruction", "Wayfarer hull"]]:
		variant.surface_walk.movement_blockage = row[0]
		check("Last step blocked" in Status.summary(variant) and row[1] in Status.summary(variant), "Existing movement reason lost: " + row[0])
	for invalid in [false, 1, {}, [], "supported", "unknown"]:
		variant.surface_walk.movement_blockage = invalid
		check("feedback unavailable" in Status.summary(variant), "Malformed movement result became a reason")
	variant.surface_walk.movement_blockage = null
	check(not "blocked" in Status.summary(variant) and not "feedback unavailable" in Status.summary(variant), "No committed blockage invented a warning")
	variant.surface_walk.eye_position = Vector3(INF, 0, 0)
	check("unavailable" in Status.summary(variant), "Invalid ground eye accepted")
	variant = snapshot.duplicate(true)
	variant.resources = {}
	check("unavailable" in Status.summary(variant) and not "Fuel 100" in Status.summary(variant), "Historical save received invented fuel")
	variant.jump.phase = "spool"
	check("Spooling" in Status.summary(variant), "Jump phase lost")
	variant.jump.phase = "transit"
	check("In transit" in Status.summary(variant), "Committed transit lost")
	variant.jump.remaining_seconds = NAN
	check("unavailable" in Status.summary(variant), "Invalid phase timer accepted")
	for dimensions in [Vector2(1280, 720), Vector2(960, 540), Vector2(640, 450), Vector2(160, 100), Vector2(2560, 1440)]:
		var bounds := Status.layout_bounds(dimensions, dimensions)
		check(bounds.has_area() and Rect2(Vector2.ZERO, dimensions).encloses(bounds), "Compact HUD escaped valid dimensions")
	check(snapshot == original, "Status presentation mutated authoritative facts")
	var clearance := snapshot.duplicate(true)
	clearance.merge({"system_id": "11", "planet_id": "12", "craft_id": "13", "frame_id": "2", "frame_version": 1, "terrain_policy": 2, "tick": "120"}, true)
	clearance.surface = {"gear_deployed": true, "landed": false, "gear_clearance": {"available": true, "reference": "deployed_pad_planes", "coordinate_frame": "planet_relative_inertial", "system_id": "11", "planet_id": "12", "craft_id": "13", "frame_id": "2", "frame_version": 1, "terrain_policy": 2, "tick": "105", "source_checksum": "14", "minimum_gap_metres": 0.65, "maximum_gap_metres": 0.69, "age_seconds": 0.125}}
	var clearance_before := clearance.duplicate(true)
	check("Last gear gap 0.65 m" in Status.summary(clearance) and "0.1 s ago" in Status.summary(clearance) and "Reference altitude" in Status.summary(clearance), "Last pad gap lost age or substituted for reference altitude")
	for field in ["minimum_gap_metres", "maximum_gap_metres", "age_seconds"]:
		for bad in [NAN, INF, -INF, "unknown", null]:
			var malformed := clearance.duplicate(true)
			malformed.surface.gear_clearance[field] = bad
			check("Unavailable" in Status.gear_clearance_text(malformed), "Malformed gap/age became a reading")
	for field in ["system_id", "planet_id", "craft_id", "frame_id", "frame_version", "terrain_policy", "reference", "coordinate_frame", "tick", "source_checksum"]:
		var malformed := clearance.duplicate(true)
		malformed.surface.gear_clearance[field] = "invalid" if field == "source_checksum" else "999"
		check("Unavailable" in Status.gear_clearance_text(malformed), "Mismatched clearance owner/clock/reference accepted: " + field)
	for field in ["reference", "coordinate_frame"]:
		for bad in [1, null, []]:
			var damaged := clearance.duplicate(true)
			damaged.surface.gear_clearance[field] = bad
			check("Unavailable" in Status.gear_clearance_text(damaged), "Malformed clearance reference/frame became a reading")
	for bad in [1, null, []]:
		var damaged := clearance.duplicate(true)
		damaged.coordinate_frame = bad
		check("Unavailable" in Status.gear_clearance_text(damaged), "Malformed current coordinate frame became a reading")
	for field in ["surface", "surface_walk", "attached"]:
		for bad in [null, 1, "unknown", []]:
			var malformed := clearance.duplicate(true)
			malformed[field] = bad
			check("Unavailable" in Status.gear_clearance_text(malformed), "Malformed nested clearance group caused a reading")
	for field in ["gear_deployed", "landed", "gear_clearance"]:
		for bad in [null, 1, "unknown", []]:
			var malformed := clearance.duplicate(true)
			malformed.surface[field] = bad
			check("Unavailable" in Status.gear_clearance_text(malformed), "Malformed clearance availability accepted")
	var malformed := clearance.duplicate(true)
	malformed.surface.gear_clearance.minimum_gap_metres = 1.0
	check("Unavailable" in Status.gear_clearance_text(malformed), "Reversed pad gap bounds accepted")
	malformed = clearance.duplicate(true)
	malformed.surface.gear_clearance.age_seconds = -1.0
	check("Unavailable" in Status.gear_clearance_text(malformed), "Negative sample age accepted")
	malformed = clearance.duplicate(true)
	malformed.coordinate_frame = "system_inertial"
	check("Unavailable" in Status.gear_clearance_text(malformed), "System-frame rendering invented a contact reading")
	for state in ["stowed", "landed", "on_foot", "attached"]:
		malformed = clearance.duplicate(true)
		if state == "stowed": malformed.surface.gear_deployed = false
		elif state == "landed": malformed.surface.landed = true
		elif state == "on_foot": malformed.surface_walk = {"eye_position": Vector3.ZERO}
		else: malformed.attached = true
		check("Unavailable" in Status.gear_clearance_text(malformed) and not "Last gear gap" in Status.summary(malformed), "Ineligible lifecycle state revived a gear reading: " + state)
	check(clearance == clearance_before, "Clearance formatter mutated observations")
	print("Native status: %d failures" % failed)
	quit(1 if failed else 0)
