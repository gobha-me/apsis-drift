extends "res://tests/survey_commissions_test.gd"
## Existing orbital family: actual C++ worlds, initialized fixture boundaries.
const Pace = preload("res://scripts/native/quiet_travel.gd")
var assertions := 0
var files: Dictionary = {}
var pairs: Array = []
var sources: Dictionary = {}
var inputs: Array = []

func check(value: bool, message: String) -> bool:
	assertions += 1
	return super.check(value, "Quiet travel: " + message)

func snapshot(owner: Variant, output: String, name: String) -> PackedByteArray:
	var path := output.path_join(name + ".json")
	var bytes := save(owner, path)
	files[name + ".json"] = FileAccess.get_sha256(path)
	check(owner.get_freedom_quiet_travel().get("multiplier") == 1, "successful Save did not return to normal speed")
	return bytes

func mode(owner: Variant) -> Dictionary:
	var value: Variant = owner.get_freedom_quiet_travel()
	check(Pace.valid_packet(value), "actual closed pacing packet refused")
	return value if value is Dictionary else {}

func record_mode(owner: Variant, output: String, name: String) -> void:
	var path := output.path_join(name + ".json")
	var file := FileAccess.open(path, FileAccess.WRITE)
	if not check(file != null, "mode witness unavailable"): return
	file.store_string(JSON.stringify(mode(owner), "\t") + "\n")
	file.close()
	files[name + ".json"] = FileAccess.get_sha256(path)

func reload(owner: Variant, source: String) -> bool:
	if not check(owner.stage_freedom_continue(source), "existing fixture Continue refused"): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return check(not pending.is_empty() and owner.commit_pending_freedom_start(pending.candidate_id), "Continue commit refused")

func fractions() -> PackedFloat64Array:
	var result := PackedFloat64Array()
	result.resize(12)
	result.fill(0.0)
	return result

func packet_units() -> void:
	var value := {"version": 1, "multiplier": 1, "available": true, "note": "Ready"}
	check(Pace.valid_packet(value) and Pace.text(value).is_empty(), "normal packet invents acceleration text")
	for rate in [2, 4]:
		value.multiplier = rate
		check(Pace.valid_packet(value) and str(rate) in Pace.text(value), "allowed target rate refused")
	for field in value:
		var bad := value.duplicate(true)
		bad.erase(field)
		check(not Pace.valid_packet(bad), "missing closed field accepted: " + field)
	# Engine String construction normalizes NUL; raw bytes remain a refused non-string.
	for change in [{"extra": true}, {"version": 1.0}, {"version": 0}, {"version": 2}, {"multiplier": true}, {"multiplier": 2.0}, {"multiplier": 0}, {"multiplier": 3}, {"multiplier": -1}, {"available": 1}, {"note": null}, {"note": ""}, {"note": "x".repeat(1025)}, {"note": "é".repeat(513)}, {"note": PackedByteArray([98, 97, 100, 0])}]:
		var bad := value.duplicate(true)
		bad.merge(change, true)
		check(not Pace.valid_packet(bad), "malformed packet accepted: " + str(change.keys()))
	for bad in [null, true, [], "packet", NAN]:
		check(not Pace.valid_packet(bad), "non-packet accepted")

func backend_checks(fixtures: String, output: String) -> void:
	var source := fixtures.path_join("commission-near.json")
	var original := FileAccess.get_file_as_bytes(source)
	var owner: Variant = restore(source)
	var peer: Variant = restore(source)
	if owner == null or peer == null: return
	check(mode(owner).multiplier == 1 and mode(owner).available, "fixture is not an eligible default-normal primary flight")
	check(snapshot(owner, output, "default-one") == original, "default1x changes existing Save")
	check(owner.set_freedom_quiet_travel(4), "eligible fourfold request refused")
	var selected := mode(owner)
	for value in [null, true, false, 1.0, 2.0, 4.0, NAN, INF, "2", [], {}, 0, -1, 3, 5, 9223372036854775807]:
		check(not owner.set_freedom_quiet_travel(value) and mode(owner) == selected, "invalid Variant/rate changed the request")
	var zero := fractions()
	for elapsed in [-1.0, NAN, INF, 60.01]:
		check(not owner.advance_freedom_flight(elapsed, zero, false) and mode(owner) == selected, "invalid elapsed changed pacing")
	for count in [0, 11, 13]:
		var bad := PackedFloat64Array()
		bad.resize(count)
		check(not owner.advance_freedom_flight(1.0 / 120.0, bad, false) and mode(owner) == selected, "invalid dimension changed pacing")
	for value in [-0.1, 1.1, NAN, INF]:
		var bad := fractions()
		bad[0] = value
		check(not owner.advance_freedom_flight(1.0 / 120.0, bad, false) and mode(owner) == selected, "invalid control changed pacing")
	check(not owner.stage_freedom_continue("") and mode(owner) == selected, "refused Continue changed request")
	record_mode(owner, output, "strict-mode-retained")
	check(snapshot(owner, output, "strict-refused") == original, "refused arguments changed whole Save")
	check(owner.set_freedom_quiet_travel(4), "paused request refused")
	check(owner.advance_freedom_flight(60.0, zero, true) and mode(owner).multiplier == 4, "paused poll erased explicit selection")
	check(owner.advance_freedom_flight(0.0, zero, false) and mode(owner).multiplier == 4, "zero elapsed consumed request")
	record_mode(owner, output, "paused-four")
	check(snapshot(owner, output, "paused-save") == original, "paused/zero elapsed accumulated simulation time")
	for row in [{"rate": 1, "elapsed": 1.0 / 120.0, "ticks": 1, "name": "one"}, {"rate": 2, "elapsed": 1.0 / 120.0, "ticks": 2, "name": "two"}, {"rate": 4, "elapsed": 1.0 / 120.0, "ticks": 4, "name": "four"}, {"rate": 4, "elapsed": 1.0 / 30.0, "ticks": 15, "name": "budget"}, {"rate": 4, "elapsed": 0.125, "ticks": 15, "name": "full-budget"}]:
		if not reload(owner, source) or not reload(peer, source): break
		var start: Dictionary = owner.get_freedom_flight_state()
		check(owner.set_freedom_quiet_travel(row.rate), "paced comparison selection refused")
		check(owner.advance_freedom_flight(row.elapsed, zero, false), "paced comparison refused")
		for tick in row.ticks:
			check(peer.advance_freedom_flight(1.0 / 120.0, zero, false), "independent ordinary comparison refused")
		var end: Dictionary = owner.get_freedom_flight_state()
		check(int(end.tick) - int(start.tick) == row.ticks and row.ticks <= 15, "requested pacing changed tick/work bound")
		check(owner.advance_freedom_flight(0.0, zero, false) and owner.get_freedom_flight_state().tick == end.tick, "bonus work banked a later tick")
		var paced_file: String = "paced-" + row.name
		var peer_file: String = "ordinary-" + row.name
		var actual := snapshot(owner, output, paced_file)
		var ordinary := snapshot(peer, output, peer_file)
		var equal := actual == ordinary
		check(equal, "paced world differs from independent ordinary ticks")
		if row.rate == 1:
			check(actual == FileAccess.get_file_as_bytes(fixtures.path_join("commission-complete.json")), "default one tick differs from prior C++ source")
		pairs.append({"rate": row.rate, "ticks": row.ticks, "paced": paced_file + ".json", "ordinary": peer_file + ".json", "bytes_equal": equal})
	# Continue never restores a pacing request; successful Save has already reset it.
	check(reload(owner, output.path_join("paced-four.json")) and mode(owner).multiplier == 1, "Continue retained transient pace")
	check(snapshot(owner, output, "continued-normal") == FileAccess.get_file_as_bytes(output.path_join("paced-four.json")), "Continue changed paced voyage")
	for phase in ["commission-station", "commission-returned", "charted-arrivals/outbound-spool"]:
		var path := fixtures.path_join(phase + ".json")
		if not reload(owner, path): continue
		check(not mode(owner).available and mode(owner).multiplier == 1 and not owner.set_freedom_quiet_travel(4), "constrained state accepted fast pacing: " + phase)
		check(owner.set_freedom_quiet_travel(1), "normal cancellation unavailable in constrained state")
		check(snapshot(owner, output, "guard-" + phase.replace("/", "-")) == FileAccess.get_file_as_bytes(path), "constraint reader/refusal changed Save")
	owner = null
	peer = null

func pad(device: int, button: int, down: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = button
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func mouse_at(point: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	Input.parse_input_event(motion)
	Input.flush_buffered_events()
	for down in [true, false]:
		var event := InputEventMouseButton.new()
		event.position = point
		event.global_position = point
		event.button_index = MOUSE_BUTTON_LEFT
		event.button_mask = MOUSE_BUTTON_MASK_LEFT if down else 0
		event.pressed = down
		Input.parse_input_event(event)
		Input.flush_buffered_events()

func open_choice(tree: SceneTree, view: Control) -> PopupMenu:
	view.hud_scroll.ensure_control_visible(view.quiet_travel_select)
	await tree.process_frame
	view.quiet_travel_select.grab_focus()
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	await tree.process_frame
	var popup: PopupMenu = view.quiet_travel_select.get_popup()
	check(popup.visible and popup.item_count == 3, "real keyboard did not open three-preset control")
	return popup

func ui_checks(tree: SceneTree, fixtures: String, assets: String, output: String) -> void:
	var old_size := tree.root.size
	var old_scale := tree.root.content_scale_size
	tree.root.size = Vector2i(1280, 720)
	tree.root.content_scale_size = Vector2i(1280, 720)
	var source := fixtures.path_join("commission-near.json")
	var original := FileAccess.get_file_as_bytes(source)
	var shell := host(tree, source, assets)
	if shell == null: return
	var view: Control = shell.current_view
	var owner: Variant = shell.bridge
	var original_tick: String = owner.get_freedom_flight_state().tick
	if not check(view is FlightView and view.paused, "actual flight host unavailable"):
		shell.free(); return
	# Synthetic selected-device admission is input harness state only.
	view.player_input.available_pads[2] = true
	view.player_input.device = 2
	view.player_input.bindings.forward.key.code = KEY_F7
	view.player_input.bindings.backward.key.code = KEY_F8
	view.player_input.install()
	for frame in 3: await tree.process_frame
	var navigation: Button
	for child in view.controls_menu.save_button.get_parent().get_children():
		if child is Button and child.text == "Flight instruments & navigation": navigation = child
	if not check(navigation != null, "navigation entry unavailable"):
		shell.free(); return
	navigation.grab_focus()
	key(KEY_SPACE, true); key(KEY_SPACE, false)
	await tree.process_frame
	check(view.instruments_open and view.quiet_travel_select.visible and not view.quiet_travel_select.disabled, "pacing control absent from actual instruments")
	var popup: PopupMenu = await open_choice(tree, view)
	key(KEY_DOWN, true); key(KEY_DOWN, false)
	key(KEY_DOWN, true); key(KEY_DOWN, false)
	key(KEY_ENTER, true); key(KEY_ENTER, false)
	await tree.process_frame
	check(mode(owner).multiplier == 4 and view.quiet_travel_select.get_selected_id() == 4, "keyboard selection did not reach backend")
	inputs.append("keyboard-four")
	view._process(1.0 / 120.0)
	check(mode(owner).multiplier == 4 and owner.get_freedom_flight_state().tick == original_tick, "paused polling erased request or advanced the clock")
	record_mode(owner, output, "keyboard-four-mode")
	check(snapshot(owner, output, "keyboard-paused") == original, "paused keyboard selection changed voyage")
	view.refresh_quiet_travel()
	await tree.process_frame
	view.hud_scroll.ensure_control_visible(view.quiet_travel_select)
	await tree.process_frame
	mouse_at(tree.root.get_final_transform() * view.quiet_travel_select.get_global_rect().get_center())
	await tree.process_frame
	popup = view.quiet_travel_select.get_popup()
	if check(popup.visible and popup.item_count == 3, "mouse did not open preset menu"):
		# Three single-line, uniform font rows, without icons or separators.
		# PopupMenu exposes Window geometry and panel margins, not item rectangles.
		var panel: StyleBox = popup.get_theme_stylebox("panel")
		var top := panel.get_content_margin(SIDE_TOP)
		var bottom := panel.get_content_margin(SIDE_BOTTOM)
		var row_height := (float(popup.size.y) - top - bottom) / 3.0
		mouse_at(Vector2(popup.position) + Vector2(float(popup.size.x) * 0.5, top + 1.5 * row_height))
	await tree.process_frame
	check(mode(owner).multiplier == 2 and view.quiet_travel_select.get_selected_id() == 2, "actual mouse row selection did not reach backend")
	inputs.append("mouse-two")
	record_mode(owner, output, "mouse-two-mode")
	check(snapshot(owner, output, "mouse-paused") == original, "mouse selection changed voyage")
	view.refresh_quiet_travel()
	await tree.process_frame
	view.quiet_travel_select.grab_focus()
	pad(2, JOY_BUTTON_A, true); pad(2, JOY_BUTTON_A, false)
	await tree.process_frame
	popup = view.quiet_travel_select.get_popup()
	check(popup.visible, "selected pad did not open menu")
	pad(2, JOY_BUTTON_DPAD_DOWN, true); pad(2, JOY_BUTTON_DPAD_DOWN, false)
	pad(2, JOY_BUTTON_DPAD_DOWN, true); pad(2, JOY_BUTTON_DPAD_DOWN, false)
	await tree.process_frame
	pad(99, JOY_BUTTON_A, true); pad(99, JOY_BUTTON_A, false)
	await tree.process_frame
	check(popup.visible and mode(owner).multiplier == 1, "foreign pad operated open popup")
	pad(2, JOY_BUTTON_A, true); pad(2, JOY_BUTTON_A, false)
	await tree.process_frame
	check(mode(owner).multiplier == 4, "selected pad did not select requested rate")
	inputs.append("selected-pad-four/foreign-pad-refused")
	record_mode(owner, output, "pad-four-mode")
	check(snapshot(owner, output, "pad-paused") == original, "selected pad selection changed voyage")
	view.refresh_quiet_travel()
	# Guard calls inject only presentation/modal state, never authoritative state.
	for guard in ["journey_dialog_open", "mode_change_pending", "recovery_notified"]:
		view.set(guard, true)
		view.quiet_travel_command(4)
		view.set(guard, false)
		check(mode(owner).multiplier == 1, "covered control accepted selection: " + guard)
	view.player_input.waiting_action = "forward"
	view.quiet_travel_command(4)
	view.player_input.waiting_action = ""
	check(mode(owner).multiplier == 1, "remap waiting accepted selection")
	view.save_dialog.show()
	view.quiet_travel_command(4)
	view.save_dialog.hide()
	check(mode(owner).multiplier == 1, "Save modal accepted selection")
	key(KEY_F7, true); key(KEY_F8, true)
	view.quiet_travel_command(4)
	check(mode(owner).multiplier == 1, "opposing held mapped controls enabled pacing")
	key(KEY_F7, false); key(KEY_F8, false)
	view.observe_neutral_controls()
	await tree.process_frame
	view.refresh_quiet_travel()
	view.quiet_travel_command(4)
	check(mode(owner).multiplier == 4, "neutral eligible selection refused")
	view.toggle_pause()
	check(not view.paused and mode(owner).multiplier == 4, "normal Resume lost explicit paused selection")
	key(KEY_F7, true); key(KEY_F8, true)
	check(mode(owner).multiplier == 1, "opposing running controls did not cancel pacing")
	key(KEY_F7, false); key(KEY_F8, false)
	view.pause_controls("Pacing lifecycle proof")
	view.show_instruments()
	view.observe_neutral_controls()
	await tree.process_frame
	view.refresh_quiet_travel()
	view.quiet_travel_command(4)
	check(mode(owner).multiplier == 4, "focus proof setup refused")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	check(view.paused and mode(owner).multiplier == 1, "focus loss failed to cancel request")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.show_instruments()
	view.observe_neutral_controls()
	await tree.process_frame
	view.refresh_quiet_travel()
	view.quiet_travel_command(4)
	check(mode(owner).multiplier == 4, "teardown proof setup refused")
	shell.free()
	await tree.process_frame
	check(mode(owner).multiplier == 1, "scene teardown retained pacing")
	check(snapshot(owner, output, "lifecycle-cancelled") == original, "input/lifecycle checks changed world")
	owner = null
	tree.root.size = old_size
	tree.root.content_scale_size = old_scale
	await tree.process_frame

func run(tree: SceneTree, fixtures: String, assets: String, output: String) -> int:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "witness directory unavailable"): return failures
	for phase in ["commission-near", "commission-complete", "commission-station", "commission-returned", "charted-arrivals/outbound-spool"]:
		var path := fixtures.path_join(phase + ".json")
		check(FileAccess.file_exists(path), "missing existing C++ phase: " + phase)
		sources[phase + ".json"] = FileAccess.get_sha256(path)
	if failures: return failures
	packet_units()
	backend_checks(fixtures, output)
	if failures: return failures
	await ui_checks(tree, fixtures, assets, output)
	for phase in sources:
		check(FileAccess.get_sha256(fixtures.path_join(phase)) == sources[phase], "test rewrote original C++ phase")
	var report := {"schema": "apsis.quiet-travel-native/1", "completed": failures == 0, "failures": failures, "assertions": assertions, "sources": sources, "files": files, "pairs": pairs, "inputs": inputs, "scope": "Existing initialized C++ orbital boundaries, independent ordinary backend ticks, real paused UI inputs and whole Save/Continue; no new flown voyage, hardware, GPU or throughput claim."}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "report unavailable"): file.store_string(JSON.stringify(report, "\t") + "\n")
	return failures
