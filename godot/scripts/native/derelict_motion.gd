extends RefCounted
## Simultaneous C++ observations; positive closing means approaching the hull.
const Derelict = preload("res://scripts/native/native_derelict_view.gd")
const Ids = preload("res://scripts/native/native_environment.gd")

static func valid_packet(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 6: return false
	if not value.get("version") is int or value.version != 1 or not value.get("available") is bool: return false
	if not Ids.decimal(value.get("tick")): return false
	for field in ["closing_metres_per_second", "transverse_metres_per_second"]:
		if not value.get(field) is float or not is_finite(value[field]) or absf(value[field]) > 4.0e9: return false
	if value.transverse_metres_per_second < 0.0: return false
	if not value.get("note") is String or value.note.is_empty(): return false
	var bytes: PackedByteArray = value.note.to_utf8_buffer()
	if bytes.size() > 1024 or bytes.has(0): return false
	if not value.available and (value.closing_metres_per_second != 0.0 or value.transverse_metres_per_second != 0.0): return false
	return true

static func valid_state(state: Dictionary) -> bool:
	var motion: Variant = state.get("derelict_motion")
	if not valid_packet(motion) or motion.tick != state.get("tick"): return false
	if not Derelict.valid_packet(state.get("derelict")): return false
	return not motion.available or (state.derelict.local and state.derelict.known and state.derelict.distance_metres > 0.0)

static func text(state: Dictionary) -> String:
	if not valid_state(state) or not state.derelict_motion.available: return ""
	var motion: Dictionary = state.derelict_motion
	var closing: float = motion.closing_metres_per_second
	# Avoid a misleading negative-zero cue at the display's tenth-m/s precision.
	if absf(closing) < 0.05: closing = 0.0
	return "Closing %+.1f m/s · Sideways %.1f m/s" % [closing, motion.transverse_metres_per_second]

static func hud_text(state: Dictionary) -> String:
	var motion_text := text(state)
	if motion_text.is_empty(): return ""
	var contact: Dictionary = state.derelict
	var distance := "%.1f km" % (contact.distance_metres / 1000.0) if contact.distance_metres >= 1000.0 else "%.1f m" % contact.distance_metres
	return "DERELICT · %s · speed %.1f m/s\n%s" % [distance, contact.relative_speed_metres_per_second, motion_text]
