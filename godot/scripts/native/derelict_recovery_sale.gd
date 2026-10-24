extends Control
## The host checks the displayed quote and complete saved owner before selling.
signal action_requested(action: String)
signal back_requested

const Finance = preload("res://scripts/native/paid_operations.gd")
var packet: Dictionary = {}
var sell_button: Button
var back: Button
var details: Label
var scroll: ScrollContainer
var readable_theme: Theme

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 18 or not value.get("version") is int or value.version != 1: return false
	for key in ["supported", "at_origin", "paid_selected", "can_sell"]:
		if not value.get(key) is bool: return false
	if not value.get("carried_count") is int or value.carried_count < 0 or value.carried_count > 1: return false
	if not value.get("capacity") is int or value.capacity != 1: return false
	for key in ["tick", "unit_credits", "gross_credits", "earned_credits", "insurance_credits", "tab_credits", "net_credits", "cash_after"]:
		if not Finance.decimal(value.get(key)): return false
	for key in ["token", "note", "contents"]:
		if not value.get(key) is String: return false
		var bytes: PackedByteArray = value[key].to_utf8_buffer()
		if bytes.has(0) or bytes.size() > (1024 if key == "note" else 4096): return false
	if value.unit_credits != "100" or value.gross_credits != str(value.carried_count * 100) or value.note.is_empty(): return false
	if not value.earned_credits in ["0", "100"]: return false
	if value.earned_credits == "100" and (value.carried_count != 0 or value.can_sell): return false
	if Finance.decimal_before("100", value.insurance_credits) or Finance.decimal_before("50", value.tab_credits) or Finance.decimal_before("100", value.net_credits): return false
	var insurance := int(value.insurance_credits)
	var tab := int(value.tab_credits)
	var gross := int(value.gross_credits)
	if insurance + tab + int(value.net_credits) != gross or tab * 2 > gross - insurance: return false
	if not value.paid_selected and (insurance != 0 or tab != 0 or value.cash_after != "0"): return false
	if (value.carried_count == 0) != value.contents.is_empty(): return false
	if not value.supported and (value.at_origin or value.paid_selected or value.can_sell or value.carried_count != 0 or value.earned_credits != "0"): return false
	if value.supported and value.token.is_empty(): return false
	if value.can_sell and (not value.supported or not value.at_origin or value.carried_count == 0): return false
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
	title.text = "Origin canister buyer"
	title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(title)
	details = Label.new()
	details.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(details)
	sell_button = Button.new()
	sell_button.pressed.connect(func(): action_requested.emit("sell"))
	column.add_child(sell_button)
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
	for button in [sell_button, back]:
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
	sell_button.disabled = packet.is_empty() or not packet.can_sell
	sell_button.text = "Sell carried canister" if packet.is_empty() else "Sell %d carried canister · %s credits gross" % [packet.carried_count, packet.gross_credits]
	if packet.is_empty():
		details.text = "Canister buyer unavailable: invalid current response. Return to the office and review again."
		return
	var lines := PackedStringArray(["Provisional price: %s credits for the sealed service canister.\nCargo: %d / %d. Selling removes the physical canister and retains its recovery note; the source stays depleted." % [packet.unit_credits, packet.carried_count, packet.capacity]])
	if not packet.contents.is_empty(): lines.append(packet.contents)
	lines.append("Historical gross canister earnings: %s credits.\nCarried canister gross: %s credits." % [packet.earned_credits, packet.gross_credits])
	if packet.paid_selected:
		if packet.can_sell:
			lines.append("This sale pays %s insurance excess, then %s operating tab.\nNet sale income: %s credits · Available cash after sale: %s credits." % [packet.insurance_credits, packet.tab_credits, packet.net_credits, packet.cash_after])
		else:
			lines.append("Paid operations selected. A current settlement quote requires a carried canister at Origin.")
	else:
		lines.append("Paid operations are not selected. Sale proceeds remain retained earnings and carry into cash if you later explicitly enroll. Selling does not enroll you or change free service.")
	lines.append(packet.note)
	details.text = "\n\n".join(lines)

func navigate(direction: int) -> void:
	var buttons: Array[Button] = []
	for button in [sell_button, back]:
		if button.visible and not button.disabled: buttons.append(button)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	var target := get_viewport().gui_get_focus_owner()
	if target in [sell_button, back] and target.visible and not target.disabled: target.pressed.emit()
