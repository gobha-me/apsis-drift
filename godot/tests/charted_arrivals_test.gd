extends "res://tests/survey_commissions_test.gd"
## Called only by orbital_survey. All gameplay and fixture state remain C++ owned.
const Arrival = preload("res://scripts/native/charted_arrivals.gd")
var assertions := 0
var saves: Array = []
var live_hosts := 0

func check(value: bool, message: String) -> bool:
	assertions += 1
	return super.check(value, "Charted arrivals: " + message)

func witness(owner: Variant, output: String, name: String, expected: PackedByteArray) -> PackedByteArray:
	var path := output.path_join(name + ".json")
	var actual := save(owner, path)
	check(actual == expected, "whole Save differs: " + name)
	saves.append({"file": name + ".json", "sha256": FileAccess.get_sha256(path), "cpp_bytes_equal": actual == expected})
	return actual

func pad(device: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = JOY_BUTTON_A
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func mouse_click(tree: SceneTree, button: Control) -> void:
	var point: Vector2 = tree.root.get_final_transform() * button.get_global_rect().get_center()
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
	check(Arrival.valid_packet(packet), "actual five-field packet refused")
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check(not Arrival.valid_packet(missing), "missing packet field accepted: " + field)
		var wrong := packet.duplicate(true)
		wrong[field] = null
		check(not Arrival.valid_packet(wrong), "null packet field accepted: " + field)
	for change in [{"extra": true}, {"version": 1.0}, {"version": 0}, {"version": 2}, {"supported": 1}, {"enabled": "true"}, {"eligible": NAN}, {"note": ""}, {"note": "x".repeat(1025)}, {"note": "é".repeat(513)}, {"supported": false, "enabled": true}, {"supported": false, "eligible": true}]:
		var bad := packet.duplicate(true)
		bad.merge(change, true)
		check(not Arrival.valid_packet(bad), "malformed five-field packet accepted: " + str(change.keys()))
	for bad in [null, [], "packet", true, NAN]:
		check(not Arrival.valid_packet(bad), "non-dictionary packet accepted")
	for value in [null, 0, 1, 0.0, 1.0, NAN, INF, "", "true", "false", [], {}]:
		check(not owner.set_freedom_charted_arrivals(value), "non-BOOL setter Variant accepted")
	witness(owner, output, "strict-arguments", original)

func contains_request_four(value: Variant) -> bool:
	if value is Dictionary:
		if value.get("request") is Dictionary and value.request.get("version") == 4 and value.request.get("charted_arrival") is Dictionary and value.request.charted_arrival.get("version") == 1:
			return true
		for child in value.values():
			if contains_request_four(child): return true
	elif value is Array:
		for child in value:
			if contains_request_four(child): return true
	return false

func backend_readers(source: String, output: String, manifest: Dictionary) -> void:
	check(manifest.get("version") == 1 and manifest.get("completed") == true and manifest.get("failures") == 0 and manifest.get("phases") is Array and manifest.phases.size() == 14, "actual C++ scenario manifest unavailable")
	if failures: return
	for row: Dictionary in manifest.phases:
		var path := source.path_join(row.file)
		var original := FileAccess.get_file_as_bytes(path)
		var owner: Variant = restore(path)
		if owner == null: return
		var packet: Dictionary = owner.get_freedom_charted_arrivals()
		check(Arrival.valid_packet(packet) and packet.supported and packet.enabled == row.enabled and packet.eligible == row.eligible and packet.note == row.note, "Continue changed actual preference/eligibility/reason: " + row.file)
		for repeat in 3: check(owner.get_freedom_charted_arrivals() == packet, "repeated reader changed packet: " + row.file)
		var name: String = row.file.get_basename()
		witness(owner, output, name + "-reader", original)
		if name == "outbound-spool" or name == "outbound-committed":
			check(not owner.set_freedom_charted_arrivals(false), "active spool/transit accepted preference mutation")
			witness(owner, output, name + "-refused", original)
		if name == "origin-selected" or name == "unknown-primary-selected":
			check(packet.enabled and not packet.eligible and not packet.note.is_empty() and row.request_version == 2, "fallback did not retain truthful old-policy admission: " + name)
		if name == "preference-off-history" or name == "preference-on-history":
			check(row.retained_request_version == 4 and contains_request_four(JSON.parse_string(original.get_string_from_utf8())), "historical request4 absent after preference change: " + name)
		var continued: Variant = restore(output.path_join(name + "-reader.json"))
		if continued != null:
			check(continued.get_freedom_charted_arrivals() == packet, "second Continue reinterpreted preference: " + name)
			witness(continued, output, name + "-continued", original)
		continued = null
		owner = null

func chart_again(view: Control) -> void:
	view.show_instruments()
	view.chart_button.button_pressed = true
	view.observe_neutral_controls()
	view.refresh_charted_arrivals()

func refused_controls(tree: SceneTree, view: Control, owner: Variant, output: String, original: PackedByteArray) -> void:
	view.focused = false
	view.player_input.focused = false
	view.set_charted_arrivals(true)
	view.focused = true
	view.player_input.focused = true
	view.mode_change_pending = true
	view.set_charted_arrivals(true)
	view.mode_change_pending = false
	view.journey_dialog_open = true
	view.set_charted_arrivals(true)
	view.journey_dialog_open = false
	view.player_input.waiting_action = "forward"
	view.set_charted_arrivals(true)
	view.player_input.waiting_action = ""
	view.save_dialog.show()
	view.set_charted_arrivals(true)
	view.save_dialog.hide()
	for child: Control in [view.controls_menu.settings_view, view.controls_menu.basics, view.controls_menu.surface_reference_view, view.controls_menu.observations_view]:
		if child == null: continue
		child.show()
		view.set_charted_arrivals(true)
		child.hide()
	var held: int = int(view.player_input.bindings.forward.key.code)
	key(held, true)
	view.set_charted_arrivals(true)
	key(held, false)
	view.observe_neutral_controls()
	view.chart_button.button_pressed = false
	view.set_charted_arrivals(true)
	view.chart_button.button_pressed = true
	view.paused = false
	view.set_charted_arrivals(true)
	view.paused = true
	view.charted_arrival_packet = {}
	view.sync_charted_arrivals_ui()
	check(view.charted_arrival_button.disabled and not view.charted_arrival_button.button_pressed, "invalid closed packet left enabled or selected UI")
	view.set_charted_arrivals(true)
	view.refresh_charted_arrivals()
	witness(owner, output, "guarded-controls", original)
	# Open the actual paused parent office. Its hidden chart control cannot act.
	view.instruments_button.pressed.emit()
	view.observe_neutral_controls()
	view.open_survey_commissions()
	check(reader(view).visible, "actual commission modal did not open for isolation check")
	view.set_charted_arrivals(true)
	witness(owner, output, "office-modal-refused", original)
	reader(view).back.pressed.emit()
	chart_again(view)
	await tree.process_frame
	view.charted_arrival_button.grab_focus()
	pad(3, true)
	pad(3, false)
	witness(owner, output, "foreign-pad-refused", original)
	view.refresh_charted_arrivals()

func seated_controls(tree: SceneTree, source: String, assets: String, output: String) -> void:
	var shell := host(tree, source.path_join("seated-off.json"), assets)
	if shell == null: return
	live_hosts += 1
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	if not check(view is FlightView and view.paused, "seated fixture did not open the actual paused flight host"):
		shell.free(); return
	var off := FileAccess.get_file_as_bytes(source.path_join("seated-off.json"))
	var on := FileAccess.get_file_as_bytes(source.path_join("seated-on.json"))
	malformed(owner, owner.get_freedom_charted_arrivals(), output, off)
	# A synthetic selected device changes only the input test harness, not C++ state.
	view.player_input.available_pads[2] = true
	view.player_input.device = 2
	view.player_input.install()
	for frame in 3: await tree.process_frame
	var navigation: Button
	for child in view.controls_menu.save_button.get_parent().get_children():
		if child is Button and child.text == "Flight instruments & navigation": navigation = child
	if not check(navigation != null, "actual paused navigation entry missing"):
		shell.free(); return
	navigation.grab_focus()
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	check(view.instruments_open and view.hud_scroll.visible and not view.controls_menu.panel.visible and view.paused, "parsed keyboard did not open instruments from the actual paused menu")
	await tree.process_frame
	view.chart_button.grab_focus()
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	check(view.chart_button.button_pressed and view.charted_arrival_button.visible and not view.charted_arrival_button.disabled and not view.charted_arrival_button.button_pressed, "parsed keyboard did not open paused Navigation chart selection")
	witness(owner, output, "navigation-reader", off)
	await refused_controls(tree, view, owner, output, off)
	await tree.process_frame
	view.charted_arrival_button.grab_focus()
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	witness(owner, output, "keyboard-enabled", on)
	view.set_charted_arrivals(false)
	witness(owner, output, "keyboard-same-frame-refused", on)
	check(view.paused and view.charted_arrival_button.button_pressed and view.charted_arrival_packet.enabled, "keyboard toggle lost actual selected state or resumed")
	await tree.process_frame
	view.hud_scroll.ensure_control_visible(view.charted_arrival_button)
	await tree.process_frame
	mouse_click(tree, view.charted_arrival_button)
	witness(owner, output, "mouse-disabled", off)
	view.set_charted_arrivals(true)
	witness(owner, output, "mouse-same-frame-refused", off)
	await tree.process_frame
	view.charted_arrival_button.grab_focus()
	pad(2, true)
	pad(2, false)
	witness(owner, output, "selected-pad-enabled", on)
	view.set_charted_arrivals(false)
	witness(owner, output, "pad-same-frame-refused", on)
	var continued: Variant = restore(output.path_join("selected-pad-enabled.json"))
	if continued != null:
		check(continued.get_freedom_charted_arrivals() == owner.get_freedom_charted_arrivals(), "Continue lost UI-selected preference/eligibility")
		witness(continued, output, "selected-pad-continued", on)
	continued = null
	check(view.paused and view.charted_arrival_button.button_pressed and tree.root.gui_get_focus_owner() == view.charted_arrival_button, "selected pad toggle lost focus, selection or pause")
	shell.free(); owner = null
	await tree.process_frame

func arrival_reader(tree: SceneTree, source: String, assets: String, output: String) -> void:
	var path := source.path_join("outbound-arrived.json")
	var original := FileAccess.get_file_as_bytes(path)
	var shell := host(tree, path, assets)
	if shell == null: return
	live_hosts += 1
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	if check(view is FlightView and view.paused, "actual arrived fixture did not open flight reader"):
		chart_again(view)
		check(Arrival.valid_packet(view.charted_arrival_packet) and view.charted_arrival_packet.enabled and not view.charted_arrival_status.text.is_empty(), "actual arrived reader lost selected preference or truthful route reason")
		witness(owner, output, "arrived-flight-reader", original)
		view.instruments_button.pressed.emit()
		check(view.paused and not view.instruments_open and view.controls_menu.panel.visible, "Back to controls resumed the arrived flight")
		witness(owner, output, "arrived-back-reader", original)
	shell.free(); owner = null
	await tree.process_frame

func run(tree: SceneTree, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "witness folder unavailable"): return failures
	var source := fixtures.path_join("charted-arrivals")
	var manifest: Variant = JSON.parse_string(FileAccess.get_file_as_string(source.path_join("manifest.json")))
	if not check(manifest is Dictionary, "C++ scenario manifest cannot parse"): return failures
	var sources := {}
	for row: Dictionary in manifest.get("phases", []): sources[row.file] = FileAccess.get_sha256(source.path_join(row.file))
	backend_readers(source, output, manifest)
	if not failures: await seated_controls(tree, source, assets, output)
	if not failures: await arrival_reader(tree, source, assets, output)
	for name in sources: check(FileAccess.get_sha256(source.path_join(name)) == sources[name], "source fixture changed: " + str(name))
	if not failures: check(live_hosts == 2 and saves.size() == 44, "completed witness/host count differs from bounded scenario")
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if not check(file != null, "report unavailable"): return failures
	var report := {"schema": "apsis.charted-arrivals-native/1", "completed": failures == 0, "failures": failures, "assertions": assertions, "cpp_phase_count": manifest.get("phases", []).size(), "live_host_count": live_hosts, "witness_count": saves.size(), "witnesses": saves, "source_sha256": sources, "scope": "Actual paused flight Navigation chart with parsed keyboard, GUI mouse and synthetic selected/foreign pad events; exact complete C++ Save parity, strict BOOL/closed packets, guards and Continue. Backend readers cover actual committed request4, Origin and earned unknown-primary fallback, and retained history after toggles. At most two actual asset hosts; no manual controller, GPU qualification, flown survey return, profit, landing or capture claim."}
	file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
