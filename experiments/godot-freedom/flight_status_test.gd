extends SceneTree
const Status = preload("res://flight_status.gd")
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func _initialize() -> void:
	var state := {"air_density": 0.0000001, "altitude": 180000.0, "clearance": 178000.0, "climb_rate": 700.0, "atmosphere_edge": 123000.0, "bound_orbit": true, "clear_orbit": false, "assist": false}
	for value in [NAN, INF, -1.0]:
		var invalid := state.duplicate()
		invalid.air_density = value
		check(Status.describe(invalid).trajectory == "CHECK TELEMETRY", "Invalid telemetry accepted")
	check(Status.describe({}).trajectory == "CHECK TELEMETRY", "Missing telemetry accepted")
	check(Status.describe(state).environment == "SPACE" and "SUBORBITAL" in Status.describe(state).trajectory, "High altitude falsely implies orbit")
	state.air_density = 0.000001
	check("TRACE AIR" in Status.describe(state).environment, "Atmosphere boundary wrong")
	state.air_density = 0.001
	check(Status.describe(state).environment == "ATMOSPHERIC FLIGHT", "Dense air mislabeled")
	state.air_density = 0.0000001
	state.clear_orbit = true
	check(Status.describe(state).trajectory == "ORBIT / COASTING", "Coasting orbit missing")
	state.main_thrust = 0.5
	check(Status.describe(state).trajectory == "ORBIT / MANEUVERING", "Powered orbit called coasting")
	state.assist = true
	check(Status.describe(state).trajectory == "ORBIT / ASSIST ACTIVE", "Assist caveat missing")
	state.clear_orbit = false
	state.bound_orbit = false
	check(Status.describe(state).trajectory == "ESCAPE TRAJECTORY", "Escape mislabeled suborbital")
	state.atmosphere_edge = 0
	state.air_density = 0
	check("AIRLESS" in Status.describe(state).environment, "Airless planet has atmosphere")
	check(Status.distance(85000) == "85.0 km" and Status.distance(60) == "60 m", "Altitude units unclear")
	print("Flight status contracts: %d failures" % failures)
	quit(0 if failures == 0 else 1)
