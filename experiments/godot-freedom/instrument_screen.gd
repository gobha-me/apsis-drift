extends Control
## Read-only flight instruments. Every number comes from authoritative C++ state.
var page := 0
var state: Dictionary = {}
const INK := Color("bcebf0")
const DIM := Color("608591")
const WARN := Color("ffc478")
const GREEN := Color("91e8be")

func write(at: Vector2, value: String, size_px: int = 32, color: Color = INK) -> void:
	draw_string(ThemeDB.fallback_font, at, value, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, color)

func row(y: float, key: String, value: String, color: Color = INK) -> void:
	write(Vector2(35, y), key, 28, DIM)
	write(Vector2(360, y), value, 36, color)

static func distance_text(metres: float) -> String:
	return "%.1f km" % (metres / 1000) if absf(metres) >= 10000 else "%.0f m" % metres

func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color("031017"))
	draw_rect(Rect2(8, 8, size.x - 16, size.y - 16), DIM, false, 2)
	write(Vector2(35, 60), ["NAV / TRAJECTORY", "FLIGHT / VECTOR", "PROPULSION / ASSIST"][page], 38)
	draw_line(Vector2(30, 80), Vector2(866, 80), DIM, 2)
	if state.is_empty():
		write(Vector2(35, 155), "WAITING FOR FLIGHT DATA", 36, WARN)
		return
	if page == 0:
		var status: Dictionary = preload("res://flight_status.gd").describe(state)
		write(Vector2(35, 127), status.environment, 30)
		write(Vector2(35, 172), status.trajectory, 30, GREEN if state.clear_orbit else WARN)
		row(227, "APOAPSIS", distance_text(state.apoapsis) if state.bound_orbit else "UNBOUND")
		row(282, "PERIAPSIS", "INTERSECTS BODY" if state.periapsis < 0 else distance_text(state.periapsis), WARN if state.periapsis < state.atmosphere_edge else GREEN)
		row(337, "ALT / DATUM", distance_text(state.altitude))
		row(392, "HORIZONTAL", "%.0f m/s" % state.horizontal_speed)
		row(447, "CIRCULAR SPEED", "%.0f m/s" % state.circular_speed)
		write(Vector2(35, 505), "AIR EDGE %s | Coast forecast only" % distance_text(state.atmosphere_edge), 27, DIM)
	else:
		if page == 1:
			write(Vector2(35, 154), "%.0f" % state.speed, 68)
			write(Vector2(235, 151), "m/s", 30, DIM)
			write(Vector2(440, 130), "CLEARANCE", 27, DIM)
			write(Vector2(440, 180), distance_text(state.clearance), 44)
			var center := Vector2(448, 307)
			draw_circle(center, 95, DIM, false, 2)
			var basis: Basis = state.body_basis
			var pitch := asin(clampf(-basis.z.y, -1, 1))
			var bank := atan2(basis.x.y, basis.y.y)
			var tilt := Vector2(cos(bank), sin(bank))
			var offset := Vector2(-tilt.y, tilt.x) * clampf(pitch * 65, -75, 75)
			draw_line(center + offset - tilt * 135, center + offset + tilt * 135, INK, 3)
			draw_line(center - Vector2(55, 0), center - Vector2(12, 0), WARN, 4)
			draw_line(center + Vector2(12, 0), center + Vector2(55, 0), WARN, 4)
			var velocity: Vector3 = state.body_velocity
			if velocity.length() > 1:
				var marker := center + Vector2(atan2(velocity.x, -velocity.z), -atan2(velocity.y, maxf(0.01, Vector2(velocity.x, velocity.z).length()))) * 65
				marker = center + (marker - center).limit_length(135)
				draw_circle(marker, 10, GREEN, false, 3)
				if velocity.z > 0:
					write(Vector2(610, 320), "AFT", 28, WARN)
			row(450, "CLIMB", "%+.0f m/s" % state.climb_rate)
			write(Vector2(35, 510), "Cyan horizon / green travel vector", 28, DIM)
		else:
			for i in 2:
				var value: float = state.main_thrust if i == 0 else state.retro_thrust
				var y := 140 + i * 70
				write(Vector2(35, y), "MAIN" if i == 0 else "RETRO", 32)
				draw_rect(Rect2(210, y - 25, 455, 24), Color("19303d"))
				draw_rect(Rect2(210, y - 25, 455 * value, 24), GREEN if i == 0 else WARN)
				write(Vector2(704, y), "%3.0f%%" % (value * 100), 34)
			var inertial_rotation: bool = state.get("flight_model", "") == "thrust-lab-2"
			row(282, "ASSIST", "ON / LIMITED" if state.assist else ("OFF / COAST" if inertial_rotation else "OFF"), WARN if state.assist else GREEN)
			var thrust: Vector3 = state.rcs_acceleration
			row(344, "ACTUAL X/Y/Z", "%+.0f / %+.0f / %+.0f" % [thrust.x, thrust.y, thrust.z])
			row(406, "AIR LOAD", "%.1f kPa" % (state.dynamic_pressure / 1000))
			write(Vector2(35, 465), "Actual thrust includes assist / units m/s²", 27, DIM)
			var limitation := "Fuel / heat / damage: not simulated"
			if inertial_rotation:
				limitation = "Spin %s | fuel/heat/damage: not simulated" % ("stabilized" if state.assist else "coasts")
			write(Vector2(35, 510), limitation, 26 if inertial_rotation else 28, WARN)
	if state.floor_guard:
		write(Vector2(35, 545), "TEST FLOOR GUARD — NOT A LANDING", 26, WARN)
