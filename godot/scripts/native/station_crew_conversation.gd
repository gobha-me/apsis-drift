extends Control
## A paused reader. Topics are supplied and revalidated by the C++ owner.
signal topic_requested(id: int)
signal back_requested
var title: Label
var text: Label
var topics: Array[Button] = []
var back: Button
var panel: PanelContainer
var readable_theme: Theme
var scroll: ScrollContainer

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var shade := ColorRect.new()
	shade.color = Color(0.01, 0.015, 0.025, 0.97)
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(shade)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]: margin.add_theme_constant_override("margin_" + side, 32)
	add_child(margin)
	panel = PanelContainer.new()
	margin.add_child(panel)
	readable_theme = Theme.new()
	panel.theme = readable_theme
	scroll = ScrollContainer.new()
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 12)
	scroll.add_child(column)
	title = Label.new()
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(title)
	text = Label.new()
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(text)
	for index in 4:
		var button := Button.new()
		button.pressed.connect(func(): topic_requested.emit(int(button.get_meta("topic_id", 0))))
		column.add_child(button)
		topics.append(button)
	back = Button.new()
	back.text = "Back · remain paused"
	back.pressed.connect(func(): back_requested.emit())
	column.add_child(back)
	resized.connect(layout_reader)
	get_window().size_changed.connect(layout_reader)
	hide()

func layout_reader() -> void:
	if readable_theme == null or not is_inside_tree() or get_window().size.x <= 0 or get_window().size.y <= 0: return
	var pixels := Vector2(get_window().size)
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	readable_theme.default_font_size = ceili(20 * scale)
	for button in topics + [back]: button.custom_minimum_size.y = ceili(44 * scale)

func open(packet: Dictionary) -> void:
	title.text = packet.name
	text.text = "Optional conversation. Choose a topic, or return to paused controls."
	scroll.scroll_vertical = 0
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

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in topics:
		if button.visible and not button.disabled: buttons.append(button)
	buttons.append(back)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in topics + [back] and target.visible and not target.disabled: target.pressed.emit()
