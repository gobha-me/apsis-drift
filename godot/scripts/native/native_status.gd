extends RefCounted
## Presentation of C++ observations; never advances or edits the voyage.

static func finite_number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func layout_bounds(logical: Vector2, pixels: Vector2) -> Rect2:
	if not logical.is_finite() or not pixels.is_finite() or logical.x <= 0.0 or logical.y <= 0.0 or pixels.x <= 0.0 or pixels.y <= 0.0: return Rect2()
	var scale := maxf(1.0, maxf(logical.x / pixels.x, logical.y / pixels.y))
	var margin := minf(16.0 * scale, minf(logical.x, logical.y) * 0.1)
	return Rect2(Vector2.ONE * margin, Vector2(minf(380.0 * scale, logical.x - 2.0 * margin), minf(220.0 * scale, logical.y - 2.0 * margin)))

static func summary(value: Dictionary) -> String:
	for field in ["altitude", "surface_speed", "radial_rate", "air_density"]:
		if not finite_number(value.get(field)): return "Flight observations unavailable"
	if not value.get("attached") is bool: return "Flight observations unavailable"
	for field in ["surface", "surface_walk", "resources", "jump", "chart"]:
		if not value.get(field, {}) is Dictionary: return "Flight observations unavailable"
	var outside: Dictionary = value.get("surface_walk", {})
	var surface: Dictionary = value.get("surface", {})
	var resources: Dictionary = value.get("resources", {})
	var jump: Dictionary = value.get("jump", {})
	if not jump.get("phase", "idle") is String: return "Flight observations unavailable"
	var phase: String = jump.get("phase", "idle")
	if phase not in ["idle", "spool", "transit"]: return "Flight observations unavailable"
	var mode := "On foot · Suit equipped" if not outside.is_empty() else "Docked" if value.attached else "Landed" if surface.get("landed", false) else "Flight · Atmosphere" if value.air_density > 0.0 else "Flight · Space"
	if outside.is_empty() and surface.get("maneuver", "off") in ["landing", "liftoff"]: mode = "Landing" if surface.maneuver == "landing" else "Liftoff"
	if phase != "idle": mode = "Jump · " + ("Spooling" if phase == "spool" else "In transit")
	if phase != "idle" and jump.has("remaining_seconds"):
		if not finite_number(jump.remaining_seconds) or jump.remaining_seconds < 0.0: return "Jump observations unavailable"
		mode += " · %.1f s" % jump.remaining_seconds
	var location := "Current system"
	var rows: Variant = value.get("chart", {}).get("rows", [])
	if not rows is Array: return "Navigation observations unavailable"
	for row in rows:
		if row is Dictionary and row.get("current", false): location = str(row.get("name", location)); break
	var lines := PackedStringArray([mode + " · " + location])
	if not outside.is_empty():
		if not outside.get("eye_position") is Vector3 or not outside.eye_position.is_finite(): return "Ground observations unavailable"
		lines.append("Wayfarer · %.1f m to craft" % outside.eye_position.length())
		lines.append("WASD / left stick walk · Right-drag / right stick look")
	else:
		lines.append("Reference altitude %.2f km" % (value.altitude / 1000.0))
		lines.append("Surface speed %.1f m/s · Radial %+.1f m/s" % [value.surface_speed, value.radial_rate])
		if value.get("assistance", false): lines.append("Flight assistance on")
		if resources.get("selected", false):
			if not finite_number(resources.get("fraction")) or resources.fraction < 0.0 or resources.fraction > 1.0 or not resources.get("jump_charges") is int or resources.jump_charges < 0 or resources.jump_charges > 3: return "Resource observations unavailable"
			lines.append("Fuel %.1f%% · Jumps %d/3%s" % [resources.fraction * 100.0, resources.jump_charges, " · EMPTY" if resources.get("operationally_empty", false) else " · LOW FUEL" if resources.get("reserve", false) else ""])
		else: lines.append("Fuel tracking unavailable for this historical save")
		if resources.get("propulsion_refused", false): lines.append("Insufficient fuel · thrust unavailable")
		for row in rows:
			if row is Dictionary and row.get("system_id", "") == jump.get("selected", "") and not row.get("current", false):
				lines.append("Jump target · " + str(row.get("name", "Unidentified destination")))
				break
	return "\n".join(lines)
