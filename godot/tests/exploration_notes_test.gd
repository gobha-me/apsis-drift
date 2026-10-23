extends RefCounted
const Notes = preload("res://scripts/native/exploration_notes.gd")

class Bridge:
	extends RefCounted
	var sample_queries := 0
	var flight_queries := 0
	var saved := "unchanged complete voyage"
	var sample_packet: Dictionary
	var derelict_packet: Dictionary
	var navigation: Dictionary = {}
	var commands: Array = []
	func mark_freedom_field_sample_site(system: String, kind: String, body: String, site: String, replace: bool) -> bool:
		commands.append([system, kind, body, site, replace])
		return true
	func get_freedom_field_samples() -> Dictionary:
		sample_queries += 1
		return sample_packet
	func get_freedom_flight_state() -> Dictionary:
		flight_queries += 1
		var result := navigation.duplicate(true)
		result["derelict"] = derelict_packet
		return result

class LeadBridge:
	extends Bridge
	var lead_queries := 0
	var lead_packet: Dictionary
	func get_freedom_station_stellar_lead() -> Dictionary:
		lead_queries += 1
		return lead_packet

static func lead(kept: bool = false) -> Dictionary:
	return {"version": 1, "selected": true, "kept": kept, "can_keep": not kept, "offer_tick": "0" if not kept else "", "received_tick": "0" if kept else "", "status": "unverified" if kept else "unrecorded", "source": "Origin bay attendant", "note": "An unverified stellar report. Use Search nearby stars; this grants no navigation fix."}

static func row(site: String = "3") -> Dictionary:
	return {"system_id": "1", "body_kind": "moon", "body_id": "2", "site_id": site, "craft_id": "4", "tick": "0", "kind": 0, "label": "Pale specimen", "source_name": "Moon"}

static func samples(selected: bool = true) -> Dictionary:
	return {"selected": selected, "capacity": 4, "carried": [], "journal": []}

static func derelict() -> Dictionary:
	return {"version": 1, "selected": true, "known": false, "local": false, "inspected": false, "can_scan": true, "can_inspect": false, "note": "Ready to scan", "history": "", "id": "", "position": Vector3.ZERO, "basis": Basis.IDENTITY, "distance_metres": 0.0, "relative_speed_metres_per_second": 0.0}

static func check(ok: bool, message: String) -> int:
	if ok: return 0
	push_error(message)
	return 1

static func run() -> int:
	var failures := 0
	var empty := {"samples": samples(false), "derelict": null}
	failures += check(Notes.pages(empty).is_empty(), "Legacy unselected notes invented pages")
	var packet := {"samples": samples(), "derelict": derelict()}
	var first := row()
	packet.samples.journal.append(first)
	var recollected := first.duplicate(true)
	recollected.tick = "120"
	recollected.craft_id = "5"
	packet.samples.carried.append(recollected)
	var original := packet.duplicate(true)
	var pages: Array = Notes.pages(packet)
	failures += check(pages.size() == 2 and "Specimen carried." in pages[1].text and "ship time 0.0 s" in pages[1].text and not "craft 5" in pages[1].text, "Recollection lost original note or joined by mutable craft/time")
	failures += check(packet == original, "Reading specimen pages mutated source packet")
	packet.samples.carried.clear()
	pages = Notes.pages(packet)
	failures += check("Specimen not carried." in pages[1].text and not "discard" in pages[1].text and not "lost" in pages[1].text, "Missing specimen invented its disposition")
	for value in [null, [], {}, {"samples": samples()}, {"samples": samples(), "derelict": null, "extra": true}]:
		failures += check(Notes.pages(value).size() == 1 and "unavailable" in Notes.pages(value)[0].text, "Malformed root did not fail closed")
	for field in first:
		var bad := packet.duplicate(true)
		bad.samples.journal[0].erase(field)
		failures += check("unavailable" in Notes.pages(bad)[0].text, "Missing specimen field accepted: " + field)
	for field in ["system_id", "body_id", "site_id", "craft_id"]:
		for value in ["0", "01", "-1", "1.0", "18446744073709551616", "", 1, NAN, INF]:
			var bad := packet.duplicate(true)
			bad.samples.journal[0][field] = value
			failures += check("unavailable" in Notes.pages(bad)[0].text, "Malformed uint64 specimen identity accepted")
	for value in ["", "00", "-1", "18446744073709551615", "18446744073709551616", 0, NAN]:
		var bad := packet.duplicate(true)
		bad.samples.journal[0].tick = value
		failures += check("unavailable" in Notes.pages(bad)[0].text, "Malformed/reserved note tick accepted")
	var edge := packet.duplicate(true)
	edge.samples.journal[0].tick = "18446744073709551614"
	edge.samples.journal[0].site_id = "18446744073709551615"
	failures += check(Notes.pages(edge).size() == 2, "Valid uint64 identity/last usable tick refused")
	for change in [{"capacity": 5}, {"capacity": 4.0}, {"selected": false}, {"extra": 0}, {"carried": {}}, {"journal": PackedByteArray()}]:
		var bad := packet.duplicate(true)
		bad.samples.merge(change, true)
		failures += check("unavailable" in Notes.pages(bad)[0].text, "Malformed sample envelope accepted")
	for change in [{"extra": 0}, {"kind": -1}, {"kind": 3}, {"kind": 0.0}, {"kind": NAN}, {"body_kind": "minor"}, {"label": ""}, {"label": "x".repeat(257)}, {"source_name": ""}, {"source_name": INF}]:
		var bad := packet.duplicate(true)
		bad.samples.journal[0].merge(change, true)
		failures += check("unavailable" in Notes.pages(bad)[0].text, "Malformed note row accepted")
	var duplicate := packet.duplicate(true)
	duplicate.samples.journal.append(first.duplicate(true))
	failures += check("unavailable" in Notes.pages(duplicate)[0].text, "Duplicate first note accepted")
	duplicate = packet.duplicate(true)
	duplicate.samples.carried = [first.duplicate(true), recollected.duplicate(true)]
	failures += check("unavailable" in Notes.pages(duplicate)[0].text, "Duplicate carried site with different clock accepted")
	duplicate = packet.duplicate(true)
	duplicate.samples.carried = [row("999")]
	failures += check("unavailable" in Notes.pages(duplicate)[0].text, "Carried specimen missing retained note accepted")
	duplicate = packet.duplicate(true)
	duplicate.samples.carried = [recollected.duplicate(true)]
	duplicate.samples.carried[0].kind = 2
	failures += check("unavailable" in Notes.pages(duplicate)[0].text, "Same exposure changed its specimen kind")
	var full := {"samples": samples(), "derelict": null}
	for i in 64: full.samples.journal.append(row(str(i + 1)))
	full.samples.carried = full.samples.journal.slice(0, 4)
	failures += check(Notes.pages(full).size() == 65, "64journal/fourpouch bound refused")
	full.samples.carried.append(full.samples.journal[4])
	failures += check("unavailable" in Notes.pages(full)[0].text, "Fifth carried specimen accepted")
	full.samples.carried.clear()
	full.samples.journal.append(row("65"))
	failures += check("unavailable" in Notes.pages(full)[0].text, "65th retained note accepted")
	var contact := derelict()
	contact.known = true
	contact.id = "77"
	packet.derelict = contact
	pages = Notes.pages(packet)
	failures += check(pages.size() == 3 and "unpowered Wayfarer" in pages[2].text and not "ID 77" in pages[2].text and "not been recorded" in pages[2].text, "Earned contact archive is missing")
	contact.inspected = true
	contact.history = "Registry unreadable. Ownership and crew fate unknown."
	contact.local = true
	contact.position = Vector3(12345, -9876, 4321)
	contact.distance_metres = 450.0
	contact.relative_speed_metres_per_second = 10.0
	pages = Notes.pages(packet)
	failures += check(contact.history in pages[2].text and not "12345" in pages[2].text and not "450" in pages[2].text and not "10.0" in pages[2].text and not contact.note in pages[2].text, "Archive leaked live pose/range or omitted earned history")
	var broken := packet.duplicate(true)
	broken.derelict.inspected = false
	failures += check("unavailable" in Notes.pages(broken)[2].text, "Unearned hidden derelict history accepted")
	broken = packet.duplicate(true)
	broken.derelict.known = false
	failures += check("unavailable" in Notes.pages(broken)[2].text, "Unknown derelict identity/pose leaked into notes")
	broken = packet.duplicate(true)
	broken.derelict.position.x = NAN
	failures += check("unavailable" in Notes.pages(broken)[2].text, "Nonfinite derelict packet accepted")
	broken = packet.duplicate(true)
	broken.samples = null
	pages = Notes.pages(broken)
	failures += check(pages.size() == 2 and "unavailable" in pages[0].text and contact.history in pages[1].text, "Bad samples discarded valid derelict family")
	broken = packet.duplicate(true)
	broken.derelict = false
	pages = Notes.pages(broken)
	failures += check(pages.size() == 3 and "ship time 0.0 s" in pages[1].text and "unavailable" in pages[2].text, "Bad derelict discarded valid specimen family")
	var bridge := Bridge.new()
	bridge.sample_packet = packet.samples
	bridge.derelict_packet = contact
	var snapshot := Notes.read_bridge(bridge)
	var saved := bridge.saved
	Notes.pages(snapshot)
	Notes.pages(snapshot)
	failures += check(bridge.sample_queries == 1 and bridge.flight_queries == 1 and bridge.saved == saved and snapshot == {"samples": packet.samples, "derelict": packet.derelict, "navigation": null}, "Archive getter polled repeatedly or changed provider state")
	Notes.read_bridge(bridge)
	failures += check(bridge.sample_queries == 2 and bridge.flight_queries == 2, "Reopen did not refresh each projection once")
	failures += check(Notes.read_bridge(null) == {"samples": null, "derelict": null, "navigation": null}, "Missing provider did not remain bounded")
	var flight := {"system_id": "1", "body_kind": "moon", "body_id": "2", "surface_marker": {"present": false}, "jump": {"phase": "idle"}}
	var navigation: Variant = Notes.navigation_context(flight)
	failures += check(Notes.navigation_valid(navigation), "Current Moon marker context refused")
	var action: Dictionary = Notes.site_action(first, navigation)
	failures += check(action.available and not action.replace and action.target == {"system_id": "1", "body_kind": "moon", "body_id": "2", "site_id": "3"}, "Site action lost recorded typed identity")
	navigation.present = true
	failures += check(Notes.site_action(first, navigation).replace, "Present marker did not require explicit replacement")
	for change in [{"system_id": "9"}, {"body_id": "9"}, {"body_kind": "planet"}, {"body_kind": "minor"}, {"idle": false}]:
		var remote: Dictionary = navigation.duplicate(true)
		remote.merge(change, true)
		failures += check(not Notes.site_action(first, remote).available, "Wrong body/system or active jump offered marker")
	for bad in [null, {}, [], {"system_id": "1", "body_kind": "moon", "body_id": "2", "present": 1, "idle": true}]:
		failures += check(not Notes.site_action(first, bad).available, "Malformed marker context enabled an action")
	for field in ["system_id", "body_id", "body_kind", "surface_marker", "jump"]:
		var bad := flight.duplicate(true)
		bad[field] = NAN
		failures += check(Notes.navigation_context(bad) == null, "Malformed navigation projection accepted")
	bridge.navigation = flight
	var projection := Notes.read_bridge(bridge)
	failures += check(Notes.pages(projection)[1].site_action.available and bridge.commands.is_empty(), "Reading notes issued a command or omitted the current site action")
	var target: Dictionary = action.target
	for bad in [{}, {"system_id": "1", "body_kind": "moon", "body_id": "2", "site_id": "03"}, {"system_id": "1", "body_kind": 1, "body_id": "2", "site_id": "3"}]:
		failures += check(not Notes.mark_bridge(bridge, bad, false).ok and bridge.commands.is_empty(), "Malformed site command reached provider")
	failures += check(Notes.mark_bridge(bridge, target, true).ok and bridge.commands == [["1", "moon", "2", "3", true]], "Explicit replace command changed typed identity or replacement intent")
	var offered := lead()
	var retained := lead(true)
	for value in [offered, retained]:
		failures += check(Notes.station_lead_valid(value), "Valid initial/retained station lead refused")
	var lead_root := {"samples": packet.samples, "derelict": packet.derelict, "station_lead": offered}
	failures += check(Notes.pages(lead_root).size() == 3, "Merely available report invented a retained note")
	lead_root.station_lead = retained
	var frozen_lead := lead_root.duplicate(true)
	for status in ["unverified", "sensor_fix_acquired", "visited"]:
		lead_root.station_lead.status = status
		var lead_pages := Notes.pages(lead_root)
		failures += check(lead_pages.size() == 4 and lead_pages[3].title == "STATION LEAD" and "Origin bay attendant" in lead_pages[3].text and "ship time 0.0 s" in lead_pages[3].text and {"unverified": "Unverified report", "sensor_fix_acquired": "Sensor fix acquired", "visited": "Visited"}[status] in lead_pages[3].text, "Retained note omitted authoritative status/provenance")
	lead_root.station_lead.status = "unverified"
	failures += check(lead_root == frozen_lead, "Station lead archive mutated its source")
	for field in retained:
		var bad := retained.duplicate(true)
		bad.erase(field)
		failures += check(not Notes.station_lead_valid(bad), "Missing station lead field accepted: " + field)
	for change in [{"extra": 1}, {"version": 2}, {"version": 1.0}, {"selected": 1}, {"kept": 1}, {"can_keep": 1}, {"selected": false}, {"can_keep": true}, {"offer_tick": "0"}, {"received_tick": ""}, {"received_tick": "00"}, {"received_tick": "18446744073709551615"}, {"received_tick": NAN}, {"status": "unrecorded"}, {"status": "resolved"}, {"source": "Other worker"}, {"note": ""}, {"note": "x".repeat(2049)}, {"note": INF}]:
		var bad := retained.duplicate(true)
		bad.merge(change, true)
		failures += check(not Notes.station_lead_valid(bad), "Malformed retained lead accepted: " + str(change))
		lead_root.station_lead = bad
		var lead_pages := Notes.pages(lead_root)
		failures += check(lead_pages.size() == 4 and "unavailable" in lead_pages[3].text and contact.history in lead_pages[2].text, "Bad lead erased other valid note families")
	for tick in ["0", "18446744073709551614"]:
		var boundary := offered.duplicate(true)
		boundary.offer_tick = tick
		failures += check(Notes.station_lead_valid(boundary), "Valid offer clock boundary refused")
	for change in [{"offer_tick": ""}, {"offer_tick": "01"}, {"offer_tick": "18446744073709551615"}, {"offer_tick": 0}, {"offer_tick": INF}, {"received_tick": "0"}, {"status": "unverified"}, {"source": ""}]:
		var bad := offered.duplicate(true)
		bad.merge(change, true)
		failures += check(not Notes.station_lead_valid(bad), "Malformed offer accepted")
	var unavailable_lead := offered.duplicate(true)
	unavailable_lead.can_keep = false
	unavailable_lead.offer_tick = ""
	unavailable_lead.source = ""
	failures += check(Notes.station_lead_valid(unavailable_lead), "Selected unavailable report refused")
	unavailable_lead.selected = false
	unavailable_lead.status = "unselected"
	failures += check(Notes.station_lead_valid(unavailable_lead), "Legacy unselected report refused")
	var lead_bridge := LeadBridge.new()
	lead_bridge.sample_packet = packet.samples
	lead_bridge.derelict_packet = contact
	lead_bridge.lead_packet = retained
	var projected := Notes.read_bridge(lead_bridge)
	Notes.pages(projected)
	Notes.pages(projected)
	failures += check(lead_bridge.lead_queries == 1 and lead_bridge.sample_queries == 1 and lead_bridge.flight_queries == 1 and lead_bridge.commands.is_empty() and lead_bridge.saved == "unchanged complete voyage" and projected.station_lead == retained, "Archive repeatedly queried/operated the lead owner")
	print("Exploration notes checks: %d failures" % failures)
	return failures
