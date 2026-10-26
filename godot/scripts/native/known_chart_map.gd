extends Control
## Cached presentation of C++ points from admitted system-location evidence.
const Finance = preload("res://scripts/native/paid_operations.gd")
const MINIMUM := Vector2(272, 176)
const FRONTIER_MINIMUM := Vector2(272, 244)
const MAXIMUM := 16384.0
const PADDING := 32.0
const MARKER_RADIUS := 22.0
var packet: Dictionary = {}
var ui_scale := 1.0

func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE
	custom_minimum_size = MINIMUM
	resized.connect(queue_redraw)

static func bounded_text(value: Variant, limit: int) -> bool:
	if not value is String: return false
	var bytes: PackedByteArray = value.to_utf8_buffer()
	return not bytes.is_empty() and bytes.size() <= limit and not bytes.has(0)

static func chart_key(value: Variant) -> bool:
	if not value is String or value.length() != 23 or not value.begins_with("system-") or value == "system-0000000000000000": return false
	for i in range(7, 23):
		var c: int = value.unicode_at(i)
		if not ((c >= 48 and c <= 57) or (c >= 97 and c <= 102)): return false
	return true

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 5: return false
	if not value.get("version") is int or value.version not in [1, 2] or not value.get("available") is bool: return false
	if not Finance.decimal(value.get("tick")) or not bounded_text(value.get("note"), 1024): return false
	if not value.get("nodes") is Array or value.nodes.size() > (8 if value.version == 2 else 6): return false
	if not value.available: return value.nodes.is_empty()
	if value.nodes.size() < 2: return false
	var keys := {}
	var current := 0
	var selected := 0
	for node: Variant in value.nodes:
		if not node is Dictionary or node.size() != 6: return false
		if not chart_key(node.get("system_id")) or keys.has(node.system_id) or not bounded_text(node.get("name"), 256): return false
		keys[node.system_id] = true
		for key in ["current", "selected"]:
			if not node.get(key) is bool: return false
		for key in ["plot_x", "plot_y"]:
			if not node.get(key) is float or not is_finite(node[key]) or node[key] < 0.0 or node[key] > 1.0: return false
		if node.current: current += 1
		if node.selected: selected += 1
	return current <= 1 and selected <= 1

static func rects_for_packet(value: Variant, dimensions: Vector2) -> Array[Rect2]:
	var result: Array[Rect2] = []
	if not valid_packet(value) or not value.available or not dimensions.is_finite(): return result
	var minimum: Vector2 = FRONTIER_MINIMUM if value.version == 2 else MINIMUM
	if dimensions.x < minimum.x or dimensions.y < minimum.y or dimensions.x > MAXIMUM or dimensions.y > MAXIMUM: return result
	var extent := dimensions - Vector2(PADDING * 2.0, PADDING * 2.0)
	for node: Dictionary in value.nodes:
		var point := Vector2(PADDING, PADDING) + Vector2(node.plot_x, node.plot_y) * extent
		result.append(Rect2(point - Vector2(MARKER_RADIUS, MARKER_RADIUS), Vector2(MARKER_RADIUS * 2.0, MARKER_RADIUS * 2.0)))
	return result

func configure(value: Variant) -> void:
	packet = value.duplicate(true) if valid_packet(value) else {}
	custom_minimum_size = (FRONTIER_MINIMUM if packet.get("version") == 2 else MINIMUM) * ui_scale
	queue_redraw()

func set_ui_scale(value: float) -> void:
	# Follow the surrounding HUD's logical-to-physical scale, with a bounded draw size.
	if not is_finite(value) or value < 1.0 or value > 16.0:
		packet = {}
		queue_redraw()
		return
	if is_equal_approx(ui_scale, value): return
	ui_scale = value
	custom_minimum_size = (FRONTIER_MINIMUM if packet.get("version") == 2 else MINIMUM) * ui_scale
	queue_redraw()

func marker_rects() -> Array[Rect2]:
	var result: Array[Rect2] = []
	for box: Rect2 in rects_for_packet(packet, size / ui_scale):
		result.append(Rect2(box.position * ui_scale, box.size * ui_scale))
	return result

func _draw() -> void:
	var boxes := marker_rects()
	if boxes.is_empty(): return
	draw_rect(Rect2(Vector2.ZERO, size), Color("111b23"))
	draw_rect(Rect2(Vector2.ONE * ui_scale, size - Vector2(2.0, 2.0) * ui_scale), Color("597080"), false, ui_scale)
	var font := get_theme_default_font()
	var font_size := roundi(20.0 * ui_scale)
	for i in boxes.size():
		var point: Vector2 = boxes[i].get_center()
		var node: Dictionary = packet.nodes[i]
		draw_circle(point, 14.0 * ui_scale, Color("233642"))
		draw_arc(point, 14.0 * ui_scale, 0.0, TAU, 40, Color("b4c4cf"), ui_scale, true)
		if node.current: draw_arc(point, 18.0 * ui_scale, 0.0, TAU, 48, Color("6ee8e0"), 2.0 * ui_scale, true)
		if node.selected: draw_rect(boxes[i].grow(-ui_scale), Color("ffd780"), false, 2.0 * ui_scale)
		var label := str(i + 1)
		var width: float = font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		var baseline: float = (font.get_ascent(font_size) - font.get_descent(font_size)) * 0.5
		draw_string(font, point + Vector2(-width * 0.5, baseline), label, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, Color("f2f7fa"))
