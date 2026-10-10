extends SceneTree
const Status = preload("res://scripts/native/native_status.gd")
var failed := 0

func check(value: bool, reason: String) -> void:
	if not value: push_error(reason); failed += 1

func _initialize() -> void:
	# Invalid data/layout is checked before any world, model or rendering stage.
	for dimensions in [Vector2.ZERO, Vector2(-1, 720), Vector2(INF, 720), Vector2(1280, NAN)]:
		check(Status.layout_bounds(dimensions, Vector2(1280, 720)) == Rect2() and Status.layout_bounds(Vector2(1280, 720), dimensions) == Rect2(), "Invalid HUD dimensions accepted")
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
	print("Native status: %d failures" % failed)
	quit(1 if failed else 0)
