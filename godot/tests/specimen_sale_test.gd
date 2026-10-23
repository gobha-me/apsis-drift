extends "res://tests/paid_operations_test.gd"
## Existing orbital family; source records/actions are owned by C++.
const Buyer = preload("res://scripts/native/specimen_sale.gd")

func malformed_sale(owner: Variant, packet: Dictionary, output: String) -> void:
	check(Buyer.valid_packet(packet), "actual buyer packet refused")
	for field in packet:
		for missing in [true, false]:
			var bad := packet.duplicate(true)
			if missing: bad.erase(field)
			else: bad[field] = null
			check(not Buyer.valid_packet(bad), "missing/null buyer field accepted: " + field)
	for change in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"carried_count": -1}, {"carried_count": 5}, {"capacity": 3}, {"capacity": 4.0}, {"unit_credits": "41"}, {"gross_credits": NAN}, {"gross_credits": "41"}, {"earned_credits": "41"}, {"insurance_credits": "101"}, {"tab_credits": "81"}, {"net_credits": "161"}, {"cash_after": "18446744073709551616"}, {"tick": "01"}, {"token": "x".repeat(4097)}, {"note": "x".repeat(1025)}, {"contents": "x".repeat(4097)}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		check(not Buyer.valid_packet(bad), "malformed buyer packet accepted: " + str(change.keys()))
	var before := save(owner, output.path_join("arguments-before.json"))
	for value in [null, 1, 1.0, NAN, INF, "", "foreign", "x".repeat(4097), "é".repeat(2049), [], {}]:
		check(not owner.sell_freedom_specimens(value), "wrong buyer token Variant/bounds accepted")
	check(save(owner, output.path_join("arguments-after.json")) == before, "refused buyer arguments changed Save")

func refused_sale(view: Control, owner: Variant, output: String, label: String) -> void:
	var before := save(owner, output.path_join(label + "-before.json"))
	var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
	view.focused = false
	provider.focused = false
	view.specimen_sale_command()
	view.focused = true
	provider.focused = true
	view.mode_change_pending = true
	view.specimen_sale_command()
	view.mode_change_pending = false
	view.journey_dialog_open = true
	view.specimen_sale_command()
	view.journey_dialog_open = false
	provider.waiting_action = "forward"
	view.specimen_sale_command()
	provider.waiting_action = ""
	view.save_dialog.show()
	view.specimen_sale_command()
	view.save_dialog.hide()
	var settings: Control = view.settings_view if view is WalkView else view.controls_menu.settings_view
	settings.show()
	view.specimen_sale_command()
	settings.hide()
	var held: int = int(provider.foot_key_codes()[0]) if view is WalkView else int(provider.bindings.forward.key.code)
	key(held, true)
	view.specimen_sale_command()
	key(held, false)
	view.observe_neutral_controls()
	var office: Control = reader(view)
	var page: Control = office.sale_view
	page.sell_button.grab_focus()
	var foreign := InputEventJoypadButton.new()
	foreign.device = 99
	foreign.button_index = JOY_BUTTON_A
	foreign.pressed = true
	if view is WalkView: view._input(foreign)
	else: view.controls_menu._input(foreign)
	view.survey_commission_command("accept")
	view.survey_commission_command("abandon")
	view.open_paid_operations()
	view.paid_operations_command("enable")
	check(office.sale_open() and not office.paid_open() and view.paused and save(owner, output.path_join(label + "-after.json")) == before, "buyer focus/held/modal/device/hidden/cross-child action changed Save")
	page.packet.token += "x"
	view.specimen_sale_command()
	check(page.packet == owner.get_freedom_specimen_sale() and save(owner, output.path_join(label + "-stale.json")) == before, "stale displayed buyer quote was committed instead of refreshed")

func run(tree: SceneTree, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "buyer witness folder unavailable"): return failures
	var source := fixtures.path_join("specimen-sales")
	var cases := [
		["sale-origin-one", "sale-earning-one"],
		["sale-origin-paid", "sale-paid-one"],
		["sale-origin-four", "sale-paid-four"]]
	var witnesses: Array = []
	for row: Array in cases:
		var shell := host(tree, source.path_join(row[0] + ".json"), assets)
		if shell == null: return failures
		var view: Control = shell.current_view
		var owner: Variant = shell.bridge
		var initial := FileAccess.get_file_as_bytes(source.path_join(row[0] + ".json"))
		var expected := FileAccess.get_file_as_bytes(source.path_join(row[1] + ".json"))
		view.observe_neutral_controls()
		view.open_survey_commissions()
		view.open_specimen_sale()
		var office: Control = reader(view)
		var page: Control = office.sale_view
		var terms: Dictionary = owner.get_freedom_specimen_sale()
		check(office.sale_open() and office.service_child_open() and page.packet == terms and not page.sell_button.disabled and view.paused, "actual buyer did not open inside paused service office")
		check(save(owner, output.path_join(row[0] + "-reader.json")) == initial, "opening quote sold specimens or posted bills")
		if row[0] == "sale-origin-one": malformed_sale(owner, terms, output)
		refused_sale(view, owner, output, row[0] + "-refused")
		page.refresh({})
		check(page.sell_button.disabled and page.packet.is_empty(), "invalid buyer reader enabled sale")
		page.sell_button.pressed.emit()
		check(save(owner, output.path_join(row[0] + "-invalid.json")) == initial, "invalid reader sold physical inventory")
		page.refresh(owner.get_freedom_specimen_sale())
		await tree.process_frame
		page.sell_button.pressed.emit()
		check(save(owner, output.path_join(row[1] + "-ui.json")) == expected and page.packet.carried_count == 0 and not page.packet.can_sell and view.paused, "explicit sale differs from complete C++ Save or leaves carried inventory")
		page.sell_button.pressed.emit()
		view.specimen_sale_command()
		check(save(owner, output.path_join(row[1] + "-duplicate.json")) == expected, "duplicate sale produced more income")
		var continued: Variant = restore(output.path_join(row[1] + "-ui.json"))
		if continued != null:
			check(save(continued, output.path_join(row[1] + "-continued.json")) == expected and continued.get_freedom_specimen_sale() == owner.get_freedom_specimen_sale(), "buyer Continue lost receipt/count/income or re-sold contents")
		continued = null
		page.back.pressed.emit()
		check(not office.sale_open() and office.visible and view.paused, "buyer Back resumed gameplay or closed parent")
		if row[0] == "sale-origin-one":
			await tree.process_frame
			view.open_paid_operations()
			check(office.paid_open() and office.paid_view.packet.opening_cash == "220", "pre-enrollment specimen earnings missing from opening quote")
			office.paid_view.enable_button.pressed.emit()
			check(save(owner, output.path_join("sale-enrolled-ui.json")) == FileAccess.get_file_as_bytes(source.path_join("sale-enrolled.json")), "explicit enrollment lost prior specimen gross or gave another grant")
			office.paid_view.back.pressed.emit()
		witnesses.append({"source": row[0], "result": row[1], "gross": terms.gross_credits, "insurance": terms.insurance_credits, "tab": terms.tab_credits, "net": terms.net_credits, "cpp_save_equal": FileAccess.get_file_as_bytes(output.path_join(row[1] + "-ui.json")) == expected})
		office.back.pressed.emit()
		check(not office.visible and view.paused, "parent Back resumed after buyer")
		shell.free(); owner = null
		await tree.process_frame
	# Typed paid-v2 must also work through the existing fuel quote UI.
	var shell := host(tree, source.path_join("sale-service-source.json"), assets)
	if shell == null: return failures
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	view.observe_neutral_controls()
	view.open_survey_commissions()
	view.open_paid_operations()
	var office: Control = reader(view)
	check(office.paid_open() and office.paid_view.packet.can_purchase, "paid-v2 fuel quote unavailable")
	await tree.process_frame
	office.paid_view.purchase_button.pressed.emit()
	check(save(owner, output.path_join("sale-service-paid-ui.json")) == FileAccess.get_file_as_bytes(source.path_join("sale-service-paid.json")), "paid-v2 native fuel purchase dropped specimen gross")
	shell.free(); owner = null
	await tree.process_frame
	for phase in ["sale-remote", "sale-recovered"]:
		var backend: Variant = restore(source.path_join(phase + ".json"))
		if backend == null: continue
		var terms: Dictionary = backend.get_freedom_specimen_sale()
		check(Buyer.valid_packet(terms) and not terms.can_sell and not backend.sell_freedom_specimens(terms.token) and save(backend, output.path_join(phase + "-refused.json")) == FileAccess.get_file_as_bytes(source.path_join(phase + ".json")), "remote/lost pouch created sale or rewrote earlier earnings")
		backend = null
	var report := {"schema": "apsis.specimen-sale-native/1", "completed": failures == 0, "failures": failures, "witnesses": witnesses, "scope": "Actual paused station and seated-flight service commands, fresh/stale/duplicate refusals, full C++ Save/Continue parity and paid-v2 fuel/enrollment. Generated contact/return/account/full-pouch fixtures have explicit initialization boundaries; not ordinary flown profitability, controller/manual/GPU acceptance."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "buyer report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
