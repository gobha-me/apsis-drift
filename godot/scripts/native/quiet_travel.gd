extends RefCounted
## Display-only view of the C++ transient scheduling request.

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4: return false
	if not value.get("version") is int or value.version != 1: return false
	if not value.get("multiplier") is int or value.multiplier not in [1, 2, 4]: return false
	if not value.get("available") is bool or not value.get("note") is String: return false
	var bytes: PackedByteArray = value.note.to_utf8_buffer()
	return not bytes.is_empty() and bytes.size() <= 1024 and not bytes.has(0)

static func text(value: Variant) -> String:
	if not valid_packet(value) or value.multiplier == 1: return ""
	return "Quiet travel · up to %d×" % value.multiplier
