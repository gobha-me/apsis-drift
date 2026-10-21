extends SceneTree
const Status = preload("res://scripts/flight/flight_status.gd")
const Guidance = preload("res://scripts/flight/guidance_screen.gd")
const FrameDiagnostics = preload("res://scripts/native/native_frame_diagnostics.gd")
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	check_frame_diagnostics()
	await check_frame_diagnostics_layout()
	var state := {"air_density": 0.0000001, "altitude": 180000.0, "clearance": 178000.0, "climb_rate": 700.0, "atmosphere_edge": 123000.0, "bound_orbit": true, "clear_orbit": false, "assist": false}
	for value in [NAN, INF, -1.0]:
		var invalid := state.duplicate()
		invalid.air_density = value
		check(Status.describe(invalid).trajectory == "CHECK TELEMETRY", "Invalid telemetry accepted")
	check(Status.describe({}).trajectory == "CHECK TELEMETRY", "Missing telemetry accepted")
	for key in ["air_density", "altitude", "clearance", "climb_rate", "atmosphere_edge", "main_thrust", "retro_thrust"]:
		for value in [null, "0", "not a number", false, true, [], {}, Vector3.ZERO, NAN, INF]:
			var malformed := state.duplicate()
			malformed[key] = value
			check(Status.describe(malformed).trajectory == "CHECK TELEMETRY", "Non-numeric/non-finite %s accepted" % key)
	for value in [null, "0", false, 0, Vector2.ZERO, Vector3(NAN, 0, 0), Vector3(0, INF, 0)]:
		var malformed := state.duplicate()
		malformed.rcs_acceleration = value
		check(Status.describe(malformed).trajectory == "CHECK TELEMETRY", "Malformed thrust vector accepted")
	for value in [null, "true", 1, [], {}]:
		var malformed := state.duplicate()
		malformed.orbit_was_established = value
		check(Status.describe(malformed).trajectory == "CHECK TELEMETRY", "Malformed orbit history accepted")
	check(Status.describe(state).environment == "SPACE" and Status.describe(state).trajectory == "NOT IN ORBIT", "High altitude falsely implies orbit")
	state.air_density = 0.000001
	check("TRACE AIR" in Status.describe(state).environment, "Atmosphere boundary wrong")
	state.air_density = 0.001
	check(Status.describe(state).environment == "ATMOSPHERIC FLIGHT", "Dense air mislabeled")
	state.air_density = 0.0000001
	state.clear_orbit = true
	check(Status.describe(state).trajectory == "ORBIT ESTABLISHED" and Status.describe(state).motion == "COASTING", "Coasting orbit missing")
	state.main_thrust = 0.5
	check(Status.describe(state).trajectory == "ORBIT ESTABLISHED" and Status.describe(state).motion == "THRUST ACTIVE", "Powered orbit truth changed or called coasting")
	state.assist = true
	check(Status.describe(state).trajectory == "ORBIT ESTABLISHED", "Assist changed orbit truth")
	state.clear_orbit = false
	state.bound_orbit = false
	check(Status.describe(state).trajectory == "NOT IN ORBIT" and "Escape" in Status.describe(state).orbit_reason, "Escape reason missing")
	# All orbit labels depend on current authoritative classification and an
	# externally maintained history bit, never assist, thrust or presentation model.
	for assist in [false, true]:
		for bound in [false, true]:
			for clear in [false, true]:
				for previous in [false, true]:
					for model in ["thrust-lab-1", "thrust-lab-2", "thrust-lab-3"]:
						state.assist = assist
						state.bound_orbit = bound
						state.clear_orbit = clear
						state.orbit_was_established = previous
						state.flight_model = model
						var before := state.duplicate(true)
						var status := Status.describe(state)
						var expected := "ORBIT ESTABLISHED" if bound and clear else ("ORBIT AT RISK" if previous else "NOT IN ORBIT")
						check(status.trajectory == expected and status.orbit_label == expected, "Orbit label depends on assist/model or ignores history")
						check(status.established == (bound and clear) and status.at_risk == (previous and not (bound and clear)), "Orbit flags disagree with label")
						check(state == before, "Read-only status changed authoritative state or history")
						if bound and clear:
							check(status.orbit_reason == "Coast when ready", "Established orbit lacks simple next step")
						elif bound:
							check("below safe orbit height" in status.orbit_reason, "Bound return path lacks plain reason")
						else:
							check("Escape" in status.orbit_reason, "Unbound path lacks plain reason")
	# Missing classifier data must not silently imply a safe orbit or return path.
	for key in ["bound_orbit", "clear_orbit"]:
		var missing := state.duplicate()
		missing.erase(key)
		check(Status.describe(missing).trajectory == "CHECK TELEMETRY", "Missing orbit classification accepted")
		missing[key] = 1
		check(Status.describe(missing).trajectory == "CHECK TELEMETRY", "Non-boolean orbit classification accepted")
	state.bound_orbit = true
	state.clear_orbit = true
	state.main_thrust = 0.0
	state.rcs_acceleration = Vector3.ZERO
	check(Status.describe(state).motion == "COASTING", "Assist enabled without thrust mislabeled as powered")
	state.rcs_acceleration = Vector3(0.02, 0, 0)
	check(Status.describe(state).motion == "THRUST ACTIVE", "RCS thrust omitted from motion description")
	state.rcs_acceleration = Vector3.ZERO
	state.retro_thrust = 0.2
	check(Status.describe(state).motion == "THRUST ACTIVE", "Retro thrust omitted from motion description")
	# Guidance cues and reference circles can never promote the authoritative
	# orbit status or instruct unnecessary circularization once orbit is clear.
	var established := Status.describe(state)
	for cue in ["climb", "sideways", "circular", "orbit"]:
		check(Guidance.cue_text({"mode": 1, "cue": cue}, established) == "Reference circle is optional", "Orbit guidance demands unnecessary reference matching")
	state.clear_orbit = false
	state.orbit_was_established = false
	var not_orbit := Status.describe(state)
	check(Guidance.cue_text({"mode": 1, "cue": "orbit"}, not_orbit) == "Check current orbit status above", "Reference cue falsely establishes orbit")
	check(Guidance.cue_text({"mode": 1, "cue": "sideways"}, not_orbit) == "Build speed along the horizon", "Ambiguous sideways instruction retained")
	check(Guidance.cue_text({"mode": 2, "cue": "return"}, established) == "Return reference ends at air edge", "Orbit status hides selected return guidance")
	# Measure fixed headlines/reasons at the actual overlay font sizes. This is
	# layout evidence only; root performs the in-cockpit GPU readability check.
	for phrase in ["ORBIT ESTABLISHED", "ORBIT AT RISK", "NOT IN ORBIT", "ORBIT DATA UNAVAILABLE"]:
		check(ThemeDB.fallback_font.get_string_size(phrase, HORIZONTAL_ALIGNMENT_LEFT, -1, 25).x <= 392, "Orbit headline exceeds chase viewport")
	for phrase in ["Coast when ready", "Lowest point below safe orbit height", "Escape path; not a closed orbit"]:
		check(ThemeDB.fallback_font.get_string_size(phrase, HORIZONTAL_ALIGNMENT_LEFT, -1, 17).x <= 392, "Orbit reason exceeds chase viewport")
	state.atmosphere_edge = 0
	state.air_density = 0
	check("AIRLESS" in Status.describe(state).environment, "Airless planet has atmosphere")
	check(Status.distance(85000) == "85.0 km" and Status.distance(60) == "60 m", "Altitude units unclear")
	print("Flight status contracts: %d failures" % failures)
	quit(0 if failures == 0 else 1)


func check_frame_diagnostics() -> void:
	for invalid in [[], [0.0], [-1.0], [NAN], [INF], [true], ["0.01"], [60.01]]:
		check(FrameDiagnostics.summary(invalid).is_empty(), "Invalid frame interval accepted: " + str(invalid))
	var input := [0.020, 0.010, 0.040, 0.030]
	var unchanged := input.duplicate()
	var result: Dictionary = FrameDiagnostics.summary(input)
	check(result.count == 4 and is_equal_approx(result.median_ms, 25.0) and is_equal_approx(result.p95_ms, 40.0) and input == unchanged, "Frame summary lost units, nearest rank or read-only input")
	var too_many: Array = []
	too_many.resize(241)
	too_many.fill(0.01)
	check(FrameDiagnostics.summary(too_many).is_empty(), "Unbounded frame-summary buffer accepted")
	var panel := FrameDiagnostics.new()
	var clock := [0, 8333]
	panel.clock_usec = func() -> int:
		clock[0] += clock[1]
		return clock[0]
	var state := {"tick": "18446744073709551615", "dropped_seconds": 0.125}
	var original := state.duplicate(true)
	panel.observe(0.01, state, [Vector2i(1280, 720)], true)
	check(not panel.enabled and panel.samples.is_empty() and not panel.visible, "Diagnostics collected while off")
	panel.set_enabled(true)
	panel.observe(60.0, state, [Vector2i(1280, 720)], true)
	check(panel.samples.is_empty(), "First active delta included prior pause time")
	for frame in 300: panel.observe(1.0 / 120.0, state, [Vector2i(1280, 720)], true)
	check(panel.samples.size() == 240 and panel.label.text.contains("p95") and panel.label.text.contains("GPU timing not sampled") and panel.label.text.contains(state.tick), "Frame diagnostics lost bounded history, metric labels or exact unsigned clock")
	panel.observe(60.0, state, [], false)
	check(panel.samples.is_empty() and not panel.visible, "Inactive diagnostics kept timing epoch")
	clock[1] = 20000
	panel.observe(60.0, state, [Vector2i(640, 450)], true)
	panel.observe(0.001, state, [Vector2i(640, 450)], true)
	check(panel.samples.size() == 1 and is_equal_approx(panel.samples[0], 0.02), "Resume included the inactive interval")
	# A stalled render frame must reflect the monotonic clock rather than an
	# engine delta hint, which can be scaled or adjusted under load.
	clock[1] = 250000
	panel.observe(0.001, state, [Vector2i(640, 450)], true)
	check(panel.samples.size() == 2 and is_equal_approx(panel.samples[1], 0.25), "Engine delta hid the wall-clock stall")
	clock[1] = -1
	panel.observe(0.01, state, [Vector2i(640, 450)], true)
	check(panel.samples.is_empty() and not panel.visible, "Backward diagnostic clock accepted")
	clock[1] = 20000
	for field in ["tick", "dropped_seconds"]:
		for value in [null, NAN, INF, -1.0, true, {}, ""]:
			var malformed := state.duplicate()
			malformed[field] = value
			panel.observe(0.01, malformed, [Vector2i(640, 450)], true)
			check(panel.samples.is_empty() and not panel.visible, "Malformed diagnostic telemetry accepted: " + field)
	for tick in ["00", "-1", "1.0", "18446744073709551616", "999999999999999999999"]:
		var malformed := state.duplicate()
		malformed.tick = tick
		panel.observe(0.01, malformed, [Vector2i(640, 450)], true)
		check(panel.samples.is_empty() and not panel.visible, "Noncanonical/overflow diagnostic tick accepted")
	for targets in [[], [Vector2i.ZERO], [Vector2i(-1, 450)], [Vector2i(640, 0)], [Vector2i(65537, 450)], [Vector2(NAN, 450)], [Vector2i(640, 450), null]]:
		panel.observe(0.01, state, targets, true)
		check(panel.samples.is_empty() and not panel.visible, "Invalid diagnostic render dimensions accepted")
	check(state == original, "Presentation diagnostics modified its source state")
	panel.set_enabled(false)
	check(panel.samples.is_empty() and not panel.visible, "Disabling diagnostics retained data or overlay")
	panel.free()


func check_frame_diagnostics_layout() -> void:
	var original_size := root.size
	var parent := Control.new()
	root.add_child(parent)
	parent.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var panel := FrameDiagnostics.new()
	parent.add_child(panel)
	var clock := [0]
	panel.clock_usec = func() -> int:
		clock[0] += 16667
		return clock[0]
	panel.set_enabled(true)
	for pixels in [Vector2i(1280, 720), Vector2i(640, 450)]:
		root.size = pixels
		for frame in 3: await process_frame
		for sample in 40:
			panel.observe(0.001, {"tick": "18446744073709551615", "dropped_seconds": 1.0e12}, [pixels, pixels], true)
		for frame in 3: await process_frame
		check(panel.visible and panel.label.text.contains("p95") and Rect2(Vector2.ZERO, parent.size).encloses(panel.get_rect()), "Hidden-container diagnostic layout failed to become bounded and visible")
		check(panel.label.size.y >= panel.label.get_minimum_size().y and panel.get_global_rect().encloses(panel.label.get_global_rect()), "Diagnostic label clipped before or after resize")
	parent.free()
	root.size = original_size
	for frame in 3: await process_frame
