extends RefCounted
## Presentation of the C++ named-jump budget. Reading it never plans a jump.
const Finance = preload("res://scripts/native/paid_operations.gd")

static func bounded_text(value: Variant, limit: int) -> bool:
	if not value is String: return false
	var bytes: PackedByteArray = value.to_utf8_buffer()
	return not bytes.is_empty() and bytes.size() <= limit and not bytes.has(0)

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 11: return false
	if not value.get("version") is int or value.version != 1: return false
	for key in ["available", "reserve", "operationally_empty"]:
		if not value.get(key) is bool: return false
	if not Finance.decimal(value.get("tick")): return false
	for key in ["jump_charges", "required_charges", "charge_shortfall"]:
		if not value.get(key) is int or value[key] < 0: return false
	if value.jump_charges > 3 or value.required_charges > 2 or value.charge_shortfall > 2: return false
	if not value.get("flight_fraction") is float or not is_finite(value.flight_fraction) or value.flight_fraction < 0.0 or value.flight_fraction > 1.0: return false
	if not bounded_text(value.get("note"), 1024) or not value.get("legs") is Array or value.legs.size() > 2: return false
	if not value.available:
		return value.legs.is_empty() and value.required_charges == 0 and value.charge_shortfall == 0
	if value.legs.is_empty() or value.required_charges != value.legs.size() or value.charge_shortfall != maxi(value.required_charges - value.jump_charges, 0): return false
	for i in value.legs.size():
		var leg: Variant = value.legs[i]
		if not leg is Dictionary or leg.size() != 4: return false
		for key in ["source_id", "destination_id"]:
			if not Finance.decimal(leg.get(key)) or leg[key] == "0": return false
		for key in ["source_name", "destination_name"]:
			if not bounded_text(leg.get(key), 256): return false
		if leg.source_id == leg.destination_id: return false
		if i > 0 and (value.legs[i - 1].destination_id != leg.source_id or value.legs[i - 1].destination_name != leg.source_name): return false
	return true

static func text(value: Variant) -> String:
	if not valid_packet(value): return "Return-charge check unavailable."
	if not value.available: return str(value.note)
	var route: String = value.legs[0].source_name
	for leg: Dictionary in value.legs:
		route += " → " + str(leg.destination_name)
	var result := "Return-charge check\n%s\n%d charges needed · %d carried" % [route, value.required_charges, value.jump_charges]
	if value.charge_shortfall > 0: result += " · SHORT %d" % value.charge_shortfall
	var notice := " · RESERVE" if value.reserve else ""
	if value.operationally_empty: notice = " · EMPTY"
	return result + "\nFlight fuel %.1f%%%s\n%s" % [value.flight_fraction * 100.0, notice, value.note]
