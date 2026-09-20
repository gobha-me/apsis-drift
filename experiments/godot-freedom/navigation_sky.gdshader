shader_type sky;
// C++ catalog texture and inertial orientation; no shader-generated star RNG.
uniform sampler2D stars : filter_linear, repeat_enable;
uniform mat3 view_to_inertial;
uniform float radius = 5499000.0;
uniform float altitude = 2000.0;
uniform float atmosphere_density = 1.0;
uniform vec3 atmosphere_tint : source_color = vec3(0.24, 0.39, 0.59);
// Opt-in authoritative star geometry; never TIME or a shader-owned orbit.
uniform bool physical_lighting = false;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_color : source_color = vec3(1.0);
uniform float sun_angular_radius = 0.00465;
void sky() {
	vec3 d = normalize(EYEDIR);
	vec3 inertial = normalize(view_to_inertial * d);
	vec2 uv = vec2(atan(inertial.z, inertial.x) / TAU + 0.5, acos(clamp(inertial.y,-1.0,1.0)) / PI);
	vec3 starlight = texture(stars, uv).rgb * 2.0;
	float daylight = physical_lighting ? smoothstep(-0.12, 0.16, sun_direction.y) : 1.0;
	starlight *= exp(-atmosphere_density * exp(-max(0.0,altitude)/8500.0)*(physical_lighting ? 0.15 + 11.85 * daylight : 12.0));
	// Analytic spherical atmosphere, eight optical samples. Presentation only.
	vec3 eye = vec3(0.0, radius + altitude, 0.0);
	float b = dot(eye,d);
	float outer = radius + 130000.0;
	float disc = b*b - dot(eye,eye) + outer*outer;
	float ground_disc = b*b-dot(eye,eye)+radius*radius;
	bool ground = ground_disc > 0.0 && b < 0.0;
	float optical = 0.0;
	float lit_optical = 0.0;
	float warm_optical = 0.0;
	if (disc > 0.0 && atmosphere_density > 0.0) {
		float start = max(0.0, -b-sqrt(disc));
		float end = max(0.0, -b+sqrt(disc));
		if(ground) end = min(end, max(0.0,-b-sqrt(ground_disc)));
		float step_length = (end-start)/8.0;
		for(int i=0;i<8;i++) {
			vec3 point = eye+d*(start+(float(i)+0.5)*step_length);
			float h = length(point)-radius;
			float weight = exp(-max(0.0,h)/8500.0)*step_length/16000.0;
			optical += weight;
			if (physical_lighting) {
				// Coarse spherical single-scattering appearance, not a climate model.
				float solar = dot(normalize(point), sun_direction);
				float horizon = -sqrt(max(0.0, 1.0 - pow(radius / max(radius, length(point)), 2.0)));
				float lit = smoothstep(horizon - 0.10, horizon + 0.12, solar);
				lit_optical += weight * lit;
				warm_optical += weight * lit * (1.0 - smoothstep(-0.03, 0.20, solar));
			}
		}
	}
	float haze = 1.0-exp(-optical*atmosphere_density);
	vec3 base = ground ? vec3(0.035,0.045,0.052) : starlight;
	COLOR = mix(base, atmosphere_tint*(0.45+0.55*max(0.0,d.y)), haze);
	if (physical_lighting) {
		float illumination = lit_optical / max(0.000001, optical);
		float warm = warm_optical / max(0.000001, optical);
		float toward_sun = max(0.0, dot(d, sun_direction));
		vec3 scatter = atmosphere_tint * (0.35 + 0.65 * max(0.0, d.y)) * illumination;
		scatter += vec3(0.55, 0.19, 0.065) * warm * pow(toward_sun, 4.0);
		vec3 night = vec3(0.001, 0.002, 0.004);
		COLOR = mix(ground ? night : starlight, night + scatter * sun_color, haze);
		float horizon = -sqrt(max(0.0, 1.0 - pow(radius / (radius + max(0.0, altitude)), 2.0)));
		float limb_width = max(0.00001, sin(sun_angular_radius));
		float visibility = smoothstep(horizon - limb_width, horizon + limb_width, sun_direction.y);
		float sun_angle = acos(clamp(dot(d, sun_direction), -1.0, 1.0));
		float antialias = max(0.00001, fwidth(sun_angle));
		float disc = 1.0 - smoothstep(max(0.0, sun_angular_radius - antialias), sun_angular_radius + antialias, sun_angle);
		if (!ground) COLOR += sun_color * disc * visibility * 6.0 * exp(-optical * atmosphere_density * 0.15);
	}
}
