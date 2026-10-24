extends "res://tests/survey_commissions_test.gd"
## Existing impact case only. Damage/repair and every Save oracle remain C++ owned.
const Repair = preload("res://scripts/native/origin_repair.gd")
const Impact = preload("res://scripts/native/native_impact_view.gd")
var assertions := 0
var proof: Array = []

func check(value: bool, message: String) -> bool:
	assertions += 1
	return super.check(value, "Origin repair: " + message)

func repair_reader(view: Control) -> Control:
	return view.origin_repair_view if view is WalkView else view.controls_menu.origin_repair_view

func record(owner: Variant, output: String, label: String, expected: PackedByteArray) -> void:
	var path := output.path_join(label + ".json")
	check(save(owner, path) == expected, "complete Save differs from actual C++ oracle: " + label)
	proof.append({"file": label + ".json", "sha256": FileAccess.get_sha256(path), "cpp_bytes_equal": FileAccess.get_file_as_bytes(path) == expected})

func pad(device: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = JOY_BUTTON_A
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func click(tree: SceneTree, button: Control) -> void:
	var point := tree.root.get_final_transform() * button.get_global_rect().get_center()
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	for down: bool in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = down
		event.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		event.position = point
		event.global_position = point
		Input.parse_input_event(event)
		Input.flush_buffered_events()

func malformed(owner: Variant, packet: Dictionary, output: String, original: PackedByteArray) -> void:
	check(Repair.valid_packet(packet), "actual closed17 quote refused")
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check(not Repair.valid_packet(missing), "missing closed quote field accepted: " + field)
		var wrong := packet.duplicate(true)
		wrong[field] = null
		check(not Repair.valid_packet(wrong), "null quote field accepted: " + field)
	for patch in [{"extra": true}, {"version": 1.0}, {"version": 2}, {"supported": 1}, {"at_origin": "yes"}, {"paid_selected": 1}, {"can_repair": 0}, {"forebody": 4}, {"landing_structure": -1}, {"severity_units": 7}, {"severity_units": 0}, {"tick": "01"}, {"tick": "18446744073709551616"}, {"unit_credits": "11"}, {"repair_credits": "1"}, {"cash_before": NAN}, {"cash_after": "1"}, {"berth_credits": "1"}, {"token": ""}, {"token": "x".repeat(4097)}, {"note": ""}, {"note": "é".repeat(513)}, {"terms": ""}, {"terms": "x".repeat(4097)}, {"supported": false, "can_repair": false}]:
		var bad := packet.duplicate(true)
		bad.merge(patch, true)
		check(not Repair.valid_packet(bad), "malformed quote accepted: " + str(patch.keys()))
	for value in [null, [], "packet", true, NAN]: check(not Repair.valid_packet(value), "non-dictionary quote accepted")
	record(owner, output, "arguments-before", original)
	for value in [null, 0, 1, 1.0, NAN, INF, [], {}, PackedByteArray([120, 0]), "", "foreign", "x".repeat(4097), "é".repeat(2049)]:
		check(not owner.repair_freedom_origin(value), "malformed/foreign token Variant accepted")
	record(owner, output, "arguments-after", original)

func refused_controls(view: Control, owner: Variant, output: String, label: String, original: PackedByteArray) -> void:
	var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
	var page := repair_reader(view)
	record(owner, output, label + "-guards-before", original)
	view.focused = false
	provider.focused = false
	view.origin_repair_command("repair")
	view.focused = true
	provider.focused = true
	view.mode_change_pending = true
	view.origin_repair_command("repair")
	view.mode_change_pending = false
	view.journey_dialog_open = true
	view.origin_repair_command("repair")
	view.journey_dialog_open = false
	provider.waiting_action = "forward"
	view.origin_repair_command("repair")
	provider.waiting_action = ""
	view.save_dialog.show()
	view.origin_repair_command("repair")
	view.save_dialog.hide()
	var settings: Control = view.settings_view if view is WalkView else view.controls_menu.settings_view
	settings.show()
	view.origin_repair_command("repair")
	settings.hide()
	var movement: int = int(provider.foot_key_codes()[0]) if view is WalkView else int(provider.bindings.forward.key.code)
	key(movement, true)
	view.origin_repair_command("repair")
	key(movement, false)
	view.observe_neutral_controls()
	page.repair_button.grab_focus()
	pad(3, true)
	pad(3, false)
	view.survey_commission_command("accept")
	view.survey_commission_command("abandon")
	# Pause toggles act as Back within a nested reader, keeping simulation paused.
	view.toggle_pause()
	check(view.paused and not view.origin_repair_open(), "modal Back resumed simulation or retained the repair reader")
	record(owner, output, label + "-guards-after", original)
	view.observe_neutral_controls()
	view.open_origin_repair()
	check(view.paused and view.origin_repair_open() and page.packet == owner.get_freedom_origin_repair(), "repair quote did not reopen paused after Back")
	page.packet.token += "x"
	view.origin_repair_command("repair")
	check(page.packet == owner.get_freedom_origin_repair(), "stale displayed quote did not refresh without confirmation")
	record(owner, output, label + "-stale", original)

func repaired_state(owner: Variant, before: Dictionary, view: Control) -> void:
	var after: Dictionary = owner.get_freedom_flight_state()
	if not check(Impact.valid(after.impact, after.craft_id, after.frame_id, after.frame_version) and after.impact.version == 2 and after.impact.forebody == 0 and after.impact.landing_structure == 0, "actual repair lacks a valid v2 current condition"): return
	check(after.tick == before.tick and after.craft_id == before.craft_id and after.resources == before.resources and after.impact.latest == before.impact.latest, "repair advanced time, replaced craft, refueled or erased the impact")
	check(after.impact.repair.boundary_serial == after.impact.latest.serial and "repaired" in Impact.feedback(after.impact) and "history" in Impact.feedback(after.impact), "retained impact is still announced as current damage")
	if view is FlightView:
		check(not view.impact_overlay.visible and not view.impact_overlay.reduced_view, "repaired cockpit retained old cracks or visibility reduction")
	for field in after.impact.repair:
		var bad: Dictionary = after.impact.duplicate(true)
		bad.repair.erase(field)
		check(not Impact.valid(bad, after.craft_id, after.frame_id, after.frame_version), "missing repaired-impact summary field accepted")
	for patch in [{"extra": true}, {"tick": "18446744073709551616"}, {"boundary_serial": "0"}, {"paid": 1}, {"credits": "-1"}]:
		var bad: Dictionary = after.impact.duplicate(true)
		bad.repair.merge(patch, true)
		check(not Impact.valid(bad, after.craft_id, after.frame_id, after.frame_version), "malformed repaired-impact summary accepted")

func run_repair(tree: SceneTree, shell: Control, owner: Variant, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "proof directory unavailable"): return failures
	var manifest_path := fixtures.path_join("repair-scope.json")
	var manifest: Variant = JSON.parse_string(FileAccess.get_file_as_string(manifest_path))
	if not check(manifest is Dictionary and manifest.get("schema") == "apsis.origin-repair-scenario/1" and manifest.get("phases") is Array and manifest.phases.size() == 10, "actual C++ repair sources/provenance unavailable"): return failures
	check("explicitly initialized" in manifest.scope and "not a flown damaged return" in manifest.scope, "initialized station/financial boundary was not disclosed")
	var hashes := {"repair-scope.json": FileAccess.get_sha256(manifest_path)}
	for row: Dictionary in manifest.phases:
		var path := fixtures.path_join(row.file)
		hashes[row.file] = FileAccess.get_sha256(path)
		var backend: Variant = restore(path)
		if backend == null: return failures
		var quote: Dictionary = backend.get_freedom_origin_repair()
		check(Repair.valid_packet(quote) and quote.tick == row.tick and quote.paid_selected == row.paid and quote.can_repair == row.can_repair and quote.cash_before == row.cash, "source Continue changed current repair quote: " + row.file)
		for repeat in 3: check(backend.get_freedom_origin_repair() == quote, "repair query changed account or condition")
		var original := FileAccess.get_file_as_bytes(path)
		if not quote.can_repair:
			check(not backend.repair_freedom_origin(quote.token), "unavailable/pristine/pending/insufficient source accepted repair")
		if row.file == "repair-pending.json":
			var state: Dictionary = backend.get_freedom_flight_state()
			check(state.impact.version == 2 and state.impact.pending_loss and Impact.valid(state.impact, state.craft_id, state.frame_id, state.frame_version), "legitimate pending repaired history is not presentable")
		record(backend, output, row.file.get_basename() + "-reader", original)
		backend = null
	# Reuse the caller's one asset host; original legacy impact proof is complete.
	var previous_mode: int = shell.process_mode
	var previous_size := tree.root.size
	shell.process_mode = Node.PROCESS_MODE_ALWAYS
	var readability: Array = []
	var cases := [
		{"name": "keyboard-station", "source": "repair-walk-free", "result": "repair-walk-free-done", "gesture": "keyboard", "size": Vector2i(640, 450)},
		{"name": "mouse-flight-paid", "source": "repair-flight-paid", "result": "repair-flight-paid-done", "gesture": "mouse", "size": Vector2i(1280, 720)},
		{"name": "pad-flight-free", "source": "repair-flight-free", "result": "repair-flight-free-done", "gesture": "pad", "size": Vector2i(1280, 720)}]
	for row: Dictionary in cases:
		tree.root.size = row.size
		var source := fixtures.path_join(row.source + ".json")
		if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "actual service host refused source: " + shell.error): break
		var view: Control = shell.current_view
		view.set_process(false)
		view.focused = true
		var provider: Variant = view.preferences_provider if view is WalkView else view.player_input
		provider.focused = true
		provider.persist = false
		var old_device: int = provider.device
		var old_pads: Dictionary = provider.available_pads.duplicate()
		provider.available_pads[2] = true
		provider.device = 2
		provider.install()
		var old_walk_pad: int = view.selected_pad if view is WalkView else -1
		var old_walk_pads: Dictionary = view.available_pads.duplicate() if view is WalkView else {}
		if view is WalkView:
			view.selected_pad = 2
			view.available_pads[2] = true
		view.pause_controls("Review existing Origin repair fixture.")
		view.observe_neutral_controls()
		for frame in 3: await tree.process_frame
		view.open_origin_repair()
		var page := repair_reader(view)
		if not check(view.origin_repair_open() and Repair.valid_packet(page.packet) and page.packet.can_repair and view.paused, "independent paused service reader did not open"): break
		var original := FileAccess.get_file_as_bytes(source)
		var expected := FileAccess.get_file_as_bytes(fixtures.path_join(row.result + ".json"))
		record(owner, output, row.name + "-reader", original)
		if row.gesture == "keyboard": malformed(owner, page.packet, output, original)
		refused_controls(view, owner, output, row.name, original)
		key(KEY_ESCAPE, true)
		key(KEY_ESCAPE, false)
		check(view.paused and not view.origin_repair_open(), "parsed Back closed controls or resumed simulation")
		record(owner, output, row.name + "-back", original)
		view.observe_neutral_controls()
		for frame in 2: await tree.process_frame
		view.open_origin_repair()
		page = repair_reader(view)
		page.repair_button.grab_focus()
		page.scroll.ensure_control_visible(page.repair_button)
		for frame in 2: await tree.process_frame
		var density := tree.root.get_final_transform().get_scale().abs()
		var font_pixels: float = page.readable_theme.default_font_size * minf(density.x, density.y)
		var button_pixels: float = page.repair_button.size.y * minf(density.x, density.y)
		check(is_finite(font_pixels) and font_pixels >= 18.0 and is_finite(button_pixels) and button_pixels >= 40.0, "repair page font/button is too small in physical pixels")
		readability.append({"input": row.gesture, "viewport_pixels": [row.size.x, row.size.y], "font_pixels": font_pixels, "button_pixels": button_pixels})
		var before: Dictionary = owner.get_freedom_flight_state()
		if row.gesture == "keyboard":
			key(KEY_SPACE, true)
			record(owner, output, "keyboard-accept-held", original)
			key(KEY_SPACE, false)
		elif row.gesture == "mouse": click(tree, page.repair_button)
		else:
			pad(2, true)
			pad(2, false)
		record(owner, output, row.name + "-repaired", expected)
		repaired_state(owner, before, view)
		view.origin_repair_command("repair")
		record(owner, output, row.name + "-duplicate", expected)
		var continued: Variant = restore(output.path_join(row.name + "-repaired.json"))
		if continued != null:
			check(continued.get_freedom_origin_repair() == owner.get_freedom_origin_repair(), "repair Continue changed account or current quote")
			record(continued, output, row.name + "-continued", expected)
		continued = null
		page.back.pressed.emit()
		check(view.paused and not view.origin_repair_open(), "service Back did not remain paused")
		provider.device = old_device
		provider.available_pads = old_pads
		if view is WalkView:
			view.selected_pad = old_walk_pad
			view.available_pads = old_walk_pads
		provider.install()
		await tree.process_frame
	tree.root.size = previous_size
	shell.process_mode = previous_mode
	for name in hashes: check(FileAccess.get_sha256(fixtures.path_join(name)) == hashes[name], "repair proof overwrote an authoritative source")
	var report := {"schema": "apsis.origin-repair-native/1", "completed": failures == 0, "failures": failures, "assertions": assertions, "source_sha256": hashes, "readability": readability, "witnesses": proof, "scope": "Actual existing impact sources and C++ free/paid repair outputs, same-clock quote/read/refusal/confirmation/Continue, one reused asset host, parsed keyboard/mouse/synthetic selected-pad and modal/neutral/Back guards. Origin attachment and aggregate account boundaries are explicitly initialized; not a flown damaged return, ordinary earnings/debt route or manual hardware/GPU acceptance. No independent physics or money rules."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "repair proof report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
