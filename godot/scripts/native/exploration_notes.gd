extends RefCounted
## Retained C++ projections, queried on opening or after an explicit marker action.
const Derelict = preload("res://scripts/native/native_derelict_view.gd")
const ROW_FIELDS := ["system_id", "body_kind", "body_id", "site_id", "craft_id", "tick", "kind", "label", "source_name"]
const MAX_TEXT := 256

static func read_bridge(owner: Variant) -> Dictionary:
	var samples: Variant = null
	var derelict: Variant = null
	var navigation: Variant = null
	if owner is Object and is_instance_valid(owner):
		if owner.has_method("get_freedom_field_samples"):
			samples = owner.call("get_freedom_field_samples")
		if owner.has_method("get_freedom_flight_state"):
			var flight: Variant = owner.call("get_freedom_flight_state")
			derelict = flight.get("derelict", null) if flight is Dictionary else false
			navigation = navigation_context(flight)
	var result := {"samples": samples, "derelict": derelict, "navigation": navigation}
	if owner is Object and is_instance_valid(owner) and owner.has_method("get_freedom_station_stellar_lead"):
		result["station_lead"] = owner.call("get_freedom_station_stellar_lead")
	return result

static func word(value: Variant, tick: bool = false) -> bool:
	if not value is String: return false
	if tick and value == "0": return true
	if not Derelict.valid_identity(value): return false
	return not tick or value != "18446744073709551615"

static func station_lead_valid(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 9: return false
	for field in ["version", "selected", "kept", "can_keep", "offer_tick", "received_tick", "status", "source", "note"]:
		if not value.has(field): return false
	if typeof(value.version) != TYPE_INT or value.version != 1: return false
	for field in ["selected", "kept", "can_keep"]:
		if not value[field] is bool: return false
	for field in ["offer_tick", "received_tick", "status", "source", "note"]:
		if not value[field] is String: return false
	if value.note.strip_edges().is_empty() or value.note.length() > 2048: return false
	if not value.selected:
		return not value.kept and not value.can_keep and value.offer_tick.is_empty() and value.received_tick.is_empty() and value.source.is_empty() and value.status == "unselected"
	if value.kept:
		return not value.can_keep and value.offer_tick.is_empty() and word(value.received_tick, true) and value.source == "Origin bay attendant" and value.status in ["unverified", "sensor_fix_acquired", "visited"]
	if not value.received_tick.is_empty() or value.status != "unrecorded": return false
	if value.can_keep: return word(value.offer_tick, true) and value.source == "Origin bay attendant"
	return value.offer_tick.is_empty() and value.source.is_empty()

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

# Keep only the currently loaded body's identity and the single marker state.
# No terrain coordinates or undiscovered descriptors cross this boundary.
static func navigation_context(flight: Variant) -> Variant:
	if not flight is Dictionary: return null
	var kind: Variant = flight.get("body_kind", "planet")
	var body: Variant = flight.get("body_id", flight.get("planet_id"))
	var marker: Variant = flight.get("surface_marker")
	var jump: Variant = flight.get("jump")
	if not kind is String or kind not in ["planet", "moon", "minor"] or not word(flight.get("system_id")) or not word(body): return null
	if not marker is Dictionary or not marker.get("present") is bool or not jump is Dictionary or not jump.get("phase") is String: return null
	return {"system_id": flight.system_id, "body_kind": kind, "body_id": body, "present": marker.present, "idle": jump.phase == "idle"}

static func navigation_valid(value: Variant) -> bool:
	return value is Dictionary and value.size() == 5 and word(value.get("system_id")) and value.get("body_kind") is String and value.get("body_kind") in ["planet", "moon", "minor"] and word(value.get("body_id")) and value.get("present") is bool and value.get("idle") is bool

static func site_action(row: Dictionary, navigation: Variant) -> Dictionary:
	var target := {}
	for field in ["system_id", "body_kind", "body_id", "site_id"]: target[field] = row[field]
	var available := navigation_valid(navigation)
	var local: bool = available and row.system_id == navigation.system_id and row.body_kind == navigation.body_kind and row.body_id == navigation.body_id
	return {"target": target, "replace": available and navigation.present, "available": local and navigation.idle, "reason": "Set the existing personal marker at this recorded exposure. This does not certify a safe route or landing." if local and navigation.idle else "Wait until the jump completes." if local else "Return to this note's body before marking its site." if available else "Marker navigation is unavailable."}

static func mark_bridge(owner: Variant, target: Dictionary, replace: bool) -> Dictionary:
	if target.size() != 4 or not target.get("body_kind") is String or target.get("body_kind") not in ["planet", "moon"]: return {"ok": false, "reason": "Select an actual retained specimen note."}
	for field in ["system_id", "body_id", "site_id"]:
		if not word(target.get(field)): return {"ok": false, "reason": "Invalid retained site identity."}
	if not owner is Object or not is_instance_valid(owner) or not owner.has_method("mark_freedom_field_sample_site"): return {"ok": false, "reason": "Marker navigation is unavailable."}
	var ok: Variant = owner.call("mark_freedom_field_sample_site", target.system_id, target.body_kind, target.body_id, target.site_id, replace)
	if not ok is bool: return {"ok": false, "reason": "Marker command returned an invalid result."}
	return {"ok": ok, "reason": "Personal marker set to the recorded exposure. Check its bearing after returning to controls; the route is not certified safe." if ok else str(owner.call("get_last_error")) if owner.has_method("get_last_error") else "Marker command refused."}

static func unavailable(family: String) -> Dictionary:
	return {"title": family, "text": family + " are unavailable. Reading this page does not change the voyage."}

static func pages(candidate: Variant) -> Array:
	if not candidate is Dictionary or not candidate.has("samples") or not candidate.has("derelict"):
		return [unavailable("Exploration notes")]
	for field in candidate:
		if field not in ["samples", "derelict", "navigation", "station_lead"]: return [unavailable("Exploration notes")]
	if candidate.size() < 2 or candidate.size() > 4:
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
			result.append({"title": row.label + " · " + row.source_name, "text": "%s\nSource: %s · %s\n%s\n\nFirst collection · ship time %.1f s\nField identification from the exposure; composition has not been analyzed." % [row.label, row.source_name, str(row.body_kind).capitalize(), "Specimen carried." if carried.has(site_key(row)) else "Specimen not carried.", float(row.tick) / 120.0], "site_action": site_action(row, candidate.get("navigation"))})
	var derelict: Variant = candidate.derelict
	if derelict != null:
		if not Derelict.valid_packet(derelict):
			result.append(unavailable("Derelict notes"))
		elif derelict.known:
			var text: String = "Acquired unpowered Wayfarer contact"
			text += "\n\n" + derelict.history if derelict.inspected else "\n\nExterior inspection has not been recorded."
			text += "\n\nRetained contact note. This archive does not show a live position or approach clearance."
			result.append({"title": "DERELICT CONTACT", "text": text})
	if candidate.has("station_lead"):
		var lead: Variant = candidate.station_lead
		if not station_lead_valid(lead):
			result.append(unavailable("Station lead"))
		elif lead.kept:
			var status: String = {"unverified": "Unverified report", "sensor_fix_acquired": "Sensor fix acquired", "visited": "Visited"}[lead.status]
			result.append({"title": "STATION LEAD", "text": "%s\nSource: %s\nReceived · ship time %.1f s\n\n%s\n\nThis stable stellar report has no expiry or freshness promise. It reserves no claim and offers no reward." % [status, lead.source, float(lead.received_tick) / 120.0, lead.note]})
	return result
