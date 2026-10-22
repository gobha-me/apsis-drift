extends RefCounted
## Read-only pages from retained C++ projections, queried once when opened.
const Derelict = preload("res://scripts/native/native_derelict_view.gd")
const ROW_FIELDS := ["system_id", "body_kind", "body_id", "site_id", "craft_id", "tick", "kind", "label", "source_name"]
const MAX_TEXT := 256

static func read_bridge(owner: Variant) -> Dictionary:
	var samples: Variant = null
	var derelict: Variant = null
	if owner is Object and is_instance_valid(owner):
		if owner.has_method("get_freedom_field_samples"):
			samples = owner.call("get_freedom_field_samples")
		if owner.has_method("get_freedom_flight_state"):
			var flight: Variant = owner.call("get_freedom_flight_state")
			derelict = flight.get("derelict", null) if flight is Dictionary else false
	return {"samples": samples, "derelict": derelict}

static func word(value: Variant, tick: bool = false) -> bool:
	if not value is String: return false
	if tick and value == "0": return true
	if not Derelict.valid_identity(value): return false
	return not tick or value != "18446744073709551615"

static func row_valid(row: Variant) -> bool:
	if not row is Dictionary or row.size() != ROW_FIELDS.size(): return false
	for field in ROW_FIELDS:
		if not row.has(field): return false
	for field in ["system_id", "body_id", "site_id", "craft_id"]:
		if not word(row[field]): return false
	if not word(row.tick, true) or row.body_kind not in ["planet", "moon"]: return false
	if typeof(row.kind) != TYPE_INT or row.kind < 0 or row.kind > 2: return false
	for field in ["label", "source_name"]:
		if not row[field] is String or row[field].strip_edges().is_empty() or row[field].length() > MAX_TEXT: return false
	return true

static func site_key(row: Dictionary) -> String:
	return "%s/%s/%s/%s" % [row.system_id, row.body_kind, row.body_id, row.site_id]

static func samples_valid(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4: return false
	if not value.get("selected") is bool or typeof(value.get("capacity")) != TYPE_INT or value.capacity != 4: return false
	if not value.get("carried") is Array or not value.get("journal") is Array: return false
	if value.carried.size() > 4 or value.journal.size() > 64: return false
	if not value.selected and (not value.carried.is_empty() or not value.journal.is_empty()): return false
	var journal := {}
	for row in value.journal:
		if not row_valid(row): return false
		var key := site_key(row)
		if journal.has(key): return false
		journal[key] = row.kind
	var carried := {}
	for row in value.carried:
		if not row_valid(row): return false
		var key := site_key(row)
		if carried.has(key) or not journal.has(key) or journal[key] != row.kind: return false
		carried[key] = true
	return true

static func unavailable(family: String) -> Dictionary:
	return {"title": family, "text": family + " are unavailable. Reading this page does not change the voyage."}

static func pages(candidate: Variant) -> Array:
	if not candidate is Dictionary or candidate.size() != 2 or not candidate.has("samples") or not candidate.has("derelict"):
		return [unavailable("Exploration notes")]
	var result: Array = []
	var samples: Variant = candidate.samples
	if not samples_valid(samples):
		result.append(unavailable("Specimen notes"))
	elif samples.selected:
		result.append({"title": "FIELD SAMPLES", "text": "Pouch: %d / 4 specimens carried.\n%d first-collection notes retained. Notes remain readable when a specimen is not carried." % [samples.carried.size(), samples.journal.size()]})
		var carried := {}
		for row in samples.carried: carried[site_key(row)] = true
		for row in samples.journal:
			result.append({"title": row.label + " · " + row.source_name, "text": "%s\nSource: %s · %s\n%s\n\nFirst collection · ship time %.1f s\nField identification from the exposure; composition has not been analyzed." % [row.label, row.source_name, str(row.body_kind).capitalize(), "Specimen carried." if carried.has(site_key(row)) else "Specimen not carried.", float(row.tick) / 120.0]})
	var derelict: Variant = candidate.derelict
	if derelict != null:
		if not Derelict.valid_packet(derelict):
			result.append(unavailable("Derelict notes"))
		elif derelict.known:
			var text: String = "Acquired unpowered Wayfarer contact"
			text += "\n\n" + derelict.history if derelict.inspected else "\n\nExterior inspection has not been recorded."
			text += "\n\nRetained contact note. This archive does not show a live position or approach clearance."
			result.append({"title": "DERELICT CONTACT", "text": text})
	return result
