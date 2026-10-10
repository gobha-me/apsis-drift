extends "res://studies/captures/native_voyage_capture.gd"
## One physical route from ordinary New Game; no initialized ground start.
const Decimal = preload("res://tests/native_roundtrip_test.gd")
const LABELS = ["station-start", "seated", "departed", "atmosphere-entry", "cruise-start", "cruise-end", "landing-ready", "landed", "surface-exited", "surface-halfway", "surface-away", "surface-back", "surface-seated", "surface-airborne", "space-exit", "home-intercept", "near-port", "captured", "station-return"]

func checkpoint_labels() -> Array:
	return LABELS

func surface_commands() -> bool:
	return true

func valid_manifest(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema_version") != 4 or value.get("route") != "surface-loop" or value.get("seed") != "42" or value.get("physical_catalog") != 2 or value.get("physical_ephemeris") != 2 or value.get("terrain_source_lod") != 8 or value.get("terrain_relief_version") != 0 or value.get("uninterrupted_match") != true: return false
	if not Decimal.bounded_decimal(value.get("final_tick")) or value.final_tick.length() > 7 or int(value.final_tick) > 1200000: return false
	var rows: Variant = value.get("checkpoints")
	if not rows is Array or rows.size() != LABELS.size(): return false
	var previous := 0
	for i in rows.size():
		var row: Variant = rows[i]
		if not row is Dictionary or row.get("label") != LABELS[i] or row.get("file") != LABELS[i] + ".json" or row.get("next_file") != LABELS[i] + "-next.json": return false
		for field in ["tick", "checksum", "next_tick", "next_checksum", "system_id", "flight_quanta"]:
			if not Decimal.bounded_decimal(row.get(field)): return false
		if row.tick.length() > 7 or row.next_tick.length() > 7: return false
		var tick := int(row.tick)
		if tick < previous or tick > 1200000 or int(row.next_tick) != tick + 1: return false
		previous = tick
		for field in ["attached", "walking", "surface_walking", "landed", "station_available", "seated"]:
			if not row.get(field) is bool: return false
		if row.walking and row.surface_walking or row.surface_walking and (not row.landed or row.attached): return false
		if row.get("jump_phase") != "idle" or row.get("jump_charges") != 3: return false
		for field in ["altitude_metres", "air_speed_metres_per_second", "air_density", "atmosphere_edge_metres", "terrain_elevation_metres"]:
			if not Checkpoints.finite_number(row.get(field)): return false
		var position: Variant = row.get("position_metres")
		if not position is Array or position.size() != 3: return false
		for component in position:
			if not Checkpoints.finite_number(component): return false
	return str(previous) == value.final_tick

func manifest_refusals(trace: Dictionary) -> bool:
	for damage in ["shape", "nonfinite", "path", "version", "clock", "owner", "terrain", "actor"]:
		var bad: Dictionary = trace.duplicate(true)
		if damage == "shape": bad.checkpoints[0].position_metres.append(0.0)
		elif damage == "nonfinite": bad.checkpoints[0].altitude_metres = NAN
		elif damage == "path": bad.checkpoints[0].file = "../other.json"
		elif damage == "version": bad.schema_version = 1
		elif damage == "clock": bad.checkpoints[0].next_tick = "18446744073709551615"
		elif damage == "owner": bad.checkpoints[0].system_id = "18446744073709551616"
		elif damage == "terrain": bad.terrain_relief_version = 1
		else: bad.checkpoints[0].surface_walking = true
		if not check(not valid_manifest(bad), "Invalid surface loop manifest accepted: " + damage): return false
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
	var neutral := PackedFloat64Array()
	neutral.resize(3 if row.walking or row.surface_walking else 12)
	var advanced: bool = restored.advance_freedom_surface_walk(1.0 / 120.0, neutral, false) if row.surface_walking else restored.advance_freedom_walk(1.0 / 120.0, neutral) if row.walking else restored.advance_freedom_flight(1.0 / 120.0, neutral, false)
	var save: String = args[2].path_join(row.label + "-continued-next.json")
	return check(advanced and restored.save_freedom_as(save) and FileAccess.get_sha256(save) == FileAccess.get_sha256(args[0].path_join(row.next_file)), "Continued full next Save differs: " + row.label)
