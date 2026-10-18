extends RefCounted
## Pointer ownership for the two ordinary native journey views.
var active := false


func set_active(value: bool) -> void:
	if active == value: return
	active = value
	# Headless checks exercise the ownership gates without claiming an OS grab.
	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if active else Input.MOUSE_MODE_VISIBLE


static func motion_delta(relative: Variant, settings: Dictionary) -> Vector2:
	if not relative is Vector2 or not relative.is_finite() or absf(relative.x) > 1000000.0 or absf(relative.y) > 1000000.0:
		return Vector2.ZERO
	var speed: Variant = settings.get("look_speed")
	if not (speed is float or speed is int) or not is_finite(float(speed)) or speed < 0.3 or speed > 4.0 or not settings.get("invert_look") is bool:
		return Vector2.ZERO
	return relative * (0.003 * float(speed) / 1.6) * Vector2(1.0, -1.0 if settings.invert_look else 1.0)
