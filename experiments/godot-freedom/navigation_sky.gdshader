shader_type sky;
// C++ catalog texture and inertial orientation; no shader-generated star RNG.
uniform sampler2D stars : filter_linear, repeat_enable;
uniform mat3 view_to_inertial;
uniform float radius = 5499000.0;
uniform float altitude = 2000.0;
uniform float atmosphere_density = 1.0;
uniform vec3 atmosphere_tint : source_color = vec3(0.24, 0.39, 0.59);
void sky() {
	vec3 d = normalize(EYEDIR);
	vec3 inertial = normalize(view_to_inertial * d);
	vec2 uv = vec2(atan(inertial.z, inertial.x) / TAU + 0.5, acos(clamp(inertial.y,-1.0,1.0)) / PI);
	vec3 starlight = texture(stars, uv).rgb * 2.0;
	starlight *= exp(-atmosphere_density * exp(-max(0.0,altitude)/8500.0)*12.0);
	// Analytic spherical atmosphere, eight optical samples. Presentation only.
	vec3 eye = vec3(0.0, radius + altitude, 0.0);
	float b = dot(eye,d);
	float outer = radius + 130000.0;
	float disc = b*b - dot(eye,eye) + outer*outer;
	float ground_disc = b*b-dot(eye,eye)+radius*radius;
	bool ground = ground_disc > 0.0 && b < 0.0;
	float optical = 0.0;
	if (disc > 0.0 && atmosphere_density > 0.0) {
		float start = max(0.0, -b-sqrt(disc));
		float end = max(0.0, -b+sqrt(disc));
		if(ground) end = min(end, max(0.0,-b-sqrt(ground_disc)));
		float step_length = (end-start)/8.0;
		for(int i=0;i<8;i++) {
			float h = length(eye+d*(start+(float(i)+0.5)*step_length))-radius;
			optical += exp(-max(0.0,h)/8500.0)*step_length/16000.0;
		}
	}
	float haze = 1.0-exp(-optical*atmosphere_density);
	vec3 base = ground ? vec3(0.035,0.045,0.052) : starlight;
	COLOR = mix(base, atmosphere_tint*(0.45+0.55*max(0.0,d.y)), haze);
}
