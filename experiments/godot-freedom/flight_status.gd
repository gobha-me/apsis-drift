extends RefCounted
## Read-only presentation. Location and predicted trajectory are separate facts.
static func describe(state: Dictionary) -> Dictionary:
	for key in ["air_density", "altitude", "clearance", "climb_rate", "atmosphere_edge"]:
		if not state.has(key) or not is_finite(float(state[key])):
			return {"environment": "POSITION DATA UNAVAILABLE", "trajectory": "CHECK TELEMETRY"}
	if state.air_density < 0:
		return {"environment": "POSITION DATA UNAVAILABLE", "trajectory": "CHECK TELEMETRY"}
	var environment := "SPACE"
	if state.atmosphere_edge <= 0:
		environment = "AIRLESS WORLD" if state.clearance < 300 else "SPACE / AIRLESS WORLD"
	elif state.air_density >= 0.001:
		environment = "ATMOSPHERIC FLIGHT"
	elif state.air_density >= 0.000001:
		environment = "UPPER ATMOSPHERE / TRACE AIR"
	if state.clearance < 300 and state.atmosphere_edge > 0:
		environment = "NEAR SURFACE / ATMOSPHERE"
	var trajectory := "SUBORBITAL / RETURN PATH"
	if not state.get("bound_orbit", true):
		trajectory = "ESCAPE TRAJECTORY"
	elif state.get("clear_orbit", false):
		var thrust: Vector3 = state.get("rcs_acceleration", Vector3.ZERO)
		trajectory = "ORBIT / COASTING"
		if state.get("assist", false):
			trajectory = "ORBIT / ASSIST ACTIVE"
		elif thrust.length() > 0.01 or state.get("main_thrust", 0.0) > 0.01 or state.get("retro_thrust", 0.0) > 0.01:
			trajectory = "ORBIT / MANEUVERING"
	elif state.clearance < 300:
		trajectory = "SURFACE FLIGHT / NOT IN ORBIT"
	return {"environment": environment, "trajectory": trajectory}

static func distance(metres: float) -> String:
	return "%.1f km" % (metres / 1000.0) if absf(metres) >= 10000 else "%.0f m" % metres
