extends Control
## Presentation of read-only C++ coast samples and geometric references.
var guidance: Dictionary = {}
var cockpit := false
const INK := Color("bcebf0")
const WARN := Color("ffc478")
const COAST := Color("91e8be")
const REFERENCE := Color("69baff")

func text_at(at: Vector2, text: String, pixels := 17, color := INK) -> void:
	draw_string(ThemeDB.fallback_font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, pixels, color)

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
	var title: String = ["", "ESTABLISH ORBIT", "ATMOSPHERIC RETURN", "ESCAPE"][mode]
	text_at(Vector2(20, 38) if cockpit else Vector2(14, 27), title + (" / MANUAL" if cockpit else ""), 34 if cockpit else 20)
	if not cockpit:
		text_at(Vector2(14, 50), "MANUAL / REFERENCE ONLY", 15, WARN)
	var coast: PackedVector2Array = guidance.coast
	var reference: PackedVector2Array = guidance.reference
	var extent := maxf(1.0, float(guidance.air_radius))
	for points in [coast, reference]:
		for point in points:
			if not point.is_finite():
				return
			extent = maxf(extent, point.length())
	var center := Vector2(180, 235) if cockpit else Vector2(size.x * 0.5, 145)
	var scale_px := (120.0 if cockpit else 80.0) / extent
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
		text_at(Vector2(14, 244), "Green: coast %.0f s  /  Blue: reference" % guidance.seconds, 16)
	var cue := "Ideal reference; not a scheduled burn"
	match str(guidance.cue):
		"below_surface": cue = "Below reference surface; forecast stopped"
		"climb": cue = "Climb clear of atmosphere first"
		"sideways": cue = "Build sideways speed above atmosphere"
		"circular": cue = "Circular reference: remove radial motion"
		"orbit": cue = "Clear orbit; coast and watch periapsis"
		"return": cue = "Return reference reaches air edge, not ground"
		"escape": cue = "Escape reference: zero-energy threshold"
		"unbound": cue = "Escape energy reached; check coast path"
		"in_atmosphere": cue = "In atmosphere; vacuum forecast unavailable"
	text_at(Vector2(20, 80) if cockpit else Vector2(14, 266), cue, 28 if cockpit else 16, WARN)
	var limit := "Vacuum only; thrust, drag and terrain omitted"
	if int(guidance.termination) == 2:
		limit = "Stops at air edge; entry safety NOT predicted"
	elif int(guidance.termination) == 3:
		limit = "Stops at datum sphere; terrain NOT predicted"
	elif int(guidance.termination) == 4:
		limit = "Forecast truncated at numerical limit"
	if cockpit:
		text_at(Vector2(365, 145), "SIDEWAYS / m/s", 26)
		text_at(Vector2(365, 192), "%.0f" % guidance.horizontal_speed, 44, COAST)
		text_at(Vector2(365, 236), "REF %.0f m/s" % guidance.reference_speed, 30, REFERENCE)
		text_at(Vector2(365, 280), "VERTICAL %+.0f m/s" % guidance.radial_speed, 28)
		text_at(Vector2(365, 322), "REF ALT %.0f km" % (guidance.reference_altitude / 1000.0), 27, REFERENCE)
		text_at(Vector2(20, 390), limit, 26, WARN)
		text_at(Vector2(20, 425), "GREEN coast %.0f s / BLUE ideal reference" % guidance.seconds, 26)
		return
	text_at(Vector2(14, 289), "REF %.0f m/s at %.0f km datum altitude" % [guidance.reference_speed, guidance.reference_altitude / 1000.0], 16, REFERENCE)
	text_at(Vector2(14, 312), "NOW sideways %.0f / vertical %+.0f m/s" % [guidance.horizontal_speed, guidance.radial_speed], 16)
	text_at(Vector2(14, 340), limit, 15, WARN)
	var footer := "Reference plane uses projected ship heading" if guidance.radial_degenerate else "Flight continues; selecting never fires engines"
	if guidance.assist or guidance.thrust_active:
		footer = "Assist / thrust active: coast path will change"
	text_at(Vector2(14, 364), footer, 15)
