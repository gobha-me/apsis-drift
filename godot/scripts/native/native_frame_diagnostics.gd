extends PanelContainer
## Optional presentation measurements. This panel never controls simulation.
const MAX_SAMPLES := 240
const UPDATE_SECONDS := 0.5
const MAX_FRAME_SECONDS := 60.0
const MAX_COUNTER := 1.0e12
var enabled := false
var samples: Array[float] = []
var label: Label
var epoch_ready := false
var update_elapsed := 0.0
var clock_usec: Callable = Time.get_ticks_usec
var last_observation_usec := -1
var laying_out := false

func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	focus_mode = Control.FOCUS_NONE
	clip_contents = true
	z_index = 100
	var backing := StyleBoxFlat.new()
	backing.bg_color = Color(0.015, 0.02, 0.03, 0.95)
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]:
		backing.set_content_margin(side, 8.0)
	add_theme_stylebox_override("panel", backing)
	label = Label.new()
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.focus_mode = Control.FOCUS_NONE
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_color_override("font_color", Color(0.93, 0.95, 1.0))
	add_child(label)
	hide()

func _ready() -> void:
	var parent := get_parent() as Control
	if parent != null: parent.resized.connect(_layout)
	label.minimum_size_changed.connect(_layout)
	_layout()

func set_enabled(value: bool) -> void:
	enabled = value
	_discard_epoch()

func _discard_epoch() -> void:
	samples.clear()
	epoch_ready = false
	update_elapsed = 0.0
	last_observation_usec = -1
	label.text = ""
	hide()

static func valid_tick(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for index in value.length():
		if value.unicode_at(index) < 48 or value.unicode_at(index) > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func valid_number(value: Variant, maximum: float) -> bool:
	return (value is float or value is int) and is_finite(float(value)) and value >= 0.0 and value <= maximum

static func summary(intervals: Array) -> Dictionary:
	if intervals.is_empty() or intervals.size() > MAX_SAMPLES: return {}
	var ordered: Array[float] = []
	for interval: Variant in intervals:
		if not valid_number(interval, MAX_FRAME_SECONDS) or interval <= 0.0: return {}
		ordered.append(float(interval))
	ordered.sort()
	var count := ordered.size()
	var middle := count >> 1
	var median := ordered[middle]
	if count % 2 == 0: median = (ordered[middle - 1] + median) * 0.5
	# Nearest-rank p95; frame intervals, not GPU render durations.
	return {"count": count, "median_ms": median * 1000.0, "p95_ms": ordered[ceili(count * 0.95) - 1] * 1000.0}

static func monitor_text(value: float, milliseconds: bool = false) -> String:
	if not valid_number(value, MAX_FRAME_SECONDS if milliseconds else MAX_COUNTER): return "unavailable"
	return "%.2f ms" % (value * 1000.0) if milliseconds else "%.0f" % value

func observe(delta: float, state: Dictionary, render_sizes: Array, active: bool) -> void:
	# Disabled/inactive observations do not read engine monitors or retain data.
	if not enabled or not active:
		_discard_epoch()
		return
	if not is_finite(delta) or delta <= 0.0 or delta > MAX_FRAME_SECONDS or not valid_tick(state.get("tick")) or not valid_number(state.get("dropped_seconds"), MAX_COUNTER) or render_sizes.is_empty() or render_sizes.size() > 8:
		_discard_epoch()
		return
	for pixels: Variant in render_sizes:
		if not pixels is Vector2i or pixels.x <= 0 or pixels.y <= 0 or pixels.x > 65536 or pixels.y > 65536:
			_discard_epoch()
			return
	# _process delta is engine-scaled and may diverge from wall time during
	# stalls. The diagnostic observes monotonic wall intervals, never ship time.
	var now: Variant = clock_usec.call()
	if not now is int or now < 0:
		_discard_epoch()
		return
	if not epoch_ready:
		epoch_ready = true
		last_observation_usec = now
		label.text = "Frame diagnostics · collecting active frames\nEngine process monitor · GPU timing not sampled"
		visible = _layout()
		return
	if now <= last_observation_usec or now - last_observation_usec > int(MAX_FRAME_SECONDS * 1000000.0):
		_discard_epoch()
		return
	var interval := float(now - last_observation_usec) / 1000000.0
	last_observation_usec = now
	samples.append(interval)
	if samples.size() > MAX_SAMPLES: samples.pop_front()
	update_elapsed = minf(UPDATE_SECONDS, update_elapsed + interval)
	if update_elapsed < UPDATE_SECONDS: return
	update_elapsed = 0.0
	var frames := summary(samples)
	var targets := PackedStringArray()
	for pixels: Vector2i in render_sizes: targets.append("%d×%d" % [pixels.x, pixels.y])
	var cpu := Performance.get_monitor(Performance.TIME_PROCESS)
	var calls := Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)
	var primitives := Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME)
	label.text = "Frame diagnostics · last %d active frames\nFrame median %.2f ms · p95 %.2f ms\nEngine process %s · GPU timing not sampled\nDraw calls %s · primitives %s\n3D targets %s\nTick %s · dropped %.3f s" % [frames.count, frames.median_ms, frames.p95_ms, monitor_text(cpu, true), monitor_text(calls), monitor_text(primitives), " / ".join(targets), state.tick, float(state.dropped_seconds)]
	visible = _layout()

func _layout() -> bool:
	if laying_out: return visible
	var parent := get_parent() as Control
	if parent == null or not parent.size.is_finite() or parent.size.x < 320.0 or parent.size.y < 200.0 or parent.size.x > 65536.0 or parent.size.y > 65536.0:
		hide()
		return false
	laying_out = true
	var scale := 1.0
	if is_inside_tree():
		var density := get_viewport().get_final_transform().get_scale().abs()
		if density.is_finite() and density.x > 0.0 and density.y > 0.0:
			scale = clampf(1.0 / minf(density.x, density.y), 1.0, 4.0)
	var font_size := roundi(18 * scale)
	if label.get_theme_font_size("font_size") != font_size:
		label.add_theme_font_size_override("font_size", font_size)
	var margin := minf(8 * scale, minf(parent.size.x, parent.size.y) * 0.1)
	var width := minf(520 * scale, parent.size.x - 2 * margin)
	var padding := 8.0 * scale
	var backing: StyleBox = get_theme_stylebox("panel")
	for side in [SIDE_LEFT, SIDE_TOP, SIDE_RIGHT, SIDE_BOTTOM]: backing.set_content_margin(side, padding)
	# Hidden containers have not laid out their children yet. Establish the
	# wrapping width before asking the label for its required height.
	label.custom_minimum_size.x = width - 2.0 * padding
	label.size.x = label.custom_minimum_size.x
	var height := maxf(140 * scale, label.get_minimum_size().y + 2.0 * padding)
	set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	offset_left = -width - margin
	offset_right = -margin
	if height > parent.size.y - 2 * margin:
		laying_out = false
		hide()
		return false
	offset_top = -height - margin
	offset_bottom = -margin
	laying_out = false
	return true
