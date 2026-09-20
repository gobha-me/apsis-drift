extends RefCounted
## Presentation transfer only. C++ owns tick, orientation and the actual star.
## The lab probe is visual geodetic, NOT canonical rotating flight state.

static func finite_number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func valid(sample: Dictionary) -> bool:
	if sample.get("enabled") != true or sample.has("error"):
		return false
	if not sample.get("direction") is Vector3 or not sample.direction.is_finite():
		return false
	if absf(sample.direction.length_squared() - 1.0) > 0.00001:
		return false
	if not sample.get("local_to_system") is Basis or not sample.local_to_system.is_finite():
		return false
	var basis: Basis = sample.local_to_system
	if not (basis.transposed() * basis).is_equal_approx(Basis.IDENTITY) or absf(basis.determinant() - 1.0) > 0.00001:
		return false
	if not finite_number(sample.get("solar_elevation_sine")) or absf(sample.solar_elevation_sine) > 1.00001:
		return false
	# This consumer is restricted to the streaming local ENU frame. Its up axis
	# must agree with the independently supplied C++ solar-elevation scalar.
	if absf(sample.solar_elevation_sine - sample.direction.y) > 0.00001:
		return false
	if not finite_number(sample.get("star_angular_radius_radians")) or sample.star_angular_radius_radians <= 0 or sample.star_angular_radius_radians >= PI / 2:
		return false
	if not sample.get("star_color") is Color:
		return false
	var color: Color = sample.star_color
	for component in [color.r, color.g, color.b, color.a]:
		if not is_finite(component) or component < 0 or component > 1:
			return false
	return sample.get("probe_frame") == "visual_geodetic" and sample.get("model") == "physical-circular-rotation-1-presentation"

static func parameters(sample: Dictionary, radius: float, altitude: float, pressure: float) -> Dictionary:
	if not valid(sample) or not is_finite(radius) or radius <= 0 or not is_finite(altitude) or not is_finite(pressure) or pressure < 0:
		return {}
	var height := maxf(0, altitude)
	var ratio := radius / (radius + height)
	# A display-only spherical limb test for sunlight on the nearby craft.
	# Terrain keeps its directional sun so an orbital crescent is not switched
	# off merely because the observer lies in the planet's shadow.
	var horizon := -sqrt(maxf(0, 1.0 - ratio * ratio))
	var width := maxf(0.00001, sin(sample.star_angular_radius_radians))
	var visibility := smoothstep(horizon - width, horizon + width, sample.direction.y)
	var day := smoothstep(-0.12, 0.16, sample.solar_elevation_sine)
	var air := clampf(pressure / 1013.25, 0, 4) * exp(-height / 18000.0)
	var direction: Vector3 = sample.direction
	var up := Vector3.RIGHT if absf(direction.dot(Vector3.UP)) > 0.99 else Vector3.UP
	return {
		"sun_basis": Basis.looking_at(-direction, up),
		"near_sun_visibility": visibility,
		"daylight": day,
		"ambient": 0.012 + 0.34 * day * minf(1, air),
		"fog_color": Color("05080f").lerp(Color("a1b8c7"), day),
		"fog_energy": 0.015 + 0.335 * day,
	}
