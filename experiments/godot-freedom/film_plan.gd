extends RefCounted
## Frame-addressed editorial plan, not a gameplay timeline.

const FPS := 24
const CockpitLayout = preload("res://cockpit_layout.gd")
const SHOTS := [
	{"id": "world", "seconds": 7, "title": "A WORLD TO GET LOST IN", "detail": "C++ procedural terrain / atmospheric appearance study"},
	{"id": "station", "seconds": 7, "title": "A PLACE TO RETURN TO", "detail": "Modular station / original hero geometry"},
	{"id": "shuttle", "seconds": 6, "title": "START SMALL", "detail": "Shuttle study / landing gear secured for flight"},
	{"id": "cabin", "seconds": 6, "title": "TAKE THE PILOT'S SEAT", "detail": "Closed cabin / instruments remain static concept artwork"},
	{"id": "terrain", "seconds": 8, "title": "PICK YOUR OWN HORIZON", "detail": "Same planet / camera flyover of C++ streamed terrain"},
	{"id": "closing", "seconds": 6, "title": "APSIS DRIFT", "detail": "F R E E D O M   /   I N - E N G I N E   P R E V I E W"},
]

static func duration() -> int:
	var total := 0
	for shot in SHOTS:
		total += int(shot.seconds)
	return total

static func valid_options(options: Dictionary) -> bool:
	return options.get("--film-fps", "24") == "24" and options.get("--film-review", "false") in ["true", "false"] \
		and options.get("--render-size", "3840x2160") in ["1920x1080", "3840x2160"] \
		and options.get("--film-shot", "all") in ["all", "world", "station", "shuttle", "cabin", "terrain", "closing"]

static func atmosphere_profile(planet: Dictionary) -> Dictionary:
	if not planet.get("atmosphere") is Dictionary or not planet.get("palette") is Dictionary:
		return {}
	var atmosphere: Dictionary = planet.atmosphere
	var pressure: Variant = atmosphere.get("pressure_millibars")
	if not (pressure is float or pressure is int) or not is_finite(float(pressure)) or pressure < 0 or pressure > 2500:
		return {}
	if atmosphere.get("class") not in ["airless", "tenuous", "temperate", "dense"]:
		return {}
	var tint: Variant = planet.palette.get("atmosphere")
	if not tint is String or not Color.html_is_valid(tint):
		return {}
	return {"enabled": atmosphere["class"] != "airless" and pressure > 0,
		"coverage": clampf(float(pressure) / 2400.0, 0.12, 0.75), "tint": Color(tint)}

static func sample(id: String, u: float, radius: float) -> Dictionary:
	if not is_finite(u) or u < 0 or u > 1 or not is_finite(radius) or radius < 1 or radius > 1.0e8:
		return {}
	# Gentle motion at cuts without holding the middle of a shot still.
	var t := u * u * (3.0 - 2.0 * u)
	match id:
		"world":
			return {"eye": Vector3(-0.85 * radius, radius * 0.55, radius * 3.65).lerp(Vector3(-0.45 * radius, radius * 0.4, radius * 3.35), t),
				"target": Vector3(0, -radius, 0), "fov": 42.0}
		"station":
			return {"eye": Vector3(240, 135, 270).lerp(Vector3(190, 115, 290), t), "target": Vector3(0, 20, 0), "fov": 42.0}
		"shuttle":
			return {"eye": Vector3(18, 7, 20).lerp(Vector3(22, 5, 13), t), "target": Vector3(0, 1, 0), "fov": 43.0}
		"cabin":
			return {"eye": CockpitLayout.eye(), "head": Vector2(-0.14, lerpf(0.36, -0.22, t)), "fov": 74.0}
		"terrain":
			return {"eye": Vector3(300, 3100, 8500).lerp(Vector3(-1300, 2700, 6200), t), "target": Vector3(-2200, 400, -6500), "fov": 57.0}
		"closing":
			return {"eye": Vector3(-18, 7, -22).lerp(Vector3(-23, 9, -29), t), "target": Vector3(0, 0.8, 0), "fov": 43.0}
	return {}
