extends Control
## Quotes are read-only. The host validates the displayed packet before acting.
signal action_requested(action: String)
signal back_requested

var packet: Dictionary = {}
var enable_button: Button
var purchase_button: Button
var retainer_button: Button
var back: Button
var details: Label
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
	var seconds := floori(float(tick) / 120.0)
	return "%d:%02d:%02d" % [floori(float(seconds) / 3600.0), floori(float(seconds % 3600) / 60.0), seconds % 60]

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 28 or not value.get("version") is int or value.version != 1: return false
	for key in ["supported", "selected", "at_origin", "can_enable", "can_purchase", "can_retainer"]:
		if not value.get(key) is bool: return false
	if not value.get("missing_jump_charges") is int or value.missing_jump_charges < 0 or value.missing_jump_charges > 3: return false
	for key in ["tick", "started_tick", "berth_until_tick", "cash", "opening_cash", "operating_tab", "recovery_due", "missing_flight_quanta", "capacity_quanta", "flight_credits", "jump_credits", "service_credits", "retainer_cash", "retainer_tab", "retainer_subsidy", "retainer_berth", "retainer_total"]:
		if not decimal(value.get(key)): return false
	for key in ["token", "terms", "note"]:
		if not value.get(key) is String: return false
		var bytes: PackedByteArray = value[key].to_utf8_buffer()
		if bytes.has(0) or bytes.size() > (4096 if key == "terms" else 1024): return false
	if value.terms.is_empty() or value.note.is_empty(): return false
	if not value.supported and (value.selected or value.can_enable or value.can_purchase or value.can_retainer): return false
	if value.supported and (value.token.is_empty() or value.capacity_quanta == "0"): return false
	if decimal_before(value.capacity_quanta, value.missing_flight_quanta): return false
	if decimal_before("200", value.operating_tab) or decimal_before("100", value.recovery_due): return false
	if decimal_before("40", value.flight_credits) or decimal_before("45", value.jump_credits) or decimal_before("85", value.service_credits): return false
	if value.can_enable and (not value.supported or value.selected or not value.at_origin): return false
	if (value.can_purchase or value.can_retainer) and (not value.supported or not value.selected or not value.at_origin): return false
	if value.selected and decimal_before(value.tick, value.started_tick): return false
	return true

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var shade := ColorRect.new()
	shade.color = Color(0.01, 0.015, 0.025, 1.0)
	shade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(shade)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]: margin.add_theme_constant_override("margin_" + side, 32)
	add_child(margin)
	var panel := PanelContainer.new()
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
	title.text = "Paid operations · optional prototype"
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(title)
	details = Label.new()
	details.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(details)
	enable_button = Button.new()
	enable_button.text = "Enable paid operations · one-way in this journey"
	enable_button.pressed.connect(func(): action_requested.emit("enable"))
	column.add_child(enable_button)
	purchase_button = Button.new()
	purchase_button.pressed.connect(func(): action_requested.emit("purchase"))
	column.add_child(purchase_button)
	retainer_button = Button.new()
	retainer_button.text = "Accept quoted noncash retainer"
	retainer_button.pressed.connect(func(): action_requested.emit("retainer"))
	column.add_child(retainer_button)
	back = Button.new()
	back.text = "Back to commission office · remain paused"
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
	for button in [enable_button, purchase_button, retainer_button, back]:
		button.custom_minimum_size.y = ceili(44 * scale)
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

func open(value: Dictionary) -> void:
	show()
	scroll.scroll_vertical = 0
	refresh(value)
	layout_reader()
	back.grab_focus()

func refresh(value: Dictionary) -> void:
	packet = value.duplicate(true) if valid_packet(value) else {}
	enable_button.disabled = packet.is_empty() or not packet.can_enable
	purchase_button.disabled = packet.is_empty() or not packet.can_purchase
	retainer_button.disabled = packet.is_empty() or not packet.can_retainer
	purchase_button.text = "Purchase quoted full fuel restoration" if packet.is_empty() else "Restore fuel stores · %s credits" % packet.service_credits
	if packet.is_empty():
		details.text = "Paid operations unavailable: invalid current response. Return to the office and review again."
		return
	var lines := PackedStringArray([packet.terms])
	if packet.selected:
		lines.append("Available cash: %s credits\nOperating tab: %s / 200 · Insurance excess: %s / 100\nBerth prepaid through ship time %s." % [packet.cash, packet.operating_tab, packet.recovery_due, clock_text(packet.berth_until_tick)])
	else:
		lines.append("Current fuel service remains free. Enabling is optional and stays selected in this saved journey.\nAvailable cash after enrollment: %s credits." % packet.opening_cash)
	if packet.supported:
		var missing_percent := 100.0 * float(packet.missing_flight_quanta) / float(packet.capacity_quanta)
		lines.append("Full restoration quote: %.1f%% of flight tank missing · %d jump charges missing.\nFlight fuel: %s credits · Jump fuel: %s credits · Total: %s credits." % [missing_percent, packet.missing_jump_charges, packet.flight_credits, packet.jump_credits, packet.service_credits])
	if packet.can_retainer:
		lines.append("Noncash retainer quote: %s credits total, including %s berth credits.\nCash debit: %s · Added operating tab: %s · Hardship subsidy: %s.\nRestores actual stores and berth coverage; gives no cash. Once per fresh accepted commission." % [packet.retainer_total, packet.retainer_berth, packet.retainer_cash, packet.retainer_tab, packet.retainer_subsidy])
	lines.append(packet.note)
	details.text = "\n\n".join(lines)

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in [enable_button, purchase_button, retainer_button, back]:
		if button.visible and not button.disabled: buttons.append(button)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in [enable_button, purchase_button, retainer_button, back] and target.visible and not target.disabled: target.pressed.emit()
