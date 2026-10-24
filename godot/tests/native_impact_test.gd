extends SceneTree
## Real versioned C++ impact fixtures and the ordinary native presenter.
## Optional software/windowed captures are bounded evidence, not FPS or
## pressure-loss qualification. Headless checks never claim visible pixels.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const ImpactView = preload("res://scripts/native/native_impact_view.gd")
const RepairChecks = preload("res://tests/origin_repair_test.gd")
var failures := 0
var captures: Array = []

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failures += 1
	return value

func _initialize() -> void:
	call_deferred("run")

func save_matches(owner: Variant, source: String, output: String) -> bool:
	return check(owner.save_freedom_as(output) and FileAccess.get_sha256(output) == FileAccess.get_sha256(source), "Complete impact Save differs from authoritative fixture: " + source.get_file())

func recovery_button(node: Node) -> Button:
	if node is Button and node.text == "Continue at Origin Station": return node
	for child in node.get_children():
		var found := recovery_button(child)
		if found != null: return found
	return null

func live_loss_case(shell: Control, owner: Variant, fixtures: String, assets: String, output: String, label: String, elapsed: float) -> void:
	var source := fixtures.path_join("catastrophic-before.json")
	var source_hash := FileAccess.get_sha256(source)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), label + ": actual pre-impact Continue refused: " + shell.error): return
	var view: Control = shell.current_view
	view.set_process(false)
	view.player_input.persist = false
	if not check(view is FlightView and view.paused and view.state.impact.selected and not view.state.impact.pending_loss and view.state.impact.latest.is_empty(), label + ": source is not the real pre-loss flight"): return
	save_matches(owner, source, output.path_join(label + "-before.json"))
	view.toggle_pause()
	if not check(not view.paused, label + ": neutral explicit flight Resume refused"): return
	for frame in 32:
		view._process(elapsed)
		if not view.error.is_empty() or owner.get_freedom_recovery_state().get("pending", false): break
	if not check(view.error.is_empty() and owner.get_freedom_recovery_state().get("pending", false), label + ": live impact batch rolled back the real loss or entered a fatal presenter error: " + view.error): return
	var loss: Dictionary = owner.get_freedom_flight_state()
	save_matches(owner, fixtures.path_join("catastrophic-pending.json"), output.path_join(label + "-pending.json"))
	if not check(shell.recovery_overlay != null, label + ": committed live loss did not open standard recovery"): return
	check(shell.recovery_overlay.get_parent() == shell.recovery_layer and shell.recovery_layer.layer > view.controls_menu.layer and not view.controls_menu.panel.visible, label + ": recovery is obscured by the flight controls canvas")
	check(view.paused and not view.player_input.enabled and view.exhaust.intensity == 0.0 and view.exhaust.withdrawal_intensity == 0.0 and view.exhaust.lift_intensity == 0.0, label + ": pending recovery retained active controls or exhaust")
	var overlay_id: int = shell.recovery_overlay.get_instance_id()
	var phase: float = view.exhaust.phase
	view.toggle_pause()
	view.recovery_requested.emit()
	var held := InputEventKey.new()
	held.physical_keycode = KEY_W
	held.pressed = true
	Input.parse_input_event(held)
	view._process(0.5)
	var released := InputEventKey.new()
	released.physical_keycode = KEY_W
	released.pressed = false
	Input.parse_input_event(released)
	view._process(0.5)
	check(view.error.is_empty() and view.paused and owner.get_freedom_flight_state() == loss and view.exhaust.phase == phase and shell.recovery_overlay.get_instance_id() == overlay_id, label + ": waiting for recovery advanced the loss, failed, or replaced its overlay")
	save_matches(owner, fixtures.path_join("catastrophic-pending.json"), output.path_join(label + "-waiting.json"))
	var resume := recovery_button(shell.recovery_overlay)
	if not check(resume != null, label + ": standard recovery has no actual Continue action"): return
	resume.pressed.emit()
	resume.pressed.emit()
	await process_frame
	var fresh: Dictionary = owner.get_freedom_flight_state()
	if not check(shell.current_view is WalkView and shell.current_view.paused and shell.recovery_overlay == null and fresh.craft_id != loss.craft_id and fresh.impact.selected and fresh.impact.forebody == 0 and fresh.impact.landing_structure == 0 and fresh.impact.latest.is_empty() and not fresh.impact.pending_loss, label + ": live recovery did not produce the ordinary paused fresh station actor"): return
	check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, 0.0, 0.0])), label + ": recovered station clock could not advance")
	save_matches(owner, fixtures.path_join("recovered.json"), output.path_join(label + "-recovered.json"))
	var walk: Dictionary = owner.get_freedom_walk_state()
	check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([1.0, 0.0, 0.0])) and owner.get_freedom_walk_state().foot_position_metres != walk.foot_position_metres and owner.get_freedom_flight_state().craft_id == fresh.craft_id, label + ": replacement actor cannot actually walk")
	check(owner.save_freedom_as(output.path_join(label + "-walking.json")), label + ": recovered walking Save As refused")
	check(FileAccess.get_sha256(source) == source_hash, label + ": live recovery rewrote the original pre-impact source")

func live_loss(fixtures: String, assets: String, output: String, label: String, elapsed: float) -> void:
	# Isolate old-code refusals so each cadence reports its own real failure.
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	await live_loss_case(shell, owner, fixtures, assets, output, label, elapsed)
	shell.free()
	shell = null
	owner = null
	for frame in 2:
		await process_frame
		if DisplayServer.get_name() != "headless": await RenderingServer.frame_post_draw

func capture_cue(view: Control, owner: Variant, source: String, output: String, label: String) -> void:
	if DisplayServer.get_name() == "headless": return
	view.paused = true
	view.cockpit = true
	view.look_offset = Vector2.ZERO
	view.controls_menu.hide_menu()
	view.update_view(0.0)
	view.set_process(false)
	view.set_process_input(false)
	var before: Dictionary = owner.get_freedom_flight_state()
	for frame in 12:
		await process_frame
	await RenderingServer.frame_post_draw
	var image: Image = root.get_texture().get_image()
	if not check(image != null and not image.is_empty() and image.get_width() == root.size.x and image.get_height() == root.size.y, "Impact capture dimensions are invalid"):
		return
	var path := output.path_join(label + ".png")
	check(image.save_png(path) == OK, "Impact cue capture cannot be saved")
	check(owner.get_freedom_flight_state() == before, "Rendering a crack advanced or edited authoritative condition")
	save_matches(owner, source, output.path_join(label + "-after-capture.json"))
	captures.append({"file": path.get_file(), "sha256": FileAccess.get_sha256(path), "pixels": [image.get_width(), image.get_height()], "fixture_sha256": FileAccess.get_sha256(source), "impact": JSON.parse_string(JSON.stringify(before.impact)), "scope": "actual saved cockpit cue; renderer/software capture, not FPS or pressure loss"})

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"):
		quit(1)
		return
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Impact output directory unavailable"):
		quit(1)
		return
	for label in ["unselected", "selected-clear", "forebody1", "forebody2", "landing-damaged", "catastrophic-before", "catastrophic-pending", "recovered"]:
		if not check(FileAccess.file_exists(args[0].path_join(label + ".json")), "Missing actual C++ impact fixture: " + label):
			quit(1)
			return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	# Validate malformed projections before constructing a scene or capture.
	check(owner.stage_freedom_continue(args[0].path_join("forebody1.json")), "Actual forebody source could not stage")
	var pending: Dictionary = owner.get_pending_freedom_start()
	if not check(not pending.is_empty(), "Impact pending source unavailable"):
		quit(1)
		return
	var projection: Dictionary = pending.flight_state
	check(ImpactView.valid(projection.impact, projection.craft_id, projection.frame_id, projection.frame_version) and not projection.impact.latest.is_empty(), "Actual damage has no valid authoritative receipt")
	for damage in ["version", "craft", "frame", "severity", "speed", "point", "fraction", "serial"]:
		var invalid: Dictionary = projection.impact.duplicate(true)
		match damage:
			"version": invalid.version = 2
			"craft": invalid.craft_id = "0" if projection.craft_id != "0" else "1"
			"frame": invalid.frame_version = 999
			"severity": invalid.forebody = 4
			"speed": invalid.latest.closing_speed = NAN
			"point": invalid.latest.contact_point_craft = PackedFloat64Array([0.0, INF, 0.0])
			"fraction": invalid.latest.fraction = 2.0
			"serial": invalid.latest.serial = "18446744073709551616"
		check(not ImpactView.valid(invalid, projection.craft_id, projection.frame_id, projection.frame_version), "Malformed condition projection accepted: " + damage)
	check(owner.discard_pending_freedom_start(pending.candidate_id), "Impact CPU preflight discard refused")
	check(owner.stage_freedom_new_game("42"), "Default New Game could not stage")
	pending = owner.get_pending_freedom_start()
	check(pending.flight_state.impact == {"selected": false}, "New Game silently selected impact consequences")
	check(owner.discard_pending_freedom_start(pending.candidate_id), "Default New Game discard refused")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var unselected := args[0].path_join("unselected.json")
	if not check(shell.select_start(owner, {"mode": "continue", "value": unselected, "assets": args[1]}), "Unselected actual impact source refused: " + shell.error):
		shell.free()
		quit(1)
		return
	var view: Control = shell.current_view
	check(view is FlightView and view.paused and not view.state.impact.selected and not view.impact_overlay.visible, "Unselected voyage acquired cracks or condition")
	check(view.state.impact_opt_in.available and view.impact_menu_button.focus_mode == Control.FOCUS_ALL and not view.impact_menu_button.disabled and view.impact_menu_button.text == "Enable experimental terrain impacts", "Available experimental action is not controller focusable or misstates persistence")
	save_matches(owner, unselected, args[2].path_join("unselected-copy.json"))
	await capture_cue(view, owner, unselected, args[2], "unselected")
	var tick: String = view.state.tick
	view.impact_menu_button.pressed.emit()
	check(view.state.impact.selected and view.state.tick == tick and view.paused and view.impact_menu_button.disabled and not view.impact_overlay.visible, "Explicit paused action changed clock, invented damage or retained a reset toggle")
	check(not view.state.impact_opt_in.available and not view.state.impact_opt_in.reason.is_empty() and view.impact_menu_button.tooltip_text == view.state.impact_opt_in.reason, "Selected action does not reflect the authoritative eligibility refusal")
	save_matches(owner, args[0].path_join("selected-clear.json"), args[2].path_join("selected-action.json"))
	check(FileAccess.get_sha256(unselected) == FileAccess.get_sha256(args[2].path_join("unselected-copy.json")), "Selecting experimental impacts rewrote historical source")
	for label in ["selected-clear", "forebody1", "forebody2", "landing-damaged"]:
		var source: String = args[0].path_join(label + ".json")
		if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": args[1]}), "Impact Continue refused: " + label + " · " + shell.error):
			shell.free()
			quit(1)
			return
		view = shell.current_view
		var before: Dictionary = owner.get_freedom_flight_state()
		check(view is FlightView and view.cockpit and view.paused and before.impact.selected, "Impact fixture lost seated/paused selected readiness")
		var severity: int = 1 if label == "forebody1" else 2 if label == "forebody2" else 0
		check(before.impact.forebody == severity and view.impact_overlay.forebody_severity == severity and view.impact_overlay.visible == (severity >= 1) and view.impact_overlay.reduced_view == (severity >= 2), "Actual regional condition does not match optical cue: " + label)
		if label == "landing-damaged":
			check(before.impact.landing_structure >= 2 and not before.impact.landing_available and view.landing_button.disabled and not view.impact_overlay.visible, "Support damage invented glazing or retained qualified landing")
		if severity >= 1:
			check(not before.impact.latest.is_empty() and before.impact.latest.region == "forebody" and not ImpactView.feedback(before.impact).is_empty(), "Visible crack has no actual regional event or concise outcome")
		# Camera choice, look and repeated presentation refresh cannot bill an
		# impact or clear the permanent individual condition.
		view.cockpit = false
		view.update_view(0.125)
		check(not view.impact_overlay.visible, "Chase camera retained a cockpit-only glass cue")
		view.cockpit = true
		view.look_offset = Vector2(0.25, -0.12)
		for delta in [0.0, 0.001, 0.5]: view.update_view(delta)
		check(owner.get_freedom_flight_state() == before and view.impact_overlay.visible == (severity >= 1), "Camera or frame cadence changed authoritative condition")
		var invalid: Dictionary = before.impact.duplicate(true)
		invalid.craft_id = "0" if before.craft_id != "0" else "1"
		check(not view.impact_overlay.apply(invalid, before.craft_id, before.frame_id, before.frame_version, true, false) and not view.impact_overlay.visible, "Wrong individual craft retained a stale optical cue")
		check(view.impact_overlay.apply(before.impact, before.craft_id, before.frame_id, before.frame_version, true, true) and not view.impact_overlay.visible, "Outside view retained a cockpit-only glass cue")
		view.update_view(0.0)
		save_matches(owner, source, args[2].path_join(label + "-presentation.json"))
		if severity >= 1:
			await capture_cue(view, owner, source, args[2], label)
	await live_loss(args[0], args[1], args[2], "live-single", 1.0 / 120.0)
	await live_loss(args[0], args[1], args[2], "live-batch", 0.1)
	var loss_source := args[0].path_join("catastrophic-pending.json")
	if not check(shell.select_start(owner, {"mode": "continue", "value": loss_source, "assets": args[1]}), "Catastrophic condition Continue refused: " + shell.error):
		shell.free()
		quit(1)
		return
	view = shell.current_view
	var loss: Dictionary = owner.get_freedom_flight_state()
	var retired_overlay: Control = view.impact_overlay
	check(loss.impact.pending_loss and loss.impact.latest.catastrophic and shell.recovery_overlay != null and "recovery pending" in ImpactView.feedback(loss.impact), "Actual catastrophic event did not reach standard recovery feedback")
	view.toggle_pause()
	check(view.paused and view.recovery_notified and not view.controls_menu.panel.visible and shell.recovery_layer.layer > view.controls_menu.layer, "Loaded pending recovery resumed controls or is hidden behind the pause menu")
	save_matches(owner, loss_source, args[2].path_join("pending-copy.json"))
	shell.continue_recovery()
	var fresh: Dictionary = owner.get_freedom_flight_state()
	check(shell.current_view is WalkView and fresh.craft_id != loss.craft_id and fresh.impact.selected and fresh.impact.forebody == 0 and fresh.impact.landing_structure == 0 and fresh.impact.latest.is_empty() and not fresh.impact.pending_loss and not is_instance_valid(retired_overlay), "Recovery retained the retired individual's crack/condition or selected another policy")
	check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, 0.0, 0.0])), "Recovered station clock could not advance")
	save_matches(owner, args[0].path_join("recovered.json"), args[2].path_join("recovered-copy.json"))
	if failures == 0:
		var repair_checks := RepairChecks.new()
		failures += await repair_checks.run_repair(self, shell, owner, args[0], args[1], args[2].path_join("origin-repair"))
		repair_checks = null
	var output := FileAccess.open(args[2].path_join("impact-report.json"), FileAccess.WRITE)
	if check(output != null, "Impact evidence report unavailable"):
		var hashes := {}
		for path in ["scripts/native/native_impact_view.gd", "scripts/native/native_flight_view.gd", "scripts/native/native_start_shell.gd", "tests/native_impact_test.gd", "bin/libapsis_freedom_bridge.so"]:
			hashes[path] = FileAccess.get_sha256("res://" + path)
		output.store_string(JSON.stringify({"schema_version": 2, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if DisplayServer.get_name() != "headless" else "headless", "scope": "authoritative impact fixtures and live single/multi-tick recovery to a walking replacement; optional software/windowed cue captures, no FPS/pressure/oxygen claim", "sources_sha256": hashes, "license": "BSD-3-Clause for repository-authored cue/capture; embedded content retains packaged licenses", "asset_license_records": "assets/native/freedom-starter-01/licenses", "captures": captures, "pass": failures == 0}, "\t") + "\n")
		output.close()
	shell.free()
	shell = null
	view = null
	retired_overlay = null
	owner = null
	output = null
	# Freed subviewports/skies retire renderer resources at frame boundaries.
	# Keep shutdown bounded while allowing deferred software-render cleanup.
	for frame in 2:
		await process_frame
		if DisplayServer.get_name() != "headless":
			await RenderingServer.frame_post_draw
	print("Native impact presentation: %d failures" % failures)
	quit(1 if failures else 0)
