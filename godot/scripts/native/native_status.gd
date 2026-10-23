extends RefCounted
## Presentation of C++ observations; never advances or edits the voyage.
const NativeIds = preload("res://scripts/native/native_environment.gd")
const REFERENCE_MARGIN_LABELS := {
	"minimum_temperature": "Minimum temperature",
	"maximum_temperature": "Maximum temperature",
	"radiation": "Radiation",
	"electrical": "Electrical field",
	"pressure": "Pressure",
	"gust": "Wind loading",
	"ground_motion": "Ground motion",
	"gravity": "Gravity rating",
	"vertical_thrust": "Vertical thrust",
	"support_load": "Landing support",
}

static func reference_limits_text(assessment: Dictionary) -> String:
	var margins: Variant = assessment.get("margins")
	if not margins is Array or margins.size() > REFERENCE_MARGIN_LABELS.size():
		return "Reference margin details are unavailable."
	var limits := PackedStringArray()
	var seen := {}
	for margin in margins:
		if not margin is Dictionary or not margin.get("kind") is String or not REFERENCE_MARGIN_LABELS.has(margin.kind) or margin.kind in seen or margin.get("rating") not in ["SAFE", "MARGINAL", "INSUFFICIENT"]:
			return "Reference margin details are unavailable."
		seen[margin.kind] = true
		if margin.rating != "SAFE":
			limits.append("%s %s" % [REFERENCE_MARGIN_LABELS[margin.kind], margin.rating])
	var detail := "Known reference limits: " + "; ".join(limits) if not limits.is_empty() else ""
	if not assessment.get("operation_supported", true):
		detail = "This frame does not support this operation." + ("\n" + detail if not detail.is_empty() else "")
	return detail


static func valid_surface_reference_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 6: return false
	for field in ["rating", "shielding_thermal", "structural", "propulsion"]:
		if not value.get(field) is String or value[field] not in ["SAFE", "MARGINAL", "INSUFFICIENT", "UNKNOWN"]: return false
	if typeof(value.get("operation_supported")) != TYPE_BOOL: return false
	var margins: Variant = value.get("margins")
	if not margins is Array or margins.size() > REFERENCE_MARGIN_LABELS.size(): return false
	var seen := {}
	for margin in margins:
		if not margin is Dictionary or margin.size() != 2 or not margin.get("kind") is String or not REFERENCE_MARGIN_LABELS.has(margin.kind) or margin.kind in seen or not margin.get("rating") is String or margin.rating not in ["SAFE", "MARGINAL", "INSUFFICIENT"]: return false
		seen[margin.kind] = true
	return true


static func surface_reference_warning(command: String, assessments: Array) -> String:
	var operations := PackedStringArray()
	if command == "request_freedom_landing": operations = PackedStringArray(["Touchdown", "Return ascent"])
	elif command == "liftoff_freedom_surface": operations = PackedStringArray(["Return ascent"])
	else: return ""
	if assessments.size() != operations.size(): return ""
	# Validate all required projections before using any of their contents.
	for assessment in assessments:
		if not valid_surface_reference_packet(assessment): return ""
	var warnings := PackedStringArray()
	for index in operations.size():
		var assessment: Dictionary = assessments[index]
		var limits := PackedStringArray()
		for rating in ["INSUFFICIENT", "MARGINAL"]:
			for margin: Dictionary in assessment.margins:
				if margin.rating == rating:
					limits.append("%s %s" % [REFERENCE_MARGIN_LABELS[margin.kind], rating])
		var details := PackedStringArray()
		if not assessment.operation_supported: details.append("frame operation unsupported")
		for limit in mini(2, limits.size()): details.append(limits[limit])
		if limits.size() > 2: details.append("more in Surface conditions")
		if not details.is_empty(): warnings.append(operations[index] + ": " + "; ".join(details))
	if warnings.is_empty(): return ""
	warnings.insert(0, "Recorded reference only; not weather or site clearance.")
	var message := "\n".join(warnings)
	return message if message.length() <= 400 else ""

class ObservationCue extends RefCounted:
	## A transient presentation latch over the bounded C++ knowledge counter.
	## Quiet reads absorb changes without aging an existing cue. Invalid elapsed
	## time clears both baseline and cue, so later reads cannot replay that change.
	const DURATION := 6.0
	const MESSAGE := "Ship observations updated\nControls → Recorded observations"
	var _baseline := -1
	var _remaining := 0.0

	static func _count(state: Dictionary) -> int:
		var chart: Variant = state.get("chart")
		if not chart is Dictionary: return -1
		for field in ["selected", "local_sensors"]:
			if typeof(chart.get(field)) != TYPE_BOOL or not chart[field]: return -1
		var count: Variant = chart.get("observation_transitions")
		if typeof(count) != TYPE_INT or count < 0 or count > 512: return -1
		return count

	func clear() -> void:
		_baseline = -1
		_remaining = 0.0

	func reset(state: Dictionary) -> void:
		_baseline = _count(state)
		_remaining = 0.0

	func observe(state: Dictionary, active_advance: bool, delta: float) -> void:
		if not is_finite(delta) or delta < 0.0:
			clear()
			return
		var count := _count(state)
		if count < 0:
			clear()
			return
		if _baseline < 0 or count < _baseline:
			reset(state)
			return
		if active_advance:
			_remaining = maxf(0.0, _remaining - delta)
			if count > _baseline: _remaining = DURATION
		_baseline = count

	func text() -> String:
		return MESSAGE if _remaining > 0.0 else ""

class SurfaceAidCue extends RefCounted:
	## Exact current aid detail, plus a short latch for an actual active handoff.
	## Quiet reads update the baseline without replaying or aging a result.
	## Invalid packets/time clear it; the first later valid read stays quiet.
	const DURATION := 6.0
	var _mode := ""
	var _active_note := ""
	var _result := ""
	var _remaining := 0.0

	static func _surface(state: Dictionary) -> Dictionary:
		var surface: Variant = state.get("surface")
		var outside: Variant = state.get("surface_walk", {})
		if not surface is Dictionary or not outside is Dictionary or not outside.is_empty(): return {}
		var mode: Variant = surface.get("maneuver")
		var note: Variant = surface.get("note")
		if not mode is String or mode not in ["off", "landing", "liftoff"] or not note is String or note.length() > 512: return {}
		return surface

	func clear() -> void:
		_mode = ""
		_active_note = ""
		_result = ""
		_remaining = 0.0

	func reset(state: Dictionary) -> void:
		clear()
		var surface := _surface(state)
		if surface.is_empty(): return
		_mode = surface.maneuver
		if _mode != "off": _active_note = surface.note

	func observe(state: Dictionary, active_advance: bool, delta: float) -> void:
		if not is_finite(delta) or delta < 0.0:
			clear()
			return
		var surface := _surface(state)
		if surface.is_empty():
			clear()
			return
		if _mode.is_empty():
			reset(state)
			return
		if active_advance: _remaining = maxf(0.0, _remaining - delta)
		if surface.maneuver != "off":
			_active_note = surface.note
			_result = ""
			_remaining = 0.0
		else:
			_active_note = ""
			if active_advance and _mode != "off":
				_result = surface.note
				_remaining = DURATION if not _result.is_empty() else 0.0
		_mode = surface.maneuver

	func accept_cancel(before: Dictionary, after: Dictionary) -> bool:
		var previous := _surface(before)
		var current := _surface(after)
		if previous.is_empty() or current.is_empty():
			clear()
			return false
		if previous.maneuver == "off" or current.maneuver != "off": return false
		reset(after)
		_result = current.note if not current.note.is_empty() else "Surface aid canceled."
		_remaining = DURATION
		return true

	func text() -> String:
		return _active_note if _mode in ["landing", "liftoff"] else _result if _remaining > 0.0 else ""

static func finite_number(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func drive_recovery_text(recovery: Variant, phase: String, detailed := false) -> String:
	if not recovery is Dictionary or typeof(recovery.get("version")) != TYPE_INT or recovery.version not in [1, 2]: return "Drive recovery unavailable"
	var strained: bool = recovery.version == 2
	if recovery.size() != (7 if strained else 5) or typeof(recovery.get("ready")) != TYPE_BOOL or typeof(recovery.get("pending_arrival")) != TYPE_BOOL: return "Drive recovery unavailable"
	var cycle: float = 45.0
	if strained:
		for field in ["cycle_seconds", "next_recovery_seconds"]:
			if not finite_number(recovery.get(field)) or recovery[field] < 45.0 or recovery[field] > 90.0: return "Drive recovery unavailable"
		cycle = recovery.cycle_seconds
	if not finite_number(recovery.get("remaining_seconds")) or recovery.remaining_seconds < 0 or recovery.remaining_seconds > cycle: return "Drive recovery unavailable"
	if not finite_number(recovery.get("extra_radius_metres")) or recovery.extra_radius_metres < 0 or recovery.extra_radius_metres > 2000: return "Drive recovery unavailable"
	if recovery.ready != (recovery.remaining_seconds == 0.0) or (recovery.ready and recovery.extra_radius_metres != 0.0) or (recovery.pending_arrival and (phase != "transit" or recovery.remaining_seconds != cycle)): return "Drive recovery unavailable"
	if recovery.pending_arrival:
		return "Drive strain · recovery starts on arrival (%.1f s)" % cycle if strained else "Drive recovery · starts on arrival"
	if recovery.ready:
		return "Drive ready · next recovery %.1f s" % recovery.next_recovery_seconds if strained else "Drive ready"
	var text := "Drive cooling · %.1f s left of %.1f s · extra arrival spread ≤ %.0f m" % [recovery.remaining_seconds, cycle, recovery.extra_radius_metres] if strained else "Drive recovery · %.1f s · extra arrival spread ≤ %.0f m" % [recovery.remaining_seconds, recovery.extra_radius_metres]
	if strained: text += "\nEarly jump: next recovery ≤ %.1f s; waiting clears strain." % recovery.next_recovery_seconds
	if detailed: text += "\nEarly jump available for 1 charge; spooling reduces these bounds."
	return text

static func surface_position_text(walking: Dictionary) -> String:
	for field in ["latitude", "longitude"]:
		if not finite_number(walking.get(field)): return "Surface position unavailable"
	if absf(walking.latitude) > PI / 2.0 or absf(walking.longitude) > PI:
		return "Surface position unavailable"
	return "Lat %+.5f° · Lon %+.5f°" % [rad_to_deg(walking.latitude), rad_to_deg(walking.longitude)]

# A rated free-flight estimate, including while landed; never a clearance.
static func rated_rise_text(state: Dictionary) -> String:
	if not state.has("rated_rise_margin"): return ""
	if not finite_number(state.rated_rise_margin): return "Rated RISE unavailable"
	var mode := "Prospective free flight" if state.get("surface") is Dictionary and state.surface.get("landed", false) == true else "Rated free flight"
	return "Full RISE − gravity: %+.1f m/s²\n%s · current attitude\nassumes fuel · excludes drag/contact" % [state.rated_rise_margin, mode]

static func layout_bounds(logical: Vector2, pixels: Vector2) -> Rect2:
	if not logical.is_finite() or not pixels.is_finite() or logical.x <= 0.0 or logical.y <= 0.0 or pixels.x <= 0.0 or pixels.y <= 0.0: return Rect2()
	var scale := maxf(1.0, maxf(logical.x / pixels.x, logical.y / pixels.y))
	var margin := minf(16.0 * scale, minf(logical.x, logical.y) * 0.1)
	return Rect2(Vector2.ONE * margin, Vector2(minf(380.0 * scale, logical.x - 2.0 * margin), minf(220.0 * scale, logical.y - 2.0 * margin)))

# Last committed ground movement only; not a prediction or hazard assessment.
static func movement_feedback(blockage: Variant) -> String:
	if blockage == null: return ""
	if not blockage is String: return "Ground movement feedback unavailable"
	match blockage:
		"terrain_unresolved": return "Last step blocked · Ground support unavailable"
		"water": return "Last step blocked · Water or shoreline"
		"slope": return "Last step blocked · Ground too steep"
		"step": return "Last step blocked · Step or drop too large"
		"craft_obstruction": return "Last step blocked · Wayfarer hull"
	return "Ground movement feedback unavailable"

static func gear_clearance_text(value: Dictionary) -> String:
	const UNAVAILABLE := "Gear clearance · Unavailable"
	var surface: Variant = value.get("surface")
	var outside: Variant = value.get("surface_walk", {})
	if not surface is Dictionary or not outside is Dictionary: return UNAVAILABLE
	for field in ["gear_deployed", "landed"]:
		if not surface.get(field) is bool: return UNAVAILABLE
	if not value.get("attached") is bool or not surface.gear_deployed or surface.landed or value.attached or not outside.is_empty(): return UNAVAILABLE
	var sample: Variant = surface.get("gear_clearance")
	if not sample is Dictionary or not sample.get("available") is bool or not sample.available: return UNAVAILABLE
	var current_frame: Variant = value.get("coordinate_frame", "planet_relative_inertial")
	if not sample.get("reference") is String or not sample.get("coordinate_frame") is String or not current_frame is String: return UNAVAILABLE
	if sample.reference != "deployed_pad_planes" or sample.coordinate_frame != "planet_relative_inertial" or current_frame != "planet_relative_inertial": return UNAVAILABLE
	for field in ["system_id", "planet_id", "craft_id", "frame_id"]:
		if not NativeIds.decimal(sample.get(field)) or not NativeIds.decimal(value.get(field)) or sample[field] != value[field]: return UNAVAILABLE
	for field in ["frame_version", "terrain_policy"]:
		if typeof(sample.get(field)) != TYPE_INT or typeof(value.get(field)) != TYPE_INT or sample[field] <= 0 or sample[field] != value[field]: return UNAVAILABLE
	for field in ["tick", "source_checksum"]:
		if not NativeIds.decimal(sample.get(field)): return UNAVAILABLE
	if not NativeIds.decimal(value.get("tick")) or sample.tick.length() > value.tick.length() or (sample.tick.length() == value.tick.length() and sample.tick > value.tick): return UNAVAILABLE
	for field in ["minimum_gap_metres", "maximum_gap_metres", "age_seconds"]:
		if not finite_number(sample.get(field)): return UNAVAILABLE
	if sample.minimum_gap_metres > sample.maximum_gap_metres or sample.age_seconds < 0.0: return UNAVAILABLE
	return "Last gear gap %.2f m · %.1f s ago" % [sample.minimum_gap_metres, sample.age_seconds]

static func summary(value: Dictionary, walking_keys: String = "WASD") -> String:
	for field in ["altitude", "surface_speed", "radial_rate", "air_density"]:
		if not finite_number(value.get(field)): return "Flight observations unavailable"
	if not value.get("attached") is bool: return "Flight observations unavailable"
	for field in ["surface", "surface_walk", "resources", "jump", "chart"]:
		if not value.get(field, {}) is Dictionary: return "Flight observations unavailable"
	var outside: Dictionary = value.get("surface_walk", {})
	var surface: Dictionary = value.get("surface", {})
	var resources: Dictionary = value.get("resources", {})
	var jump: Dictionary = value.get("jump", {})
	if not jump.get("phase", "idle") is String: return "Flight observations unavailable"
	var phase: String = jump.get("phase", "idle")
	if phase not in ["idle", "spool", "transit"]: return "Flight observations unavailable"
	var mode := "On foot · Suit equipped" if not outside.is_empty() else "Docked" if value.attached else "Landed" if surface.get("landed", false) else "Flight · Atmosphere" if value.air_density > 0.0 else "Flight · Space"
	if outside.is_empty() and surface.get("maneuver", "off") in ["landing", "liftoff"]: mode = "Landing" if surface.maneuver == "landing" else "Liftoff"
	if phase != "idle": mode = "Jump · " + ("Spooling" if phase == "spool" else "In transit")
	if phase != "idle" and jump.has("remaining_seconds"):
		if not finite_number(jump.remaining_seconds) or jump.remaining_seconds < 0.0: return "Jump observations unavailable"
		mode += " · %.1f s" % jump.remaining_seconds
	var location := "Current system"
	var rows: Variant = value.get("chart", {}).get("rows", [])
	if not rows is Array: return "Navigation observations unavailable"
	for row in rows:
		if row is Dictionary and row.get("current", false): location = str(row.get("name", location)); break
	var lines := PackedStringArray([mode + " · " + location])
	if jump.has("recovery"):
		var recovery_text := drive_recovery_text(jump.recovery, phase)
		if recovery_text == "Drive recovery unavailable": return recovery_text
		lines.append(recovery_text)
	if not outside.is_empty():
		if not outside.get("eye_position") is Vector3 or not outside.eye_position.is_finite(): return "Ground observations unavailable"
		var blocked := movement_feedback(outside.get("movement_blockage"))
		if not blocked.is_empty(): lines.append(blocked)
		lines.append("Wayfarer · %.1f m to craft" % outside.eye_position.length())
		var access: Variant = outside.get("return_access", {})
		if access is Dictionary and access.get("available") is bool and access.available and finite_number(access.get("distance_metres")) and access.distance_metres >= 0.0 and access.get("within_reach") is bool:
			lines.append("RETURN · %.1f m · %s" % [access.distance_metres, "Within reach · Return through hatch" if access.within_reach else "Approach ground access"])
		else:
			lines.append("RETURN · Return access unavailable")
		lines.append("%s / left stick walk · Mouse / look keys / right stick look" % walking_keys)
	else:
		lines.append("Reference altitude %.2f km" % (value.altitude / 1000.0))
		if surface.get("gear_deployed", false) and not surface.get("landed", false) and not value.attached:
			lines.append(gear_clearance_text(value))
		lines.append("Surface speed %.1f m/s · Radial %+.1f m/s" % [value.surface_speed, value.radial_rate])
		if value.get("assistance", false): lines.append("Flight assistance on")
		if resources.get("selected", false):
			if not finite_number(resources.get("fraction")) or resources.fraction < 0.0 or resources.fraction > 1.0 or not resources.get("jump_charges") is int or resources.jump_charges < 0 or resources.jump_charges > 3: return "Resource observations unavailable"
			lines.append("Fuel %.1f%% · Jumps %d/3%s" % [resources.fraction * 100.0, resources.jump_charges, " · EMPTY" if resources.get("operationally_empty", false) else " · LOW FUEL" if resources.get("reserve", false) else ""])
		else: lines.append("Fuel tracking unavailable for this historical save")
		if resources.get("propulsion_refused", false): lines.append("Insufficient fuel · thrust unavailable")
		if jump.has("destination_name"):
			if not jump.destination_name is String or jump.destination_name.is_empty() or jump.destination_name.length() > 128: return "Jump observations unavailable"
			lines.append(("Jump target · " if jump.get("route_kind") in ["foreign_moon", "foreign_minor"] else "Planet target · ") + jump.destination_name)
		else:
			for row in rows:
				if row is Dictionary and row.get("system_id", "") == jump.get("selected", "") and not row.get("current", false):
					lines.append("Jump target · " + str(row.get("name", "Unidentified destination")))
					break
	return "\n".join(lines)
