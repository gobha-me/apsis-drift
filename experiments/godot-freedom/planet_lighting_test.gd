extends SceneTree
## Presentation mapping contract, not physical ephemeris or GPU qualification.
const Lighting = preload("res://planet_lighting.gd")
var failures := 0

func check(ok: bool, reason: String) -> void:
	if not ok:
		failures += 1
		push_error(reason)

func sample(direction: Vector3) -> Dictionary:
	return {"enabled": true, "direction": direction, "local_to_system": Basis.IDENTITY,
		"solar_elevation_sine": direction.y, "star_angular_radius_radians": 0.00465,
		"star_color": Color.WHITE, "probe_frame": "visual_geodetic",
		"model": "physical-circular-rotation-1-presentation"}

func _initialize() -> void:
	var noon := sample(Vector3.UP)
	var night := sample(Vector3.DOWN)
	var day := Lighting.parameters(noon, 6371000, 0, 1013.25)
	var dark := Lighting.parameters(night, 6371000, 0, 1013.25)
	check(not day.is_empty() and not dark.is_empty(), "Both vertical sun poles accept fallback basis")
	check(day.near_sun_visibility == 1 and dark.near_sun_visibility == 0, "Surface day/night craft visibility")
	check(day.ambient > dark.ambient and day.fog_energy > dark.fog_energy, "Ambient and fog agree with day/night")
	for direction in [Vector3.UP, Vector3.DOWN, Vector3.RIGHT, Vector3(1, -0.1, 0).normalized()]:
		var input := sample(direction)
		var before := input.duplicate(true)
		var result := Lighting.parameters(input, 6371000, 250000, 1013.25)
		check(result.sun_basis.z.is_equal_approx(direction), "Light +Z points toward star; rays travel -Z")
		check(input == before, "Presentation query does not write authoritative sample")
	var low_sun := sample(Vector3(1, -0.1, 0).normalized())
	check(Lighting.parameters(low_sun, 6371000, 0, 1013.25).near_sun_visibility == 0, "Below ground horizon")
	check(Lighting.parameters(low_sun, 6371000, 250000, 1013.25).near_sun_visibility == 1, "Visible below local horizontal from orbit")
	check(Lighting.parameters(noon, 6371000, 0, 0).ambient == dark.ambient, "No fabricated atmospheric ambient in vacuum")
	# Actual authored-home globe inspection: star radius 10.5 degrees, planet
	# radius 23.6 degrees at observer radius 2.5R. No anti-solar light leak.
	var large_star := sample(Vector3.DOWN)
	large_star.star_angular_radius_radians = 0.183547532694299
	check(Lighting.parameters(large_star, 5764000, 8646000, 0).near_sun_visibility == 0, "High-altitude anti-solar star fully occulted")
	for height_ratio in [0.0, 0.04, 1.5, 10.0]:
		var horizon := -acos(1.0 / (1.0 + height_ratio))
		for offset in [-0.02, 0.0, 0.02]:
			var angle: float = horizon + offset
			var limb := sample(Vector3(cos(angle), sin(angle), 0))
			limb.star_angular_radius_radians = 0.01
			var visibility: float = Lighting.parameters(limb, 6371000, 6371000 * height_ratio, 0).near_sun_visibility
			check(absf(visibility - (0.0 if offset < 0 else (1.0 if offset > 0 else 0.5))) < 0.0001, "Angular limb coverage independent of observer height")
	for patch in [{"enabled": false}, {"direction": Vector3.ZERO}, {"direction": Vector3(NAN, 0, 0)},
		{"direction": Vector3(2, 0, 0)}, {"local_to_system": Basis(Vector3.ONE, Vector3.ONE, Vector3.ONE)},
		{"local_to_system": Basis.FLIP_X}, {"solar_elevation_sine": NAN}, {"solar_elevation_sine": true},
		{"solar_elevation_sine": 2}, {"solar_elevation_sine": -1}, {"star_angular_radius_radians": 0}, {"star_angular_radius_radians": INF},
		{"star_color": Color(NAN, 0, 0)}, {"star_color": Color(2, 0, 0)}, {"probe_frame": "planet_fixed"},
		{"model": "unknown"}, {"error": "refused"}]:
		var bad := noon.duplicate(true)
		bad.merge(patch, true)
		check(Lighting.parameters(bad, 6371000, 0, 1013.25).is_empty(), "Malformed sample refused: " + str(patch))
	for args in [[0, 0, 0], [INF, 0, 0], [6371000, NAN, 0], [6371000, 0, -1], [6371000, 0, NAN]]:
		check(Lighting.parameters(noon, args[0], args[1], args[2]).is_empty(), "Invalid environment rejected")
	print("planet lighting: %d failures" % failures)
	quit(0 if failures == 0 else 1)
