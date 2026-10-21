extends RefCounted
## Presentation only. C++ owns acquisition, qualification and retained results.
const REQUIRED_TICKS := 3600

static func valid(packet: Variant) -> bool:
	if not packet is Dictionary or packet.size() != 9: return false
	for key in ["selected", "active", "can_begin", "completed"]:
		if not packet.get(key) is bool: return false
	for key in ["note", "body_name"]:
		if not packet.get(key) is String or packet[key].length() > 512: return false
	if not packet.get("acquired_ticks") is int or packet.acquired_ticks < 0 or packet.acquired_ticks > REQUIRED_TICKS: return false
	if not packet.get("required_ticks") is int or packet.required_ticks != REQUIRED_TICKS: return false
	if not packet.get("result") is Dictionary: return false
	if packet.active and (not packet.selected or packet.completed or packet.can_begin or packet.acquired_ticks == REQUIRED_TICKS): return false
	if packet.completed:
		if not packet.selected or packet.can_begin or packet.acquired_ticks != REQUIRED_TICKS or packet.result.size() != 2: return false
		if not packet.result.get("character") is String or packet.result.character.is_empty() or packet.result.character.length() > 64: return false
		if not packet.result.get("water_basis_points") is int or packet.result.water_basis_points < 0 or packet.result.water_basis_points > 10000: return false
	elif not packet.result.is_empty(): return false
	if not packet.selected and (packet.acquired_ticks != 0 or packet.active or packet.completed): return false
	return true

static func text(packet: Variant) -> String:
	if not valid(packet): return ""
	if packet.completed:
		return "Survey complete · %s · %s · water %.1f%%\nControls → Recorded observations · terrain model, not landing clearance" % [packet.body_name.left(64), packet.result.character, float(packet.result.water_basis_points) / 100.0]
	if packet.active:
		return "Orbital survey · %.1f / 30 s · %s" % [floorf(float(packet.acquired_ticks) / 12.0) / 10.0, packet.note.left(100)]
	return ""

static func compact_text(packet: Variant, observations_updated := false) -> String:
	if not valid(packet): return ""
	if packet.completed: return "Survey complete · New records" if observations_updated else "Survey complete · Records"
	if packet.active:
		# This is the C++ supplied held notice, not a presentation-side orbit test.
		var held := " · held" if packet.note.begins_with("Survey held:") else ""
		return "Orbital survey · %.1f / 30 s%s" % [floorf(float(packet.acquired_ticks) / 12.0) / 10.0, held]
	return ""

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for index in value.length():
		if value.unicode_at(index) < 48 or value.unicode_at(index) > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func valid_provenance(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4: return false
	for key in ["started_tick", "completed_tick", "craft_id"]:
		if not decimal(value.get(key)): return false
	if not value.get("acquired_ticks") is int or value.acquired_ticks != REQUIRED_TICKS: return false
	var start: String = value.started_tick
	var finish: String = value.completed_tick
	return start.length() < finish.length() or (start.length() == finish.length() and start <= finish)
