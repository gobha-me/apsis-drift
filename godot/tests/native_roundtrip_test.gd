extends "res://studies/captures/native_voyage_capture.gd"
## The composed neighboring trip uses production views, commands and C++ saves.
const LABELS = ["station-start", "seated", "departed", "source-ready", "outbound-selection", "outbound-spool", "outbound-commit", "outbound-transit", "outbound-arrival", "neighbor-observed", "return-selection", "return-spool", "return-commit", "return-transit", "return-arrival", "near-port", "captured", "replenished", "station-return"]
var continuation_frame_policy := 2

func checkpoint_labels() -> Array:
	return LABELS

func labels_for_trace(value: Variant) -> Array:
	return ["surface-ascent"] + LABELS.slice(3) if value is Dictionary and value.get("schema_version") == 7 else LABELS

func start_options(trace: Dictionary, args: PackedStringArray) -> Dictionary:
	if trace.get("schema_version") == 7:
		continuation_frame_policy = int(trace.frame_policy)
		return {"mode": "continue", "value": args[0].path_join(trace.source_file), "assets": args[1]}
	return super.start_options(trace, args)

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

func valid_continuation(value: Dictionary) -> bool:
	if value.get("route") != "native-neighbor-from-ascent" or value.get("start_mode") != "continue" or value.get("source_file") != "surface-ascent.json" or not Checkpoints.finite_number(value.get("frame_policy")) or (value.frame_policy != 1 and value.frame_policy != 2) or value.get("terrain_policy") not in [2, 3] or value.get("uninterrupted_scope") != "suffix_from_declared_continue_only": return false
	if stream_start_tick(value) < 0 or not targeting_grade().is_empty(): return false
	if value.get("flight_profile") != ("advanced" if value.get("profile") == "pilot" else "assisted"): return false
	for field in ["initial_flight_quanta", "inherited_spent_quanta"]:
		if not bounded_decimal(value.get(field)) or value[field].length() > 16 or int(value[field]) > 3032640000000000: return false
	var initial := int(value.initial_flight_quanta)
	if initial + int(value.inherited_spent_quanta) != 3032640000000000: return false
	for field in ["effort_quanta", "flight_quanta_before_service"]:
		var amount: Variant = value.get(field)
		if not Checkpoints.finite_number(amount) or amount != int(amount) or amount < 0 or amount > initial: return false
	return initial - int(value.effort_quanta) == int(value.flight_quanta_before_service)

func valid_manifest(value: Variant) -> bool:
	if not value is Dictionary or (value.get("schema_version") != 2 and value.get("schema_version") != 7) or value.get("seed") != "42" or value.get("physical_catalog") != 2 or value.get("physical_ephemeris") != 2 or value.get("terrain_source_lod") != 8 or value.get("terrain_relief_version") != 0 or value.get("uninterrupted_match") != true or value.get("profile") not in ["assisted", "pilot"]: return false
	var continuation: bool = value.schema_version == 7
	if continuation:
		if not valid_continuation(value): return false
	elif value.get("route") != "native-neighbor": return false
	if not valid_targeting(value): return false
	var labels := labels_for_trace(value)
	var initial_tick := stream_start_tick(value)
	var digits := 7 if continuation else 6
	if not bounded_decimal(value.get("final_tick")) or value.final_tick.length() > digits or int(value.final_tick) > initial_tick + 750000: return false
	for field in ["origin_system", "neighbor_system"]:
		if not valid_record(["jump_select", 0, [value.get(field)]], labels, true): return false
	if value.origin_system == value.neighbor_system: return false
	var rows: Variant = value.get("checkpoints")
	if not rows is Array or rows.size() != labels.size(): return false
	var previous := initial_tick
	var previous_fuel := int(value.initial_flight_quanta) if continuation else 0
	var jump_offsets := [0, 0, 0, 0, 360, 361, 600, 720, 720, 720, 1080, 1081, 1320]
	var jump_phases := ["idle", "idle", "idle", "spool", "transit", "transit", "idle", "idle", "idle", "spool", "transit", "transit", "idle", "idle", "idle", "idle", "idle"]
	for i in rows.size():
		var row: Variant = rows[i]
		if not row is Dictionary or row.get("label") != labels[i] or row.get("file") != labels[i] + ".json" or row.get("next_file") != labels[i] + "-next.json": return false
		for field in ["tick", "checksum", "next_tick", "next_checksum", "system_id", "flight_quanta"]:
			if not bounded_decimal(row.get(field)): return false
		if row.tick.length() > digits or row.next_tick.length() > digits: return false
		var tick := int(row.tick)
		if tick < previous or tick > initial_tick + 750000 or int(row.next_tick) != tick + 1: return false
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
		if continuation:
			var expected_frame := "system_inertial" if value.frame_policy == 2 and i < 3 else "planet_relative_inertial"
			if row.get("coordinate_frame") != expected_frame or not row.get("surface_walking") is bool or not row.get("landed") is bool or row.surface_walking or row.landed or row.seated != (i < 16) or row.walking != (i == 16) or row.attached != (i >= 14): return false
			if row.jump_phase != jump_phases[i] or row.jump_charges != (3 if i < 4 or i >= 15 else 2 if i < 10 else 1): return false
			if i < jump_offsets.size() and tick != initial_tick + jump_offsets[i]: return false
			if i == 15 and row.tick != rows[14].tick: return false
			if i == 0 and (row.tick != value.initial_tick or row.flight_quanta != value.initial_flight_quanta or row.jump_phase != "idle" or row.jump_charges != 3 or row.altitude_metres < row.atmosphere_edge_metres): return false
			if row.flight_quanta.length() > 16: return false
			var fuel := int(row.flight_quanta)
			if (i < 15 and fuel > previous_fuel) or (i >= 15 and fuel != 3032640000000000): return false
			if i == 14 and fuel != int(value.flight_quanta_before_service): return false
			previous_fuel = fuel
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
	if trace.get("schema_version") == 7:
		for damage in ["source", "mode", "scope", "initial", "clock", "frame", "policy", "pair", "actor", "fuel", "baseline", "charge", "phase"]:
			var bad: Dictionary = trace.duplicate(true)
			if damage == "source": bad.source_file = "../source.json"
			elif damage == "mode": bad.start_mode = "new_game"
			elif damage == "scope": bad.uninterrupted_scope = "whole_journey"
			elif damage == "initial": bad.initial_tick = str(int(bad.initial_tick) + 1)
			elif damage == "clock":
				bad.checkpoints[1].tick = str(int(bad.initial_tick) + 1)
				bad.checkpoints[1].next_tick = str(int(bad.initial_tick) + 2)
			elif damage == "frame": bad.checkpoints[0].coordinate_frame = "system_inertial" if trace.frame_policy == 1 else "planet_relative_inertial"
			elif damage == "policy": bad.frame_policy = 3
			elif damage == "pair": bad.frame_policy = 2 if trace.frame_policy == 1 else 1
			elif damage == "actor": bad.checkpoints[0].surface_walking = true
			elif damage == "fuel": bad.checkpoints[14].flight_quanta = "3032640000000000"
			elif damage == "baseline": bad.inherited_spent_quanta = str(int(bad.inherited_spent_quanta) + 1)
			elif damage == "charge": bad.checkpoints[4].jump_charges = 3
			else: bad.checkpoints[4].jump_phase = "spool"
			if not check(not valid_manifest(bad), "Invalid continuation manifest accepted: " + damage): return false
		for initial in [null, -1, "-1", "01", "1200001", "18446744073709551615", str(int(trace.initial_tick) + 1)]:
			var bad: Dictionary = trace.duplicate(true)
			bad.initial_tick = initial
			if not check(not valid_stream(OS.get_cmdline_user_args()[0].path_join("commands.jsonl"), bad, labels_for_trace(trace), true), "Invalid continuation stream clock accepted"): return false
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
	if row.has("coordinate_frame"):
		# The legacy policy-1 projection omits frame tags and its existing motion
		# vectors are already canonical planet-relative values. Whole saves below
		# independently verify the actual saved policy and frame at every boundary.
		if not check(state.get("frame_policy", 1) == continuation_frame_policy and state.get("coordinate_frame", "planet_relative_inertial") == row.coordinate_frame and state.get("observation_frame", "planet_relative_inertial") == "planet_relative_inertial" and state.get("surface_walk", {}).is_empty(), "Continuation lost its saved frame or retained a ground actor: " + row.label): return false
	if view is FlightView or view is WalkView:
		if not check(view.sky_material != null and view.sky_material.get_shader_parameter("sun_direction") == state.star_direction and view.sky_material.get_shader_parameter("sun_color") == state.lighting.star_color and view.sky_material.get_shader_parameter("radius") == state.planet_radius and view.sky_material.get_shader_parameter("altitude") == state.altitude, "Current C++ sky diverged at real journey boundary: " + row.label): return false
		if view is WalkView and not check(view.sky_owner.tick == state.tick and is_equal_approx(view.station_environment.ambient_light_energy, 0.7), "Station sky tick/fill changed across boarding boundary: " + row.label): return false
	var boarding: Dictionary = owner.get_freedom_boarding_state()
	if view is WalkView and row.label == "station-return":
		var walk_before: Dictionary = owner.get_freedom_walk_state()
		var records: Dictionary = owner.get_freedom_observations()
		view.open_observations()
		if not check(view.observations_open() and view.paused and view.observations_view.readings == records and not records.subjects.is_empty(), "Returned station cannot read actual trip observations"): return false
		view.observations_view.change_page(1)
		view.close_observations()
		if not check(view.paused and owner.get_freedom_walk_state() == walk_before and owner.get_freedom_flight_state() == state and owner.get_freedom_observations() == records, "Returned station archive changed voyage, actor or knowledge"): return false
	if view is FlightView:
		var paused_before: bool = view.paused
		var survey_text: String = view.surface_conditions_text()
		for operation in [3, 4]:
			var survey: Dictionary = owner.get_freedom_environment_assessment(operation)
			if not check(not survey.has("error") and survey.get("rating") in ["SAFE", "MARGINAL", "INSUFFICIENT", "UNKNOWN"], "Current world lost its validated environmental survey: " + row.label): return false
			if not check(survey.get("margins") is Array and survey.margins.size() <= 10, "Reference detail exceeded its typed bound: " + row.label): return false
			var seen := {}
			for margin: Dictionary in survey.margins:
				if not check(margin.size() == 2 and margin.get("kind") in FlightView.REFERENCE_MARGIN_LABELS and margin.kind not in seen and margin.get("rating") in ["SAFE", "MARGINAL", "INSUFFICIENT"], "Reference detail leaked unresolved/raw facts or invalid categories: " + row.label): return false
				seen[margin.kind] = true
				if margin.rating != "SAFE" and not check("%s %s" % [FlightView.REFERENCE_MARGIN_LABELS[margin.kind], margin.rating] in survey_text, "Known limiting reference omitted from the paused warning: " + row.label): return false
			for axis in ["shielding_thermal", "structural", "propulsion"]:
				if not check(survey.get(axis) in ["SAFE", "MARGINAL", "INSUFFICIENT", "UNKNOWN"] and str(survey[axis]) in survey_text, "Survey text differs from the C++ knowledge projection: " + row.label): return false
		if row.label == "neighbor-observed":
			view.pause_controls("Read the actual surface reference.")
			view.show_surface_conditions()
			if not check(view.controls_menu.surface_reference_view.visible and view.controls_menu.surface_reference_view.reference_text == survey_text, "Actual neighboring reference omitted its retained limits"): return false
			if not check(view.controls_menu.back_from_nested() and view.paused, "Actual reference Back failed to remain paused"): return false
			if not paused_before and not resume(view): return false
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
	if row.has("coordinate_frame"):
		# Compare C++-decoded values, avoiding JSON decimal rounding in GDScript.
		var canonical: Dictionary = restored.get_freedom_flight_state()
		var motion_fields := ["canonical_position_metres", "canonical_velocity_metres_per_second"] if continuation_frame_policy == 2 else ["position_metres", "velocity_metres_per_second"]
		for field in motion_fields + ["orientation_wxyz", "angular_velocity_body"]:
			if not check(state[field] == canonical[field], "Continued canonical motion differs: " + row.label): return false
	if not check(restored.get_freedom_boarding_state() == boarding, "Continue changed pilot or seat presentation"): return false
	var neutral := PackedFloat64Array()
	neutral.resize(3 if row.walking else 12)
	var advanced: bool = restored.advance_freedom_walk(1.0 / 120.0, neutral) if row.walking else restored.advance_freedom_flight(1.0 / 120.0, neutral, false)
	var save: String = args[2].path_join(row.label + "-continued-next.json")
	return check(advanced and restored.save_freedom_as(save) and FileAccess.get_sha256(save) == FileAccess.get_sha256(args[0].path_join(row.next_file)), "Continued next tick differs from C++: " + row.label)
