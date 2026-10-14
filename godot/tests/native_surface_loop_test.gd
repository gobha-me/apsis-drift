extends "res://studies/captures/native_voyage_capture.gd"
## One physical route from ordinary New Game or an explicitly selected policy-2
## station Save of that same starter; no initialized ground start.
const Decimal = preload("res://tests/native_roundtrip_test.gd")
const LABELS = ["station-start", "seated", "departed", "atmosphere-entry", "cruise-start", "cruise-end", "landing-ready", "landed", "surface-exited", "surface-halfway", "surface-away", "surface-back", "surface-seated", "surface-airborne", "space-exit", "home-intercept", "near-port", "captured", "station-return"]

func start_options(trace: Dictionary, args: PackedStringArray) -> Dictionary:
	if trace.get("frame_policy", 1) == 2:
		return {"mode": "continue", "value": args[0].path_join("station-start.json"), "assets": args[1]}
	return super.start_options(trace, args)

func expected_frame(label: String) -> String:
	return "system_inertial" if label in ["departed", "atmosphere-entry", "cruise-start", "cruise-end", "landing-ready", "surface-airborne", "space-exit", "home-intercept", "near-port", "orbit-before-hold", "orbit-hold-selected", "orbit-hold-running", "orbit-hold-disabled"] else "planet_relative_inertial"

func checkpoint_labels() -> Array:
	return LABELS

func labels_for_trace(trace: Variant) -> Array:
	var labels: Array = LABELS.duplicate()
	if trace is Dictionary and trace.get("schema_version") == 6:
		for i in 4:
			labels.insert(3 + i, ["orbit-before-hold", "orbit-hold-selected", "orbit-hold-running", "orbit-hold-disabled"][i])
		if trace.get("flight_profile") == "advanced": labels.insert(labels.find("landing-ready") + 1, "manual-gear")
	return labels

func surface_commands() -> bool:
	return true

func valid_manifest(value: Variant) -> bool:
	if not value is Dictionary: return false
	var profile: bool = value.get("schema_version") == 6 and value.get("route") == "physical-profile" and value.get("frame_policy") == 2 and value.get("flight_profile") in ["assisted", "advanced"]
	var selected: bool = profile or value.get("schema_version") == 5 and value.get("route") == "selected-surface-loop" and value.get("frame_policy") == 2
	if not selected and (value.get("schema_version") != 4 or value.get("route") != "surface-loop" or value.has("frame_policy")): return false
	if value.get("seed") != "42" or value.get("physical_catalog") != 2 or value.get("physical_ephemeris") != 2 or value.get("terrain_source_lod") != 8 or value.get("terrain_relief_version") != 0 or value.get("uninterrupted_match") != true: return false
	if not Decimal.bounded_decimal(value.get("final_tick")) or value.final_tick.length() > 7 or int(value.final_tick) > 1200000: return false
	if profile:
		if value.get("hold_ticks") != 120: return false
		if value.flight_profile == "assisted":
			if value.get("hold_active_ticks") != 120 or not Checkpoints.finite_number(value.get("hold_force_ticks")) or value.hold_force_ticks != int(value.hold_force_ticks) or value.hold_force_ticks < 1 or value.hold_force_ticks > 120: return false
		elif value.get("hold_active_ticks") != 0 or value.get("hold_force_ticks") != 0: return false
	var labels := labels_for_trace(value)
	var rows: Variant = value.get("checkpoints")
	if not rows is Array or rows.size() != labels.size(): return false
	var previous := 0
	for i in rows.size():
		var row: Variant = rows[i]
		if not row is Dictionary or row.get("label") != labels[i] or row.get("file") != labels[i] + ".json" or row.get("next_file") != labels[i] + "-next.json": return false
		for field in ["tick", "checksum", "next_tick", "next_checksum", "system_id", "flight_quanta"]:
			if not Decimal.bounded_decimal(row.get(field)): return false
		if row.tick.length() > 7 or row.next_tick.length() > 7: return false
		var tick := int(row.tick)
		if tick < previous or tick > 1200000 or int(row.next_tick) != tick + 1: return false
		previous = tick
		for field in ["attached", "walking", "surface_walking", "landed", "station_available", "seated"]:
			if not row.get(field) is bool: return false
		if selected and row.get("coordinate_frame") != expected_frame(row.label): return false
		if profile:
			if not row.get("assistance") is bool or row.assistance != (i < 2 or value.flight_profile == "assisted"): return false
			if not row.get("hold_enabled") is bool or row.hold_enabled != (row.label in ["orbit-hold-selected", "orbit-hold-running"]): return false
			if not row.get("orbit_classification") in ["stable", "decaying", "impact", "escape"]: return false
			if row.label in ["orbit-before-hold", "orbit-hold-selected"] and row.orbit_classification != "stable": return false
		if row.walking and row.surface_walking or row.surface_walking and (not row.landed or row.attached): return false
		if row.get("jump_phase") != "idle" or row.get("jump_charges") != 3: return false
		for field in ["altitude_metres", "air_speed_metres_per_second", "air_density", "atmosphere_edge_metres", "terrain_elevation_metres"]:
			if not Checkpoints.finite_number(row.get(field)): return false
		var position: Variant = row.get("position_metres")
		if not position is Array or position.size() != 3: return false
		for component in position:
			if not Checkpoints.finite_number(component): return false
	if profile:
		# Selection and disable are zero-time requests; the flown hold is 120 ticks.
		if rows[3].tick != rows[2].tick or rows[4].tick != rows[3].tick or int(rows[5].tick) != int(rows[4].tick) + 120 or rows[6].tick != rows[5].tick: return false
	return str(previous) == value.final_tick

func manifest_refusals(trace: Dictionary) -> bool:
	for damage in ["shape", "nonfinite", "path", "version", "clock", "owner", "terrain", "actor", "frame"]:
		var bad: Dictionary = trace.duplicate(true)
		if damage == "shape": bad.checkpoints[0].position_metres.append(0.0)
		elif damage == "nonfinite": bad.checkpoints[0].altitude_metres = NAN
		elif damage == "path": bad.checkpoints[0].file = "../other.json"
		elif damage == "version": bad.schema_version = 1
		elif damage == "clock": bad.checkpoints[0].next_tick = "18446744073709551615"
		elif damage == "owner": bad.checkpoints[0].system_id = "18446744073709551616"
		elif damage == "terrain": bad.terrain_relief_version = 1
		elif damage == "actor": bad.checkpoints[0].surface_walking = true
		else: bad.frame_policy = 99
		if not check(not valid_manifest(bad), "Invalid surface loop manifest accepted: " + damage): return false
	if trace.get("frame_policy", 1) == 2:
		var bad_frame: Dictionary = trace.duplicate(true)
		bad_frame.checkpoints[2].coordinate_frame = "planet_relative_inertial"
		if not check(not valid_manifest(bad_frame), "Selected departure accepted the wrong canonical frame"): return false
	if trace.get("schema_version") == 6:
		for index in [3, 4, 5, 6]:
			var bad_duration: Dictionary = trace.duplicate(true)
			bad_duration.checkpoints[index].tick = str(int(bad_duration.checkpoints[index].tick) + 1)
			bad_duration.checkpoints[index].next_tick = str(int(bad_duration.checkpoints[index].next_tick) + 1)
			if not check(not valid_manifest(bad_duration), "Physical hold accepted contradictory checkpoint timing"): return false
		for field in ["flight_profile", "hold_ticks", "hold_active_ticks"]:
			var bad_profile: Dictionary = trace.duplicate(true)
			bad_profile.erase(field)
			if not check(not valid_manifest(bad_profile), "Malformed physical profile accepted: " + field): return false
		for op in ["hold_on", "hold_off"]:
			if not check(valid_record([op, 0, []], labels_for_trace(trace), false, true, true) and not valid_record([op, 0, []], LABELS, false, true) and not valid_record([op, 0, [1]], labels_for_trace(trace), false, true, true), "Profile hold grammar escaped scope/bounds"): return false
	var zero := "0000000000000000"
	for bad in [["surface_walk", 16, [zero, zero, zero]], ["surface_walk", 1, [zero, zero]], ["surface_exit", 1, []], ["land", 0, [1]], ["surface_walk", 1, ["000000000000f07f", zero, zero]]]:
		if not check(not valid_record(bad, LABELS, false, true), "Invalid ground command accepted"): return false
	return check(not valid_record(["surface_exit", 0, []]) and not valid_record(["surface_walk", 1, [zero, zero, zero]]), "Ground extension changed original voyage grammar")

func key(physical: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = physical
	event.physical_keycode = physical
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func surface_step(view: Control, demand: PackedFloat64Array, count: int) -> bool:
	if not check(view is FlightView and view.surface_walking() and demand[1] == 0.0 and demand[2] == 0.0 and absf(demand[0]) == 1.0, "Ground replay lost recorded input ownership"): return false
	var physical := KEY_W if demand[0] > 0.0 else KEY_S
	key(physical, true)
	var sampled: PackedFloat64Array = view.surface_controls(0.0)
	check(sampled == demand, "Physical walking keys differ from recorded C++ controls")
	if not failed: view._process(float(count) / 120.0)
	key(physical, false)
	return not failed

func surface_action(view: Control, op: String) -> bool:
	if not check(view is FlightView, "Ground action reached station view"): return false
	view.player_input.axes.fill(0.0)
	if not resume(view): return false
	if op == "assistance_on": view.request_assistance(true)
	elif op in ["surface_exit", "surface_return"]: view.surface_walk_button.pressed.emit()
	elif op == "land": view.landing_button.pressed.emit()
	elif op == "liftoff": view.liftoff_button.pressed.emit()
	else: view.stow_gear_button.pressed.emit()
	return check(view.error.is_empty(), "Native surface action refused: " + op)

func checkpoint_review(view: Control, owner: Variant, row: Dictionary) -> bool:
	var state: Dictionary = owner.get_freedom_flight_state()
	if not check(state.system_id == row.system_id and state.resources.quantity_quanta == row.flight_quanta and state.resources.jump_charges == row.jump_charges and state.surface.landed == row.landed and (view is FlightView and view.surface_walking()) == row.surface_walking, "Ground actor, anchor or resources differ: " + row.label): return false
	if row.has("assistance"):
		if not check(state.assistance == row.assistance and state.hold_enabled == row.hold_enabled and state.orbit_classification == row.orbit_classification, "Physical profile/hold/trajectory differs: " + row.label): return false
		if row.label == "manual-gear" or row.label == "landed":
			if not check(state.surface.maneuver == "off", "Contact checkpoint retained a transient landing aid"): return false
	if row.has("coordinate_frame"):
		if not check(state.frame_policy == 2 and state.coordinate_frame == row.coordinate_frame and state.observation_frame == "planet_relative_inertial", "Selected canonical/observation frame differs: " + row.label): return false
	if row.surface_walking:
		if not check(view.camera.position.is_equal_approx(state.surface_walk.eye_position) and view.terrain_camera.transform == view.camera.transform and not view.assist_button.visible, "Ground eye or input ownership lost: " + row.label): return false
		if row.label == "surface-away":
			var before: Dictionary = owner.get_freedom_flight_state()
			view.surface_walk_button.pressed.emit()
			if not check(view.error.is_empty() and owner.get_freedom_flight_state() == before, "Distant return moved the actor"): return false
	# Independently Continue every actual boundary and compare its next full Save.
	var args := OS.get_cmdline_user_args()
	var restored: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(restored.stage_freedom_continue(args[0].path_join(row.file)), "Surface loop Continue could not stage"): return false
	var pending: Dictionary = restored.get_pending_freedom_start()
	if not check(not pending.is_empty() and restored.commit_pending_freedom_start(pending.candidate_id), "Surface loop Continue could not commit"): return false
	if row.has("coordinate_frame"):
		# C++ Continue retains exact bits; Godot can round JSON trace decimals.
		var canonical: Dictionary = restored.get_freedom_flight_state()
		for field in ["canonical_position_metres", "canonical_velocity_metres_per_second", "orientation_wxyz", "angular_velocity_body"]:
			if not check(state[field] == canonical[field], "Selected canonical motion differs from complete Continue: " + row.label): return false
	var neutral := PackedFloat64Array()
	neutral.resize(3 if row.walking or row.surface_walking else 12)
	var advanced: bool = restored.advance_freedom_surface_walk(1.0 / 120.0, neutral, false) if row.surface_walking else restored.advance_freedom_walk(1.0 / 120.0, neutral) if row.walking else restored.advance_freedom_flight(1.0 / 120.0, neutral, false)
	var save: String = args[2].path_join(row.label + "-continued-next.json")
	return check(advanced and restored.save_freedom_as(save) and FileAccess.get_sha256(save) == FileAccess.get_sha256(args[0].path_join(row.next_file)), "Continued full next Save differs: " + row.label)
