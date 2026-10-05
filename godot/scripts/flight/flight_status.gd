extends RefCounted
## Read-only presentation. Location and predicted trajectory are separate facts.
static func describe(state: Dictionary) -> Dictionary:
	var unavailable := {"environment": "POSITION DATA UNAVAILABLE", "trajectory": "CHECK TELEMETRY", "orbit_label": "ORBIT DATA UNAVAILABLE", "orbit_reason": "Check flight telemetry", "motion": "", "established": false, "at_risk": false}
	for key in ["air_density", "altitude", "clearance", "climb_rate", "atmosphere_edge"]:
		if not (state.get(key) is int or state.get(key) is float) or not is_finite(float(state[key])):
			return unavailable
	if state.air_density < 0:
		return unavailable
	for key in ["main_thrust", "retro_thrust"]:
		if state.has(key) and (not (state[key] is int or state[key] is float) or not is_finite(float(state[key]))):
			return unavailable
	if state.has("rcs_acceleration") and (not state.rcs_acceleration is Vector3 or not state.rcs_acceleration.is_finite()):
		return unavailable
	if state.has("orbit_was_established") and not state.orbit_was_established is bool:
		return unavailable
	var environment := "SPACE"
	if state.atmosphere_edge <= 0:
		environment = "AIRLESS WORLD" if state.clearance < 300 else "SPACE / AIRLESS WORLD"
	elif state.air_density >= 0.001:
		environment = "ATMOSPHERIC FLIGHT"
	elif state.air_density >= 0.000001:
		environment = "UPPER ATMOSPHERE / TRACE AIR"
	if state.clearance < 300 and state.atmosphere_edge > 0:
		environment = "NEAR SURFACE / ATMOSPHERE"
	# Orbit is a C++ trajectory fact, never an assist setting, altitude guess,
	# guidance cue or resemblance to the optional reference circle.
	if typeof(state.get("bound_orbit")) != TYPE_BOOL or typeof(state.get("clear_orbit")) != TYPE_BOOL:
		unavailable.environment = environment
		return unavailable
	var established: bool = state.bound_orbit and state.clear_orbit
	var at_risk: bool = not established and state.get("orbit_was_established", false) == true
	var trajectory := "ORBIT ESTABLISHED" if established else ("ORBIT AT RISK" if at_risk else "NOT IN ORBIT")
	var reason := "Coast when ready"
	if not established:
		reason = "Lowest point below safe orbit height" if state.bound_orbit else "Escape path; not a closed orbit"
	var thrust: Vector3 = state.get("rcs_acceleration", Vector3.ZERO)
	var powered: bool = thrust.length() > 0.01 or state.get("main_thrust", 0.0) > 0.01 or state.get("retro_thrust", 0.0) > 0.01
	return {"environment": environment, "trajectory": trajectory, "orbit_label": trajectory, "orbit_reason": reason, "motion": "THRUST ACTIVE" if powered else "COASTING", "established": established, "at_risk": at_risk}

static func distance(metres: float) -> String:
	return "%.1f km" % (metres / 1000.0) if absf(metres) >= 10000 else "%.0f m" % metres
