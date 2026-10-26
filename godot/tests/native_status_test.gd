extends SceneTree
const Status = preload("res://scripts/native/native_status.gd")
const MouseLook = preload("res://scripts/native/native_mouse_look.gd")
const StarSearch = preload("res://scripts/native/native_star_search.gd")
const StationView = preload("res://scripts/native/native_station_view.gd")
var failed := 0

func check(value: bool, reason: String) -> void:
	if not value: push_error(reason); failed += 1

func _initialize() -> void:
	# Invalid data/layout is checked before any world, model or rendering stage.
	check_star_search_packets()
	check_mouse_boundaries()
	check_surface_reference_warning()
	check_observation_cue()
	check_surface_aid_cue()
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
	var recovering := snapshot.duplicate(true)
	recovering.jump.recovery = {"version": 1, "remaining_seconds": 45.0, "extra_radius_metres": 2000.0, "ready": false, "pending_arrival": false}
	check("Drive recovery · 45.0 s" in Status.summary(recovering), "Recovery countdown and uncertainty bound disappeared from compact HUD")
	for field in ["remaining_seconds", "extra_radius_metres"]:
		for invalid in [null, NAN, INF, -1.0, "45"]:
			var damaged := recovering.duplicate(true)
			damaged.jump.recovery[field] = invalid
			check("unavailable" in Status.summary(damaged), "Malformed recovery reading reached HUD: " + field)
	for damage in ["extra", "version", "remaining", "radius", "ready", "pending"]:
		var damaged := recovering.duplicate(true)
		match damage:
			"extra": damaged.jump.recovery.extra = true
			"version": damaged.jump.recovery.version = 1.0
			"remaining": damaged.jump.recovery.remaining_seconds = 45.1
			"radius": damaged.jump.recovery.extra_radius_metres = 2000.1
			"ready": damaged.jump.recovery.ready = true
			"pending": damaged.jump.recovery.pending_arrival = true
		check("unavailable" in Status.summary(damaged), "Inconsistent recovery packet reached HUD: " + damage)
	var strained := snapshot.duplicate(true)
	strained.jump.recovery = {"version": 2, "remaining_seconds": 75.0, "extra_radius_metres": 2000.0, "ready": false, "pending_arrival": false, "cycle_seconds": 75.0, "next_recovery_seconds": 90.0}
	var strained_before := strained.duplicate(true)
	check("75.0 s left of 75.0 s" in Status.summary(strained) and "next recovery ≤ 90.0 s" in Status.summary(strained), "Strain HUD lost current cooling or the next-arrival consequence")
	check("1 charge" in Status.drive_recovery_text(strained.jump.recovery, "idle", true), "Strain changed or hid the one-charge early-jump bill")
	for field in ["cycle_seconds", "next_recovery_seconds", "remaining_seconds", "extra_radius_metres"]:
		for invalid in [null, true, NAN, INF, -INF, -1.0, "75"]:
			var damaged := strained.duplicate(true)
			damaged.jump.recovery[field] = invalid
			check("unavailable" in Status.summary(damaged), "Malformed strain reading reached HUD: " + field)
	for change in [{"cycle_seconds": 44.9}, {"cycle_seconds": 90.1}, {"cycle_seconds": 60.0}, {"next_recovery_seconds": 44.9}, {"next_recovery_seconds": 90.1}, {"version": 3}, {"version": 2.0}]:
		var damaged := strained.duplicate(true)
		damaged.jump.recovery.merge(change, true)
		check("unavailable" in Status.summary(damaged), "Unsupported strain cycle reached HUD: " + str(change))
	var pending := strained.duplicate(true)
	pending.jump.phase = "transit"
	pending.jump.remaining_seconds = 2.0
	pending.jump.recovery.pending_arrival = true
	check("starts on arrival (75.0 s)" in Status.summary(pending), "Pending strain pretended cooling had already started")
	pending.jump.recovery.remaining_seconds = 74.0
	check("unavailable" in Status.summary(pending), "Pending strain countdown advanced before actual arrival")
	var cooled := strained.duplicate(true)
	cooled.jump.recovery.remaining_seconds = 0.0
	cooled.jump.recovery.extra_radius_metres = 0.0
	cooled.jump.recovery.ready = true
	cooled.jump.recovery.next_recovery_seconds = 45.0
	check("Drive ready · next recovery 45.0 s" in Status.summary(cooled), "Fully cooled drive retained a longer next cycle")
	check(strained == strained_before, "Strain formatting changed authoritative observations")

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
	check("T/F/G/H / left stick walk" in Status.summary(variant, "T/F/G/H") and not "WASD" in Status.summary(variant, "T/F/G/H"), "On-foot primary HUD ignored configured walking keys")
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


func check_mouse_boundaries() -> void:
	var preferences := {"look_speed": 1.6, "invert_look": false}
	var original := preferences.duplicate(true)
	for invalid in [null, true, 1, "motion", [], {}, Vector2(INF, 0), Vector2(0, NAN), Vector2(1000001, 0)]:
		check(MouseLook.motion_delta(invalid, preferences) == Vector2.ZERO, "Malformed mouse motion reached the camera")
	for invalid in [null, true, NAN, INF, -INF, 0.0, 4.1, "speed", [], {}]:
		check(MouseLook.motion_delta(Vector2(100, 50), {"look_speed": invalid, "invert_look": false}) == Vector2.ZERO, "Invalid mouse sensitivity accepted")
	for invalid in [null, 1, "inverted", [], {}]:
		check(MouseLook.motion_delta(Vector2.ONE, {"look_speed": 1.6, "invert_look": invalid}) == Vector2.ZERO, "Invalid mouse inversion accepted")
	check(MouseLook.motion_delta(Vector2(100, 50), preferences).is_equal_approx(Vector2(0.3, 0.15)), "Default mouse look signs/rate changed")
	check(MouseLook.motion_delta(Vector2(100, 50), {"look_speed": 3.2, "invert_look": true}).is_equal_approx(Vector2(0.6, -0.3)), "Mouse look ignored shared speed/inversion")
	check(preferences == original, "Mouse read changed control preferences")
	for invalid in [null, [], {}, {"minimum_x": 0, "maximum_x": 0, "minimum_z": -1, "maximum_z": 1}, {"minimum_x": NAN, "maximum_x": 1, "minimum_z": -1, "maximum_z": 1}, {"minimum_x": -1, "maximum_x": 1, "minimum_z": -INF, "maximum_z": 1}]:
		check(not StationView.valid_walk_access(invalid), "Malformed station access limits reached a visible guide")
	check(StationView.valid_walk_access({"minimum_x": -24.5, "maximum_x": 0.45, "minimum_z": -0.875, "maximum_z": 0.875}), "Existing finite corridor limits refused")


func observation_state(count: Variant) -> Dictionary:
	return {"chart": {"selected": true, "local_sensors": true, "observation_transitions": count}}


func check_observation_cue() -> void:
	var cue := Status.ObservationCue.new()
	const MESSAGE := "Ship observations updated\nControls → Recorded observations"
	var state := observation_state(12)
	state.chart["observed_facts"] = 6
	state.chart["unread_metadata"] = {"name": "Hidden body", "seed": "private"}
	var original := var_to_bytes(state)
	check(cue.text().is_empty(), "Fresh observation cue invented a finding")
	cue.reset(state)
	cue.observe(state, true, 0.1)
	check(cue.text().is_empty(), "Load baseline or repeated count announced old knowledge")
	var improved := state.duplicate(true)
	improved.chart.observation_transitions = 13
	cue.observe(improved, true, 0.1)
	check(cue.text() == MESSAGE and improved.chart.observed_facts == state.chart.observed_facts, "Confidence improvement without a new fact lost the generic cue")
	var improved_before := var_to_bytes(improved)
	cue.observe(improved, true, 5.5)
	cue.observe(improved, false, 1000.0)
	check(cue.text() == MESSAGE, "Paused/menu read aged the observation cue")
	cue.observe(improved, true, 0.5)
	check(cue.text().is_empty(), "Unchanged count retriggered or exceeded six active seconds")
	cue.observe(improved, true, 0.0)
	check(cue.text().is_empty(), "Repeated expired observation count replayed a finding")
	var latest := observation_state(20)
	cue.observe(latest, true, 0.0)
	cue.observe(latest, true, 5.0)
	latest.chart.observation_transitions = 25
	cue.observe(latest, true, 1.0)
	check(cue.text() == MESSAGE, "Coalesced batch did not refresh the single observation cue")
	cue.observe(latest, true, 6.0)
	check(cue.text().is_empty(), "Coalesced cue did not expire at its bounded duration")
	latest.chart.observation_transitions = 26
	cue.observe(latest, false, 0.0)
	cue.observe(latest, true, 0.1)
	check(cue.text().is_empty(), "Quiet read deferred a synthetic finding until Resume")
	latest.chart.observation_transitions = 27
	cue.observe(latest, true, 0.0)
	cue.observe(observation_state(1), false, 0.0)
	check(cue.text().is_empty(), "Lower count retained a cue from another selection")
	cue.observe(observation_state(1), true, 0.0)
	check(cue.text().is_empty(), "Lower counter reset announced historical knowledge")
	cue.observe(observation_state(2), true, 0.0)
	cue.reset(observation_state(100))
	check(cue.text().is_empty(), "Explicit Load/reset retained another selection's cue")
	cue.clear()
	cue.observe(observation_state(101), true, 0.0)
	check(cue.text().is_empty(), "First valid count after clear replayed retained findings")
	for bad in [null, true, false, -1, 513, 1.0, NAN, INF, -INF, "1", [], {}]:
		cue.reset(state)
		cue.observe(improved, true, 0.0)
		var malformed := observation_state(bad)
		var before := var_to_bytes(malformed)
		cue.observe(malformed, true, 0.0)
		check(cue.text().is_empty() and var_to_bytes(malformed) == before, "Malformed counter retained a cue or mutated its input")
		cue.observe(improved, true, 0.0)
		check(cue.text().is_empty(), "Malformed counter did not clear its baseline")
	for malformed in [{}, {"chart": null}, {"chart": []}, {"chart": {}}, {"chart": {"selected": false, "local_sensors": true, "observation_transitions": 20}}, {"chart": {"selected": true, "local_sensors": false, "observation_transitions": 20}}, {"chart": {"selected": 1, "local_sensors": true, "observation_transitions": 20}}, {"chart": {"selected": true, "local_sensors": 1, "observation_transitions": 20}}]:
		cue.reset(state)
		cue.observe(improved, true, 0.0)
		cue.observe(malformed, false, 0.0)
		check(cue.text().is_empty(), "Missing/unselected sensor projection retained a cue")
		cue.observe(improved, true, 0.0)
		check(cue.text().is_empty(), "Missing/unselected projection retained its baseline")
	for bad_delta in [-0.1, NAN, INF, -INF]:
		for active in [true, false]:
			cue.reset(state)
			cue.observe(improved, true, 0.0)
			cue.observe(improved, active, bad_delta)
			check(cue.text().is_empty(), "Invalid elapsed time retained or advanced a cue")
			cue.observe(improved, true, 0.0)
			check(cue.text().is_empty(), "Invalid elapsed time did not clear its baseline")
	cue.reset(observation_state(0))
	cue.observe(observation_state(512), true, 0.0)
	check(cue.text() == MESSAGE, "Inclusive counter bounds refused a valid coalesced update")
	cue.observe(observation_state(512), true, 1.0e30)
	check(cue.text().is_empty(), "Large finite elapsed time escaped bounded expiry")
	check(var_to_bytes(state) == original and var_to_bytes(improved) == improved_before, "Observation cue mutated authoritative input dictionaries")


func aid_state(mode: Variant, note: Variant) -> Dictionary:
	return {"surface": {"maneuver": mode, "note": note}, "surface_walk": {}}


func check_surface_aid_cue() -> void:
	var cue := Status.SurfaceAidCue.new()
	var active := aid_state("landing", "Reducing descent with current thrust")
	var finished := aid_state("off", "Landed on deployed gear")
	var original := var_to_bytes(active)
	var original_finished := var_to_bytes(finished)
	check(cue.text().is_empty(), "Fresh aid cue invented an outcome")
	cue.reset(finished)
	cue.observe(finished, true, 0.1)
	check(cue.text().is_empty(), "Load replayed an old aid result")
	cue.reset(active)
	check(cue.text() == active.surface.note, "Current landing aid lost its actual C++ note")
	cue.observe(finished, true, 0.1)
	check(cue.text() == finished.surface.note, "Active advance lost its actual terminal aid note")
	cue.observe(finished, true, 5.5)
	cue.observe(finished, false, 1000.0)
	check(cue.text() == finished.surface.note, "Paused/menu read aged the aid result")
	cue.observe(finished, true, 0.5)
	check(cue.text().is_empty(), "Aid outcome exceeded six active seconds")
	cue.observe(finished, true, 0.0)
	check(cue.text().is_empty(), "Repeated terminal state replayed an expired outcome")
	cue.reset(active)
	cue.observe(finished, false, 0.0)
	cue.observe(finished, true, 0.1)
	check(cue.text().is_empty(), "Quiet pause/focus change deferred an outcome until Resume")
	cue.reset(active)
	cue.observe(finished, true, 0.0)
	var lift := aid_state("liftoff", "Rising until deployed pads clear")
	cue.observe(lift, false, 0.0)
	check(cue.text() == lift.surface.note, "New liftoff retained another aid's terminal result")
	cue.observe(aid_state("off", ""), true, 0.0)
	check(cue.text().is_empty(), "An empty automatic stop invented a reason")
	cue.reset(active)
	check(cue.accept_cancel(active, aid_state("off", "")) and cue.text() == "Surface aid canceled.", "Accepted explicit cancellation lost its truthful fallback")
	var cancellation := aid_state("off", "Surface aid canceled by manual control; gear remains deployed")
	cue.reset(active)
	check(cue.accept_cancel(active, cancellation) and cue.text() == cancellation.surface.note, "Explicit cancellation replaced actual C++ feedback")
	cue.clear()
	check(not cue.accept_cancel(finished, finished) and cue.text().is_empty(), "Inactive cancellation invented an aid outcome")
	for bad in [null, true, false, 1, NAN, INF, -INF, [], {}, "unknown"]:
		for field in ["maneuver", "note"]:
			var malformed := active.duplicate(true)
			malformed.surface[field] = bad
			# A free-form string is valid note data, unlike an unknown mode.
			if field == "note" and bad is String: continue
			var before := var_to_bytes(malformed)
			cue.reset(active)
			cue.observe(finished, true, 0.0)
			cue.observe(malformed, true, 0.0)
			check(cue.text().is_empty() and var_to_bytes(malformed) == before, "Malformed aid field retained a cue or mutated input: " + field)
			cue.observe(finished, true, 0.0)
			check(cue.text().is_empty(), "Malformed aid input retained a transition baseline")
	for malformed in [{}, {"surface": null}, {"surface": []}, {"surface": {}}, aid_state("off", "x".repeat(513)), {"surface": active.surface, "surface_walk": {"eye_position": Vector3.ZERO}}, {"surface": active.surface, "surface_walk": null}]:
		cue.reset(active)
		cue.observe(finished, true, 0.0)
		cue.observe(malformed, false, 0.0)
		check(cue.text().is_empty(), "Missing, oversized or outside-actor aid packet retained feedback")
	for bad_delta in [-0.1, NAN, INF, -INF]:
		for advance in [true, false]:
			cue.reset(active)
			cue.observe(finished, true, 0.0)
			cue.observe(finished, advance, bad_delta)
			check(cue.text().is_empty(), "Invalid aid elapsed time retained a result")
			cue.observe(finished, true, 0.0)
			check(cue.text().is_empty(), "Invalid aid elapsed time retained its transition baseline")
	var longest := aid_state("liftoff", "x".repeat(512))
	cue.reset(longest)
	check(cue.text() == longest.surface.note, "Inclusive aid note bound refused valid text")
	cue.observe(finished, true, 0.0)
	cue.observe(finished, true, 1.0e30)
	check(cue.text().is_empty(), "Large finite aid time escaped bounded expiry")
	cue.reset(active)
	cue.observe(finished, true, 0.0)
	cue.reset(finished)
	check(cue.text().is_empty(), "Explicit selection reset retained another view's outcome")
	check(var_to_bytes(active) == original and var_to_bytes(finished) == original_finished, "Aid cue mutated authoritative dictionaries")


func reference_packet(margins: Array, rating: String = "UNKNOWN", supported: Variant = true) -> Dictionary:
	return {"rating": rating, "shielding_thermal": rating, "structural": rating, "propulsion": rating, "operation_supported": supported, "margins": margins}


func check_surface_reference_warning() -> void:
	const LAND := "request_freedom_landing"
	const LIFT := "liftoff_freedom_surface"
	const CAVEAT := "Recorded reference only; not weather or site clearance."
	var known := reference_packet([{"kind": "pressure", "rating": "INSUFFICIENT"}], "INSUFFICIENT")
	var unknown := reference_packet([])
	# Complete malformed packets refuse before any known partial warning is used.
	for packet in [null, true, 0, "unknown", [], {}, {"error": "legacy survey unavailable"}]:
		check(Status.surface_reference_warning(LAND, [known, packet]).is_empty() and Status.surface_reference_warning(LIFT, [packet]).is_empty(), "Missing/error reference packet became a command warning")
	for command in ["", "request_landing", "cancel_freedom_surface_maneuver", "begin_freedom_surface_walk"]:
		check(Status.surface_reference_warning(command, [known]).is_empty(), "Unrelated command acquired a reference warning")
	for packets in [[], [known], [known, known, known]]:
		check(Status.surface_reference_warning(LAND, packets).is_empty(), "Landing accepted the wrong number of operation projections")
	for packets in [[], [known, known]]:
		check(Status.surface_reference_warning(LIFT, packets).is_empty(), "Liftoff accepted the wrong number of operation projections")
	for field in ["rating", "shielding_thermal", "structural", "propulsion"]:
		for value in [null, true, false, 0, 1, NAN, INF, [], {}, "safe", "unknown", ""]:
			var malformed := known.duplicate(true)
			malformed[field] = value
			check(Status.surface_reference_warning(LIFT, [malformed]).is_empty(), "Malformed reference rating accepted: " + field)
	for value in [null, 0, 1, "false", [], {}]:
		var malformed := known.duplicate(true)
		malformed.operation_supported = value
		check(Status.surface_reference_warning(LIFT, [malformed]).is_empty(), "Malformed support flag became a known restriction")
	for field in known:
		var missing := known.duplicate(true)
		missing.erase(field)
		check(Status.surface_reference_warning(LIFT, [missing]).is_empty(), "Reference packet accepted a missing field: " + field)
	var extra := known.duplicate(true)
	extra.hidden_value = "secret measured value"
	check(Status.surface_reference_warning(LIFT, [extra]).is_empty(), "Open reference packet admitted hidden values")
	for margins in [null, {}, "unknown", [null], [[]], [{}], [{"kind": "pressure"}], [{"rating": "MARGINAL"}], [{"kind": "hidden_value", "rating": "MARGINAL"}], [{"kind": "pressure", "rating": "UNKNOWN"}], [{"kind": "pressure", "rating": 1}], [{"kind": "pressure", "rating": "INSUFFICIENT", "remaining": -999}], [{"kind": "pressure", "rating": "MARGINAL"}, {"kind": "pressure", "rating": "INSUFFICIENT"}]]:
		var malformed := known.duplicate(true)
		malformed.margins = margins
		check(Status.surface_reference_warning(LIFT, [malformed]).is_empty(), "Malformed, duplicate or unknown margin became a warning")
	var excessive := known.duplicate(true)
	for repeat in 10: excessive.margins.append({"kind": "pressure", "rating": "MARGINAL"})
	check(Status.surface_reference_warning(LIFT, [excessive]).is_empty(), "Oversized reference margin packet accepted")
	check(Status.surface_reference_warning(LAND, [unknown, unknown]).is_empty(), "Unknown conditions invented a safe or hazardous assessment")
	var safe := reference_packet([{"kind": "gravity", "rating": "SAFE"}], "SAFE")
	check(Status.surface_reference_warning(LAND, [safe, safe]).is_empty(), "Safe reference invented a commitment warning")
	var mixed := reference_packet([{"kind": "pressure", "rating": "MARGINAL"}], "UNKNOWN")
	var before := var_to_bytes([mixed, unknown])
	var warning := Status.surface_reference_warning(LAND, [mixed, unknown])
	check(warning == CAVEAT + "\nTouchdown: Pressure MARGINAL" and not "SAFE" in warning and not "UNKNOWN" in warning and not "Return ascent" in warning, "Known mixed reference lost redaction or invented the missing return assessment")
	check(var_to_bytes([mixed, unknown]) == before, "Warning formatting mutated operation projections")
	check(Status.surface_reference_warning(LIFT, [known]) == CAVEAT + "\nReturn ascent: Pressure INSUFFICIENT", "Liftoff warning used the wrong operation or lost its reference caveat")
	var unsupported := reference_packet([], "UNKNOWN", false)
	check(Status.surface_reference_warning(LIFT, [unsupported]) == CAVEAT + "\nReturn ascent: frame operation unsupported", "Known unsupported frame disappeared behind unrelated unknown margins")
	var competing := reference_packet([{"kind": "pressure", "rating": "MARGINAL"}, {"kind": "gust", "rating": "INSUFFICIENT"}, {"kind": "gravity", "rating": "MARGINAL"}, {"kind": "vertical_thrust", "rating": "INSUFFICIENT"}], "INSUFFICIENT")
	warning = Status.surface_reference_warning(LIFT, [competing])
	check("Wind loading INSUFFICIENT" in warning and "Vertical thrust INSUFFICIENT" in warning and not "MARGINAL" in warning and "more in Surface conditions" in warning, "Warning cap hid severe known limits or omitted its detail pointer")
	var all_limits := reference_packet([], "INSUFFICIENT", false)
	for kind in Status.REFERENCE_MARGIN_LABELS: all_limits.margins.append({"kind": kind, "rating": "INSUFFICIENT"})
	before = var_to_bytes([all_limits, all_limits])
	warning = Status.surface_reference_warning(LAND, [all_limits, all_limits])
	check(not warning.is_empty() and warning.length() <= 400 and warning.count("INSUFFICIENT") == 4 and warning.count("more in Surface conditions") == 2 and warning.begins_with(CAVEAT), "Two maximum operation packets escaped the bounded warning contract")
	check(var_to_bytes([all_limits, all_limits]) == before, "Bounded warning reordered or edited known margins")
	# Preserve the existing paused reference formatter's exact legacy behavior.
	check(Status.reference_limits_text({"margins": []}).is_empty(), "Paused reference lost its default supported behavior")
	check(Status.reference_limits_text({"margins": known.margins, "operation_supported": false}) == "This frame does not support this operation.\nKnown reference limits: Pressure INSUFFICIENT", "Paused reference text changed while sharing the formatter")
	check(Status.reference_limits_text({"margins": [{"kind": "pressure", "rating": "MARGINAL", "legacy_extra": 7}]}) == "Known reference limits: Pressure MARGINAL", "Existing paused formatter acquired new closed-shape restrictions")
	check(Status.reference_limits_text({}) == "Reference margin details are unavailable." and Status.reference_limits_text({"margins": [{"kind": "pressure", "rating": "UNKNOWN"}]}) == "Reference margin details are unavailable.", "Existing paused invalid-data wording changed")


func check_star_search_packets() -> void:
	var legacy := {"selected": true, "active": false, "can_begin": false, "acquired_ticks": 0, "completed": 2, "note": "Legacy regional search"}
	check(StarSearch.valid(legacy) and "2 / 2" in StarSearch.text(legacy), "Legacy six-key search shape or wording changed")
	var frontier := legacy.duplicate(true)
	frontier.version = 2
	frontier.completed = 3
	frontier.can_begin = true
	frontier.note = "Declared reach applies to the current source system"
	var original := frontier.duplicate(true)
	check(StarSearch.valid(frontier) and "3" in StarSearch.text(frontier) and not " / 4" in StarSearch.text(frontier), "Frontier search rejected retained fixes or disclosed a hidden-total denominator")
	frontier.completed = 4
	frontier.can_begin = false
	check(StarSearch.valid(frontier), "Version2 four-record boundary refused")
	for change in [{"version": 1}, {"version": 2.0}, {"version": 3}, {"target": "hidden"}, {"remaining": 1}, {"completed": 5}, {"completed": 4, "can_begin": true}, {"active": true, "acquired_ticks": 1200}, {"active": true, "can_begin": true}, {"selected": false, "completed": 1}]:
		var malformed := original.duplicate(true)
		malformed.merge(change, true)
		check(not StarSearch.valid(malformed), "Malformed or target-leaking frontier search admitted: " + str(change.keys()))
	var wrong_legacy := legacy.duplicate(true)
	wrong_legacy.completed = 3
	check(not StarSearch.valid(wrong_legacy), "Legacy search silently accepted the new completion bound")
	check(original == {"selected": true, "active": false, "can_begin": true, "acquired_ticks": 0, "completed": 3, "note": "Declared reach applies to the current source system", "version": 2}, "Search formatting mutated its input")
