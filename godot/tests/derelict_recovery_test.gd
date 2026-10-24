extends "res://tests/survey_commissions_test.gd"
## One existing Shell; C++ owns all source poses, retrieval ticks and accounting.
const Recovery = preload("res://scripts/native/derelict_recovery.gd")
const Buyer = preload("res://scripts/native/derelict_recovery_sale.gd")
const Finance = preload("res://scripts/native/paid_operations.gd")
const Title = preload("res://scripts/native/native_title.gd")
const PHASES = ["station-selected", "ready", "started", "half", "near", "complete", "cancelled", "interrupted", "origin-carried", "origin-paid", "sold", "paid-sold", "prior-sale-enrolled", "lost-cargo", "unfinished-loss", "origin-debt", "debt-sold"]
var assertions := 0
var sources: Dictionary = {}
var files: Dictionary = {}
var input_witnesses: Array = []

func check(value: bool, message: String) -> bool:
	assertions += 1
	return super.check(value, "Derelict recovery: " + message)

func zero() -> PackedFloat64Array:
	var result := PackedFloat64Array()
	result.resize(12)
	result.fill(0.0)
	return result

func record(owner: Variant, output: String, name: String, expected: PackedByteArray) -> PackedByteArray:
	var path := output.path_join(name + ".json")
	var actual := save(owner, path)
	var equal := actual == expected
	check(equal, "whole Save differs: " + name)
	files[name + ".json"] = {"sha256": FileAccess.get_sha256(path), "bytes_equal": equal}
	return actual

func click_key(tree: SceneTree, button: Button) -> void:
	button.grab_focus()
	await tree.process_frame
	await tree.process_frame
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	await tree.process_frame

func pad(device: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = JOY_BUTTON_A
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func click_mouse(tree: SceneTree, button: Button) -> void:
	button.grab_focus()
	await tree.process_frame
	var point := button.get_global_rect().get_center()
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	tree.root.push_input(motion, true)
	for down in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		event.position = point
		event.global_position = point
		event.pressed = down
		tree.root.push_input(event, true)
	await tree.process_frame

func continue_shared(tree: SceneTree, shell: Control, owner: Variant, path: String, assets: String) -> Control:
	if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "shared Shell Continue refused: " + shell.error): return null
	var view: Control = shell.current_view
	view.set_process(false)
	view.focused = true
	var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
	provider.persist = false
	provider.focused = true
	provider.device = -1
	if view is WalkView:
		view.selected_pad = -1
	else:
		view.controls_menu.show_menu()
	provider.install()
	view.observe_neutral_controls()
	await tree.process_frame
	return view

func packet_units(progress: Dictionary, sale: Dictionary) -> void:
	check(Recovery.valid_packet(progress) and Buyer.valid_packet(sale), "actual closed packets refused")
	for value in [progress, sale]:
		for field in value:
			var bad: Dictionary = value.duplicate(true)
			bad.erase(field)
			check(not (Recovery.valid_packet(bad) if value == progress else Buyer.valid_packet(bad)), "missing closed field accepted: " + field)
			bad = value.duplicate(true)
			bad[field] = null
			check(not (Recovery.valid_packet(bad) if value == progress else Buyer.valid_packet(bad)), "null field accepted: " + field)
	for change in [{"extra": 0}, {"version": 1.0}, {"version": 2}, {"selected": 1}, {"known": "yes"}, {"can_begin": null}, {"active": 1}, {"acquired_ticks": -1}, {"acquired_ticks": 3601}, {"acquired_ticks": 0.0}, {"required_ticks": 3599}, {"status": "unknown"}, {"status": "carried", "acquired_ticks": 0}, {"active": true, "status": "unclaimed"}, {"tick": "01"}, {"tick": "18446744073709551616"}, {"note": ""}, {"note": "é".repeat(513)}, {"note": PackedByteArray([98, 0])}]:
		var bad := progress.duplicate(true)
		bad.merge(change, true)
		check(not Recovery.valid_packet(bad), "malformed progress accepted: " + str(change.keys()))
	for change in [{"extra": 0}, {"version": 1.0}, {"supported": 1}, {"at_origin": "yes"}, {"paid_selected": 0}, {"can_sell": null}, {"carried_count": 2}, {"capacity": 4}, {"capacity": 1.0}, {"unit_credits": "40"}, {"gross_credits": "99"}, {"earned_credits": "200"}, {"earned_credits": "100", "carried_count": 1}, {"insurance_credits": "101"}, {"tab_credits": "51"}, {"net_credits": "101"}, {"cash_after": "18446744073709551616"}, {"tick": 1}, {"token": "é".repeat(2049)}, {"note": "x".repeat(1025)}, {"contents": "x".repeat(4097)}]:
		var bad := sale.duplicate(true)
		bad.merge(change, true)
		check(not Buyer.valid_packet(bad), "malformed sale accepted: " + str(change.keys()))
	for bad in [null, [], true, NAN, "packet"]:
		check(not Recovery.valid_packet(bad) and not Buyer.valid_packet(bad), "non-dictionary packet accepted")

func backend_readers(source: String, output: String, manifest: Dictionary) -> void:
	check(manifest.get("schema") == "apsis.derelict-recovery-scenario/1" and manifest.get("completed") == true and manifest.get("phases") is Array and manifest.phases.size() == PHASES.size(), "scenario manifest/roster unavailable")
	if failures: return
	for index in PHASES.size():
		var stem: String = PHASES[index]
		if not check(manifest.phases[index] is Dictionary, "scenario row is not an object"): return
		var row: Dictionary = manifest.phases[index]
		if not check(row.get("file") == stem + ".json", "scenario reordered or substituted a phase"): return
		var path := source.path_join(row.file)
		var original := FileAccess.get_file_as_bytes(path)
		var owner: Variant = restore(path)
		if owner == null: return
		var progress: Dictionary = owner.get_freedom_derelict_recovery()
		var quote: Dictionary = owner.get_freedom_derelict_recovery_sale()
		check(Recovery.valid_packet(progress) and progress.selected and progress.tick == row.tick and progress.status == row.status and progress.acquired_ticks == row.acquired_ticks, "Continue changed retrieval progress/history: " + stem)
		check(Buyer.valid_packet(quote) and quote.supported and quote.earned_credits == row.earned_credits and quote.paid_selected == (row.paid_version != 0), "Continue changed cargo or retained earnings: " + stem)
		var finance: Dictionary = owner.get_freedom_paid_operations()
		check(Finance.valid_packet(finance) and finance.selected == (row.paid_version != 0) and (not finance.selected or finance.cash == row.cash), "Continue changed effective paid income/debt: " + stem)
		if stem == "station-selected":
			var contact: Dictionary = owner.get_freedom_flight_state().derelict
			check(not progress.known and not progress.can_begin and contact.id.is_empty() and contact.position == Vector3.ZERO and contact.distance_metres == 0.0, "selected recovery disclosed an unobserved source")
		check(owner.get_freedom_flight_state().get("derelict_recovery") == progress, "cheap flight packet differs from explicit reader")
		for repeat in 3:
			check(owner.get_freedom_derelict_recovery() == progress and owner.get_freedom_derelict_recovery_sale() == quote, "repeated reads changed actual packet")
		record(owner, output, stem + "-read", original)
		var continued: Variant = restore(output.path_join(stem + "-read.json"))
		if continued != null:
			check(continued.get_freedom_derelict_recovery() == progress and continued.get_freedom_derelict_recovery_sale() == quote, "second Continue changed finite claim/account")
			record(continued, output, stem + "-continued", original)
		continued = null
		if stem in ["complete", "lost-cargo", "unfinished-loss"]:
			check(not quote.can_sell and not owner.sell_freedom_derelict_recovery(quote.token), "remote/lost/empty source sold from notes")
			record(owner, output, stem + "-remote-refused", original)
		if stem == "origin-carried":
			packet_units(progress, quote)
			for value in [null, true, 1, 1.0, NAN, INF, "", "foreign", "x".repeat(4097), "é".repeat(2049), PackedByteArray([98, 0]), [], {}]:
				check(not owner.sell_freedom_derelict_recovery(value), "invalid sale token Variant/bounds accepted")
			for value in [null, true, false, 0.0, 1.0, NAN, INF, -1, 3, "0", [], {}]:
				check(not owner.stage_freedom_derelict_recovery_new_game("42", value, false), "invalid drive Variant/bounds staged a new journey")
			for value in [null, 0, 1, 0.0, 1.0, "false", [], {}]:
				check(not owner.stage_freedom_derelict_recovery_new_game("42", 0, value), "nonboolean service selection staged a new journey")
			check(owner.get_pending_freedom_start().is_empty(), "refused factory arguments retained a pending owner")
			record(owner, output, "sale-arguments-refused", original)
		owner = null

func title_selection(tree: SceneTree, shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	# Temporarily suspend the sole existing host while the real title owns input.
	var old: Control = shell.current_view
	var mode: int = old.process_mode
	old.process_mode = Node.PROCESS_MODE_DISABLED
	var title := Title.new()
	title.persist_controls = false
	title.initial_controller_device = -1
	var selection: Dictionary = {}
	title.start_requested.connect(func(value: Dictionary): selection.assign(value))
	tree.root.add_child(title)
	await tree.process_frame
	check(not title.derelict_recovery_choice.button_pressed and title.derelict_recovery_choice.disabled, "default title enables recovery")
	title.network_choice.grab_focus()
	key(KEY_SPACE, true); key(KEY_SPACE, false)
	await tree.process_frame
	for index in 3:
		key(KEY_DOWN, true); key(KEY_DOWN, false)
	key(KEY_ENTER, true); key(KEY_ENTER, false)
	await tree.process_frame
	check(title.network_choice.selected == 3 and not title.derelict_recovery_choice.disabled and not title.derelict_recovery_choice.button_pressed, "charted/uncharted choice did not leave recovery opt-in off")
	title.scroll.ensure_control_visible(title.derelict_recovery_choice)
	await click_key(tree, title.derelict_recovery_choice)
	check(title.derelict_recovery_choice.button_pressed, "real title keyboard did not enable recovery")
	title.scroll.ensure_control_visible(title.new_button)
	await click_key(tree, title.new_button)
	check(selection.get("derelict_recovery") == true and selection.get("uncharted") == true, "real New Game command omitted explicit opt-in")
	title.free()
	old.process_mode = mode
	selection["assets"] = assets
	if check(shell.select_start(owner, selection), "selected actual New Game staging refused: " + shell.error):
		shell.current_view.set_process(false)
		check(shell.current_view is WalkView and owner.get_freedom_derelict_recovery().selected and owner.get_freedom_derelict_recovery().status == "unclaimed", "opt-in did not create a paused ordinary station journey")
		record(owner, output, "title-selected", FileAccess.get_file_as_bytes(source.path_join("station-selected.json")))
	input_witnesses.append("title-keyboard-chart3/default-off/explicit-enable/new-game")

func retrieval_guards(tree: SceneTree, view: Control, owner: Variant, output: String, original: PackedByteArray) -> void:
	for field in ["mode_change_pending", "journey_dialog_open", "recovery_notified"]:
		view.set(field, true)
		view.derelict_recovery_command()
		view.set(field, false)
	view.focused = false
	view.player_input.focused = false
	view.derelict_recovery_command()
	view.focused = true
	view.player_input.focused = true
	view.player_input.waiting_action = "forward"
	view.derelict_recovery_command()
	view.player_input.waiting_action = ""
	view.save_dialog.show()
	view.derelict_recovery_command()
	view.save_dialog.hide()
	view.controls_menu.settings_view.show()
	view.derelict_recovery_command()
	view.controls_menu.settings_view.hide()
	# Opposing physical controls must remain non-neutral even if axes cancel.
	var forward := int(view.player_input.bindings.forward.key.code)
	var backward := int(view.player_input.bindings.backward.key.code)
	key(forward, true); key(backward, true)
	view.derelict_recovery_command()
	key(forward, false); key(backward, false)
	view.observe_neutral_controls()
	record(owner, output, "retrieval-guards", original)
	await tree.process_frame

func retrieval_ui(tree: SceneTree, shell: Control, owner: Variant, source: String, assets: String, output: String, manifest: Dictionary) -> void:
	var view: Control = await continue_shared(tree, shell, owner, source.path_join("ready.json"), assets)
	if not check(view is FlightView, "ready retrieval lacks real flight host"): return
	var ready := FileAccess.get_file_as_bytes(source.path_join("ready.json"))
	await retrieval_guards(tree, view, owner, output, ready)
	view.controls_menu.left_scroll.ensure_control_visible(view.derelict_recovery_button)
	await click_key(tree, view.derelict_recovery_button)
	record(owner, output, "keyboard-started", FileAccess.get_file_as_bytes(source.path_join("started.json")))
	check(owner.get_freedom_derelict_recovery().active and view.paused, "real keyboard did not begin paused retrieval")
	input_witnesses.append("keyboard-begin")
	var neutral := zero()
	check(owner.advance_freedom_flight(1.0, neutral, true), "paused poll refused")
	record(owner, output, "paused-started", FileAccess.get_file_as_bytes(source.path_join("started.json")))
	check(not owner.begin_freedom_derelict_recovery(), "duplicate begin accepted")
	record(owner, output, "duplicate-start-refused", FileAccess.get_file_as_bytes(source.path_join("started.json")))
	# Backend stepping is explicit: the view's automatic processing is disabled.
	# These ordinary intervals do not claim a manually flown matched-speed hold.
	for tick in 1800:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "real retrieval interval refused"): return
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	record(owner, output, "actual-half", FileAccess.get_file_as_bytes(source.path_join("half.json")))
	view.player_input.available_pads[2] = true
	view.player_input.device = 2
	view.player_input.install()
	view.observe_neutral_controls()
	view.controls_menu.left_scroll.ensure_control_visible(view.derelict_recovery_cancel_button)
	view.derelict_recovery_cancel_button.grab_focus()
	await tree.process_frame
	pad(99, true); pad(99, false)
	await tree.process_frame
	record(owner, output, "foreign-pad-half", FileAccess.get_file_as_bytes(source.path_join("half.json")))
	pad(2, true); pad(2, false)
	await tree.process_frame
	record(owner, output, "selected-pad-cancelled", FileAccess.get_file_as_bytes(source.path_join("cancelled.json")))
	input_witnesses.append("selected-pad-cancel/foreign-pad-refused")
	view = await continue_shared(tree, shell, owner, source.path_join("half.json"), assets)
	if view == null: return
	for tick in 1799:
		if not check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "continued retrieval interval refused"): return
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	check(owner.get_freedom_derelict_recovery().acquired_ticks == 3599 and "29.9 / 30 s" in view.primary_status.text, "near-completion rounded up or lost progress")
	record(owner, output, "continued-near", FileAccess.get_file_as_bytes(source.path_join("near.json")))
	check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "completion interval refused")
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	check(owner.get_freedom_derelict_recovery().status == "carried" and "one sealed service canister" in view.primary_status.text, "completion did not carry the one physical item")
	record(owner, output, "actual-complete", FileAccess.get_file_as_bytes(source.path_join("complete.json")))
	var peer: Variant = restore(source.path_join("started.json"))
	if peer != null:
		# JSON numbers may be floats; validate the bounded integer value before casting.
		var interruption: Variant = manifest.get("interruption_ticks")
		var valid_interruption: bool = (interruption is int or interruption is float) and is_finite(float(interruption)) and interruption >= 1 and interruption <= 600 and float(interruption) == floorf(float(interruption))
		var interruption_controls: Variant = manifest.get("interruption_controls")
		var valid_controls: bool = interruption_controls is Array and interruption_controls.size() == 12
		if valid_controls:
			for index in 12:
				var value: Variant = interruption_controls[index]
				if not (value is int or value is float) or not is_finite(float(value)) or float(value) != (1.0 if index == 0 else 0.0):
					valid_controls = false
					break
		check(valid_interruption and valid_controls, "actual interruption witness unavailable")
		if failures == 0:
			for tick in range(int(interruption)):
				if not check(peer.advance_freedom_flight(1.0 / 120.0, PackedFloat64Array(manifest.interruption_controls), false), "powered interruption refused"): break
			record(peer, output, "actual-interrupted", FileAccess.get_file_as_bytes(source.path_join("interrupted.json")))
	peer = null

func sale_guards(tree: SceneTree, view: Control, owner: Variant, output: String, original: PackedByteArray) -> void:
	var office: Control = reader(view)
	for field in ["mode_change_pending", "journey_dialog_open", "recovery_notified"]:
		view.set(field, true)
		view.derelict_recovery_sale_command()
		view.set(field, false)
	view.focused = false
	view.player_input.focused = false
	view.derelict_recovery_sale_command()
	view.focused = true
	view.player_input.focused = true
	view.player_input.waiting_action = "forward"
	view.derelict_recovery_sale_command()
	view.player_input.waiting_action = ""
	view.save_dialog.show()
	view.derelict_recovery_sale_command()
	view.save_dialog.hide()
	view.controls_menu.settings_view.show()
	view.derelict_recovery_sale_command()
	view.controls_menu.settings_view.hide()
	var forward := int(view.player_input.bindings.forward.key.code)
	var backward := int(view.player_input.bindings.backward.key.code)
	key(forward, true); key(backward, true)
	view.derelict_recovery_sale_command()
	key(forward, false); key(backward, false)
	view.observe_neutral_controls()
	view.survey_commission_command("accept")
	view.survey_commission_command("abandon")
	view.paid_operations_command("enable")
	view.specimen_sale_command()
	view.open_paid_operations()
	view.open_specimen_sale()
	check(office.recovery_sale_open() and not office.paid_open() and not office.sale_open() and not office.panel.visible, "hidden sibling/parent office action escaped the modal child")
	record(owner, output, "sale-ui-guards", original)
	await tree.process_frame

func reader_sizes(tree: SceneTree, owner: Variant, page: Control, output: String, original: PackedByteArray) -> void:
	for pixels in [Vector2i(640, 450), Vector2i(1280, 720)]:
		tree.root.size = pixels
		await tree.process_frame
		await tree.process_frame
		page.layout_reader()
		var factor := minf(float(pixels.x) / page.size.x, float(pixels.y) / page.size.y)
		check(page.scroll.get_global_rect().size.is_finite() and page.scroll.size.x > 0 and page.scroll.size.y > 0 and page.get_global_rect().grow(1.0).encloses(page.scroll.get_global_rect()), "recovery sale reader escapes viewport")
		check(float(page.readable_theme.default_font_size) * factor >= 18.0, "reader font falls below physical18px")
		for button: Button in [page.sell_button, page.back]:
			button.grab_focus()
			page.scroll.ensure_control_visible(button)
			await tree.process_frame
			await tree.process_frame
			check(button.size.y * factor >= 43.9 and page.scroll.get_global_rect().grow(1.0).encloses(button.get_global_rect()), "44px action cannot scroll fully into view")
		record(owner, output, "reader-%dx%d" % [pixels.x, pixels.y], original)

func open_sale(tree: SceneTree, shell: Control, owner: Variant, path: String, assets: String) -> Control:
	var view: Control = await continue_shared(tree, shell, owner, path, assets)
	if not check(view is FlightView and view.paused, "Origin cargo fixture lost the actual seated flight host"): return null
	view.observe_neutral_controls()
	view.open_survey_commissions()
	view.open_derelict_recovery_sale()
	var office: Control = reader(view)
	check(office.visible and office.recovery_sale_open() and office.service_child_open() and not office.panel.visible and view.paused, "Origin sale child did not open as a paused modal reader")
	return view

func sale_ui(tree: SceneTree, shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	var view: Control = await open_sale(tree, shell, owner, source.path_join("origin-carried.json"), assets)
	if view == null: return
	var office: Control = reader(view)
	var page: Control = office.recovery_sale_view
	var carried := FileAccess.get_file_as_bytes(source.path_join("origin-carried.json"))
	record(owner, output, "sale-open-neutral", carried)
	await sale_guards(tree, view, owner, output, carried)
	# A real paid enrollment changes the reviewed token without advancing time.
	# The stale displayed canister quote must refresh/refuse, not sell it.
	var reviewed: Dictionary = page.packet.duplicate(true)
	check(owner.enable_freedom_paid_operations(owner.get_freedom_paid_operations().token), "stale-quote enrollment prerequisite refused")
	var paid := FileAccess.get_file_as_bytes(source.path_join("origin-paid.json"))
	record(owner, output, "stale-source-enrolled", paid)
	await tree.process_frame
	view.derelict_recovery_sale_command()
	check(page.packet != reviewed and page.packet == owner.get_freedom_derelict_recovery_sale() and page.packet.can_sell, "stale quote was replaced silently or not refreshed")
	record(owner, output, "stale-sale-refused", paid)
	await reader_sizes(tree, owner, page, output, paid)
	page.scroll.ensure_control_visible(page.sell_button)
	await tree.process_frame
	await click_mouse(tree, page.sell_button)
	var paid_sold := FileAccess.get_file_as_bytes(source.path_join("paid-sold.json"))
	record(owner, output, "mouse-paid-sold", paid_sold)
	check(page.packet.carried_count == 0 and not page.packet.can_sell and owner.get_freedom_paid_operations().cash == "280" and view.paused, "actual mouse sale did not settle exactly one100-credit canister")
	input_witnesses.append("mouse-paid-sale")
	check(not owner.sell_freedom_derelict_recovery(reviewed.token) and not owner.sell_freedom_derelict_recovery(page.packet.token), "stale/duplicate physical sale paid again")
	view.derelict_recovery_sale_command()
	record(owner, output, "duplicate-sale-refused", paid_sold)
	await click_key(tree, page.back)
	check(not office.recovery_sale_open() and office.visible and office.panel.visible and view.paused, "child Back closed the office or resumed simulation")
	await click_key(tree, office.back)
	check(not office.visible and view.paused, "office Back resumed simulation")
	# Free-profile income is retained first, then explicit enrollment gives280,
	# the same total as enrollment before sale, without a second opening grant.
	view = await open_sale(tree, shell, owner, source.path_join("origin-carried.json"), assets)
	if view == null: return
	office = reader(view)
	page = office.recovery_sale_view
	check(not page.packet.paid_selected and "retained earnings" in page.details.text, "free journey claims spendable cash or silently enrolls")
	page.scroll.ensure_control_visible(page.sell_button)
	page.sell_button.grab_focus()
	await tree.process_frame
	await tree.process_frame
	key(KEY_SPACE, true)
	await tree.process_frame
	record(owner, output, "keyboard-free-held", FileAccess.get_file_as_bytes(source.path_join("origin-carried.json")))
	key(KEY_SPACE, false)
	await tree.process_frame
	record(owner, output, "keyboard-free-sold", FileAccess.get_file_as_bytes(source.path_join("sold.json")))
	input_witnesses.append("keyboard-free-sale")
	await click_key(tree, page.back)
	view.open_paid_operations()
	check(office.paid_open() and office.paid_view.packet.opening_cash == "280", "later enrollment omitted retained sale earnings")
	await click_key(tree, office.paid_view.enable_button)
	var enrolled := FileAccess.get_file_as_bytes(source.path_join("prior-sale-enrolled.json"))
	record(owner, output, "prior-sale-enrolled-ui", enrolled)
	check(not owner.enable_freedom_paid_operations(owner.get_freedom_paid_operations().token), "duplicate enrollment granted another budget")
	record(owner, output, "duplicate-enrollment-refused", enrolled)
	# This source is explicitly an initialized financial aggregate, not flown
	# spending/debt. The sale itself is an actual selected-pad GUI command.
	view = await open_sale(tree, shell, owner, source.path_join("origin-debt.json"), assets)
	if view == null: return
	office = reader(view)
	page = office.recovery_sale_view
	check(page.packet.gross_credits == "100" and page.packet.insurance_credits == "60" and page.packet.tab_credits == "20" and page.packet.net_credits == "20" and page.packet.cash_after == "20", "debt quote changed insurance-first/half-remainder ordering")
	view.player_input.available_pads[2] = true
	view.player_input.device = 2
	view.player_input.install()
	view.observe_neutral_controls()
	page.scroll.ensure_control_visible(page.sell_button)
	page.sell_button.grab_focus()
	await tree.process_frame
	pad(99, true); pad(99, false)
	await tree.process_frame
	record(owner, output, "foreign-pad-debt-refused", FileAccess.get_file_as_bytes(source.path_join("origin-debt.json")))
	pad(2, true); pad(2, false)
	await tree.process_frame
	record(owner, output, "selected-pad-debt-sold", FileAccess.get_file_as_bytes(source.path_join("debt-sold.json")))
	check(view.paused and owner.get_freedom_paid_operations().cash == "20" and owner.get_freedom_paid_operations().operating_tab == "65" and owner.get_freedom_paid_operations().recovery_due == "0", "selected pad changed net income or residual debt")
	input_witnesses.append("selected-pad-debt-sale/foreign-pad-refused")

func run_shared(tree: SceneTree, shell: Control, owner: Variant, source: String, assets: String, output: String, legacy: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "new witness directory unavailable"): return failures
	var old_size := tree.root.size
	var old_scale := tree.root.content_scale_size
	tree.root.size = Vector2i(1280, 720)
	tree.root.content_scale_size = Vector2i(1280, 720)
	await tree.process_frame
	var manifest: Variant = JSON.parse_string(FileAccess.get_file_as_string(source.path_join("manifest.json")))
	for stem: String in PHASES:
		var path := source.path_join(stem + ".json")
		check(FileAccess.file_exists(path), "actual source phase missing: " + stem)
		sources[stem + ".json"] = FileAccess.get_sha256(path)
	check(manifest is Dictionary, "actual scenario manifest is not an object")
	var original_legacy := FileAccess.get_file_as_bytes(legacy)
	var old: Variant = restore(legacy)
	if old != null:
		check(not old.get_freedom_flight_state().has("derelict_recovery") and Recovery.valid_packet(old.get_freedom_derelict_recovery()) and not old.get_freedom_derelict_recovery().selected, "old Continue silently enabled recovery")
		check(not old.begin_freedom_derelict_recovery() and not old.cancel_freedom_derelict_recovery(), "unselected journey admitted retrieval commands")
		record(old, output, "legacy-unselected", original_legacy)
	old = null
	if failures == 0:
		backend_readers(source, output, manifest)
	if failures == 0:
		await title_selection(tree, shell, owner, source, assets, output)
	if failures == 0:
		await retrieval_ui(tree, shell, owner, source, assets, output, manifest)
	if failures == 0:
		await sale_ui(tree, shell, owner, source, assets, output)
	for filename: String in sources:
		check(FileAccess.get_sha256(source.path_join(filename)) == sources[filename], "Godot changed an actual C++ source phase")
	check(FileAccess.get_file_as_bytes(legacy) == original_legacy, "legacy source bytes changed")
	tree.root.size = old_size
	tree.root.content_scale_size = old_scale
	var report := {"schema": "apsis.derelict-recovery-native/1", "completed": failures == 0, "failures": failures, "assertions": assertions, "source_sha256": sources, "saves": files, "inputs": input_witnesses, "scope": "Actual opt-in title/keyboard retrieval/selected-pad cancellation, backend ordinary retrieval intervals and full C++ phase/Continue byte parity; keyboard/mouse/pad Origin sales and read/modal/refusal proof. Home/120m approach/Origin attachment/financial source boundaries are disclosed initialized fixtures; no uninterrupted return/profit, animated transfer, manual handling or GPU claim."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "native recovery report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
