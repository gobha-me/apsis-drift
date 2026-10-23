extends "res://tests/survey_commissions_test.gd"
## Reuses the orbital contract and actual C++ fixtures; no presentation money law.
const Finance = preload("res://scripts/native/paid_operations.gd")

func malformed_paid(owner: Variant, packet: Dictionary, output: String) -> void:
	check(Finance.valid_packet(packet), "closed financial packet refused")
	for field in packet:
		var bad := packet.duplicate(true)
		bad.erase(field)
		check(not Finance.valid_packet(bad), "missing financial field accepted: " + field)
	for field in packet:
		var bad := packet.duplicate(true)
		bad[field] = null
		check(not Finance.valid_packet(bad), "null financial field accepted: " + field)
	for change in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"tick": "01"}, {"cash": "18446744073709551616"}, {"operating_tab": NAN}, {"missing_jump_charges": 4}, {"missing_jump_charges": 1.0}, {"token": "x".repeat(1025)}, {"terms": "x".repeat(4097)}, {"note": "x".repeat(1025)}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		check(not Finance.valid_packet(bad), "invalid financial packet accepted: " + str(change.keys()))
	var before := save(owner, output.path_join("arguments-before.json"))
	for value in [null, 1, 1.0, NAN, INF, "", "x".repeat(1025), "foreign", "é".repeat(513), [], {}]:
		for method in ["enable_freedom_paid_operations", "purchase_freedom_paid_fuel", "request_freedom_paid_retainer"]:
			check(not owner.call(method, value), "invalid financial command accepted: " + method)
	check(save(owner, output.path_join("arguments-after.json")) == before, "refused financial arguments mutated Save")

func refused_paid(view: Control, owner: Variant, action: String, output: String, label: String) -> void:
	var before := save(owner, output.path_join(label + "-before.json"))
	var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
	view.focused = false
	provider.focused = false
	view.paid_operations_command(action)
	view.focused = true
	provider.focused = true
	view.mode_change_pending = true
	view.paid_operations_command(action)
	view.mode_change_pending = false
	view.journey_dialog_open = true
	view.paid_operations_command(action)
	view.journey_dialog_open = false
	provider.waiting_action = "forward"
	view.paid_operations_command(action)
	provider.waiting_action = ""
	view.save_dialog.show()
	view.paid_operations_command(action)
	view.save_dialog.hide()
	var settings: Control = view.settings_view if view is WalkView else view.controls_menu.settings_view
	settings.show()
	view.paid_operations_command(action)
	settings.hide()
	var held: int = int(provider.foot_key_codes()[0]) if view is WalkView else int(provider.bindings.forward.key.code)
	key(held, true)
	view.paid_operations_command(action)
	key(held, false)
	view.observe_neutral_controls()
	var office: Control = reader(view)
	var page: Control = office.paid_view
	var button: Button = {"enable": page.enable_button, "purchase": page.purchase_button, "retainer": page.retainer_button}[action]
	button.grab_focus()
	var foreign := InputEventJoypadButton.new()
	foreign.device = 99
	foreign.button_index = JOY_BUTTON_A
	foreign.pressed = true
	if view is WalkView: view._input(foreign)
	else: view.controls_menu._input(foreign)
	# Hidden commission controls cannot act through the open financial child.
	view.survey_commission_command("accept")
	view.survey_commission_command("abandon")
	check(office.visible and office.paid_open() and view.paused and save(owner, output.path_join(label + "-after.json")) == before, "financial modal/focus/held/foreign-pad or hidden action mutated Save")
	page.packet.token += "x"
	view.paid_operations_command(action)
	check(page.packet == owner.get_freedom_paid_operations() and save(owner, output.path_join(label + "-stale.json")) == before, "stale displayed financial quote committed instead of refreshing")

func run(tree: SceneTree, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "financial witness folder unavailable"): return failures
	var source := fixtures.path_join("paid-operations")
	var cases := [
		["commission-station", "enable", "paid-selected"],
		["paid-fuel-station", "purchase", "paid-fuel-bought"],
		["paid-retainer-station", "retainer", "paid-retainer-funded"]]
	for row: Array in cases:
		var shell := host(tree, source.path_join(row[0] + ".json"), assets)
		if shell == null: return failures
		var view: Control = shell.current_view
		var owner: Variant = shell.bridge
		var initial := FileAccess.get_file_as_bytes(source.path_join(row[0] + ".json"))
		var expected := FileAccess.get_file_as_bytes(source.path_join(row[2] + ".json"))
		view.observe_neutral_controls()
		view.open_survey_commissions()
		view.open_paid_operations()
		var office: Control = reader(view)
		var page: Control = office.paid_view
		check(office.visible and office.paid_open() and page.packet == owner.get_freedom_paid_operations() and view.paused, "financial reader did not open inside actual paused office")
		check(save(owner, output.path_join(row[1] + "-reader.json")) == initial, "reading financial terms selected or billed the account")
		if row[1] == "enable": malformed_paid(owner, page.packet, output)
		refused_paid(view, owner, row[1], output, row[1] + "-refused")
		await tree.process_frame
		var action_button: Button = {"enable": page.enable_button, "purchase": page.purchase_button, "retainer": page.retainer_button}[row[1]]
		action_button.pressed.emit()
		check(save(owner, output.path_join(row[1] + "-ui.json")) == expected, "financial action differs from direct complete C++ Save: " + row[1])
		view.paid_operations_command(row[1])
		check(save(owner, output.path_join(row[1] + "-duplicate.json")) == expected, "duplicate financial action changed Save")
		var continued: Variant = restore(output.path_join(row[1] + "-ui.json"))
		if continued != null:
			check(save(continued, output.path_join(row[1] + "-continued.json")) == expected and continued.get_freedom_paid_operations() == owner.get_freedom_paid_operations(), "financial Continue changed cash/terms/stores")
		continued = null
		page.back.pressed.emit()
		check(not office.paid_open() and office.visible and view.paused, "financial Back resumed or closed parent office")
		office.back.pressed.emit()
		check(not office.visible and view.paused, "office Back resumed gameplay")
		shell.free(); owner = null
		await tree.process_frame
	# Read-only selected account is also displayed in the actual flight host.
	var shell := host(tree, source.path_join("commission-returned.json"), assets)
	if shell == null: return failures
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	check(view is FlightView, "paid returned fixture did not open flight host")
	view.observe_neutral_controls()
	view.open_survey_commissions()
	view.open_paid_operations()
	check(reader(view).paid_open() and Finance.valid_packet(reader(view).paid_view.packet), "flight host lacks selected paid reader")
	refused_paid(view, owner, "purchase", output, "flight-refused")
	reader(view).paid_view.back.pressed.emit()
	await tree.process_frame
	view.survey_commission_command("submit")
	check(save(owner, output.path_join("flight-paid-ui.json")) == FileAccess.get_file_as_bytes(source.path_join("commission-paid.json")), "actual paid flight submission lost income settlement")
	shell.free(); owner = null
	await tree.process_frame
	var report := {"schema": "apsis.paid-operations-native/1", "completed": failures == 0, "failures": failures, "scope": "Actual paused station and flight hosts, explicit enrollment/purchase/capped retainer and submission; complete C++ Save parity and Continue. Depleted fuel/insolvent account and flight poses are initialized fixtures, not ordinary flown profit or player acceptance."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "financial report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
