extends Control
## A paused reader. Topics are supplied and revalidated by the C++ owner.
signal topic_requested(id: int)
signal keep_requested
signal service_requested(service: String)
signal back_requested
var title: Label
var text: Label
var topics: Array[Button] = []
var back: Button
var keep: Button
var services: Array[Button] = []
var offer_tick := ""
var panel: PanelContainer
var readable_theme: Theme
var scroll: ScrollContainer
var response_scroll: ScrollContainer
var reply_up: Button
var reply_down: Button
var column: VBoxContainer
var header: HBoxContainer
var backing: StyleBoxFlat
var displayed_text := ""
var layout_pending := false

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel = PanelContainer.new()
	add_child(panel)
	backing = StyleBoxFlat.new()
	backing.bg_color = Color(0.01, 0.015, 0.025, 1.0)
	panel.add_theme_stylebox_override("panel", backing)
	readable_theme = Theme.new()
	panel.theme = readable_theme
	column = VBoxContainer.new()
	panel.add_child(column)
	header = HBoxContainer.new()
	column.add_child(header)
	title = Label.new()
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	header.add_child(title)
	reply_up = Button.new()
	reply_up.text = "↑"
	reply_up.accessibility_name = "Earlier reply lines"
	reply_up.tooltip_text = "Read earlier lines of this reply"
	reply_up.pressed.connect(func(): scroll_reply(-1))
	header.add_child(reply_up)
	reply_down = Button.new()
	reply_down.text = "↓"
	reply_down.accessibility_name = "Later reply lines"
	reply_down.tooltip_text = "Read later lines of this reply"
	reply_down.pressed.connect(func(): scroll_reply(1))
	header.add_child(reply_down)
	response_scroll = ScrollContainer.new()
	response_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	response_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(response_scroll)
	text = Label.new()
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	response_scroll.add_child(text)
	scroll = ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	var choices := VBoxContainer.new()
	choices.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(choices)
	for index in 5:
		var button := Button.new()
		button.pressed.connect(func(): topic_requested.emit(int(button.get_meta("topic_id", 0))))
		choices.add_child(button)
		topics.append(button)
	keep = Button.new()
	keep.text = "Keep this lead"
	keep.pressed.connect(func(): keep_requested.emit())
	choices.add_child(keep)
	clear_lead()
	for service in ["office", "repair"]:
		var button := Button.new()
		button.text = "Survey office · work and sales" if service == "office" else "Origin hull repair"
		button.set_meta("service", service)
		button.pressed.connect(func(): service_requested.emit(str(button.get_meta("service"))))
		choices.add_child(button)
		services.append(button)
	back = Button.new()
	back.text = "Back · remain paused"
	back.pressed.connect(func(): back_requested.emit())
	column.add_child(back)
	resized.connect(layout_reader)
	get_window().size_changed.connect(layout_reader)
	hide()

func layout_reader() -> void:
	if readable_theme == null or not is_inside_tree(): return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or not size.is_finite() or minf(pixels.x, pixels.y) <= 0 or minf(size.x, size.y) <= 0: return
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	readable_theme.default_font_size = ceili(20 * scale)
	for side in [SIDE_LEFT, SIDE_RIGHT, SIDE_TOP, SIDE_BOTTOM]: backing.set_content_margin(side, 12 * scale)
	column.add_theme_constant_override("separation", ceili(8 * scale))
	header.add_theme_constant_override("separation", ceili(8 * scale))
	(scroll.get_child(0) as VBoxContainer).add_theme_constant_override("separation", ceili(8 * scale))
	for button in topics + [keep] + services + [back]:
		button.custom_minimum_size.y = ceili(44 * scale)
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	for button in [reply_up, reply_down]: button.custom_minimum_size = Vector2.ONE * ceili(44 * scale)
	response_scroll.custom_minimum_size.y = ceili(64 * scale)
	scroll.custom_minimum_size.y = ceili(64 * scale)
	fit_panel(scale)
	if not layout_pending:
		layout_pending = true
		settle_panel_layout.call_deferred()

func settle_panel_layout() -> void:
	if not is_inside_tree():
		layout_pending = false
		return
	await get_tree().process_frame
	layout_pending = false
	if not is_inside_tree(): return
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or not size.is_finite() or minf(pixels.x, pixels.y) <= 0 or minf(size.x, size.y) <= 0: return
	# Font/width changes invalidate wrapped minimum sizes asynchronously. The
	# first size assignment may clamp to an obsolete, taller minimum. Reapply
	# once after container layout; never shrink text or discard any choices.
	fit_panel(maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y)))

func fit_panel(scale: float) -> void:
	# This is a reading card beside the scene, not a screen-sized modal shade.
	panel.size = Vector2(minf(560 * scale, size.x * 0.44), size.y * 0.8)
	panel.position = Vector2(size.x - panel.size.x - 16 * scale, (size.y - panel.size.y) * 0.5)

func _process(_delta: float) -> void:
	# The host assigns authoritative reply text through the existing Label API.
	# New text starts at its beginning; choice focus never moves this scrollbar.
	if visible and text.text != displayed_text:
		displayed_text = text.text
		response_scroll.scroll_vertical = 0

func scroll_reply(direction: int) -> void:
	if direction not in [-1, 1]: return
	var bar := response_scroll.get_v_scroll_bar()
	response_scroll.scroll_vertical = clampi(response_scroll.scroll_vertical + direction * maxi(1, floori(bar.page * 0.8)), 0, maxi(0, ceili(bar.max_value - bar.page)))

func open(packet: Dictionary) -> void:
	clear_lead()
	# The owning view supplies fresh, validated service availability on entry.
	for button in services:
		button.disabled = true
		button.tooltip_text = "Station service is unavailable in this journey."
	title.text = packet.name
	text.text = "Optional conversation. Choose a topic, or return to paused controls."
	scroll.scroll_vertical = 0
	response_scroll.scroll_vertical = 0
	displayed_text = text.text
	for index in topics.size():
		topics[index].visible = index < packet.topics.size()
		topics[index].disabled = not topics[index].visible
		topics[index].text = ""
		topics[index].set_meta("topic_id", 0)
		if topics[index].visible:
			topics[index].text = packet.topics[index].title
			topics[index].set_meta("topic_id", packet.topics[index].id)
	show()
	layout_reader()
	topics[0].grab_focus()

func set_service(service: String, available: bool, reason: String) -> void:
	for button in services:
		if button.get_meta("service") == service:
			button.disabled = not available
			button.tooltip_text = reason

func clear_lead() -> void:
	offer_tick = ""
	keep.hide()
	keep.disabled = true

func show_lead(packet: Dictionary, dialogue: String = "") -> void:
	var details: String = packet.note
	if packet.kept:
		var status: String = {"unverified": "Unverified report", "sensor_fix_acquired": "Sensor fix acquired", "visited": "Visited"}[packet.status]
		details = "%s\nSource: %s\nReceived · ship time %.1f s\n\n%s" % [status, packet.source, float(packet.received_tick) / 120.0, details]
	elif packet.can_keep:
		details += "\n\nSource: " + packet.source
	text.text = dialogue + "\n\n" + details if not dialogue.is_empty() and dialogue != packet.note else details
	offer_tick = packet.offer_tick
	keep.visible = packet.can_keep
	keep.disabled = not packet.can_keep
	if keep.has_focus() and keep.disabled: back.grab_focus()

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in topics:
		if button.visible and not button.disabled: buttons.append(button)
	if keep.visible and not keep.disabled: buttons.append(keep)
	for button in services:
		if button.visible and not button.disabled: buttons.append(button)
	buttons.append_array([reply_up, reply_down, back])
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in topics + [keep] + services + [reply_up, reply_down, back] and target.visible and not target.disabled: target.pressed.emit()
