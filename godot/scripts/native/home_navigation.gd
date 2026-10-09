extends Control
## Presentation of the existing C++ home projection. Never commands flight.
const INSET := 28.0
const COLOR := Color(0.4, 0.9, 1.0)
var cue: Dictionary = {}
var render_scale := 1.0

static func project(camera: Camera3D, target: Vector3, bounds: Vector2, inset: float = INSET) -> Dictionary:
	if camera == null or not camera.is_inside_tree() or not target.is_finite() or not bounds.is_finite() or not is_finite(inset) or inset <= 0 or bounds.x <= inset * 2 or bounds.y <= inset * 2:
		return {}
	var transform := camera.global_transform
	if not transform.is_finite() or absf(transform.basis.determinant()) < 0.000001:
		return {}
	var local := transform.affine_inverse() * target
	if not local.is_finite() or not is_finite(local.length_squared()) or local.length_squared() < 0.00000001:
		return {}
	var viewport_size := camera.get_viewport().get_visible_rect().size
	if not viewport_size.is_finite() or viewport_size.x <= 0 or viewport_size.y <= 0:
		return {}
	var centre := bounds * 0.5
	var behind := local.z > 0.0
	var pixel := centre
	var direction := Vector2(local.x, -local.y)
	if not behind and local.z < -camera.near:
		pixel = camera.unproject_position(target) / viewport_size * bounds
		if not pixel.is_finite(): return {}
		direction = pixel - centre
	var safe := Rect2(Vector2.ONE * inset, bounds - Vector2.ONE * inset * 2)
	var in_view := not behind and local.z < -camera.near and safe.has_point(pixel)
	if not in_view:
		if direction.length_squared() < 0.00000001: direction = Vector2.RIGHT
		direction = direction.normalized()
		var half := bounds * 0.5 - Vector2.ONE * inset
		var factor := minf(half.x / absf(direction.x) if absf(direction.x) > 0.000001 else INF, half.y / absf(direction.y) if absf(direction.y) > 0.000001 else INF)
		pixel = centre + direction * factor
	return {"position": pixel, "direction": direction.normalized(), "in_view": in_view, "behind": behind}

static func globe_occludes(camera_position: Vector3, target: Vector3, centre: Vector3, radius: float) -> bool:
	if not camera_position.is_finite() or not target.is_finite() or not centre.is_finite() or not is_finite(radius) or radius <= 0: return false
	var ray := target - camera_position
	var distance := ray.length_squared()
	if not is_finite(distance) or distance < 0.00000001: return false
	var fraction := clampf((centre - camera_position).dot(ray) / distance, 0.0, 1.0)
	return fraction > 0.0 and fraction < 1.0 and (camera_position + ray * fraction - centre).length_squared() < radius * radius

func update_cue(camera: Camera3D, target: Vector3, attached: bool) -> void:
	render_scale = size.x / maxf(1.0, float(get_window().size.x))
	cue = {} if attached else project(camera, target, size, INSET * render_scale)
	queue_redraw()

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)

func _draw() -> void:
	if cue.is_empty(): return
	var p: Vector2 = cue.position
	if cue.in_view:
		draw_polyline(PackedVector2Array([p + Vector2(0, -7) * render_scale, p + Vector2(7, 0) * render_scale, p + Vector2(0, 7) * render_scale, p + Vector2(-7, 0) * render_scale, p + Vector2(0, -7) * render_scale]), COLOR, 2.0, true)
	else:
		var d: Vector2 = cue.direction
		var side := Vector2(-d.y, d.x)
		draw_polyline(PackedVector2Array([p - d * 12 * render_scale + side * 6 * render_scale, p, p - d * 12 * render_scale - side * 6 * render_scale]), COLOR, 2.0, true)
	var label := p + Vector2(-58 if p.x > size.x - 80 * render_scale else 12, 6) * render_scale
	draw_rect(Rect2(label + Vector2(-3, -18) * render_scale, Vector2(53, 24) * render_scale), Color(0.0, 0.0, 0.0, 0.85))
	draw_string(ThemeDB.fallback_font, label, "HOME", HORIZONTAL_ALIGNMENT_LEFT, -1, roundi(18 * render_scale), COLOR)
