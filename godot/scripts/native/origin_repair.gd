extends Control
## A paused service quote; the C++ owner revalidates every repair.
signal action_requested(action: String)
signal back_requested

const Finance = preload("res://scripts/native/paid_operations.gd")
var packet: Dictionary = {}
var repair_button: Button
var back: Button
var details: Label
var scroll: ScrollContainer
var readable_theme: Theme

static func subtract_cost(value: String, cost: int) -> String:
	var digits := value
	var borrow := cost
	for index in range(digits.length() - 1, -1, -1):
		var digit := digits.unicode_at(index) - 48 - borrow % 10
		borrow = floori(float(borrow) / 10.0)
		if digit < 0:
			digit += 10
			borrow += 1
		digits = digits.substr(0, index) + str(digit) + digits.substr(index + 1)
	if borrow != 0: return ""
	while digits.length() > 1 and digits.begins_with("0"): digits = digits.substr(1)
	return digits

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 17 or not value.get("version") is int or value.version != 1: return false
	for key in ["supported", "at_origin", "paid_selected", "can_repair"]:
		if not value.get(key) is bool: return false
	for key in ["forebody", "landing_structure", "severity_units"]:
		if not value.get(key) is int or value[key] < 0 or value[key] > (6 if key == "severity_units" else 3): return false
	if value.severity_units != value.forebody + value.landing_structure: return false
	for key in ["tick", "unit_credits", "repair_credits", "cash_before", "cash_after", "berth_credits"]:
		if not Finance.decimal(value.get(key)): return false
	for key in ["token", "note", "terms"]:
		if not value.get(key) is String: return false
		var bytes: PackedByteArray = value[key].to_utf8_buffer()
		if bytes.has(0) or bytes.size() > (1024 if key == "note" else 4096): return false
	if value.unit_credits != "10" or value.note.is_empty() or value.terms.is_empty(): return false
	var cost: int = value.severity_units * 10 if value.paid_selected else 0
	if value.repair_credits != str(cost): return false
	if not value.supported:
		return not value.at_origin and not value.paid_selected and not value.can_repair and value.severity_units == 0 and value.token.is_empty() and value.cash_before == "0" and value.cash_after == "0" and value.berth_credits == "0"
	if value.token.is_empty(): return false
	if not value.paid_selected and (value.cash_before != "0" or value.cash_after != "0" or value.berth_credits != "0"): return false
	if value.can_repair:
		if not value.at_origin or value.severity_units == 0 or Finance.decimal_before(value.cash_before, str(cost)): return false
		if value.cash_after != subtract_cost(value.cash_before, cost): return false
	elif value.cash_after != value.cash_before: return false
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
	title.text = "Origin craft repair"
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(title)
	details = Label.new()
	details.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(details)
	repair_button = Button.new()
	repair_button.pressed.connect(func(): action_requested.emit("repair"))
	column.add_child(repair_button)
	back = Button.new()
	back.text = "Back to controls · remain paused"
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
	for button in [repair_button, back]:
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
	repair_button.disabled = packet.is_empty() or not packet.can_repair
	repair_button.text = "Repair craft" if packet.is_empty() else "Repair both regions · %s credits" % packet.repair_credits
	if packet.is_empty():
		details.text = "Repair unavailable: invalid current quote. Back stays paused; reopen the service to review again."
		return
	var lines := PackedStringArray(["Current damage · forebody %d / 3 · landing structure %d / 3.\nRepair removes all current noncatastrophic damage from both regions; the individual craft and impact history are retained." % [packet.forebody, packet.landing_structure]])
	if packet.paid_selected:
		lines.append("Provisional tariff: %s credits per severity unit. Repair: %s credits.\nDue berth cash charge: %s credits. Cash after berth: %s credits.\nCash after repair: %s credits. No repair credit or financing." % [packet.unit_credits, packet.repair_credits, packet.berth_credits, packet.cash_before, packet.cash_after])
	else:
		lines.append("Technical mode · repair is free. This does not enroll you in paid operations.")
	lines.append("Repairs are completed immediately. Fuel, jump charges, cargo and drive state stay unchanged.")
	lines.append(packet.terms)
	lines.append(packet.note)
	details.text = "\n\n".join(lines)
	if get_viewport().gui_get_focus_owner() == repair_button and repair_button.disabled: back.grab_focus()

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in [repair_button, back]:
		if button.visible and not button.disabled: buttons.append(button)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in [repair_button, back] and target.visible and not target.disabled: target.pressed.emit()
