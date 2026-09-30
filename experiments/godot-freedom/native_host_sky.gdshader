shader_type sky;
// Angular host silhouette from the authoritative station/host separation.
uniform vec3 host_direction = vec3(0.0, 0.0, -1.0);
uniform float radius_ratio : hint_range(0.0, 1.0) = 0.5;

void sky() {
	vec3 ray = normalize(EYEDIR);
	float along = dot(ray, host_direction);
	float discriminant = radius_ratio * radius_ratio - (1.0 - along * along);
	COLOR = vec3(0.008, 0.014, 0.025);
	if (along > 0.0 && discriminant >= 0.0) {
		vec3 normal = normalize(ray * (along - sqrt(discriminant)) - host_direction);
		float light = 0.15 + 0.85 * max(dot(normal, normalize(vec3(1.0, 0.6, 0.4))), 0.0);
		COLOR = vec3(0.16, 0.34, 0.58) * light;
	}
}
