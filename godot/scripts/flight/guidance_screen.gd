extends Control
## Presentation of read-only C++ coast samples and geometric references.
var guidance: Dictionary = {}
var orbit_status: Dictionary = {}
var cockpit := false
const INK := Color("bcebf0")
const WARN := Color("ffc478")
const COAST := Color("91e8be")
const REFERENCE := Color("69baff")

func text_at(at: Vector2, text: String, pixels := 17, color := INK) -> void:
	draw_string(ThemeDB.fallback_font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, pixels, color)

static func cue_text(plan: Dictionary, status: Dictionary) -> String:
	if int(plan.get("mode", 0)) == 1 and status.get("established", false):
		return "Reference circle is optional"
	match str(plan.get("cue", "")):
		"below_surface": return "Below surface; forecast stopped"
		"climb": return "Climb clear of atmosphere first"
		"sideways": return "Build speed along the horizon"
		"circular": return "Reference circle is optional"
		"orbit": return "Check current orbit status above"
		"return": return "Return reference ends at air edge"
		"escape": return "Escape reference: energy threshold"
		"unbound": return "Escape energy reached; check path"
		"in_atmosphere": return "In air; vacuum forecast unavailable"
	return "Ideal reference; not a scheduled burn"

func _draw() -> void:
	if guidance.is_empty():
		return
	draw_rect(Rect2(Vector2.ZERO, size), Color(0.01, 0.035, 0.05, 0.94))
	if not guidance.get("ok", false):
		text_at(Vector2(14, 28), "GUIDANCE UNAVAILABLE", 19, WARN)
		return
	var mode: int = guidance.get("mode", 0)
	if mode < 1 or mode > 3:
		return
	var title: String = ["", "ORBIT GUIDANCE", "RETURN GUIDANCE", "ESCAPE GUIDANCE"][mode]
	text_at(Vector2(20, 35) if cockpit else Vector2(14, 25), title, 28 if cockpit else 17)
	# The exact same authoritative status is injected into cockpit and chase
	# views. A circle, forecast cue or speed target cannot establish an orbit.
	text_at(Vector2(20, 80) if cockpit else Vector2(14, 55), str(orbit_status.get("orbit_label", "ORBIT DATA UNAVAILABLE")), 38 if cockpit else 25, COAST if orbit_status.get("established", false) else WARN)
	text_at(Vector2(20, 112) if cockpit else Vector2(14, 78), str(orbit_status.get("orbit_reason", "Check flight telemetry")), 26 if cockpit else 17)
	var coast: PackedVector2Array = guidance.coast
	var reference: PackedVector2Array = guidance.reference
	var extent := maxf(1.0, float(guidance.air_radius))
	for points in [coast, reference]:
		for point in points:
			if not point.is_finite():
				return
			extent = maxf(extent, point.length())
	var center := Vector2(170, 270) if cockpit else Vector2(size.x * 0.5, 156)
	var scale_px := (92.0 if cockpit else 60.0) / extent
	draw_circle(center, scale_px * float(guidance.air_radius), Color("70567e"), false, 1.0)
	draw_circle(center, scale_px, Color("243d48"))
	for pair in [[reference, REFERENCE], [coast, COAST]]:
		var points: PackedVector2Array = pair[0]
		for i in range(1, points.size()):
			# Dashed blue reference is distinct from solid green current coast.
			if pair[1] == REFERENCE and i % 3 == 0:
				continue
			draw_line(center + points[i - 1] * scale_px, center + points[i] * scale_px, pair[1], 2.0, true)
	if not coast.is_empty():
		draw_circle(center + coast[0] * scale_px, 4, INK)
	if not cockpit:
		text_at(Vector2(14, 235), "Green: coast %.0f s / Blue: reference" % guidance.seconds, 15)
	text_at(Vector2(20, 148) if cockpit else Vector2(14, 257), cue_text(guidance, orbit_status), 26 if cockpit else 16, WARN)
	var limit := "Vacuum only; thrust, drag and terrain omitted"
	if int(guidance.termination) == 2:
		limit = "Stops at air edge; entry safety NOT predicted"
	elif int(guidance.termination) == 3:
		limit = "Stops at datum sphere; terrain NOT predicted"
	elif int(guidance.termination) == 4:
		limit = "Forecast truncated at numerical limit"
	if cockpit:
		text_at(Vector2(365, 190), "HORIZON SPEED / m/s", 26)
		text_at(Vector2(365, 237), "%.0f" % guidance.horizontal_speed, 44, COAST)
		text_at(Vector2(365, 275), "REF %.0f m/s" % guidance.reference_speed, 30, REFERENCE)
		text_at(Vector2(365, 313), "VERTICAL %+.0f m/s" % guidance.radial_speed, 28)
		text_at(Vector2(365, 350), "REF ALT %.0f km" % (guidance.reference_altitude / 1000.0), 27, REFERENCE)
		text_at(Vector2(20, 390), limit, 26, WARN)
		text_at(Vector2(20, 425), "GREEN coast %.0f s / BLUE ideal reference" % guidance.seconds, 26)
		return
	text_at(Vector2(14, 280), "REF %.0f m/s at %.0f km datum altitude" % [guidance.reference_speed, guidance.reference_altitude / 1000.0], 16, REFERENCE)
	text_at(Vector2(14, 302), "HORIZON %.0f / VERTICAL %+.0f m/s" % [guidance.horizontal_speed, guidance.radial_speed], 16)
	text_at(Vector2(14, 325), limit, 15, WARN)
	var footer := "Reference uses projected ship heading" if guidance.radial_degenerate else "Flight continues; guidance never fires engines"
	if guidance.thrust_active:
		footer = "Active thrust can change the coast path"
	text_at(Vector2(14, 347), footer, 15)
	text_at(Vector2(14, 369), str(orbit_status.get("motion", "")), 15)
