shader_type sky;
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
void sky() {
	vec3 d = normalize(EYEDIR);
	vec3 eye = vec3(0.0, radius + altitude, 0.0);
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
}
