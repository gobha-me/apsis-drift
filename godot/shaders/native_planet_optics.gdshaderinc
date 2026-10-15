// Immutable C++ optics1. This include is shared by sky and actual terrain rays.
uniform bool optical_enabled = false;
uniform sampler2D optical_atlas : filter_linear, repeat_enable;
uniform mat3 optical_fixed_from_tangent;
uniform vec3 optical_planet_center;
uniform float optical_reference_radius;
uniform float optical_lower_height;
uniform float optical_upper_height;
uniform float optical_opacity_cap;
uniform vec3 optical_tint : source_color;
uniform vec3 optical_sun_direction;
uniform vec3 optical_sun_color : source_color;

vec2 optical_sphere_interval(vec3 eye, vec3 direction, float sphere_radius) {
	float along = dot(eye, direction);
	vec3 closest = eye - direction * along;
	float discriminant = sphere_radius * sphere_radius - dot(closest, closest);
	if (discriminant <= 0.0) return vec2(1.0, -1.0);
	float half_length = sqrt(discriminant);
	return vec2(-along - half_length, -along + half_length);
}

vec4 optical_segment(vec3 eye, vec3 direction, float first, float last) {
	if (last <= first) return vec4(0.0);
	float step_length = (last - first) / 8.0;
	vec4 sum = vec4(0.0);
	for (int sample_index = 0; sample_index < 8; sample_index++) {
		vec3 point = eye + direction * (first + (float(sample_index) + 0.5) * step_length);
		float radial_distance = length(point);
		float height = radial_distance - optical_reference_radius;
		float t = clamp((height - optical_lower_height) / (optical_upper_height - optical_lower_height), 0.0, 1.0);
		vec3 fixed_point = optical_fixed_from_tangent * point;
		float latitude = asin(clamp(fixed_point.z / max(0.0001, radial_distance), -1.0, 1.0));
		float longitude = atan(fixed_point.y, fixed_point.x);
		float u = fract((longitude + PI) / TAU);
		float v = clamp((latitude + PI * 0.5) / PI, 0.0, 1.0);
		// Longitude wraps between nodes63/0. Pole rows are coherent and y-clamped.
		vec2 texel = vec2((u * 64.0 + 0.5) / 64.0, (v * 31.0 + 0.5) / 32.0);
		float density = texture(optical_atlas, texel).r * 4.0 * t * (1.0 - t);
		float weight = density * step_length;
		float horizon = -sqrt(max(0.0, 1.0 - pow(optical_reference_radius / max(optical_reference_radius, radial_distance), 2.0)));
		float solar = dot(point / max(0.0001, radial_distance), optical_sun_direction);
		float lit = smoothstep(horizon - 0.08, horizon + 0.08, solar);
		sum += vec4(optical_tint * optical_sun_color * lit * weight, weight);
	}
	return sum;
}

// Return unpremultiplied sun-lit scattering and a bounded selected opacity.
vec4 optical_ray(vec3 eye_tangent, vec3 direction, float maximum_distance) {
	if (!optical_enabled || maximum_distance <= 0.0 || optical_upper_height <= optical_lower_height) return vec4(0.0);
	vec3 eye = eye_tangent - optical_planet_center;
	vec2 outer = optical_sphere_interval(eye, direction, optical_reference_radius + optical_upper_height);
	float first = max(0.0, outer.x);
	float last = min(maximum_distance, outer.y);
	if (last <= first) return vec4(0.0);
	// Clip the thin shell BEFORE quadrature, rather than sampling a whole orbit
	// segment that could miss every kilometre of the actual optical layer.
	vec2 inner = optical_sphere_interval(eye, direction, optical_reference_radius + optical_lower_height);
	vec4 sum;
	if (inner.y > inner.x) {
		sum = optical_segment(eye, direction, first, min(last, inner.x));
		sum += optical_segment(eye, direction, max(first, inner.y), last);
	} else {
		sum = optical_segment(eye, direction, first, last);
	}
	float opacity = optical_opacity_cap * (1.0 - exp(-sum.a / (optical_upper_height - optical_lower_height)));
	return vec4(sum.rgb / max(0.000001, sum.a), clamp(opacity, 0.0, optical_opacity_cap));
}
