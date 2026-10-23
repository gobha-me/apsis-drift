extends RefCounted
## Display-only validation of the authoritative navigation preference.

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 5: return false
	if not value.get("version") is int or value.version != 1: return false
	for key in ["supported", "enabled", "eligible"]:
		if not value.get(key) is bool: return false
	if not value.get("note") is String: return false
	var bytes: PackedByteArray = value.note.to_utf8_buffer()
	if bytes.is_empty() or bytes.size() > 1024 or bytes.has(0): return false
	if not value.supported and (value.enabled or value.eligible): return false
	return true
