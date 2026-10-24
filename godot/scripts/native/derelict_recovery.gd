extends RefCounted
## C++ owns retrieval intervals, cargo and permanent source depletion.
const Finance = preload("res://scripts/native/paid_operations.gd")

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 10 or not value.get("version") is int or value.version != 1: return false
	for key in ["selected", "known", "can_begin", "active"]:
		if not value.get(key) is bool: return false
	if not value.get("acquired_ticks") is int or value.acquired_ticks < 0 or value.acquired_ticks > 3600: return false
	if not value.get("required_ticks") is int or value.required_ticks != 3600: return false
	if not Finance.decimal(value.get("tick")): return false
	if not value.get("status") is String or not value.status in ["unclaimed", "retrieving", "carried", "lost", "sold"]: return false
	if not value.get("note") is String or value.note.is_empty(): return false
	var bytes: PackedByteArray = value.note.to_utf8_buffer()
	if bytes.size() > 1024 or bytes.has(0): return false
	if value.active != (value.status == "retrieving"): return false
	if value.active and (not value.selected or not value.known or value.acquired_ticks >= 3600): return false
	if value.status == "unclaimed" and value.acquired_ticks != 0: return false
	if value.status in ["carried", "lost", "sold"] and (not value.selected or not value.known or value.acquired_ticks != 3600): return false
	if not value.selected and (value.known or value.can_begin or value.active or value.status != "unclaimed"): return false
	if value.can_begin and (not value.selected or not value.known or value.active or value.status != "unclaimed"): return false
	return true

static func text(value: Variant) -> String:
	if not valid_packet(value) or not value.selected: return ""
	match value.status:
		"retrieving": return "Canister retrieval: %.1f / 30 s" % (floorf(float(value.acquired_ticks) / 12.0) / 10.0)
		"carried": return "Cargo: one sealed service canister"
		"lost": return "Canister lost · source depleted · note retained"
		"sold": return "Canister sold · source depleted · note retained"
	return "Canister retrieval available after exterior inspection" if value.known else ""
