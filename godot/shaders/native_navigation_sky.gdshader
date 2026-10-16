shader_type sky;
#include "res://shaders/native_planet_optics.gdshaderinc"
// Current C++ native tangent frame, body profile and system star. No TIME/RNG.
uniform float radius;
uniform float altitude;
uniform float scale_height;
uniform float sea_density;
uniform float atmosphere_edge;
uniform vec3 atmosphere_tint : source_color;
uniform vec3 sun_direction;
uniform vec3 sun_color : source_color;
uniform float sun_angular_radius;
uniform float sun_visibility;
// C++ same-tick native-tangent projections, in far-to-near order. These are
// optical silhouettes, never world meshes or supported approach surfaces.
uniform int distant_contact_count = 0;
uniform vec3 distant_contact_directions[2];
uniform float distant_contact_angular_radii[2];
uniform vec3 distant_contact_light_directions[2];
uniform vec4 distant_contact_tints[2];
uniform float distant_contact_spins[2];
void sky() {
	vec3 d = normalize(EYEDIR);
	// World origin is the craft's C++ tangent projection; POSITION is the
	// actual camera offset, including chase/cockpit and suited walking eyes.
	vec3 eye = vec3(0.0, radius + altitude, 0.0) + POSITION;
	float b = dot(eye, d);
	float outer = radius + atmosphere_edge;
	float disc = b * b - dot(eye, eye) + outer * outer;
	float ground_disc = b * b - dot(eye, eye) + radius * radius;
	bool ground = ground_disc > 0.0 && b < 0.0;
	float optical = 0.0;
	float lit_optical = 0.0;
	float warm_optical = 0.0;
	if (disc > 0.0 && scale_height > 0.0 && sea_density > 0.0) {
		float start = max(0.0, -b - sqrt(disc));
		float end = max(0.0, -b + sqrt(disc));
		if (ground) end = min(end, max(0.0, -b - sqrt(ground_disc)));
		float step_length = (end - start) / 8.0;
		for (int i = 0; i < 8; i++) {
			vec3 point = eye + d * (start + (float(i) + 0.5) * step_length);
			float h = max(0.0, length(point) - radius);
			// Same declared exponential/taper profile as C++ force sampling.
			float taper = clamp((atmosphere_edge - h) / scale_height, 0.0, 1.0);
			float weight = (sea_density / 1.225) * exp(-h / scale_height) * taper * taper * (3.0 - 2.0 * taper) * step_length / 16000.0;
			float solar = dot(normalize(point), sun_direction);
			float horizon = -sqrt(max(0.0, 1.0 - pow(radius / max(radius, length(point)), 2.0)));
			float lit = smoothstep(horizon - 0.10, horizon + 0.12, solar);
			optical += weight;
			lit_optical += weight * lit;
			warm_optical += weight * lit * (1.0 - smoothstep(-0.03, 0.20, solar));
		}
	}
	float haze = 1.0 - exp(-optical);
	float illumination = lit_optical / max(0.000001, optical);
	float warm = warm_optical / max(0.000001, optical);
	float toward_sun = max(0.0, dot(d, sun_direction));
	vec3 scatter = atmosphere_tint * (0.35 + 0.65 * max(0.0, d.y)) * illumination;
	scatter += vec3(0.55, 0.19, 0.065) * warm * pow(toward_sun, 4.0);
	COLOR = scatter * sun_color * haze;
	float sun_angle = acos(clamp(dot(d, sun_direction), -1.0, 1.0));
	float antialias = max(0.00001, fwidth(sun_angle));
	float sun_disc = 1.0 - smoothstep(max(0.0, sun_angular_radius - antialias), sun_angular_radius + antialias, sun_angle);
	if (!ground) COLOR += sun_color * sun_disc * sun_visibility * 6.0 * exp(-optical * 0.15);
	if (!ground) {
		for (int i = 0; i < 2; i++) {
			if (i >= distant_contact_count) break;
			vec3 centre = distant_contact_directions[i];
			float angular_radius = distant_contact_angular_radii[i];
			float angle = atan(length(cross(d, centre)), clamp(dot(d, centre), -1.0, 1.0));
			float edge = max(0.000001, fwidth(angle));
			float coverage = 1.0 - smoothstep(max(0.0, angular_radius - edge), angular_radius + edge, angle);
			if (coverage <= 0.0 || angular_radius <= 0.0) continue;
			vec3 reference = abs(centre.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
			vec3 right = normalize(cross(reference, centre));
			vec3 up = cross(centre, right);
			vec2 disc_point = vec2(dot(d, right), dot(d, up)) / sin(angular_radius);
			float facing = sqrt(max(0.0, 1.0 - dot(disc_point, disc_point)));
			vec3 normal = normalize(right * disc_point.x + up * disc_point.y - centre * facing);
			float phase_light = max(0.0, dot(normal, distant_contact_light_directions[i]));
			float spin = distant_contact_spins[i];
			vec2 rotated = mat2(vec2(cos(spin), sin(spin)), vec2(-sin(spin), cos(spin))) * vec2(disc_point.x, facing);
			// Coarse analytic albedo variation; rotation comes only from C++.
			float mottling = 0.82 + 0.12 * sin(rotated.x * 11.0 + disc_point.y * 5.0) * sin(disc_point.y * 13.0 - rotated.y * 4.0);
			vec3 contact_color = distant_contact_tints[i].rgb * mottling * (0.025 + 0.975 * phase_light) * sun_color;
			contact_color = contact_color * exp(-optical * 0.15) + scatter * sun_color * haze;
			COLOR = mix(COLOR, contact_color, coverage);
		}
	}
	if (optical_enabled && disc > 0.0) {
		float ray_end = max(0.0, -b + sqrt(disc));
		if (ground) ray_end = min(ray_end, max(0.0, -b - sqrt(ground_disc)));
		vec4 cover = optical_ray(POSITION, d, ray_end);
		COLOR = mix(COLOR, cover.rgb, cover.a);
	}
}
