extends Control
## Screen-relative virtual stick. The owning view supplies capture/reset gates.
const Controls = preload("res://scripts/ui/player_input.gd")
const DEADZONE := 0.08
const RADIUS_FRACTION := 0.16
const MAX_DIMENSION := 65536.0
const MAX_DELTA := 1000000.0
var offset := Vector2.ZERO


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE


static func valid_dimensions(value: Variant) -> bool:
	if not (value is Vector2 or value is Vector2i): return false
	var dimensions := Vector2(value)
	return dimensions.is_finite() and dimensions.x >= 1.0 and dimensions.y >= 1.0 and dimensions.x <= MAX_DIMENSION and dimensions.y <= MAX_DIMENSION


static func valid_settings(settings: Dictionary) -> bool:
	for item in [["look_speed", 0.3, 4.0], ["curve", 1.0, 3.0]]:
		var value: Variant = settings.get(item[0])
		if not (value is float or value is int) or not is_finite(float(value)) or value < item[1] or value > item[2]: return false
	return settings.get("invert_look") is bool


func valid_offset() -> bool:
	# Normalizing binary32 Vector2 can round a few ULP beyond unit length.
	return offset.is_finite() and offset.length_squared() <= 1.000002


func motion(relative: Variant, dimensions: Variant, settings: Dictionary) -> bool:
	if not relative is Vector2 or not relative.is_finite() or absf(relative.x) > MAX_DELTA or absf(relative.y) > MAX_DELTA or not valid_dimensions(dimensions) or not valid_settings(settings) or not valid_offset():
		clear()
		return false
	var viewport_size := Vector2(dimensions)
	var radius: float = minf(viewport_size.x, viewport_size.y) * RADIUS_FRACTION
	# Sensitivity changes mouse travel once; demands apply only response shaping.
	var next: Vector2 = offset + relative / radius * (float(settings.look_speed) / 1.6)
	if not next.is_finite():
		clear()
		return false
	offset = next.normalized() if next.length_squared() > 1.0 else next
	queue_redraw()
	return true


func demands(settings: Dictionary) -> Vector2:
	if not valid_settings(settings) or not valid_offset():
		clear()
		return Vector2.ZERO
	var magnitude: float = minf(offset.length(), 1.0)
	if magnitude <= DEADZONE: return Vector2.ZERO
	var amount: float = Controls.shape(magnitude, DEADZONE, float(settings.curve))
	var direction := offset.normalized()
	# Screen Y points down; upward travel commands positive craft pitch.
	return Vector2(direction.y if settings.invert_look else -direction.y, direction.x) * amount


func clear() -> void:
	offset = Vector2.ZERO
	queue_redraw()


func _draw() -> void:
	if not valid_dimensions(size) or not valid_offset(): return
	var center := size * 0.5
	var radius: float = minf(size.x, size.y) * RADIUS_FRACTION
	var point := center + offset * radius
	draw_arc(center, radius, 0.0, TAU, 64, Color(0.78, 0.9, 1.0, 0.14), 1.0, true)
	draw_arc(center, 6.0, 0.0, TAU, 24, Color(0.78, 0.9, 1.0, 0.75), 1.0, true)
	draw_line(center, point, Color(0.78, 0.9, 1.0, 0.6), 1.0, true)
	draw_arc(point, 8.0, 0.0, TAU, 24, Color(0.95, 0.98, 1.0, 0.9), 1.5, true)
