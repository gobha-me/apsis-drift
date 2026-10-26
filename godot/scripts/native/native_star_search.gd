extends RefCounted
## C++ supplies progress and completed-fix count. No hidden target is projected.
const REQUIRED_TICKS := 1200
const MAX_COMPLETED := 2

static func valid(packet: Variant) -> bool:
	if not packet is Dictionary: return false
	var frontier: bool = packet.size() == 7 and packet.get("version") is int and packet.version == 2
	if not frontier and (packet.size() != 6 or packet.has("version")): return false
	var maximum: int = 4 if frontier else MAX_COMPLETED
	for key in ["selected", "active", "can_begin"]:
		if not packet.get(key) is bool: return false
	if not packet.get("note") is String or packet.note.length() > 512: return false
	if not packet.get("completed") is int or packet.completed < 0 or packet.completed > maximum: return false
	if not packet.get("acquired_ticks") is int or packet.acquired_ticks < 0 or packet.acquired_ticks >= REQUIRED_TICKS: return false
	if not packet.selected and (packet.active or packet.can_begin or packet.completed != 0 or packet.acquired_ticks != 0): return false
	if packet.active and (not packet.selected or packet.can_begin or packet.completed == maximum): return false
	if not packet.active and packet.acquired_ticks != 0: return false
	if packet.completed == maximum and packet.can_begin: return false
	return true

static func text(packet: Variant) -> String:
	if not valid(packet) or not packet.selected: return ""
	if packet.active:
		return "Star search · %.1f / 10 s · %s" % [floorf(float(packet.acquired_ticks) / 12.0) / 10.0, packet.note.left(100)]
	if packet.get("version") == 2:
		return "Stellar fixes retained · %d · %s" % [packet.completed, packet.note.left(100)]
	if packet.completed > 0:
		return "Stellar fixes · %d / 2 · Navigation chart" % packet.completed
	return ""
