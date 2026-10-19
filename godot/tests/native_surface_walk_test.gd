extends "res://studies/captures/native_voyage_capture.gd"
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
const Navigation = preload("res://scripts/native/home_navigation.gd")
const PositionStatus = preload("res://scripts/native/native_status.gd")
const Landmarks = preload("res://scripts/native/native_surface_landmarks.gd")
## Actual native root, input, C++ terrain and complete Save comparisons.
## The source is explicitly a near-ground test setup, not a flight acceptance.

func key(physical: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = physical
	event.physical_keycode = physical
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()


func checkpoint(owner: Variant, source: String, output: String, name: String) -> bool:
	var path := output.path_join(name + ".json")
	return check(owner.save_freedom_as(path) and FileAccess.get_sha256(path) == FileAccess.get_sha256(source.path_join(name + ".json")), "Complete surface voyage differs at " + name)


func check_surface_action_reference(view: Control, owner: Variant, output: String, command: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var was_paused: bool = view.paused
	var prefix := output.path_join("reference-liftoff" if command == "liftoff_freedom_surface" else "reference-landing")
	if not check(owner.save_freedom_as(prefix + "-before.json"), "Surface action reference baseline Save refused"): return
	var bytes := FileAccess.get_file_as_bytes(prefix + "-before.json")
	var operations: Array = [3, 4] if command == "request_freedom_landing" else [4]
	var assessments: Array = []
	for operation in operations:
		var assessment: Dictionary = owner.get_freedom_environment_assessment(operation)
		check(PositionStatus.valid_surface_reference_packet(assessment) and not assessment.get("margins", []).is_empty(), "Actual observed seed42 surface route lacks known redacted reference margins")
		assessments.append(assessment)
	check(owner.get_freedom_flight_state() == before and owner.save_freedom_as(prefix + "-read.json") and FileAccess.get_file_as_bytes(prefix + "-read.json") == bytes, "Reading actual reference margins changed the complete saved voyage")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(prefix + "-before.json"), "Direct surface action peer Continue staging refused"): return
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(peer.commit_pending_freedom_start(pending.candidate_id) and peer.call(command), "Direct actual surface command refused"): return
	var expected: Array = []
	for operation in operations: expected.append(peer.get_freedom_environment_assessment(operation))
	check(expected == assessments, "A zero-time surface command changed its recorded reference margins")
	var warning := PositionStatus.surface_reference_warning(command, expected)
	if command == "liftoff_freedom_surface": view.liftoff_button.pressed.emit()
	else: view.landing_button.pressed.emit()
	var actual: Dictionary = owner.get_freedom_flight_state()
	var direct: Dictionary = peer.get_freedom_flight_state()
	check(view.error.is_empty() and view.paused == was_paused and actual.tick == before.tick and actual.tick == direct.tick and actual.checksum == direct.checksum and actual.resources.quantity_quanta == before.resources.quantity_quanta and actual.resources.jump_charges == before.resources.jump_charges, "Surface warning changed pause, authoritative time, fuel or direct-command physics")
	var base := "Landing aid requested." if command == "request_freedom_landing" else "Liftoff aid requested."
	if was_paused: base += " Resume flight explicitly."
	var message := base + ("\n" + warning if not warning.is_empty() else "")
	check(view.save_status.text == message and message in view.primary_status.tooltip_text, "Accepted surface action did not append exactly its actual known reference warning")
	if was_paused:
		check(view.controls_menu.message.text == message and view.player_input.status == message, "Paused reference warning did not reach the existing command feedback")
	check(owner.save_freedom_as(prefix + "-ui.json") and peer.save_freedom_as(prefix + "-direct.json") and FileAccess.get_file_as_bytes(prefix + "-ui.json") == FileAccess.get_file_as_bytes(prefix + "-direct.json"), "Surface warning integration changed complete canonical state relative to the direct C++ command")
	print("Actual surface reference %s: %s" % [command, warning if not warning.is_empty() else "quiet known margins"])
	peer = null


func check_airborne_liftoff_refusal(view: Control, owner: Variant, output: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var paused_before: bool = view.paused
	var path := output.path_join("reference-airborne-refusal.json")
	if not check(owner.save_freedom_as(path), "Airborne reference refusal baseline Save refused"): return
	var bytes := FileAccess.get_file_as_bytes(path)
	view.port_command("liftoff_freedom_surface")
	var diagnostic: String = owner.get_last_error()
	check(not diagnostic.is_empty() and view.save_status.text == diagnostic and view.paused == paused_before and owner.get_freedom_flight_state() == before and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Refused airborne liftoff appended reference feedback or changed the voyage")


func check_rated_rise_actor(view: Control, owner: Variant, output: String, available: bool) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	check(before.has("rated_rise_margin") == available, "Rated RISE availability disagrees with seated/ground actor ownership")
	if available:
		var margin: Variant = before.get("rated_rise_margin")
		if not check(margin is float and is_finite(margin), "Actual landed craft omitted a finite prospective RISE estimate"): return
	var path := output.path_join("rated-rise-seated.json" if available else "rated-rise-outside.json")
	check(owner.save_freedom_as(path), "Rated RISE actor inspection baseline failed")
	var bytes := FileAccess.get_file_as_bytes(path)
	view.show_instruments()
	for repeat in 3:
		check(owner.get_freedom_flight_state() == before, "Rated RISE actor read changed the voyage or clock")
		view.update_view(0.0)
	check(view.paused and view.hud_scroll.visible and ("Full RISE" in view.telemetry.text) == available and not "Full RISE" in view.primary_status.text, "Expanded rated RISE instrument leaked on foot or disappeared while seated")
	if available: check("Prospective free flight" in view.telemetry.text, "Landed estimate lost its prospective label")
	check(owner.get_freedom_flight_state() == before and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Rated RISE actor inspection changed full save bytes")
	view.pause_controls("Rated RISE actor inspection complete")


func check_surface_position(view: Control, owner: Variant, path: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var actor: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not check(actor is Dictionary and actor.get("actor") is Dictionary and actor.actor.get("foot_position_metres") is Array and actor.actor.foot_position_metres.size() == 3, "Saved ground position unavailable for independent direction check"): return
	var walking: Dictionary = before.surface_walk
	if not check(walking.get("latitude") is float and walking.get("longitude") is float and is_finite(walking.latitude) and is_finite(walking.longitude) and absf(walking.latitude) <= PI / 2.0 and absf(walking.longitude) <= PI, "Actual surface coordinates are unavailable or outside radians bounds"): return
	# Scalar doubles retain the saved direction; a Godot Vector3 would round it
	# to presentation float precision before this independent convention check.
	var foot: Array = actor.actor.foot_position_metres
	var radius := sqrt(float(foot[0]) * float(foot[0]) + float(foot[1]) * float(foot[1]) + float(foot[2]) * float(foot[2]))
	var x := cos(walking.latitude) * cos(walking.longitude)
	var y := cos(walking.latitude) * sin(walking.longitude)
	var z := sin(walking.latitude)
	check(absf(x - float(foot[0]) / radius) < 0.000000000001 and absf(y - float(foot[1]) / radius) < 0.000000000001 and absf(z - float(foot[2]) / radius) < 0.000000000001, "Surface coordinates describe a different direction than the actual saved actor")
	var label := PositionStatus.surface_position_text(walking)
	check(label in view.telemetry.text and not "Lat " in view.primary_status.text and not "Lon " in view.primary_status.text, "Actual expanded instruments lost own position or added coordinates to the compact HUD")
	var bytes := FileAccess.get_file_as_bytes(path)
	for repeat in 3:
		check(owner.get_freedom_flight_state() == before, "Reading surface position changed its actor, clock or observations")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Surface position inspection changed complete saved voyage bytes")


func walk(view: Control, physical: int, ticks: int) -> void:
	if not resume(view): return
	key(physical, true)
	var controls: PackedFloat64Array = view.surface_controls(0.0)
	check(controls[0] == (-1.0 if physical == KEY_S else 1.0), "Physical walking key did not reach native input")
	while ticks > 0 and not failed:
		var count := mini(ticks, 15)
		view._process(float(count) / 120.0)
		check(view.error.is_empty(), "Surface input refused: " + view.error)
		ticks -= count
	key(physical, false)


func check_depth_inputs() -> void:
	for eye in [Vector3(NAN, 0, 0), Vector3(0, INF, 0), Vector3(0, 0, -INF), Vector3(3e38, 3e38, 3e38)]:
		check(FlightView.surface_depth_range(eye, 100000.0) == Vector2.ZERO, "Nonfinite surface camera eye accepted")
	for limit in [NAN, INF, -INF, -1.0, 0.0, 199.0, 1000000001.0]:
		check(FlightView.surface_depth_range(Vector3.ZERO, limit) == Vector2.ZERO, "Invalid surface depth envelope accepted")
	check(FlightView.surface_depth_range(Vector3.ZERO, 100000.0) == Vector2(0.05, 200.0), "Near surface depth changed ordinary close framing")
	for distance in [250.0, 1000.0, 100000.0, 1000000.0]:
		var depth: Vector2 = FlightView.surface_depth_range(Vector3(0, 0, distance), 100000.0)
		check(depth.x >= 0.05 and depth.y <= 100000.0 and depth.y / depth.x <= 500001.0 and depth.y >= minf(distance + 16.0, 100000.0), "Surface depth escaped its distance/ratio/distant envelope")


func saved_anchor(path: String) -> Dictionary:
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	# Existing format30->29/28/27/25->23 wrappers own this unchanged anchor.
	for level in 12:
		if not value is Dictionary: return {}
		if value.get("surface") is Dictionary and value.surface.get("landed") is Dictionary:
			return value.surface.landed.duplicate(true)
		value = value.get("voyage", value.get("base"))
	return {}


func check_return_bearing(view: Control) -> void:
	var access: Dictionary = view.state.surface_walk.return_access
	check(access.available and view.ship_marker.visible and view.ship_marker.label_text == "RETURN" and not view.home_marker.visible, "Ground return access marker unavailable")
	var expected := Navigation.project(view.camera, access.position, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.ship_marker.cue == expected, "RETURN bearing diverged from actual projected entry")
	check("RETURN" in view.primary_status.text and "%.1f m" % access.distance_metres in view.primary_status.text, "Compact ground HUD omitted access range")


func check_return_fallback(view: Control) -> void:
	var original: Dictionary = view.state.duplicate(true)
	# Simulate only an unavailable presentation observation, never mutate C++.
	view.state.surface_walk.return_access = {"available": false}
	view.update_view(0.0)
	var expected := Navigation.project(view.camera, Vector3.ZERO, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.error.is_empty() and view.ship_marker.visible and view.ship_marker.label_text == "SHIP" and view.ship_marker.cue == expected, "Unavailable return access lost the explicit craft-centre fallback")
	check("Return access unavailable" in view.primary_status.text and not view.home_marker.visible, "Unavailable return access showed a guessed distance or changed HOME")
	view.state = original
	view.update_view(0.0)
	check_return_bearing(view)


func marker_state_without_metadata(state: Dictionary) -> Dictionary:
	var result := state.duplicate(true)
	result.erase("surface_marker")
	return result


func activate_marker_button(view: Control, button: Button, controller := false) -> void:
	var mode := button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(button)
	check(button.visible and not button.disabled and button.has_focus() and button.can_process(), "Marker action lacks keyboard/controller focus")
	if controller:
		for pressed in [true, false]:
			var event := InputEventJoypadButton.new()
			event.device = view.player_input.device
			event.button_index = JOY_BUTTON_A
			event.pressed = pressed
			Input.parse_input_event(event)
			Input.flush_buffered_events()
			await process_frame
	else:
		key(KEY_ENTER, true)
		await process_frame
		key(KEY_ENTER, false)
	button.process_mode = mode
	button.release_focus()


func check_personal_surface_marker(source: String, assets: String, output: String) -> void:
	# Independent actual continuation leaves the existing full-save route intact.
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Marker outside fixture did not Continue"):
		shell.free()
		return
	var view: Control = shell.current_view
	var initial: Dictionary = owner.get_freedom_flight_state()
	var records: Dictionary = owner.get_freedom_observations()
	check(view.paused and view.surface_walking() and initial.surface_marker.can_mark and not initial.surface_marker.present and not view.show_personal_marker, "Fresh outside marker selection is unavailable or invented")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.surface_marker_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(owner.get_freedom_flight_state() == initial, "Unfocused marker callback changed the journey")
	key(KEY_W, true)
	view.toggle_pause()
	check(view.paused and owner.get_freedom_flight_state() == initial, "Held walking input bypassed Resume while reviewing a marker")
	await activate_marker_button(view, view.surface_marker_button)
	key(KEY_W, false)
	var marked: Dictionary = owner.get_freedom_flight_state()
	var point: Dictionary = marked.surface_marker.duplicate(true)
	check(view.paused and point.present and point.current_body and point.tick == initial.tick and point.distance_metres < 0.000001 and view.surface_marker_button.text == "Replace marker", "Actual paused mark did not record the current foot/tick with explicit replace")
	check(marker_state_without_metadata(marked) == marker_state_without_metadata(initial) and owner.get_freedom_observations() == records, "Mark changed clock, craft, actor, resources or knowledge provenance")
	check(not owner.mark_freedom_surface_marker() and owner.get_freedom_flight_state() == marked, "Repeated mark silently overwrote the selected point")
	for field in ["version", "selected", "present", "current_body", "can_mark", "can_clear", "position", "distance_metres", "system_id", "planet_id", "tick", "system_name", "planet_name"]:
		var malformed := marked.duplicate(true)
		malformed.surface_marker[field] = null
		check(not FlightView.valid_surface_marker(malformed), "Malformed personal marker field accepted: " + field)
	var malformed := marked.duplicate(true)
	malformed.surface_marker.position = Vector3(NAN, 0, 0)
	check(not FlightView.valid_surface_marker(malformed), "Nonfinite personal marker position accepted")
	malformed = marked.duplicate(true)
	malformed.surface_marker.distance_metres = -1.0
	check(not FlightView.valid_surface_marker(malformed), "Negative personal marker range accepted")
	malformed = marked.duplicate(true)
	malformed.surface_marker.tick = "18446744073709551615"
	check(not FlightView.valid_surface_marker(malformed), "Future personal marker tick accepted")
	await activate_marker_button(view, view.show_surface_marker_button)
	check(view.show_personal_marker and view.ship_marker.label_text == "MARK" and not view.home_marker.visible and "RETURN" in view.primary_status.text, "MARK overlapped return/home cues or removed hatch range")
	walk(view, KEY_S, 120)
	view.pause_controls("Inspect the personal marker", true)
	var moved: Dictionary = owner.get_freedom_flight_state()
	check(moved.surface_marker.tick == point.tick and moved.surface_marker.distance_metres > 0.1, "Walking moved the saved marker instead of changing its range")
	var expected := Navigation.project(view.camera, moved.surface_marker.position, view.ship_marker.size, Navigation.INSET * view.ship_marker.render_scale)
	check(view.ship_marker.cue == expected and "straight-line distance" in view.primary_status.text and "RETURN" in view.primary_status.text, "Current-body MARK bearing or compact return information diverged")
	view.player_input.device = 0
	view.player_input.available_pads[0] = true
	view.player_input.install()
	var before_replace: Dictionary = owner.get_freedom_flight_state()
	var replace_records: Dictionary = owner.get_freedom_observations()
	await activate_marker_button(view, view.surface_marker_button, true)
	var replaced: Dictionary = owner.get_freedom_flight_state()
	check(view.paused and replaced.surface_marker.tick == replaced.tick and replaced.surface_marker.tick != point.tick and replaced.surface_marker.distance_metres < 0.000001, "Selected-controller Replace did not record the new foot/tick")
	check(marker_state_without_metadata(replaced) == marker_state_without_metadata(before_replace) and owner.get_freedom_observations() == replace_records, "Replace changed the clock or actual knowledge/provenance")
	var saved := output.path_join("personal-marker.json")
	check(owner.save_freedom_as(saved), "Personal marker Save As refused")
	var bytes := FileAccess.get_file_as_bytes(saved)
	for repeat in 3: view.update_view(0.0)
	check(owner.save_freedom_as(saved + ".after.json") and FileAccess.get_file_as_bytes(saved + ".after.json") == bytes, "Marker projection/review changed the complete saved journey")
	check(shell.select_start(owner, {"mode": "continue", "value": saved, "assets": assets}), "Personal marker Continue refused")
	view = shell.current_view
	check(view.paused and not view.show_personal_marker and view.state.surface_marker == replaced.surface_marker, "Continue changed the saved point or retained the presentation target choice")
	check_return_bearing(view)
	walk(view, KEY_W, 120)
	view.surface_walk_button.pressed.emit()
	check(view.paused and not view.surface_walking() and view.state.surface_marker.present and view.state.surface_marker.tick == replaced.surface_marker.tick, "Reboarding lost the personal surface marker")
	await activate_marker_button(view, view.show_surface_marker_button)
	var aboard: Dictionary = owner.get_freedom_flight_state()
	expected = Navigation.project(view.camera, aboard.surface_marker.position, view.home_marker.size, Navigation.INSET * view.home_marker.render_scale)
	check(view.home_marker.visible and view.home_marker.label_text == "MARK" and not view.ship_marker.visible and view.home_marker.cue == expected, "Seated MARK did not use the shared C++ craft-relative projection")
	var clear_records: Dictionary = owner.get_freedom_observations()
	await activate_marker_button(view, view.clear_surface_marker_button)
	var cleared: Dictionary = owner.get_freedom_flight_state()
	check(view.paused and cleared.surface_marker.selected and not cleared.surface_marker.present and not view.show_personal_marker and view.home_marker.label_text == "HOME", "Clear failed to retain explicit metadata selection or restore HOME")
	check(marker_state_without_metadata(cleared) == marker_state_without_metadata(aboard) and owner.get_freedom_observations() == clear_records, "Clear changed actor, craft, time or sensor provenance")
	shell.free()


func landmark_state_without_metadata(state: Dictionary) -> Dictionary:
	var result := state.duplicate(true)
	result.erase("surface_landmarks")
	return result


func saved_landmark_metadata(path: String) -> Dictionary:
	var value: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	for level in 12:
		if not value is Dictionary: break
		if value.get("knowledge") is Dictionary:
			return value.knowledge.get("surface_landmarks", {})
		value = value.get("voyage", value.get("base"))
	check(false, "Landmark oracle could not locate the saved knowledge owner")
	return {}


func landmark_material_bytes(material: ShaderMaterial) -> PackedByteArray:
	return var_to_bytes([material.get_shader_parameter("surface_landmarks_enabled"), material.get_shader_parameter("surface_landmark_count"), material.get_shader_parameter("surface_landmark_centres"), material.get_shader_parameter("surface_landmark_radii"), material.get_shader_parameter("surface_landmark_tints")])


func check_landmark_packet(state: Dictionary) -> void:
	var presenter := Landmarks.new()
	var material := ShaderMaterial.new()
	material.shader = load("res://shaders/terrain.gdshader")
	var invalid_material := ShaderMaterial.new()
	check(Landmarks.valid(state) and presenter.apply(state, [material]), "Actual C++ landmark packet did not reach the terrain shader")
	var first: Dictionary = state.surface_landmarks.sites[0]
	check(material.get_shader_parameter("surface_landmarks_enabled") == true and material.get_shader_parameter("surface_landmark_count") == state.surface_landmarks.sites.size() and material.get_shader_parameter("surface_landmark_centres")[0] == first.position and is_equal_approx(material.get_shader_parameter("surface_landmark_radii")[0], first.radius_metres), "Exposure shader lost its C++ native-tangent centre/radius")
	var accepted := landmark_material_bytes(material)
	var moved := state.duplicate(true)
	moved.surface_landmarks.sites[0].position += Vector3(1, 0, 0)
	check(not presenter.apply(moved, [material, invalid_material]) and landmark_material_bytes(material) == accepted, "Invalid second terrain destination partially changed a valid material")
	var invalid_states: Array[Dictionary] = []
	for field in state.surface_landmarks:
		var bad := state.duplicate(true)
		bad.surface_landmarks[field] = null
		invalid_states.append(bad)
	for field in first:
		var bad := state.duplicate(true)
		bad.surface_landmarks.sites[0][field] = null
		invalid_states.append(bad)
	for pair in [["position", Vector3(NAN, 0, 0)], ["radius_metres", 27.999], ["radius_metres", 52.001], ["radius_metres", INF], ["distance_metres", -0.1], ["distance_metres", NAN], ["tint", Color(NAN, 0, 0, 1)], ["tint", Color(1, 1, 1, 0.5)], ["kind", 3], ["label", "x".repeat(65)], ["description", "x".repeat(257)], ["inspected", true]]:
		var bad := state.duplicate(true)
		bad.surface_landmarks.sites[0][pair[0]] = pair[1]
		invalid_states.append(bad)
	for pair in [["version", 2], ["system_id", "1" if state.system_id != "1" else "2"], ["planet_id", "1" if state.planet_id != "1" else "2"], ["tick", "0"], ["inspection_count", -1], ["inspection_count", 65], ["inspection_count", 1.0], ["selected", false], ["can_enable", true], ["unexpected", true]]:
		var bad := state.duplicate(true)
		bad.surface_landmarks[pair[0]] = pair[1]
		invalid_states.append(bad)
	for id in ["", "00", "+1", "-1", " 1", "1x", "18446744073709551616"]:
		var bad := state.duplicate(true)
		bad.surface_landmarks.sites[0].id = id
		invalid_states.append(bad)
	var oversized := state.duplicate(true)
	oversized.surface_landmarks.sites = []
	for index in 9: oversized.surface_landmarks.sites.append(first.duplicate(true))
	invalid_states.append(oversized)
	var duplicate := state.duplicate(true)
	duplicate.surface_landmarks.sites.append(first.duplicate(true))
	invalid_states.append(duplicate)
	var inconsistent := state.duplicate(true)
	inconsistent.surface_landmarks.sites[0].nearby = false
	inconsistent.surface_landmarks.sites[0].can_inspect = true
	invalid_states.append(inconsistent)
	for bad in invalid_states:
		var bytes := var_to_bytes(bad)
		check(not Landmarks.valid(bad) and not presenter.apply(bad, [material]) and Landmarks.nearest_nearby(bad).is_empty() and landmark_material_bytes(material) == accepted and var_to_bytes(bad) == bytes, "Malformed exposure packet changed accepted shader/input state or produced a prompt")
	var missing := state.duplicate(true)
	missing.erase("surface_landmarks")
	check(Landmarks.valid(missing) and presenter.apply(missing, [material]) and material.get_shader_parameter("surface_landmarks_enabled") == false and material.get_shader_parameter("surface_landmark_count") == 0, "Historical missing selection did not keep landmark shading disabled")


func check_surface_landmarks(source: String, assets: String, output: String) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("surface_landmark_historical.json"), "assets": assets}), "Explicit historical landmark fixture did not Continue"):
		shell.free()
		return
	var view: Control = shell.current_view
	var historical: Dictionary = owner.get_freedom_flight_state()
	var historical_records: Dictionary = owner.get_freedom_observations()
	var historical_save := output.path_join("landmark-historical-before.json")
	check(view.paused and historical.surface_landmarks.can_enable and not historical.surface_landmarks.selected and view.landmark_enable_button.visible and owner.save_freedom_as(historical_save), "Historical journey silently selected landmarks or lost explicit enable")
	var historical_bytes := FileAccess.get_file_as_bytes(historical_save)
	for repeat in 3: view.update_view(0.0)
	check(owner.save_freedom_as(historical_save + ".read.json") and FileAccess.get_file_as_bytes(historical_save + ".read.json") == historical_bytes and saved_landmark_metadata(historical_save).is_empty(), "Historical reads adopted landmark metadata")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.landmark_enable_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(owner.get_freedom_flight_state() == historical, "Unfocused Enable changed historical selection")
	await activate_marker_button(view, view.landmark_enable_button)
	var enabled: Dictionary = owner.get_freedom_flight_state()
	check(view.paused and enabled.surface_landmarks.selected and enabled.surface_landmarks.inspection_count == 0 and not view.landmark_enable_button.visible and landmark_state_without_metadata(enabled) == landmark_state_without_metadata(historical) and owner.get_freedom_observations() == historical_records, "Explicit Enable changed clock, resources, facts or actor instead of selecting metadata")
	if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("surface_landmark_outside.json"), "assets": assets}), "Actual nearby exposure fixture did not Continue"):
		shell.free()
		return
	view = shell.current_view
	var initial: Dictionary = owner.get_freedom_flight_state()
	var site: Dictionary = Landmarks.nearest_nearby(initial)
	if not check(view.paused and view.surface_walking() and initial.surface_landmarks.selected and initial.surface_landmarks.inspection_count == 0 and not site.is_empty() and site.can_inspect and not site.inspected, "Actual supported outside foot is not near an uninspected exposure"):
		shell.free()
		return
	check_landmark_packet(initial)
	check(view.landmark_inspect_button.visible and not view.landmark_inspect_button.disabled and site.label in view.primary_status.text and "RETURN" in view.primary_status.text, "Nearby exposure lost its actual prompt or displaced RETURN")
	var invalid_before: Dictionary = owner.get_freedom_flight_state()
	var invalid_path := output.path_join("landmark-invalid-id-before.json")
	check(owner.save_freedom_as(invalid_path), "Invalid exposure ID baseline Save refused")
	var invalid_bytes := FileAccess.get_file_as_bytes(invalid_path)
	for id in ["", "00", "+1", "-1", " 1", "1x", "18446744073709551616"]:
		check(not owner.inspect_freedom_surface_landmark(id) and owner.get_freedom_flight_state() == invalid_before, "Malformed decimal exposure ID changed a saved voyage")
	check(owner.save_freedom_as(invalid_path + ".after.json") and FileAccess.get_file_as_bytes(invalid_path + ".after.json") == invalid_bytes, "Malformed decimal exposure IDs changed complete saved bytes")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.landmark_inspect_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(owner.get_freedom_flight_state() == invalid_before and not view.controls_menu.surface_reference_view.visible, "Unfocused Inspect changed history or opened a reference")
	await activate_marker_button(view, view.surface_marker_button)
	var before: Dictionary = owner.get_freedom_flight_state()
	var records: Dictionary = owner.get_freedom_observations()
	var marker: Dictionary = before.surface_marker.duplicate(true)
	key(KEY_W, true)
	view.toggle_pause()
	check(view.paused and owner.get_freedom_flight_state() == before, "Held movement bypassed Resume before exposure inspection")
	await activate_marker_button(view, view.landmark_inspect_button)
	key(KEY_W, false)
	var inspected: Dictionary = owner.get_freedom_flight_state()
	var reference: Control = view.controls_menu.surface_reference_view
	check(view.paused and inspected.surface_landmarks.inspection_count == 1 and Landmarks.nearest_nearby(inspected).inspected and reference.visible and site.description in reference.body.text and "Exposure inspected and recorded." in reference.body.text, "Keyboard Inspect did not show the first-inspection description while paused")
	check(landmark_state_without_metadata(inspected) == landmark_state_without_metadata(before) and inspected.surface_marker == marker and owner.get_freedom_observations() == records, "Inspection changed clock, resources, knowledge provenance or the manual marker")
	var first_path := output.path_join("landmark-first-inspection.json")
	check(owner.save_freedom_as(first_path), "First exposure inspection Save refused")
	var metadata := saved_landmark_metadata(first_path)
	check(metadata.get("version") == 1 and metadata.get("inspections") is Array and metadata.inspections.size() == 1 and int(metadata.inspections[0].tick) == int(before.tick), "Saved inspection lost its first authoritative tick")
	view.controls_menu.close_surface_reference()
	check(view.paused and view.landmark_inspect_button.has_focus(), "Exposure Back resumed or lost its invoking action")
	if resume(view): view._process(0.125)
	view.pause_controls("Review the exposure again")
	view.player_input.device = 0
	view.player_input.available_pads[0] = true
	view.player_input.install()
	var repeat_before: Dictionary = owner.get_freedom_flight_state()
	var repeat_records: Dictionary = owner.get_freedom_observations()
	var repeat_path := output.path_join("landmark-repeat-before.json")
	check(owner.save_freedom_as(repeat_path), "Repeated inspection baseline Save refused")
	var repeat_bytes := FileAccess.get_file_as_bytes(repeat_path)
	await activate_marker_button(view, view.landmark_inspect_button, true)
	check(view.paused and reference.visible and "Exposure inspection already recorded." in reference.body.text and owner.get_freedom_flight_state() == repeat_before and owner.get_freedom_observations() == repeat_records, "Controller repeated Inspect changed existing history or lost its description")
	check(owner.save_freedom_as(repeat_path + ".after.json") and FileAccess.get_file_as_bytes(repeat_path + ".after.json") == repeat_bytes and saved_landmark_metadata(repeat_path + ".after.json") == metadata, "Repeated inspection changed full save bytes or its first tick")
	view.controls_menu.close_surface_reference()
	check(shell.select_start(owner, {"mode": "continue", "value": repeat_path, "assets": assets}), "Inspected exposure Save/Continue refused")
	view = shell.current_view
	check(view.paused and view.state.surface_landmarks == repeat_before.surface_landmarks and view.state.surface_marker == repeat_before.surface_marker and owner.get_freedom_observations() == repeat_records, "Continue lost exposure inspection or independent marker/fact provenance")
	check(owner.save_freedom_as(repeat_path + ".continued.json") and FileAccess.get_file_as_bytes(repeat_path + ".continued.json") == repeat_bytes, "Inspected exposure Continue rewrote its complete save")
	shell.free()

func surface_context_event(view: Control, event: InputEvent) -> void:
	# Deliver real events while the fixture's disabled shell prevents automatic
	# clock advancement. Only this event delivery temporarily processes input.
	var view_mode := view.process_mode
	var input_mode: int = view.player_input.process_mode
	var was_processing: bool = view.is_processing()
	view.set_process(false)
	view.process_mode = Node.PROCESS_MODE_ALWAYS
	view.player_input.process_mode = Node.PROCESS_MODE_ALWAYS
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	view.player_input.process_mode = input_mode
	view.process_mode = view_mode
	view.set_process(was_processing)


func surface_context_key(view: Control, pressed: bool, echo := false) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = KEY_E
	event.keycode = KEY_E
	event.pressed = pressed
	event.echo = echo
	surface_context_event(view, event)


func surface_context_pad(view: Control, device: int) -> void:
	for pressed in [true, false]:
		var event := InputEventJoypadButton.new()
		event.device = device
		event.button_index = JOY_BUTTON_A
		event.pressed = pressed
		surface_context_event(view, event)


func check_surface_context(source: String, assets: String, output: String) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("surface_landmark_outside.json"), "assets": assets}), "Context exposure fixture did not Continue"):
		shell.free()
		return
	var view: Control = shell.current_view
	var original: Dictionary = owner.get_freedom_flight_state()
	var records: Dictionary = owner.get_freedom_observations()
	var site := Landmarks.nearest_nearby(original)
	# The existing near-exposure fixture can still be within hatch reach. Walk
	# toward its actual site until the two contextual actions are unambiguous.
	for step in 240:
		original = owner.get_freedom_flight_state()
		site = Landmarks.nearest_nearby(original)
		if site.is_empty() or not site.can_inspect or not original.surface_walk.return_access.within_reach: break
		var walking: Dictionary = original.surface_walk
		var direction: Vector3 = walking.basis.inverse() * (site.position - walking.eye_position)
		var planar := Vector2(direction.x, -direction.z).normalized()
		if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([planar.y, planar.x, 0.0]), false), "Actual context-fixture approach refused"):
			shell.free()
			return
	original = owner.get_freedom_flight_state()
	site = Landmarks.nearest_nearby(original)
	records = owner.get_freedom_observations()
	view.state = original
	view.update_view(0.0)
	if not check(not site.is_empty() and site.can_inspect and not original.surface_walk.return_access.within_reach, "Context fixture lacks a reachable exposure away from the hatch"):
		shell.free()
		return
	check(view.context_source == view.landmark_inspect_button and "E / left click" in view.context_action.text, "Nearby exposure lacks the direct compact action")
	# Presentation-only priority probe; no physical observation/command is forged.
	view.state = original.duplicate(true)
	view.state.surface_walk.return_access.within_reach = true
	view.update_compact_hud()
	check(view.context_source == view.surface_walk_button and owner.get_freedom_flight_state() == original, "Reachable hatch failed to take priority over inspection")
	view.state = original
	view.update_view(0.0)
	if not resume(view): shell.free(); return
	for gate in ["movement", "look", "focus", "modal", "echo"]:
		if gate == "movement": key(KEY_W, true)
		elif gate == "look": key(KEY_RIGHT, true)
		elif gate == "focus":
			view.focused = false
			view.player_input.focused = false
		elif gate == "modal": view.journey_dialog_open = true
		surface_context_key(view, true, gate == "echo")
		surface_context_key(view, false)
		if gate == "movement": key(KEY_W, false)
		elif gate == "look": key(KEY_RIGHT, false)
		elif gate == "focus":
			view.focused = true
			view.player_input.focused = true
		elif gate == "modal": view.journey_dialog_open = false
		check(owner.get_freedom_flight_state() == original and not view.controls_menu.surface_reference_view.visible and not view.paused, "Refused direct inspection changed state or opened UI: " + gate)
	surface_context_key(view, true)
	surface_context_key(view, false)
	var inspected: Dictionary = owner.get_freedom_flight_state()
	var reference: Control = view.controls_menu.surface_reference_view
	check(view.paused and reference.visible and site.description in reference.body.text and inspected.surface_landmarks.inspection_count == 1, "E did not pause and inspect the actual nearby exposure")
	check(landmark_state_without_metadata(inspected) == landmark_state_without_metadata(original) and owner.get_freedom_observations() == records, "Direct inspection changed clock, fuel, actor or observation provenance")
	var path := output.path_join("context-first-inspection.json")
	check(owner.save_freedom_as(path), "Direct inspection Save refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	surface_context_key(view, true)
	surface_context_key(view, false)
	check(owner.get_freedom_flight_state() == inspected and reference.visible, "Paused repeat E changed the inspection or reader")
	view.controls_menu.close_surface_reference()
	view.player_input.device = 19
	view.player_input.available_pads = {19: true, 9: true}
	view.player_input.install()
	if not resume(view): shell.free(); return
	surface_context_pad(view, 9)
	view.player_input.available_pads[19] = false
	surface_context_pad(view, 19)
	view.player_input.available_pads[19] = true
	check(not view.paused and not reference.visible and owner.get_freedom_flight_state() == inspected, "Foreign/disconnected pad gained direct inspection")
	surface_context_pad(view, 19)
	check(view.paused and reference.visible and "already recorded" in reference.body.text, "Selected controller failed to reopen the existing exposure")
	view.controls_menu.close_surface_reference()
	if not resume(view): shell.free(); return
	view.sync_mouse_look()
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		surface_context_event(view, click)
	check(view.paused and reference.visible and owner.save_freedom_as(path + ".repeat.json") and FileAccess.get_file_as_bytes(path + ".repeat.json") == bytes, "Captured click failed to inspect or repeated actions changed full Save bytes")
	view.controls_menu.close_surface_reference()
	check(view.paused, "Direct exposure Back resumed walking")
	key(KEY_W, true)
	view.toggle_pause()
	key(KEY_W, false)
	check(view.paused, "Direct exposure Back weakened the neutral Resume gate")
	# The existing outside-at-hatch Save tests E against its ordinary flight
	# strafe binding, comparing the complete result with the direct C++ command.
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Context return fixture did not Continue")
	view = shell.current_view
	var before_return := output.path_join("context-return-before.json")
	check(view.state.surface_walk.return_access.within_reach and owner.save_freedom_as(before_return), "Context return lacks real within-reach access")
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	check(peer.stage_freedom_continue(before_return) and peer.commit_pending_freedom_start(peer.get_pending_freedom_start().candidate_id) and peer.return_from_freedom_surface_walk(), "Direct C++ context-return reference refused")
	if resume(view):
		surface_context_key(view, true)
		surface_context_key(view, false)
	check(view.paused and not view.surface_walking() and not view.player_input.surface_interaction_enabled, "E failed to return through the actual hatch and pause seated input")
	check(owner.save_freedom_as(before_return + ".actual.json") and peer.save_freedom_as(before_return + ".expected.json") and FileAccess.get_file_as_bytes(before_return + ".actual.json") == FileAccess.get_file_as_bytes(before_return + ".expected.json"), "Direct foot return differs from the exact C++ command result")
	shell.free()


func check_suit_lamp(view: Control, owner: Variant, output: String) -> void:
	var state: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join("lamp-before.json")
	check(owner.save_freedom_as(path), "Suit lamp save oracle refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	var sun := [view.light.light_energy, view.local_light.light_energy, view.planet_environment.ambient_light_energy]
	check(view.paused and view.suit_lamp.visible and view.suit_lamp_enabled and view.suit_lamp_button.visible and view.suit_lamp_button.focus_mode == Control.FOCUS_ALL, "Outside lamp lost default light or accessible paused control")
	check(view.suit_lamp.get_parent() == view.camera and view.suit_lamp.get_world_3d() == view.terrain_camera.get_world_3d() and view.suit_lamp.light_cull_mask == 3 and view.suit_lamp.spot_range <= 25.0 and view.suit_lamp.spot_range >= 18.0 and view.suit_lamp.shadow_enabled, "Suit lamp lost bounded shared-world illumination/occlusion")
	# Real GUI activation changes only the presentation; it cannot Resume.
	# Playback disables the shell's processing. Enable only this GUI control,
	# keeping the view and authoritative simulation disabled during frame waits.
	var button_mode: int = view.suit_lamp_button.process_mode
	view.suit_lamp_button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	view.suit_lamp_button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(view.suit_lamp_button)
	check(view.suit_lamp_button.has_focus() and view.suit_lamp_button.can_process(), "Lamp GUI activation lacks focus or processing")
	key(KEY_ENTER, true)
	await process_frame
	key(KEY_ENTER, false)
	view.suit_lamp_button.process_mode = button_mode
	check(not view.suit_lamp.visible and not view.suit_lamp_enabled and view.suit_lamp_button.text == "Suit lamp: Off" and view.paused, "Actual paused lamp activation failed or resumed walking")
	view.focused = false
	view.suit_lamp_button.pressed.emit()
	view.focused = true
	check(not view.suit_lamp_enabled, "Unfocused lamp callback changed its choice")
	view.suit_lamp_button.pressed.emit()
	check(view.suit_lamp.visible and view.suit_lamp_button.text == "Suit lamp: On" and view.paused, "Lamp could not turn on while retaining pause")
	var offset: Transform3D = view.suit_lamp.transform
	var look: Vector2 = view.look_offset
	view.look_offset += Vector2(0.2, -0.1)
	view.update_view(0.0)
	check(view.suit_lamp.global_transform.is_equal_approx(view.camera.global_transform * offset) and view.suit_lamp.transform == offset, "Lamp did not follow the real ground eye/look")
	view.look_offset = look
	view.update_view(0.0)
	view.suit_lamp_button.pressed.emit() # Leave off to check fresh-view reset below.
	check(view.paused and owner.get_freedom_flight_state() == state and owner.save_freedom_as(output.path_join("lamp-after.json")) and FileAccess.get_file_as_bytes(output.path_join("lamp-after.json")) == bytes, "Lamp inspection/toggle changed complete voyage or simulation clock")
	check(sun == [view.light.light_energy, view.local_light.light_energy, view.planet_environment.ambient_light_energy], "Lamp altered actual sunlight or ambient policy")
	view.suit_lamp_button.release_focus()


func check_paused_return_feedback(view: Control, owner: Variant, output: String) -> void:
	var before: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join("paused-return-before.json")
	check(owner.save_freedom_as(path), "Paused return feedback Save oracle refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	var menu: CanvasLayer = view.controls_menu
	var button: Button = view.surface_walk_menu_button
	check(view.paused and view.focused and view.surface_walking() and menu.panel.visible and button.visible and not button.disabled, "Distant paused return action is unavailable")
	# Enable only the actual button; the replay shell/world remain disabled.
	var button_mode: int = button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await process_frame
	button.grab_focus()
	menu.left_scroll.ensure_control_visible(button)
	check(button.has_focus() and button.can_process(), "Paused return GUI lacks focus or processing")
	key(KEY_ENTER, true)
	await process_frame
	key(KEY_ENTER, false)
	var refusal := str(owner.get_last_error())
	check(not refusal.is_empty(), "Actual distant return did not produce a provider refusal")
	for frame in 3:
		menu._process(0.0)
		await process_frame
	check(menu.panel.visible and menu.message.is_visible_in_tree() and refusal in menu.message.text and menu.controls.status == refusal and view.save_status.text == refusal, "Paused return refusal disappeared behind the menu or on its next frame")
	check(root.get_visible_rect().encloses(menu.message.get_global_rect()), "Paused return refusal is outside the visible viewport")
	check(view.paused and view.focused and button.has_focus() and owner.get_freedom_flight_state() == before and owner.save_freedom_as(output.path_join("paused-return-after.json")) and FileAccess.get_file_as_bytes(output.path_join("paused-return-after.json")) == bytes, "Paused refusal changed focus, clock, actor, craft or complete save")
	button.process_mode = button_mode
	button.release_focus()


func check_visibility_refresh(view: Control, owner: Variant, output: String, label: String, anchor: Dictionary) -> void:
	var path := output.path_join(label + ".json")
	var after := output.path_join(label + "-refresh.json")
	var state: Dictionary = owner.get_freedom_flight_state()
	var model: int = view.staged_model.get_instance_id()
	check(owner.save_freedom_as(path), "Visibility baseline Save failed")
	var bytes := FileAccess.get_file_as_bytes(path)
	for frame in 3: view.update_view(0.0)
	check(view.error.is_empty() and view.paused and owner.get_freedom_flight_state() == state and view.staged_model.get_instance_id() == model and view.staged_model.valid_current_pose(), "Camera refresh changed the paused actor/craft/model")
	check(owner.save_freedom_as(after) and FileAccess.get_file_as_bytes(after) == bytes and saved_anchor(path) == anchor, "Visibility refresh changed complete save or landed anchor")
	check(view.camera.position.is_equal_approx(state.surface_walk.eye_position) and view.camera.transform == view.terrain_camera.transform and view.camera.fov == view.terrain_camera.fov, "Surface cameras lost authoritative eye/basis/FOV")
	check_return_bearing(view)
	check(view.camera.far >= view.camera.position.length() + 16.0 and view.camera.far <= view.terrain_camera.far and view.camera.far / view.camera.near <= 500001.0, "Landed craft remains beyond the close-pass far plane")
	check(view.camera.cull_mask == 3 and view.terrain_camera.cull_mask == 1 and view.camera.get_world_3d() == view.terrain_camera.get_world_3d() and view.light.light_cull_mask == 1 and view.local_light.light_cull_mask == 2, "Visibility repair lost shared terrain occlusion or light/layer ownership")
	check(-view.camera.basis.z.dot((-view.camera.position).normalized()) > 0.9, "Actual backward walking no longer looks toward the retained craft")


func check_far_visibility(source: String, assets: String, output: String, rendered: bool, captures: Array) -> void:
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Far visibility source refused: " + shell.error)
	if failed: shell.free(); return
	var view: Control = shell.current_view
	var model: int = view.staged_model.get_instance_id()
	var original_near: float = view.camera.near
	var original_far: float = view.camera.far
	var anchor := saved_anchor(source.path_join("exited.json"))
	check(not anchor.is_empty() and view.paused and view.surface_walking(), "Far visibility lost the actual landed surface actor")
	# Real supported commands, not a shifted camera or fabricated actor save.
	walk(view, KEY_S, 15000)
	check(view.error.is_empty() and view.camera.position.length() > 250.0 and view.staged_model.get_instance_id() == model, "Actual supported walk failed to reach beyond250m; do not relocate/flatten the fixture")
	if failed: shell.free(); return
	view.pause_controls("Inspecting the actual distant landed craft")
	check_visibility_refresh(view, owner, output, "visibility-far", anchor)
	if failed: shell.free(); return
	if rendered:
		var prior_cockpit: bool = view.cockpit
		await capture_phase(view, owner, output, {"label": "visibility-far"}, captures)
		view.cockpit = prior_cockpit
		view.update_view(0.0)
	var far_path := output.path_join("visibility-far.json")
	var far_bytes := FileAccess.get_file_as_bytes(far_path)
	var far_state: Dictionary = owner.get_freedom_flight_state()
	check(shell.select_start(owner, {"mode": "continue", "value": far_path, "assets": assets}), "Distant actor paused Continue refused: " + shell.error)
	if failed: shell.free(); return
	view = shell.current_view
	model = view.staged_model.get_instance_id()
	check(view.paused and view.surface_walking() and owner.get_freedom_flight_state() == far_state and FileAccess.get_file_as_bytes(far_path) == far_bytes, "Far Continue changed the actor, craft or source bytes")
	check_visibility_refresh(view, owner, output, "visibility-continued", anchor)
	walk(view, KEY_W, 6000)
	check(view.camera.position.length() < 180.0 and view.staged_model.get_instance_id() == model, "Actual return did not cross the old200m clip boundary")
	view.pause_controls("Inspecting the returned near view")
	check_visibility_refresh(view, owner, output, "visibility-near", anchor)
	check(view.camera.near == original_near and view.camera.far == original_far, "Near on-foot return retained unnecessarily distant depth")
	if failed: shell.free(); return
	if rendered:
		var prior_cockpit: bool = view.cockpit
		await capture_phase(view, owner, output, {"label": "visibility-near"}, captures)
		view.cockpit = prior_cockpit
		view.update_view(0.0)
	walk(view, KEY_W, 9000)
	view.surface_walk_button.pressed.emit()
	check(view.error.is_empty() and view.paused and not view.surface_walking() and view.staged_model.get_instance_id() == model and view.camera.near == original_near and view.camera.far == original_far, "Far walk/reboard lost the retained craft or exact seated depth")
	shell.free()


func check_ground_controller_look(view: Control, owner: Variant, output: String) -> void:
	var settings: Dictionary = view.player_input.settings.duplicate(true)
	var persist: bool = view.player_input.persist
	view.player_input.persist = false
	check(view.player_input.apply_preferences({"deadzone": 0.18, "curve": 1.4, "look_speed": 1.6, "invert_look": false, "prompts": 0}), "Ground default look preferences refused")
	var device: int = view.player_input.device
	var look: Vector2 = view.look_offset
	var before: Dictionary = owner.get_freedom_flight_state()
	var saved := output.path_join("controller-look-before.json")
	check(owner.save_freedom_as(saved), "Ground controller baseline Save refused")
	var bytes := FileAccess.get_file_as_bytes(saved)
	view.player_input.device = 0
	for axis in [JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
		var event := InputEventJoypadMotion.new()
		event.device = 0
		event.axis = axis
		event.axis_value = 1.0 if axis == JOY_AXIS_RIGHT_X else -1.0
		Input.parse_input_event(event)
	Input.flush_buffered_events()
	view.surface_controls(0.1)
	check(view.look_offset.is_equal_approx(Vector2(look.x + 0.18, look.y - 0.18)), "Ground right-stick default rate/sign changed")
	view.look_offset = look
	check(view.player_input.apply_preferences({"deadzone": 0.18, "curve": 1.4, "look_speed": 2.4, "invert_look": true, "prompts": 0}), "Ground stick look preferences refused")
	view.surface_controls(0.1)
	check(view.look_offset.is_equal_approx(Vector2(look.x + 0.27, look.y + 0.27)), "Ground right stick ignored speed/inversion preferences")
	view.paused = true
	var paused_look: Vector2 = view.look_offset
	view.surface_controls(0.1)
	check(view.look_offset == paused_look and owner.get_freedom_flight_state() == before and owner.save_freedom_as(saved) and FileAccess.get_file_as_bytes(saved) == bytes, "Configured ground look bypassed pause or changed complete C++ save")
	for axis in [JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y]:
		var event := InputEventJoypadMotion.new()
		event.device = 0
		event.axis = axis
		event.axis_value = 0.0
		Input.parse_input_event(event)
	Input.flush_buffered_events()
	view.player_input.settings = settings
	view.player_input.persist = persist
	view.player_input.device = device
	view.look_offset = look
	check(resume(view), "Ground controller probe did not restore neutral Resume")


func check_ground_pause_heading(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	for trigger in ["pause", "focus", "menu", "save"]:
		if not check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("exited.json"), "assets": assets}), "Heading source Continue refused"): return
		var view: Control = shell.current_view
		if not check(view.surface_walking() and resume(view), "Heading source did not resume on foot"): return
		if trigger == "pause":
			check_ground_controller_look(view, owner, output)
			var original := source.path_join("exited.json")
			var original_bytes := FileAccess.get_file_as_bytes(original)
			var keyboard_reference: Variant = ClassDB.instantiate("FreedomBridge")
			if not check(keyboard_reference.stage_freedom_continue(original), "Keyboard ground reference Continue refused"): return
			var keyboard_pending: Dictionary = keyboard_reference.get_pending_freedom_start()
			if not check(keyboard_reference.commit_pending_freedom_start(keyboard_pending.candidate_id), "Keyboard ground reference commit refused"): return
			# The ground view retains look in Godot's single-precision Vector2;
			# construct the expected rightward command with that same public type.
			var expected_look := Vector2(-float(owner.get_freedom_flight_state().surface_walk.heading), 0)
			expected_look += Vector2(float(view.player_input.settings.look_speed), 0) * (1.0 / 120.0)
			var keyboard_heading := wrapf(-float(expected_look.x), -PI, PI)
			if not check(keyboard_reference.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, keyboard_heading]), false) and keyboard_reference.save_freedom_as(output.path_join("keyboard-ground-reference.json")), "Keyboard ground reference tick refused"): return
			key(KEY_RIGHT, true)
			view._process(1.0 / 120.0)
			key(KEY_RIGHT, false)
			if not check(view.error.is_empty() and owner.save_freedom_as(output.path_join("keyboard-ground-actual.json")) and FileAccess.get_file_as_bytes(output.path_join("keyboard-ground-actual.json")) == FileAccess.get_file_as_bytes(output.path_join("keyboard-ground-reference.json")), "Actual ground keyboard look diverged from the independent C++ walking tick"): return
			if not check(FileAccess.get_file_as_bytes(original) == original_bytes, "Keyboard ground look changed its source Save"): return
		var button := InputEventMouseButton.new()
		button.button_index = MOUSE_BUTTON_RIGHT
		button.pressed = true
		button.button_mask = MOUSE_BUTTON_MASK_RIGHT
		Input.parse_input_event(button)
		Input.flush_buffered_events()
		var motion := InputEventMouseMotion.new()
		motion.relative = Vector2(150, 0)
		motion.button_mask = MOUSE_BUTTON_MASK_RIGHT
		# The deterministic shell disables automatic event delivery/processing;
		# feed the real mouse event to the public handler, then a normal frame.
		view._input(motion)
		view._process(1.0 / 120.0)
		var release: InputEventMouseButton = button.duplicate()
		release.pressed = false
		release.button_mask = 0
		Input.parse_input_event(release)
		Input.flush_buffered_events()
		var turned: Dictionary = owner.get_freedom_flight_state()
		if not check(view.error.is_empty() and absf(turned.surface_walk.heading) > 0.1, "Actual mouse turn did not earn a nonzero heading"): return
		var prefix := output.path_join("heading-" + trigger)
		if not check(owner.save_freedom_as(prefix + "-turned.json"), "Earned heading Save refused"): return
		var before_bytes := FileAccess.get_file_as_bytes(prefix + "-turned.json")
		var reference: Variant = ClassDB.instantiate("FreedomBridge")
		if not check(reference.stage_freedom_continue(prefix + "-turned.json"), "Neutral reference Continue refused: " + str(reference.get_last_error())): return
		var pending: Dictionary = reference.get_pending_freedom_start()
		if not check(not pending.is_empty() and reference.commit_pending_freedom_start(pending.candidate_id), "Neutral reference commit refused: " + str(reference.get_last_error())): return
		if not check(reference.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, turned.surface_walk.heading]), false) and reference.save_freedom_as(prefix + "-reference.json"), "Neutral same-heading reference refused: " + str(reference.get_last_error())): return
		if trigger == "pause": view.toggle_pause()
		elif trigger == "focus":
			view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
			view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
		elif trigger == "menu": view.instruments_button.pressed.emit()
		else:
			view.controls_menu.export_save_button.pressed.emit()
			check(view.save_dialog.visible, "Surface Save As did not open")
			view.save_dialog.hide()
			view.save_dialog.file_selected.emit(prefix + "-selected.json")
			check(FileAccess.get_file_as_bytes(prefix + "-selected.json") == before_bytes, "Actual Save As changed paused heading/actor/craft")
		view._process(0.125)
		check(view.paused and view.error.is_empty() and owner.get_freedom_flight_state() == turned and owner.save_freedom_as(prefix + "-paused.json") and FileAccess.get_file_as_bytes(prefix + "-paused.json") == before_bytes, "Pause/focus/menu/save changed the committed voyage: " + trigger)
		if not resume(view): return
		view._process(1.0 / 120.0)
		var after: Dictionary = owner.get_freedom_flight_state()
		check(after.surface_walk.heading == turned.surface_walk.heading and after.tick.to_int() == turned.tick.to_int() + 1, "First neutral resumed tick reset the earned heading or clock: " + trigger)
		if not check(owner.save_freedom_as(prefix + "-resumed.json"), "Resumed heading Save refused"): return
		check(after == reference.get_freedom_flight_state() and FileAccess.get_file_as_bytes(prefix + "-resumed.json") == FileAccess.get_file_as_bytes(prefix + "-reference.json"), "Neutral resume changed actor/body/resources beyond its genuine reference tick: " + trigger)
		var saved_before: Dictionary = JSON.parse_string(before_bytes.get_string_from_utf8())
		var saved_after: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(prefix + "-resumed.json"))
		check(saved_after.actor.foot_position_metres == saved_before.actor.foot_position_metres and saved_anchor(prefix + "-resumed.json") == saved_anchor(prefix + "-turned.json"), "Stationary resume moved foot or landed anchor: " + trigger)
		check(view.camera.position.is_equal_approx(after.surface_walk.eye_position) and view.camera.transform == view.terrain_camera.transform, "Resumed ground eye/registered cameras disagreed: " + trigger)
	# Reinstall the original genuine seated fixture before the old canonical
	# input route; none of these four short transactions enters its goldens.
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("source.json"), "assets": assets}), "Heading check did not reset the canonical route")


func check_walk_feedback(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	var view: Control = shell.current_view
	view.surface_walk_button.pressed.emit()
	check(view.paused and view.surface_walking() and view.state.surface_walk.get("movement_blockage") == null, "Exit invented an uncommitted blockage")
	walk(view, KEY_W, 120)
	check(view.error.is_empty() and not view.paused and view.state.surface_walk.movement_blockage == "craft_obstruction" and "Last step blocked" in view.primary_status.text and "Wayfarer hull" in view.primary_status.text, "Actual physical walking into the craft lost compact feedback or paused")
	view.pause_controls("Inspecting last committed movement")
	var frozen: Dictionary = owner.get_freedom_flight_state()
	var saved := output.path_join("walk-blocked.json")
	check(owner.save_freedom_as(saved), "Blocked actual walk Save As refused")
	var bytes := FileAccess.get_file_as_bytes(saved)
	view._process(0.125)
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([0, 0]), false) and owner.get_freedom_flight_state() == frozen and owner.save_freedom_as(saved) and FileAccess.get_file_as_bytes(saved) == bytes and "Wayfarer hull" in view.primary_status.text, "Pause/refused advance/getters changed committed blockage or complete voyage")
	var continued: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(continued.stage_freedom_continue(saved), "Blocked-walk reference Continue refused: " + str(continued.get_last_error())): return
	var pending: Dictionary = continued.get_pending_freedom_start()
	if not check(not pending.is_empty() and continued.commit_pending_freedom_start(pending.candidate_id), "Blocked-walk reference commit refused: " + str(continued.get_last_error())): return
	if not check(continued.get_freedom_flight_state().surface_walk.movement_blockage == null and continued.save_freedom_as(output.path_join("walk-blocked-continued.json")) and FileAccess.get_file_as_bytes(output.path_join("walk-blocked-continued.json")) == bytes, "Continue invented prior feedback or changed complete save"): return
	var original_settings: Dictionary = view.player_input.settings.duplicate(true)
	var original_persist: bool = view.player_input.persist
	view.player_input.persist = false
	var custom := original_settings.duplicate(true)
	custom.merge({"walk_forward": KEY_T, "walk_backward": KEY_G, "walk_left": KEY_F, "walk_right": KEY_H}, true)
	check(view.player_input.apply_preferences(custom), "Custom surface walking keys refused")
	key(KEY_T, true)
	key(KEY_G, true)
	view.toggle_pause()
	check(view.paused and not view.controls_armed and not view.surface_interaction_neutral(), "Opposed configured surface keys bypassed Resume/interaction")
	key(KEY_T, false)
	key(KEY_G, false)
	check(owner.save_freedom_as(saved) and FileAccess.get_file_as_bytes(saved) == bytes, "Walking settings changed complete surface save")
	if resume(view):
		check("T/F/G/H" in view.hint.text, "Surface help failed to show configured walking keys")
		key(KEY_S, true)
		check(view.surface_controls(0.0)[0] == 0.0, "Removed S continued moving surface actor")
		key(KEY_S, false)
		key(KEY_G, true)
		check(view.surface_controls(0.0) == PackedFloat64Array([-1, 0, 0]), "Custom backward key failed to reach surface controls")
		view._process(0.125)
		key(KEY_G, false)
	check(view.player_input.apply_preferences(original_settings), "Surface walking settings restore refused")
	view.player_input.persist = original_persist
	check(view.error.is_empty() and not view.paused and view.state.surface_walk.movement_blockage == null and not "Last step blocked" in view.primary_status.text, "Supported movement retained a misleading obstacle warning")
	check(continued.advance_freedom_surface_walk(0.125, PackedFloat64Array([-1, 0, 0]), false) and continued.save_freedom_as(output.path_join("walk-feedback-reference.json")) and owner.save_freedom_as(output.path_join("walk-feedback-after.json")) and FileAccess.get_file_as_bytes(output.path_join("walk-feedback-reference.json")) == FileAccess.get_file_as_bytes(output.path_join("walk-feedback-after.json")), "Feedback changed actual walk/clock/resources compared with independent Continue")
	# Reset this short real-input probe before the inherited canonical trace.
	check(shell.select_start(owner, {"mode": "continue", "value": source.path_join("source.json"), "assets": assets}), "Feedback check did not restore the canonical source")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	check_depth_inputs()
	if failed: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var wave := ProjectSettings.globalize_path("user://surface-audio.wav")
	var recording := FileAccess.open(wave, FileAccess.WRITE)
	recording.store_buffer(load("res://tests/recorded_ship_audio_test.gd").fixture(2))
	recording.close()
	shell.configure_audio({"audio_hum": wave, "audio_propulsion": wave, "audio_persist": false})
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Surface test output unavailable"): shell.free(); quit(1); return
	await check_personal_surface_marker(args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	await check_surface_landmarks(args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	check_surface_context(args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	if not check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("source.json"), "assets": args[1]}), "Landed source did not stage: " + shell.error): shell.free(); quit(1); return
	check_ground_pause_heading(shell, owner, args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	check_walk_feedback(shell, owner, args[0], args[1], args[2])
	if failed: shell.free(); quit(1); return
	var view: Control = shell.current_view
	var model_id: int = view.staged_model.get_instance_id()
	var audio_id: int = shell.audio_session.get_instance_id()
	var captures: Array = []
	var rendered := DisplayServer.get_name() != "headless"
	check(view.paused and not view.surface_walking() and view.state.surface.landed, "Continue lost landed seated readiness")
	check_rated_rise_actor(view, owner, args[2], true)
	check(not view.ship_marker.visible and view.home_marker.label_text == "HOME", "Seated landing showed a ground ship bearing or changed HOME")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Seated view exposed the on-foot lamp")
	check(not view.hud_scroll.visible and "Landed" in view.primary_status.text and view.context_action.disabled, "Landed compact HUD lost actual mode or allowed a paused maneuver")
	view.surface_walk_button.pressed.emit()
	check(view.paused and view.surface_walking() and view.liftoff_button.disabled and view.assist_button.disabled, "Actual exit button did not transfer input and pause")
	check_rated_rise_actor(view, owner, args[2], false)
	check_return_bearing(view)
	check(view.state.surface_walk.return_access.within_reach and view.state.surface_walk.return_access.distance_metres == 0.0, "Exit failed to identify its exact reachable ground access")
	check_return_fallback(view)
	var malformed: Dictionary = view.state.duplicate(true)
	malformed.surface_walk.return_access.position = Vector3(NAN, 0, 0)
	check(not FlightView.valid_state(malformed), "Nonfinite return point accepted")
	malformed = view.state.duplicate(true)
	malformed.surface_walk.return_access.distance_metres = -1.0
	check(not FlightView.valid_state(malformed), "Negative return range accepted")
	check(view.light.light_cull_mask == 1 and view.local_light.light_cull_mask == 2 and view.light.light_energy == 1.0 and is_equal_approx(view.local_light.light_energy, NativeEnvironment.sun_visibility(view.state)), "Suited view lost separate terrain/local sunlight")
	check(view.surface_walk_menu_button.focus_mode == Control.FOCUS_ALL and not view.surface_walk_menu_button.disabled and view.controls_menu.saved_assist_button.disabled, "Surface menu lost controller focus or assistance guard")
	await check_suit_lamp(view, owner, args[2])
	var hold_before: Dictionary = owner.get_freedom_flight_state()
	check(view.controls_menu.saved_hold_button.disabled and not hold_before.orbit_hold.available and not owner.set_freedom_hold(true) and owner.get_freedom_flight_state() == hold_before, "Outside pilot gained ship orbit hold")
	check(not view.orbital_forecast.visible and not view.jump_button.visible and not view.assist_button.visible, "Outside HUD retained unrelated flight actions")
	check("On foot" in view.primary_status.text and "Suit equipped" in view.primary_status.text and not "Reference altitude" in view.primary_status.text, "Suited compact HUD retained ship motion")
	var help_before: Dictionary = owner.get_freedom_flight_state()
	view.controls_menu.show_basics()
	view.controls_menu.basics.page = 9
	view.controls_menu.basics.refresh()
	check(view.controls_menu.basics.visible and "On foot" in view.controls_menu.basics._paused.text and "Return through hatch" in view.controls_menu.basics.body.text and "oxygen countdown" in view.controls_menu.basics.body.text, "Actual suited help lost actor, return or endurance boundary")
	view._process(0.125)
	check(view.paused and owner.get_freedom_flight_state() == help_before, "Suited reference advanced or replaced the C++ actor")
	view.controls_menu.close_basics()
	for button in view.controls_menu.saved_port_buttons.values():
		check(button.disabled, "Outside controls menu retained a flight action")
	checkpoint(owner, args[0], args[2], "exited")
	check_surface_position(view, owner, args[2].path_join("exited.json"))
	var exited_position := [view.state.surface_walk.latitude, view.state.surface_walk.longitude]
	var title_save := args[2].path_join("title-paused.json")
	check(owner.save_freedom_as(title_save), "Outside pre-Title Save refused")
	var title_hash := FileAccess.get_sha256(title_save)
	view.controls_menu.title_button.pressed.emit()
	view.toggle_pause()
	view._process(0.125)
	check(shell.title_origin == view and view.paused and view.surface_walking() and owner.save_freedom_as(title_save) and FileAccess.get_sha256(title_save) == title_hash, "Outside Title advanced/changed actor or craft")
	shell.title_confirmation.get_cancel_button().pressed.emit()
	check(shell.current_view == view and view.paused and view.surface_walking() and root.gui_get_focus_owner() == view.controls_menu.title_button, "Outside Title Cancel lost paused selection")
	shell.audio_session.refresh()
	check(shell.audio_session.audio.diagnostics().targets == Vector3.ZERO, "Outside actor retained cockpit or propulsion playback")
	var frozen: Dictionary = owner.get_freedom_flight_state()
	for size in [0, 2, 4]:
		var controls := PackedFloat64Array()
		controls.resize(size)
		check(not owner.advance_freedom_surface_walk(0.125, controls, false), "Wrong walking buffer dimensions accepted")
	for value in [NAN, INF, -INF]:
		check(not owner.advance_freedom_surface_walk(value, PackedFloat64Array([0, 0, 0]), false), "Nonfinite elapsed time accepted")
		for axis in 3:
			var controls := PackedFloat64Array([0, 0, 0])
			controls[axis] = value
			check(not owner.advance_freedom_surface_walk(0.125, controls, false), "Nonfinite walking axis accepted")
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([2, 0, 0]), false) and not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([0, 0, 4]), false), "Out-of-range walking command accepted")
	check(owner.get_freedom_flight_state() == frozen, "Invalid input mutated the live voyage")
	view._process(0.125)
	check(owner.get_freedom_flight_state().tick == frozen.tick, "Paused walking advanced shared time")
	check(view.camera.position.is_equal_approx(view.state.surface_walk.eye_position) and view.terrain_camera.transform == view.camera.transform, "Camera lost C++ ground eye or terrain frame")
	key(KEY_S, true)
	view.toggle_pause()
	check(view.paused, "Held walking key bypassed neutral resume")
	key(KEY_S, false)
	if failed: shell.free(); quit(1); return
	if rendered: await capture_phase(view, owner, args[2], {"label": "exited"}, captures)
	walk(view, KEY_S, 600)
	checkpoint(owner, args[0], args[2], "halfway")
	check_surface_position(view, owner, args[2].path_join("halfway.json"))
	var halfway_position := [view.state.surface_walk.latitude, view.state.surface_walk.longitude]
	check(absf(halfway_position[0] - exited_position[0]) + absf(halfway_position[1] - exited_position[1]) > 0.0000000001, "Actual walking left the surface position fixed at the ship")
	check(not view.state.surface_walk.return_access.within_reach and view.state.surface_walk.return_access.distance_metres > 3.5, "Distant RETURN cue incorrectly claims reach")
	check_return_bearing(view)
	if failed: shell.free(); quit(1); return
	check(view.context_source == view.surface_walk_button and not view.context_action.disabled, "Actual ground return action is inaccessible")
	view.context_action.pressed.emit()
	check(view.surface_walking() and not view.paused and view.error.is_empty(), "Distant return changed input ownership")
	check(not view.save_status.text.is_empty() and view.save_status.text.left(180) in view.primary_status.text, "Actual return refusal disappeared behind the hidden detail wall")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	var paused_tick: String = view.state.tick
	view._process(0.125)
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and view.state.tick == paused_tick, "Focus loss advanced ground time or resumed silently")
	await check_paused_return_feedback(view, owner, args[2])
	if failed: shell.free(); quit(1); return
	# Continue re-stages the actual actor, craft and terrain in the same root.
	view.controls_menu.load_button.pressed.emit()
	shell.load_dialog.file_selected.emit(args[2].path_join("halfway.json"))
	shell.confirm_load()
	check(shell.load_origin == null and shell.current_view != view and shell.error.is_empty(), "Outside in-game Load did not stage")
	view = shell.current_view
	check(view.paused and view.surface_walking() and shell.audio_session.get_instance_id() == audio_id, "Outside Continue lost paused actor or duplicated audio owner")
	check(view.state.surface_walk.latitude == halfway_position[0] and view.state.surface_walk.longitude == halfway_position[1], "Outside Continue changed the committed own-position readout")
	check_surface_position(view, owner, args[2].path_join("halfway.json"))
	check(view.suit_lamp_enabled and view.suit_lamp.visible and view.suit_lamp_button.text == "Suit lamp: On", "Outside Continue persisted the presentation-only lamp choice or lost default illumination")
	check_return_bearing(view)
	walk(view, KEY_S, 600)
	checkpoint(owner, args[0], args[2], "far")
	check_surface_position(view, owner, args[2].path_join("far.json"))
	check(absf(view.state.surface_walk.latitude - view.state.latitude) + absf(view.state.surface_walk.longitude - view.state.longitude) > 0.0000000001, "Far ground position substituted the retained craft position")
	check(view.camera.position.is_equal_approx(view.state.surface_walk.eye_position) and view.camera.position.length() > 20.0, "Walking did not move the rendered eye away from the craft")
	check_return_bearing(view)
	if rendered: await capture_phase(view, owner, args[2], {"label": "far"}, captures)
	var bad_path := args[2].path_join("corrupt.json")
	var bad: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[2].path_join("far.json")))
	bad.actor.planet = 0
	var file := FileAccess.open(bad_path, FileAccess.WRITE)
	file.store_string(JSON.stringify(bad))
	file.close()
	frozen = owner.get_freedom_flight_state()
	check(not shell.select_start(owner, {"mode": "continue", "value": bad_path, "assets": args[1]}) and owner.get_freedom_flight_state() == frozen and shell.current_view == view, "Corrupt outside Continue replaced the live owner")
	walk(view, KEY_W, 1200)
	checkpoint(owner, args[0], args[2], "returned")
	check_surface_position(view, owner, args[2].path_join("returned.json"))
	if rendered: await capture_phase(view, owner, args[2], {"label": "returned"}, captures)
	var retained_model: int = view.staged_model.get_instance_id()
	view.context_action.pressed.emit()
	check(view.paused and not view.surface_walking() and view.staged_model.get_instance_id() == retained_model and retained_model != model_id, "Nearby return failed or replaced the continued ship")
	check(not view.ship_marker.visible, "Ground ship bearing remained visible after reboarding")
	check(view.state.surface_walk.is_empty(), "Reboarding retained an outside position projection")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Suit lamp remained active after reboarding")
	checkpoint(owner, args[0], args[2], "seated")
	check(shell.select_start(owner, {"mode": "continue", "value": args[2].path_join("seated.json"), "assets": args[1]}), "Returned seated Continue did not stage")
	view = shell.current_view
	check_surface_action_reference(view, owner, args[2], "liftoff_freedom_surface")
	if resume(view):
		for batch in 8: view._process(0.125)
	checkpoint(owner, args[0], args[2], "lifted")
	check(not view.state.surface.landed and view.error.is_empty(), "Returned pilot cannot lift off with real thrust")
	check(not view.ship_marker.visible, "Ground ship bearing remained visible in flight")
	check(not view.suit_lamp.visible and not view.suit_lamp_button.visible, "Suit lamp remained active during flight")
	check_airborne_liftoff_refusal(view, owner, args[2])
	view.pause_controls("Inspecting an accepted landing request without advancing")
	check_surface_action_reference(view, owner, args[2], "request_freedom_landing")
	# Declared destruction is an explicit fixture, not an invented exposure event.
	check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("pending-loss.json"), "assets": args[1]}), "Pending outside loss failed to stage")
	check(shell.recovery_overlay != null and shell.current_view.surface_walking() and shell.current_view.paused, "Pending loss lost outside actor or explicit recovery choice")
	frozen = owner.get_freedom_flight_state()
	check(not owner.advance_freedom_surface_walk(0.125, PackedFloat64Array([1, 0, 0]), false) and owner.get_freedom_flight_state() == frozen, "Recorded loss allowed outside movement")
	check(shell.select_start(owner, {"mode": "recovery", "assets": args[1]}), "Outside replacement failed to stage")
	check(shell.current_view is WalkView and shell.current_view.paused and shell.recovery_overlay == null, "Recovery failed to restore one paused station actor")
	checkpoint(owner, args[0], args[2], "recovered")
	if not failed: await check_far_visibility(args[0], args[1], args[2], rendered, captures)
	var report := FileAccess.open(args[2].path_join("surface-walk-report.json"), FileAccess.WRITE)
	if report != null:
		report.store_string(JSON.stringify({"pass": not failed, "scope": "near-ground native suited walking, complete save parity; instant hatch transfer; no oxygen/environment damage policy", "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if rendered else "headless", "asset_license_records": "native-assets/stowed/licenses", "operating_asset_provenance": "native-assets/operating/provenance.json", "captures": captures}, "\t") + "\n")
		report.close()
	shell.free()
	DirAccess.remove_absolute(wave)
	print("Native surface walk: %d failures" % int(failed))
	quit(1 if failed else 0)
