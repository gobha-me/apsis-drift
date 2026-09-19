extends SceneTree
const Plan = preload("res://film_plan.gd")

func _initialize() -> void:
	var errors := 0
	if Plan.duration() != 40 or not Plan.valid_options({}):
		errors += 1
	for bad in [{"--film-fps": "0"}, {"--film-fps": "NAN"}, {"--film-review": "maybe"}, {"--render-size": "-1x1080"}, {"--film-shot": "unknown"}]:
		if Plan.valid_options(bad):
			errors += 1
	for t in [-1.0, 1.01, INF, NAN]:
		if not Plan.sample("world", t, 5499000).is_empty():
			errors += 1
	for radius in [0.0, -1.0, 0.01, 1.0e300, INF, NAN]:
		if not Plan.sample("world", 0.5, radius).is_empty():
			errors += 1
	if not Plan.sample("unknown", 0.5, 5499000).is_empty():
		errors += 1
	var planet := {"atmosphere": {"class": "dense", "pressure_millibars": 1561}, "palette": {"atmosphere": "#644e99"}}
	if not Plan.atmosphere_profile(planet).get("enabled", false):
		errors += 1
	for pressure in [-1, 2501, INF, NAN, "1561"]:
		var bad: Dictionary = planet.duplicate(true)
		bad.atmosphere.pressure_millibars = pressure
		if not Plan.atmosphere_profile(bad).is_empty():
			errors += 1
	planet.atmosphere = {"class": "airless", "pressure_millibars": 0}
	if Plan.atmosphere_profile(planet).get("enabled", true):
		errors += 1
	if not Plan.atmosphere_profile({}).is_empty():
		errors += 1
	for shot in Plan.SHOTS:
		var count: int = shot.seconds * Plan.FPS
		for i in range(count):
			var pose := Plan.sample(shot.id, float(i) / (count - 1), 5499000)
			if pose.is_empty() or not pose.eye.is_finite() or pose.fov < 30 or pose.fov > 90:
				errors += 1
			if shot.id == "cabin":
				if pose.eye != Plan.CockpitLayout.eye() or absf(pose.head.y) > 2.44:
					errors += 1
			elif not pose.target.is_finite() or pose.eye.distance_to(pose.target) < 20:
				errors += 1
	print("Film contract: 960 camera poses, finite/dimension/options bounds, atmosphere gating, seated eye and 40-second duration; %d failures" % errors)
	quit(0 if errors == 0 else 1)
