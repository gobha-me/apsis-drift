extends RefCounted
## Actual saved craft, presentation-only head aim and the public gear command.
const Shell = preload("res://scripts/native/native_start_shell.gd")

static func save(owner: Variant, path: String, check: Callable) -> PackedByteArray:
	if not check.call(owner.save_freedom_as(path), "Cockpit gear complete Save refused"): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

static func continue_peer(owner: Variant, path: String, check: Callable) -> bool:
	if not check.call(owner.stage_freedom_continue(path), "Cockpit gear peer Continue staging refused"): return false
	return check.call(owner.commit_pending_freedom_start(owner.get_pending_freedom_start().candidate_id), "Cockpit gear peer Continue commit refused")

static func key(view: Control, down: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = KEY_E
	event.keycode = KEY_E
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	view.player_input._input(event)

static func click(view: Control, down: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = down
	view._input(event)

static func aim(view: Control, check: Callable) -> bool:
	var control: Node3D = view.cockpit_control
	if not check.call(control != null and control.bound, "Actual cockpit lacks its current SYSTEMS gear target"): return false
	view.cockpit = true
	view.update_view(0.0)
	# Only set a presentation head angle, never the saved actor/craft pose.
	var direction: Vector3 = view.ship.global_basis.inverse() * (control.global_position - view.camera.global_position)
	var look := Vector2(atan2(direction.x, -direction.z), -atan2(direction.y, Vector2(direction.x, direction.z).length()))
	if not check.call(absf(look.x) <= 1.2 and absf(look.y) <= 0.8, "Current SYSTEMS gear patch is outside the existing cockpit head-look range"): return false
	view.look_offset = look
	view.update_view(0.0)
	return check.call(control.visible and control.aimed and view.player_input.cockpit_interaction_enabled, "Actual eye ray did not hit the depth-tested console gear patch")

static func parity(view: Control, owner: Variant, peer: Variant, output: String, name: String, tick: String, check: Callable) -> void:
	var actual: Dictionary = owner.get_freedom_flight_state()
	check.call(actual == peer.get_freedom_flight_state() and actual.tick == tick and actual.surface.maneuver == "off" and view.error.is_empty(), "Cockpit gear differs from immediate direct C++ gear-only command: " + name)
	check.call(save(owner, output.path_join(name + "-ui.json"), check) == save(peer, output.path_join(name + "-direct.json"), check), "Cockpit gear changed complete Save beyond the direct command: " + name)
	check.call(view.cockpit_control.label.text == ("GEAR DOWN" if actual.surface.gear_deployed else "GEAR UP"), "Cockpit switch falsely reports accepted gear state: " + name)

static func run(tree: SceneTree, source: String, assets: String, output: String, check: Callable) -> void:
	if not check.call(DirAccess.make_dir_recursive_absolute(output) == OK, "Cockpit gear output unavailable"): return
	var original := FileAccess.get_file_as_bytes(source)
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check.call(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Cockpit gear actual Continue refused: " + shell.error): shell.free(); return
	var view: Control = shell.current_view
	view.set_process(false)
	view.player_input.persist = false
	view.player_input.device = -1
	view.player_input.install()
	await tree.process_frame
	await tree.process_frame
	check.call(view.paused and not view.cockpit_control_aimed(), "Continue armed a cockpit gesture")
	view.observe_neutral_controls()
	view.toggle_pause()
	view.sync_mouse_look()
	if not aim(view, check): shell.free(); return
	var before: Dictionary = owner.get_freedom_flight_state()
	var control: Node3D = view.cockpit_control
	check.call(not control.ray_hits(Vector3(NAN, 0, 0), Vector3.FORWARD) and not control.ray_hits(view.camera.global_position, Vector3.ZERO) and not control.ray_hits(view.camera.global_position, view.camera.global_basis.z), "Malformed/zero/backward cockpit ray acquired an action")
	var outside_eye: Vector3 = control.global_transform * Vector3(0.2, 0, 0.5)
	check.call(not control.ray_hits(outside_eye, -control.global_basis.z), "A ray outside the physical gear patch acquired an action")
	var baseline := save(owner, output.path_join("before.json"), check)
	# The genuine landed fixture proves refusal without inventing unsupported
	# support. Release is a separate real command, not claimed flown ascent.
	if before.surface.landed:
		key(view, true)
		key(view, false)
		check.call(owner.get_freedom_flight_state() == before and not view.save_status.text.is_empty() and save(owner, output.path_join("landed-refused.json"), check) == baseline and view.cockpit_control.label.text == "GEAR DOWN", "Refused landed gear stow changed state or displayed an accepted transition")
		if not check.call(owner.liftoff_freedom_surface() and owner.cancel_freedom_surface_maneuver(), "Actual supported gear test release refused"): shell.free(); return
		view.state = owner.get_freedom_flight_state()
		view.update_view(0.0)
	for assisted in [true, false]:
		if not check.call(owner.set_freedom_assistance(assisted), "Cockpit gear assistance setup refused"): break
		view.state = owner.get_freedom_flight_state()
		view.player_input.assist = assisted
		view.update_view(0.0)
		if not aim(view, check): break
		var prefix := "assisted" if assisted else "manual"
		var path := output.path_join(prefix + "-source.json")
		baseline = save(owner, path, check)
		if not continue_peer(peer, path, check): break
		before = owner.get_freedom_flight_state()
		var deployed: bool = not before.surface.gear_deployed
		key(view, true)
		check.call(peer.set_freedom_landing_gear(deployed), "Independent gear-only command refused")
		parity(view, owner, peer, output, prefix + "-key", before.tick, check)
		var held := save(owner, output.path_join(prefix + "-held.json"), check)
		var repeat := InputEventKey.new()
		repeat.physical_keycode = KEY_E
		repeat.pressed = true
		view.player_input._input(repeat)
		check.call(view.player_input.sample().thrust_axes[5] == 0.0 and save(owner, output.path_join(prefix + "-repeat.json"), check) == held, "Held cockpit E repeated or fired flight strafe")
		key(view, false)
		click(view, true)
		click(view, true)
		click(view, false)
		check.call(peer.set_freedom_landing_gear(not deployed), "Independent click gear command refused")
		parity(view, owner, peer, output, prefix + "-click", before.tick, check)
		# Selected A uses the same action and cannot be stolen by another pad.
		view.player_input.device = 19
		view.player_input.available_pads = {19: true, 9: true}
		view.player_input.install()
		check.call(view.paused, "Controller remapping did not pause cockpit input")
		view.observe_neutral_controls()
		view.toggle_pause()
		view.sync_mouse_look()
		if not aim(view, check): break
		var pad := InputEventJoypadButton.new()
		pad.button_index = JOY_BUTTON_A
		pad.pressed = true
		pad.device = 9
		view.player_input._input(pad)
		check.call(owner.get_freedom_flight_state() == peer.get_freedom_flight_state(), "Foreign pad changed the gear")
		pad.device = 19
		view.player_input._input(pad)
		view.player_input._input(pad)
		pad.pressed = false
		view.player_input._input(pad)
		check.call(peer.set_freedom_landing_gear(deployed), "Independent selected-pad gear command refused")
		parity(view, owner, peer, output, prefix + "-pad", before.tick, check)
		view.player_input.device = -1
		view.player_input.install()
		check.call(view.paused, "Keyboard remapping did not pause cockpit input")
		view.observe_neutral_controls()
		view.toggle_pause()
		view.sync_mouse_look()
		if not aim(view, check): break
		before = owner.get_freedom_flight_state()
		for gate in ["thrust", "focus", "modal", "pause", "camera"]:
			if gate == "thrust": Input.action_press("pilot_forward", 0.6)
			elif gate == "focus": view.focused = false
			elif gate == "modal": view.journey_dialog_open = true
			elif gate == "pause": view.pause_controls("Gear pause guard")
			elif gate == "camera": view.change_camera()
			view.request_cockpit_gear()
			check.call(owner.get_freedom_flight_state() == before, "Cockpit gear bypassed " + gate)
			if gate == "thrust": Input.action_release("pilot_forward")
			elif gate == "focus": view.focused = true
			elif gate == "modal": view.journey_dialog_open = false
			elif gate == "pause":
				view.observe_neutral_controls()
				view.toggle_pause()
				view.sync_mouse_look()
			elif gate == "camera": view.change_camera()
			if not aim(view, check): break
	view.pause_controls("Cockpit gear checks complete")
	check.call(not view.cockpit_control_aimed() and not view.player_input.cockpit_interaction_enabled, "Pause retained cockpit intent")
	var continued_path := output.path_join("continue.json")
	var frozen := save(owner, continued_path, check)
	var next := Shell.new()
	next.presentation_only = true
	next.process_mode = Node.PROCESS_MODE_DISABLED
	tree.root.add_child(next)
	next.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	check.call(next.select_start(peer, {"mode": "continue", "value": continued_path, "assets": assets}), "Gear result Continue refused")
	if next.current_view != null:
		check.call(next.current_view.paused and not next.current_view.cockpit_control_aimed() and not next.current_view.player_input.cockpit_interaction_enabled and save(peer, output.path_join("continued.json"), check) == frozen, "Continue changed accepted gear or retained cockpit input")
	next.free()
	shell.free()
	check.call(FileAccess.get_file_as_bytes(source) == original, "Cockpit control test changed its real input fixture")
	await tree.process_frame
