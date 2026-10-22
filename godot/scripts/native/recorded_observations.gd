extends "res://scripts/flight/flight_basics.gd"
## Reuse the paused reference's pagination, readable sizing and pad scrolling.
## This view consumes C++ records and retained notes, never world descriptors.
## Mark site is an explicit command; reading and pagination remain read-only.
var query: Callable
var notes_query: Callable
var readings: Dictionary = {}
var note_pages: Array = []
var mark_command: Callable
var mark_button: Button
var action_message := ""
const OrbitalSurvey = preload("res://scripts/native/native_orbital_survey.gd")
const ExplorationNotes = preload("res://scripts/native/exploration_notes.gd")
const VALUE_FIELDS := {
	"identity_type": {"name": TYPE_STRING},
	"physical": {"radius_kilometres": TYPE_INT, "gravity_milli_g": TYPE_INT},
	"atmosphere": {"class": TYPE_STRING, "pressure_millibars": TYPE_INT},
	"hazards": {"min_temperature_mk": TYPE_INT, "max_temperature_mk": TYPE_INT, "radiation_nsv_per_hour": TYPE_INT, "electric_field_v_per_m": TYPE_INT, "gust_mm_per_second": TYPE_INT, "ground_acceleration_mm_per_second2": TYPE_INT, "visibility": TYPE_STRING, "maximum_obscuration_basis_points": TYPE_INT},
	"landing": {"character": TYPE_STRING, "water_basis_points": TYPE_INT},
	"station_ports": {"count": TYPE_INT},
	"presence": {"arrival_tick": TYPE_STRING},
}

static func valid_readings(candidate: Variant) -> bool:
	if not candidate is Dictionary or not candidate.get("selected") is bool or not candidate.get("tick") is String or not candidate.get("subjects") is Array: return false
	if candidate.subjects.size() > 128 or (not candidate.selected and not candidate.subjects.is_empty()): return false
	for subject in candidate.subjects:
		if not subject is Dictionary: return false
		if subject.has("orbital_survey") and not OrbitalSurvey.valid_provenance(subject.orbital_survey): return false
		for key in ["name", "kind", "system_id", "subject_id"]:
			if not subject.get(key) is String: return false
		if not subject.get("facts") is Array or subject.facts.size() > 8: return false
		for fact in subject.facts:
			if not fact is Dictionary or not fact.get("value") is Dictionary: return false
			for key in ["fact", "confidence", "source", "source_id", "tick", "craft_id"]:
				if not fact.get(key) is String: return false
			if fact.confidence not in ["contact", "probable", "resolved", "visited"]: return false
			if fact.confidence in ["contact", "probable"] and not fact.value.is_empty(): return false
			var fields: Dictionary = VALUE_FIELDS.get(fact.fact, {})
			if fact.fact == "location":
				fields = {"x_metres": TYPE_STRING, "y_metres": TYPE_STRING, "z_metres": TYPE_STRING} if fact.value.has("x_metres") else {"radius_kilometres": TYPE_STRING, "period_ticks": TYPE_STRING, "epoch_phase_turns": TYPE_INT, "inclination_microdegrees": TYPE_INT, "ascending_node_turns": TYPE_INT}
			elif fields.is_empty(): return false
			if not fact.value.is_empty():
				for key in fields:
					if typeof(fact.value.get(key)) != fields[key]: return false
	return true

func _ready() -> void:
	super._ready()
	mark_button = _button(_buttons, "Mark site", mark_current_site)
	_buttons.move_child(mark_button, 2)
	refresh()
	_apply_readable_scale()

func navigation_buttons() -> Array:
	var result: Array = []
	for button in [previous_button, next_button, mark_button, back_button]:
		if is_instance_valid(button) and button.visible and not button.disabled: result.append(button)
	return result

func _apply_readable_scale() -> void:
	super._apply_readable_scale()
	if is_instance_valid(mark_button):
		mark_button.add_theme_font_size_override("font_size", roundi(24 * _readable_scale))
		mark_button.custom_minimum_size.y = 48 * _readable_scale

func change_page(direction: int) -> void:
	action_message = ""
	super.change_page(direction)

func current_site_action() -> Dictionary:
	var facts_count: int = 1 if readings.has("error") else readings.get("subjects", []).size()
	var index: int = page - facts_count
	if index < 0 or index >= note_pages.size(): return {}
	return note_pages[index].get("site_action", {})

func refresh_marker_action() -> void:
	if not is_instance_valid(mark_button): return
	var action := current_site_action()
	mark_button.visible = not action.is_empty()
	mark_button.disabled = action.is_empty() or not action.get("available", false) or not mark_command.is_valid()
	mark_button.text = "Replace marker" if action.get("replace", false) else "Mark site"
	mark_button.tooltip_text = str(action.get("reason", ""))
	if mark_button.has_focus() and (not mark_button.visible or mark_button.disabled): back_button.grab_focus()
	var ordered := navigation_buttons()
	for index in ordered.size():
		var button: Button = ordered[index]
		button.focus_neighbor_left = button.get_path_to(ordered[posmod(index - 1, ordered.size())])
		button.focus_neighbor_right = button.get_path_to(ordered[(index + 1) % ordered.size()])
		button.focus_previous = button.focus_neighbor_left
		button.focus_next = button.focus_neighbor_right

func mark_current_site() -> void:
	if not is_visible_in_tree() or not is_instance_valid(mark_button) or not mark_button.visible or mark_button.disabled or not mark_command.is_valid() or not is_instance_valid(controls) or not controls.focused or not controls.waiting_action.is_empty(): return
	var action := current_site_action()
	if action.is_empty() or not action.get("available", false): return
	var result: Variant = mark_command.call(action.target.duplicate(true), action.replace)
	if not result is Dictionary or not result.get("ok") is bool or not result.get("reason") is String: return
	# Explicit actions refresh the one-shot projection, including stale marker
	# presence after a refusal. Reading ordinary pages never invokes a command.
	note_pages = ExplorationNotes.pages(notes_query.call()) if notes_query.is_valid() else []
	action_message = result.reason
	refresh()
	if mark_button.visible and not mark_button.disabled: mark_button.grab_focus()

func titles() -> Array:
	var subjects: Array = readings.get("subjects", [])
	var result: Array = ["Unavailable observations"] if readings.has("error") else subjects.duplicate()
	result.append_array(note_pages)
	return result if not result.is_empty() else ["No records"]

func open() -> void:
	action_message = ""
	var candidate: Variant = query.call() if query.is_valid() else null
	readings = candidate if valid_readings(candidate) else {"error": "Observation records are unavailable."}
	note_pages = ExplorationNotes.pages(notes_query.call()) if notes_query.is_valid() else []
	super.open()
	if next_button.disabled: back_button.grab_focus()

func refresh() -> void:
	if not is_instance_valid(heading): return
	var subjects: Array = readings.get("subjects", [])
	var count := titles().size()
	page = clampi(page, 0, count - 1)
	heading.text = "RECORDED OBSERVATIONS  %d / %d" % [page + 1, count]
	_paused.text = "PAUSED / RETAINED KNOWLEDGE"
	if readings.get("selected", false):
		_paused.text += " · ship time " + time_text(readings.tick)
	_hint.text = "Left / right or Tab: select · Up / down: scroll\nB / Circle / Esc / Start: back to controls"
	var facts_count := 1 if readings.has("error") else subjects.size()
	if page >= facts_count and not note_pages.is_empty():
		var note: Dictionary = note_pages[page - facts_count]
		body.text = str(note.title) + "\n\n" + str(note.text)
	elif readings.has("error"):
		body.text = str(readings.error)
	elif subjects.is_empty():
		body.text = "No observations have been recorded." if readings.get("selected", false) else "This journey has no selected observation records."
		body.text += " Reading this page does not start sensors or upgrade a save."
	else:
		body.text = subject_text(subjects[page])
	if not action_message.is_empty(): body.text += "\n\n" + action_message
	previous_button.disabled = count < 2
	next_button.disabled = count < 2
	refresh_marker_action()
	if is_visible_in_tree() and next_button.disabled:
		back_button.grab_focus()

static func subject_text(subject: Dictionary) -> String:
	var lines := PackedStringArray([str(subject.name), "%s · system %s" % [str(subject.kind).capitalize(), subject.system_id], ""])
	if subject.has("orbital_survey") and OrbitalSurvey.valid_provenance(subject.orbital_survey):
		var survey: Dictionary = subject.orbital_survey
		lines.append("Orbital terrain survey · 30 s acquired\nStarted at ship time %s · completed %s · craft %s\n" % [time_text(survey.started_tick), time_text(survey.completed_tick), survey.craft_id])
	for fact: Dictionary in subject.facts:
		lines.append("%s · %s" % [str(fact.fact).replace("_", " ").capitalize(), str(fact.confidence).to_upper()])
		lines.append(value_text(fact.fact, fact.value))
		lines.append("Source: %s %s · ship time %s · craft %s" % [str(fact.source).replace("_", " "), fact.source_id, time_text(fact.tick), fact.craft_id])
		lines.append("")
	lines.append("Recorded models are not live weather, certified landing sites or a guarantee of safety.")
	return "\n".join(lines)

static func time_text(tick: String) -> String:
	return "%.1f s" % (tick.to_float() / 120.0)

static func value_text(fact: String, value: Dictionary) -> String:
	if value.is_empty(): return "Exact values unresolved."
	match fact:
		"identity_type": return str(value.name)
		"location":
			if value.has("x_metres"):
				return "Chart position: (%s, %s, %s) m" % [value.x_metres, value.y_metres, value.z_metres]
			return "Orbit radius: %s km · period: %.1f h\nEpoch phase: %.2f° · inclination: %.2f° · ascending node: %.2f°" % [value.radius_kilometres, str(value.period_ticks).to_float() / 432000.0, float(value.epoch_phase_turns) * 360.0 / 4294967296.0, float(value.inclination_microdegrees) / 1000000.0, float(value.ascending_node_turns) * 360.0 / 4294967296.0]
		"physical": return "Radius: %s km · surface gravity: %.3f g" % [value.radius_kilometres, float(value.gravity_milli_g) / 1000.0]
		"atmosphere": return "Surface model: %s · pressure: %s mbar" % [value["class"], value.pressure_millibars]
		"hazards": return "Reference envelope\nTemperature: %.1f–%.1f K\nRadiation: %s nSv/h · electric field: %s V/m\nGust: %.1f m/s · ground acceleration: %.3f m/s²\nVisibility: %s · maximum obscuration: %.1f%%" % [float(value.min_temperature_mk) / 1000.0, float(value.max_temperature_mk) / 1000.0, value.radiation_nsv_per_hour, value.electric_field_v_per_m, float(value.gust_mm_per_second) / 1000.0, float(value.ground_acceleration_mm_per_second2) / 1000.0, value.visibility, float(value.maximum_obscuration_basis_points) / 100.0]
		"landing": return "Terrain model: %s · water coverage: %.1f%%" % [value.character, float(value.water_basis_points) / 100.0]
		"station_ports": return "Recorded ports: %s" % value.count
		"presence": return "Physical arrival recorded at ship time %s" % time_text(value.arrival_tick)
	return "No readable value."
