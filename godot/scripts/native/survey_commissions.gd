extends Control
## A read-only, paused office reader. The owner revalidates every command.
signal action_requested(action: String)
signal back_requested

var packet: Dictionary = {}
var accept_button: Button
var submit_button: Button
var abandon_button: Button
var back: Button
var details: Label
var panel: PanelContainer
var scroll: ScrollContainer
var readable_theme: Theme

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for index in value.length():
		if value.unicode_at(index) < 48 or value.unicode_at(index) > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func decimal_before(left: String, right: String) -> bool:
	return left.length() < right.length() or (left.length() == right.length() and left < right)

static func clock_text(tick: String) -> String:
	return seconds_text(floori(float(tick) / 120.0))

static func seconds_text(seconds: int) -> String:
	var hours := floori(float(seconds) / 3600.0)
	var minutes := floori(float(seconds % 3600) / 60.0)
	return "%d:%02d:%02d" % [hours, minutes, seconds % 60]

static func remaining_text(deadline: String, now: String) -> String:
	if not decimal_before(now, deadline): return "expired"
	var seconds := maxf(0.0, (float(deadline) - float(now)) / 120.0)
	return "less than 1 s" if seconds < 1.0 else seconds_text(floori(seconds))

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 24 or not value.get("version") is int or value.version != 1: return false
	for key in ["supported", "selected", "at_origin", "can_accept", "can_submit", "can_abandon", "completed", "expired"]:
		if not value.get(key) is bool: return false
	for key in ["sequence", "offer_sequence"]:
		if not value.get(key) is int or value[key] < 0 or value[key] > 4294967295: return false
	if not value.get("acquired_ticks") is int or value.acquired_ticks < 0 or value.acquired_ticks > 3600: return false
	if not value.get("required_ticks") is int or value.required_ticks != 3600: return false
	if not value.get("reward_credits") is int or value.reward_credits != 100: return false
	for key in ["tick", "offer_until", "accepted_tick", "deadline_tick", "earnings", "system_id", "planet_id"]:
		if not decimal(value.get(key)): return false
	for key in ["body_name", "note", "last_payment"]:
		if not value.get(key) is String or value[key].length() > 512: return false
	if value.note.is_empty() or decimal_before("429496729500", value.earnings) or int(value.earnings) % 100 != 0: return false
	if not value.supported and (value.selected or value.can_accept or value.can_submit or value.can_abandon): return false
	if not value.selected and (value.sequence != 0 or value.acquired_ticks != 0 or value.completed or value.expired or value.earnings != "0"): return false
	if value.sequence == 0:
		if value.acquired_ticks != 0 or value.completed or value.expired or value.accepted_tick != "0" or value.deadline_tick != "0": return false
	else:
		if decimal_before(value.tick, value.accepted_tick) or not decimal_before(value.accepted_tick, value.deadline_tick): return false
		if value.completed != (value.acquired_ticks == 3600) or value.expired != (not decimal_before(value.tick, value.deadline_tick)): return false
	if value.can_accept:
		if not value.supported or not value.at_origin or value.offer_sequence == 0 or not decimal_before(value.tick, value.offer_until) or (value.sequence != 0 and not value.expired): return false
	if value.can_submit and (not value.supported or not value.selected or not value.at_origin or value.sequence == 0 or not value.completed or value.expired): return false
	if value.can_abandon and (not value.supported or not value.selected or not value.at_origin or value.sequence == 0): return false
	if value.sequence != 0 or value.can_accept:
		if value.system_id == "0" or value.planet_id == "0" or value.body_name.is_empty(): return false
	return true

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
	var title := Label.new()
	title.text = "Survey commissions"
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(title)
	details = Label.new()
	details.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(details)
	accept_button = Button.new()
	accept_button.text = "Accept commission · 100 credits"
	accept_button.pressed.connect(func(): action_requested.emit("accept"))
	column.add_child(accept_button)
	submit_button = Button.new()
	submit_button.text = "Submit completed report"
	submit_button.pressed.connect(func(): action_requested.emit("submit"))
	column.add_child(submit_button)
	abandon_button = Button.new()
	abandon_button.text = "Abandon commission"
	abandon_button.pressed.connect(func(): action_requested.emit("abandon"))
	column.add_child(abandon_button)
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
	for button in [accept_button, submit_button, abandon_button, back]: button.custom_minimum_size.y = ceili(44 * scale)

func open(value: Dictionary) -> void:
	show()
	scroll.scroll_vertical = 0
	refresh(value)
	layout_reader()
	if not accept_button.disabled: accept_button.grab_focus()
	elif not submit_button.disabled: submit_button.grab_focus()
	else: back.grab_focus()

func refresh(value: Dictionary) -> void:
	var focused := get_viewport().gui_get_focus_owner()
	packet = value.duplicate(true) if valid_packet(value) else {}
	accept_button.disabled = packet.is_empty() or not packet.can_accept
	submit_button.disabled = packet.is_empty() or not packet.can_submit
	abandon_button.disabled = packet.is_empty() or not packet.can_abandon
	if packet.is_empty():
		details.text = "Commission office unavailable: the current response was invalid. Return to Controls and try again."
	else:
		var lines := PackedStringArray(["Optional earning prototype · 100 credits per fresh report.\n30 seconds of qualifying orbital measurement · accepted terms reserved for four game hours.\nFuel service remains free; no rent, bills, debt or spending in this stage.", "Earned credits: " + packet.earnings])
		if not packet.body_name.is_empty(): lines.append("Target: " + packet.body_name)
		if packet.sequence != 0:
			lines.append("Commission #%d · %.1f / 30 s\nReport due at ship time %s · Time left: %s." % [packet.sequence, floorf(float(packet.acquired_ticks) / 12.0) / 10.0, clock_text(packet.deadline_tick), remaining_text(packet.deadline_tick, packet.tick)])
		elif packet.can_accept:
			lines.append("Offer changes at ship time %s · Time left: %s." % [clock_text(packet.offer_until), remaining_text(packet.offer_until, packet.tick)])
		lines.append(packet.note)
		if not packet.last_payment.is_empty(): lines.append("Latest report paid: 100 credits. Receipt retained.")
		details.text = "\n\n".join(lines)
	if focused in [accept_button, submit_button, abandon_button, back] and (not focused.visible or focused.disabled): back.grab_focus()

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in [accept_button, submit_button, abandon_button, back]:
		if button.visible and not button.disabled: buttons.append(button)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in [accept_button, submit_button, abandon_button, back] and target.visible and not target.disabled: target.pressed.emit()
