extends "res://studies/captures/native_voyage_capture.gd"
## The composed neighboring trip uses production views, commands and C++ saves.
const LABELS = ["station-start", "seated", "departed", "source-ready", "outbound-selection", "outbound-spool", "outbound-commit", "outbound-transit", "outbound-arrival", "neighbor-observed", "return-selection", "return-spool", "return-commit", "return-transit", "return-arrival", "near-port", "captured", "replenished", "station-return"]

func checkpoint_labels() -> Array:
	return LABELS

func travel_commands() -> bool:
	return true

func targeting_grade() -> String:
	return ""

func valid_targeting(value: Dictionary) -> bool:
	var grade := targeting_grade()
	if grade.is_empty(): return not value.has("targeting_grade") and not value.has("targeting")
	if value.get("profile") != "pilot" or value.get("targeting_grade") != grade: return false
	var legs: Variant = value.get("targeting")
	if not legs is Array or legs.size() != 2: return false
	for i in 2:
		var leg: Variant = legs[i]
		if not leg is Dictionary or leg.get("leg") != ("outbound" if i == 0 else "return") or leg.get("quality") != grade: return false
		for field in ["heading_millidegrees", "drift_basis_points", "aligned_radius_metres", "envelope_radius_metres"]:
			var number: Variant = leg.get(field)
			if not Checkpoints.finite_number(number) or number < 0 or number > 45000 or number != int(number): return false
		if leg.drift_basis_points > 200 or leg.aligned_radius_metres < 1001 or leg.aligned_radius_metres > 2000: return false
		if leg.envelope_radius_metres != leg.aligned_radius_metres * (1 if grade == "ALIGNED" else 10): return false
		if grade == "ALIGNED" and leg.heading_millidegrees > 3000: return false
		if grade == "OFFSET" and (leg.heading_millidegrees < 18000 or leg.heading_millidegrees > 22000): return false
	return true

static func bounded_decimal(value: Variant) -> bool:
	return Checkpoints.decimal(value) and (value.length() == 1 or not value.begins_with("0")) and (value.length() < 20 or value <= "18446744073709551615")

func valid_manifest(value: Variant) -> bool:
	if not value is Dictionary or value.get("schema_version") != 2 or value.get("route") != "native-neighbor" or value.get("seed") != "42" or value.get("physical_catalog") != 2 or value.get("physical_ephemeris") != 2 or value.get("terrain_source_lod") != 8 or value.get("terrain_relief_version") != 0 or value.get("uninterrupted_match") != true or value.get("profile") not in ["assisted", "pilot"]: return false
	if not valid_targeting(value): return false
	if not bounded_decimal(value.get("final_tick")) or value.final_tick.length() > 6 or int(value.final_tick) > 750000: return false
	for field in ["origin_system", "neighbor_system"]:
		if not valid_record(["jump_select", 0, [value.get(field)]], LABELS, true): return false
	if value.origin_system == value.neighbor_system: return false
	var rows: Variant = value.get("checkpoints")
	if not rows is Array or rows.size() != LABELS.size(): return false
	var previous := 0
	for i in rows.size():
		var row: Variant = rows[i]
		if not row is Dictionary or row.get("label") != LABELS[i] or row.get("file") != LABELS[i] + ".json" or row.get("next_file") != LABELS[i] + "-next.json": return false
		for field in ["tick", "checksum", "next_tick", "next_checksum", "system_id", "flight_quanta"]:
			if not bounded_decimal(row.get(field)): return false
		if row.tick.length() > 6 or row.next_tick.length() > 6: return false
		var tick := int(row.tick)
		if tick < previous or tick > 750000 or int(row.next_tick) != tick + 1: return false
		previous = tick
		for field in ["attached", "walking", "station_available", "seated"]:
			if not row.get(field) is bool: return false
		if row.get("jump_phase") not in ["idle", "spool", "transit"] or not Checkpoints.finite_number(row.get("jump_charges")) or row.jump_charges != int(row.jump_charges) or row.jump_charges < 0 or row.jump_charges > 3: return false
		for field in ["altitude_metres", "air_speed_metres_per_second", "air_density", "atmosphere_edge_metres", "terrain_elevation_metres"]:
			if not Checkpoints.finite_number(row.get(field)): return false
		var position: Variant = row.get("position_metres")
		if not position is Array or position.size() != 3: return false
		for component in position:
			if not Checkpoints.finite_number(component): return false
	return str(previous) == value.final_tick

func manifest_refusals(trace: Dictionary) -> bool:
	for damage in ["shape", "nonfinite", "path", "version", "clock", "charges", "owner", "terrain", "profile"]:
		var bad: Dictionary = trace.duplicate(true)
		if damage == "shape": bad.checkpoints[0].position_metres.append(0.0)
		elif damage == "nonfinite": bad.checkpoints[0].altitude_metres = NAN
		elif damage == "path": bad.checkpoints[0].file = "../other.json"
		elif damage == "version": bad.schema_version = 1
		elif damage == "clock": bad.checkpoints[0].next_tick = "18446744073709551615"
		elif damage == "charges": bad.checkpoints[0].jump_charges = 4
		elif damage == "owner": bad.checkpoints[0].system_id = "18446744073709551616"
		elif damage == "terrain": bad.terrain_relief_version = 1
		else: bad.profile = "invented"
		if not check(not valid_manifest(bad), "Invalid round-trip manifest accepted: " + damage): return false
	if not targeting_grade().is_empty():
		for damage in ["quality", "leg", "shape", "radius", "nonfinite", "heading", "drift"]:
			var bad: Dictionary = trace.duplicate(true)
			if damage == "quality": bad.targeting_grade = "invented"
			elif damage == "leg": bad.targeting[0].leg = "return"
			elif damage == "shape": bad.targeting.pop_back()
			elif damage == "radius": bad.targeting[0].envelope_radius_metres += 1
			elif damage == "nonfinite": bad.targeting[0].heading_millidegrees = NAN
			elif damage == "heading": bad.targeting[0].heading_millidegrees = 46000
			else: bad.targeting[0].drift_basis_points = 201
			if not check(not valid_manifest(bad), "Invalid Pilot targeting manifest accepted: " + damage): return false
	for record in [["jump_select", 0, ["SYSTEM-0000000000000001"]], ["jump_select", 0, ["system-00000000000000001"]], ["jump_begin", 1, []], ["port_select", 0, [0]], ["port_select", 0, [1.5]], ["replenish", 0, [1]]]:
		if not check(not valid_record(record, LABELS, true), "Invalid travel command accepted"): return false
	return true

func checkpoint_review(view: Control, owner: Variant, row: Dictionary) -> bool:
	var state: Dictionary = owner.get_freedom_flight_state()
	var boarding: Dictionary = owner.get_freedom_boarding_state()
	if view is FlightView:
		var paused_before: bool = view.paused
		var survey_text: String = view.surface_conditions_text()
		for operation in [3, 4]:
			var survey: Dictionary = owner.get_freedom_environment_assessment(operation)
			if not check(not survey.has("error") and survey.get("rating") in ["SAFE", "MARGINAL", "INSUFFICIENT", "UNKNOWN"], "Current world lost its validated environmental survey: " + row.label): return false
			for axis in ["shielding_thermal", "structural", "propulsion"]:
				if not check(survey.get(axis) in ["SAFE", "MARGINAL", "INSUFFICIENT", "UNKNOWN"] and str(survey[axis]) in survey_text, "Survey text differs from the C++ knowledge projection: " + row.label): return false
		if not check(owner.get_freedom_flight_state() == state and view.paused == paused_before, "Survey changed flight or pause at " + row.label): return false
	if not check(state.system_id == row.system_id and state.station_available == row.station_available and state.jump.phase == row.jump_phase and state.resources.jump_charges == row.jump_charges and state.resources.quantity_quanta == row.flight_quanta and boarding.get("seated", false) == row.seated, "Current world, crew or bill differs: " + row.label): return false
	if not targeting_grade().is_empty() and row.label in ["outbound-commit", "return-commit"]:
		var trace: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(OS.get_cmdline_user_args()[0].path_join("trace.json")))
		var leg: Dictionary = trace.targeting[0 if row.label == "outbound-commit" else 1]
		if not check(state.jump.quality == leg.quality and state.jump.heading_error_degrees == leg.heading_millidegrees / 1000.0 and state.jump.drift_percent == leg.drift_basis_points / 100.0 and state.jump.envelope_radius_metres == leg.envelope_radius_metres, "Committed Pilot grade or displayed consequences differ: " + row.label): return false
	if view is FlightView:
		if not check(view.home_marker.visible == row.station_available, "Home cues do not follow the actual world"): return false
	if not row.station_available:
		var before: Dictionary = owner.get_freedom_flight_state()
		if not check(owner.get_freedom_station_geometry().is_empty() and not owner.replenish_freedom_resources() and owner.get_freedom_flight_state() == before, "Neighbor exposed or granted phantom home service"): return false
	# Each persisted boundary independently continues and performs one real tick,
	# compared with the C++ oracle; the ongoing UI replay keeps its actual craft.
	var args := OS.get_cmdline_user_args()
	var restored: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(restored.stage_freedom_continue(args[0].path_join(row.file)), "Round-trip Continue could not stage"): return false
	var pending: Dictionary = restored.get_pending_freedom_start()
	if not check(not pending.is_empty() and restored.commit_pending_freedom_start(pending.candidate_id), "Round-trip Continue could not commit"): return false
	if not check(restored.get_freedom_boarding_state() == boarding, "Continue changed pilot or seat presentation"): return false
	var neutral := PackedFloat64Array()
	neutral.resize(3 if row.walking else 12)
	var advanced: bool = restored.advance_freedom_walk(1.0 / 120.0, neutral) if row.walking else restored.advance_freedom_flight(1.0 / 120.0, neutral, false)
	var save: String = args[2].path_join(row.label + "-continued-next.json")
	return check(advanced and restored.save_freedom_as(save) and FileAccess.get_sha256(save) == FileAccess.get_sha256(args[0].path_join(row.next_file)), "Continued next tick differs from C++: " + row.label)
