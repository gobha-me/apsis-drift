extends Control
## A read-only, paused office reader. The owner revalidates every command.
signal action_requested(action: String)
signal back_requested
signal paid_open_requested
signal paid_action_requested(action: String)
signal sale_open_requested
signal sale_action_requested(action: String)
signal recovery_sale_open_requested
signal recovery_sale_action_requested(action: String)

const PaidOperations = preload("res://scripts/native/paid_operations.gd")
var paid_view: Control
var paid_button: Button
var paid_packet: Dictionary = {}
const SpecimenSale = preload("res://scripts/native/specimen_sale.gd")
var sale_view: Control
var sale_button: Button
var sale_packet: Dictionary = {}
const RecoverySale = preload("res://scripts/native/derelict_recovery_sale.gd")
var recovery_sale_view: Control
var recovery_sale_button: Button
var recovery_sale_packet: Dictionary = {}

var packet: Dictionary = {}
var requested_kind := 1
const ReturnCharge = preload("res://scripts/native/native_return_charge_check.gd")
var requested_target: Dictionary = {}
var offers_packet: Dictionary = {}
var destination_buttons: Array[Button] = []
var orbital_button: Button
var geological_button: Button
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
	if not value is Dictionary or not value.get("version") is int: return false
	var geological: bool = value.version == 2
	if value.version not in [1, 2] or value.size() != (27 if geological else 24): return false
	if geological:
		if not value.get("kind") is int or value.kind != 2: return false
		for field in ["specimen", "submission_token"]:
			if not value.get(field) is String or value[field].to_utf8_buffer().size() > (2048 if field == "submission_token" else 512): return false
			for index in value[field].length():
				if value[field].unicode_at(index) == 0 or (field == "submission_token" and value[field].unicode_at(index) > 127): return false
	for key in ["supported", "selected", "at_origin", "can_accept", "can_submit", "can_abandon", "completed", "expired"]:
		if not value.get(key) is bool: return false
	if geological:
		if value.completed != (not value.specimen.is_empty() and not value.submission_token.is_empty()): return false
		if value.specimen.is_empty() != value.submission_token.is_empty(): return false
	for key in ["sequence", "offer_sequence"]:
		if not value.get(key) is int or value[key] < 0 or value[key] > 4294967295: return false
	if not value.get("acquired_ticks") is int or value.acquired_ticks < 0 or value.acquired_ticks > 3600: return false
	if not value.get("required_ticks") is int or value.required_ticks != (0 if geological else 3600): return false
	if geological and value.acquired_ticks != 0: return false
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
		if (not geological and value.completed != (value.acquired_ticks == 3600)) or value.expired != (not decimal_before(value.tick, value.deadline_tick)): return false
		if geological and value.expired and value.completed: return false
	if value.can_accept:
		if not value.supported or not value.at_origin or value.offer_sequence == 0 or not decimal_before(value.tick, value.offer_until) or (value.sequence != 0 and not value.expired): return false
	if value.can_submit and (not value.supported or not value.selected or not value.at_origin or value.sequence == 0 or not value.completed or value.expired): return false
	if value.can_abandon and (not value.supported or not value.selected or not value.at_origin or value.sequence == 0): return false
	if value.sequence != 0 or value.can_accept:
		if value.system_id == "0" or value.planet_id == "0" or value.body_name.is_empty(): return false
	return true

static func valid_offers(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4 or not value.get("version") is int or value.version != 1 or not decimal(value.get("tick")): return false
	if not ReturnCharge.bounded_text(value.get("note"), 1024) or not value.get("offers") is Array or value.offers.size() > 2: return false
	var seen: Dictionary = {}
	for row: Variant in value.offers:
		if not row is Dictionary or row.size() != 5: return false
		for field in ["system_id", "planet_id"]:
			if not decimal(row.get(field)) or row[field] == "0": return false
		if not ReturnCharge.bounded_text(row.get("system_name"), 256) or not ReturnCharge.bounded_text(row.get("body_name"), 512): return false
		if not ReturnCharge.valid_packet(row.get("route")) or row.route.tick != value.tick: return false
		if row.route.available and (row.route.required_charges != 2 or row.route.legs[0].destination_id != row.system_id or row.route.legs[1].destination_id != row.route.legs[0].source_id): return false
		var identity: String = row.system_id + "/" + row.planet_id
		if seen.has(identity): return false
		seen[identity] = true
	return true

func query(bridge: Variant) -> Variant:
	if requested_kind == 1 and not requested_target.is_empty():
		return bridge.get_freedom_survey_commissions_for_target(requested_target.system_id, requested_target.planet_id)
	return bridge.get_freedom_survey_commissions(requested_kind)

func refresh_offers(value: Variant) -> void:
	offers_packet = value.duplicate(true) if valid_offers(value) else {}
	refresh(packet)

func request_destination(index: int) -> void:
	action_requested.emit("choose_destination_" + str(index))

func choose_target(index: int, bridge: Variant) -> void:
	if packet.is_empty() or packet.sequence != 0 or requested_kind != 1 or offers_packet.is_empty() or index < 0 or index >= offers_packet.offers.size(): return
	var row: Dictionary = offers_packet.offers[index]
	requested_target = {"system_id": row.system_id, "planet_id": row.planet_id}
	var quote: Variant = query(bridge)
	refresh(quote if valid_packet(quote) and quote.tick == offers_packet.tick and quote.system_id == row.system_id and quote.planet_id == row.planet_id and quote.body_name == row.body_name else {})
	back.grab_focus()

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
	orbital_button = Button.new()
	orbital_button.text = "Choose orbital measurement work"
	orbital_button.pressed.connect(func(): action_requested.emit("choose_orbital"))
	column.add_child(orbital_button)
	geological_button = Button.new()
	geological_button.text = "Choose geological specimen work"
	geological_button.pressed.connect(func(): action_requested.emit("choose_geological"))
	column.add_child(geological_button)
	for index in 2:
		var destination := Button.new()
		destination.pressed.connect(request_destination.bind(index))
		column.add_child(destination)
		destination_buttons.append(destination)
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
	paid_button = Button.new()
	paid_button.text = "Paid operations · terms and fuel quotes"
	paid_button.pressed.connect(func(): paid_open_requested.emit())
	column.add_child(paid_button)
	sale_button = Button.new()
	sale_button.text = "Sell carried specimens · Origin buyer"
	sale_button.pressed.connect(func(): sale_open_requested.emit())
	column.add_child(sale_button)
	recovery_sale_button = Button.new()
	recovery_sale_button.text = "Sell recovered canister · Origin buyer"
	recovery_sale_button.pressed.connect(func(): recovery_sale_open_requested.emit())
	column.add_child(recovery_sale_button)
	back = Button.new()
	back.text = "Back · remain paused"
	back.pressed.connect(func(): back_requested.emit())
	column.add_child(back)
	paid_view = PaidOperations.new()
	paid_view.action_requested.connect(func(action: String): paid_action_requested.emit(action))
	paid_view.back_requested.connect(func(): back_requested.emit())
	add_child(paid_view)
	sale_view = SpecimenSale.new()
	sale_view.action_requested.connect(func(action: String): sale_action_requested.emit(action))
	sale_view.back_requested.connect(func(): back_requested.emit())
	add_child(sale_view)
	recovery_sale_view = RecoverySale.new()
	recovery_sale_view.action_requested.connect(func(action: String): recovery_sale_action_requested.emit(action))
	recovery_sale_view.back_requested.connect(func(): back_requested.emit())
	add_child(recovery_sale_view)
	resized.connect(layout_reader)
	get_window().size_changed.connect(layout_reader)
	hide()

func layout_reader() -> void:
	if readable_theme == null or not is_inside_tree() or get_window().size.x <= 0 or get_window().size.y <= 0: return
	var pixels := Vector2(get_window().size)
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	readable_theme.default_font_size = ceili(20 * scale)
	for button in [orbital_button, geological_button, destination_buttons[0], destination_buttons[1], accept_button, submit_button, abandon_button, paid_button, sale_button, recovery_sale_button, back]:
		button.custom_minimum_size.y = ceili(44 * scale)
		button.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

func open(value: Dictionary) -> void:
	paid_view.hide()
	sale_view.hide()
	recovery_sale_view.hide()
	panel.show()
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
	if not packet.is_empty():
		requested_kind = packet.get("kind", 1)
		if packet.sequence != 0: requested_target.clear()
	var offers: Array = offers_packet.get("offers", []) if not packet.is_empty() and offers_packet.get("tick") == packet.tick else []
	for index in destination_buttons.size():
		var button: Button = destination_buttons[index]
		button.visible = requested_kind == 1 and index < offers.size()
		button.disabled = packet.is_empty() or packet.sequence != 0
		if button.visible:
			var row: Dictionary = offers[index]
			button.text = "Survey destination · " + row.system_name + " / " + row.body_name
			button.disabled = button.disabled or (packet.system_id == row.system_id and packet.planet_id == row.planet_id)
			button.tooltip_text = "Abandon existing work before changing its destination." if packet.sequence != 0 else "Review this destination's full terms; browsing spends nothing and changes no navigation."
	orbital_button.disabled = packet.is_empty() or packet.sequence != 0 or requested_kind == 1
	geological_button.disabled = packet.is_empty() or packet.sequence != 0 or requested_kind == 2
	var geological: bool = requested_kind == 2
	accept_button.text = "Accept geological commission · 100 credits" if geological else "Accept commission · 100 credits"
	if not packet.is_empty() and packet.sequence != 0 and packet.expired:
		accept_button.text = "Renew same-target commission · 100 credits"
	submit_button.text = "Deliver named specimen · consume one · 100 credits" if geological else "Submit completed report"
	accept_button.disabled = packet.is_empty() or not packet.can_accept
	submit_button.disabled = packet.is_empty() or not packet.can_submit
	abandon_button.disabled = packet.is_empty() or not packet.can_abandon
	paid_button.disabled = paid_packet.is_empty()
	sale_button.disabled = sale_packet.is_empty()
	recovery_sale_button.visible = not recovery_sale_packet.is_empty() and recovery_sale_packet.supported
	recovery_sale_button.disabled = recovery_sale_packet.is_empty()
	if packet.is_empty():
		details.text = "Commission office unavailable: the current response was invalid. Return to Controls and try again."
	else:
		var operating := "Fuel service remains free unless you explicitly select paid operations."
		if not paid_packet.is_empty() and paid_packet.selected:
			operating = "Paid operations selected · Cash: %s · Operating tab: %s · Insurance excess: %s.\nOpen Paid operations for current berth coverage and fuel quotes." % [paid_packet.cash, paid_packet.operating_tab, paid_packet.recovery_due]
		var terms := "Optional geological work · 100 credits for one fresh carried specimen.\nCollect after acceptance on the named planet; deliver one item at Origin before four game hours. Discovery notes and other items remain. No additional 40-credit sale is booked." if geological else "Optional survey work · 100 credits per fresh report.\n30 seconds of qualifying orbital measurement · accepted terms reserved for four game hours."
		var lines := PackedStringArray([terms + "\n" + operating, "Historical gross commission earnings: " + packet.earnings])
		if not sale_packet.is_empty() and sale_packet.supported: lines.append("Historical gross specimen earnings: " + sale_packet.earned_credits)
		if not recovery_sale_packet.is_empty() and recovery_sale_packet.supported: lines.append("Historical gross canister earnings: " + recovery_sale_packet.earned_credits)
		if not packet.body_name.is_empty(): lines.append("Target: " + packet.body_name)
		if not geological:
			for row: Dictionary in offers:
				if row.system_id == packet.system_id and row.planet_id == packet.planet_id:
					lines.append(ReturnCharge.text(row.route) + "\nCharge shortfall is information, not an acceptance gate. No fuel, insertion, deadline or profit guarantee.")
		if packet.sequence != 0:
			if geological:
				lines.append("Commission #%d · %s\nDelivery due at ship time %s · Time left: %s." % [packet.sequence, "fresh specimen carried" if packet.completed else "fresh physical specimen required", clock_text(packet.deadline_tick), remaining_text(packet.deadline_tick, packet.tick)])
				if not packet.specimen.is_empty(): lines.append("Deliver exactly: " + packet.specimen + ". This item will be consumed; its discovery note remains.")
			else:
				lines.append("Commission #%d · %.1f / 30 s\nReport due at ship time %s · Time left: %s." % [packet.sequence, floorf(float(packet.acquired_ticks) / 12.0) / 10.0, clock_text(packet.deadline_tick), remaining_text(packet.deadline_tick, packet.tick)])
		elif packet.can_accept:
			lines.append("Offer changes at ship time %s · Time left: %s." % [clock_text(packet.offer_until), remaining_text(packet.offer_until, packet.tick)])
		lines.append(packet.note)
		if not packet.last_payment.is_empty(): lines.append("Latest report paid: 100 credits. Receipt retained.")
		details.text = "\n\n".join(lines)
	if focused in [orbital_button, geological_button, destination_buttons[0], destination_buttons[1], accept_button, submit_button, abandon_button, paid_button, sale_button, recovery_sale_button, back] and (not focused.visible or focused.disabled): back.grab_focus()

func navigate(direction: int) -> void:
	if recovery_sale_open():
		recovery_sale_view.navigate(direction)
		return
	if sale_open():
		sale_view.navigate(direction)
		return
	if paid_open():
		paid_view.navigate(direction)
		return
	var buttons: Array[Button] = []
	for button in [orbital_button, geological_button, destination_buttons[0], destination_buttons[1], accept_button, submit_button, abandon_button, paid_button, sale_button, recovery_sale_button, back]:
		if button.visible and not button.disabled: buttons.append(button)
	var index := buttons.find(get_viewport().gui_get_focus_owner())
	buttons[posmod(index + direction, buttons.size())].grab_focus()

func accept() -> void:
	if recovery_sale_open():
		recovery_sale_view.accept()
		return
	if sale_open():
		sale_view.accept()
		return
	if paid_open():
		paid_view.accept()
		return
	var target := get_viewport().gui_get_focus_owner()
	if target in [orbital_button, geological_button, destination_buttons[0], destination_buttons[1], accept_button, submit_button, abandon_button, paid_button, sale_button, recovery_sale_button, back] and target.visible and not target.disabled: target.pressed.emit()

func paid_open() -> bool:
	return is_instance_valid(paid_view) and paid_view.visible

func refresh_paid(value: Dictionary) -> void:
	paid_packet = value.duplicate(true) if PaidOperations.valid_packet(value) else {}
	refresh(packet)

func open_paid(value: Dictionary) -> void:
	if service_child_open(): return
	refresh_paid(value)
	panel.hide()
	paid_view.open(paid_packet)

func close_paid() -> void:
	if not paid_open(): return
	paid_view.hide()
	panel.show()
	paid_button.grab_focus()
	scroll.ensure_control_visible(paid_button)

func sale_open() -> bool:
	return is_instance_valid(sale_view) and sale_view.visible

func service_child_open() -> bool:
	return paid_open() or sale_open() or recovery_sale_open()

func refresh_sale(value: Dictionary) -> void:
	sale_packet = value.duplicate(true) if SpecimenSale.valid_packet(value) else {}
	refresh(packet)

func open_sale(value: Dictionary) -> void:
	if service_child_open(): return
	refresh_sale(value)
	panel.hide()
	sale_view.open(sale_packet)

func close_sale() -> void:
	if not sale_open(): return
	sale_view.hide()
	panel.show()
	sale_button.grab_focus()
	scroll.ensure_control_visible(sale_button)

func recovery_sale_open() -> bool:
	return is_instance_valid(recovery_sale_view) and recovery_sale_view.visible

func refresh_recovery_sale(value: Dictionary) -> void:
	recovery_sale_packet = value.duplicate(true) if RecoverySale.valid_packet(value) else {}
	refresh(packet)

func open_recovery_sale(value: Dictionary) -> void:
	if service_child_open(): return
	refresh_recovery_sale(value)
	panel.hide()
	recovery_sale_view.open(recovery_sale_packet)

func close_recovery_sale() -> void:
	if not recovery_sale_open(): return
	recovery_sale_view.hide()
	panel.show()
	recovery_sale_button.grab_focus()
	scroll.ensure_control_visible(recovery_sale_button)
