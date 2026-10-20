extends Node3D
## Read-only saved-flight instruments on the three authenticated screen planes.
const MoonState = preload("res://scripts/native/native_moon_state.gd")
const MinorState = preload("res://scripts/native/native_minor_state.gd")
const EnvironmentState = preload("res://scripts/native/native_environment.gd")
const REFRESH_SECONDS := 0.1
var viewports: Array[SubViewport] = []
var faces: Array[Control] = []
var panels: Array[MeshInstance3D] = []
var presented_rows: Array[PackedStringArray] = []
var _elapsed := 0.0

class InstrumentFace extends Control:
	var lines := PackedStringArray()
	func _draw() -> void:
		draw_rect(Rect2(Vector2.ZERO, size), Color("031017"))
		draw_rect(Rect2(Vector2(7, 7), size - Vector2(14, 14)), Color("527482"), false, 2.0)
		if lines.is_empty(): return
		var step := (size.y - 58.0) / maxf(1.0, lines.size() - 1)
		for index in lines.size():
			var ink := Color("bcebf0") if index == 0 else Color("91e8be")
			draw_string(ThemeDB.fallback_font, Vector2(20, 36 + step * index), lines[index], HORIZONTAL_ALIGNMENT_LEFT, size.x - 40, 28 if index == 0 else 25, ink)

static func finite(value: Variant) -> bool:
	return (value is float or value is int) and is_finite(float(value))

static func valid_screens(screens: Variant) -> bool:
	if not screens is Array or screens.size() != 3: return false
	for index in 3:
		var screen: Variant = screens[index]
		if not screen is Dictionary or not finite(screen.get("page")) or screen.page != index: return false
		var columns: Variant = screen.get("transform")
		if not columns is Array or columns.size() != 4: return false
		for column in columns:
			if not column is Array or column.size() != 3: return false
			for value in column:
				if not finite(value) or absf(value) > 100.0: return false
		var basis := Basis(Vector3(columns[0][0], columns[0][1], columns[0][2]), Vector3(columns[1][0], columns[1][1], columns[1][2]), Vector3(columns[2][0], columns[2][1], columns[2][2]))
		if not basis.is_equal_approx(basis.orthonormalized()) or absf(basis.determinant() - 1.0) > 0.001: return false
		var extent: Variant = screen.get("size")
		if not extent is Array or extent.size() != 2: return false
		for value in extent:
			if not finite(value) or value < 0.1 or value > 1.0: return false
	return true

static func valid_state(state: Dictionary) -> bool:
	if state.get("mode") != "freedom_flight" or not EnvironmentState.decimal(state.get("tick")) or not state.get("assistance") is bool or not state.get("attached") is bool: return false
	if state.get("body_kind") == "minor":
		if not MinorState.valid(state): return false
	elif state.get("body_kind") == "moon":
		if not MoonState.valid(state): return false
	else:
		for field in ["altitude", "planet_radius", "surface_speed", "radial_rate", "periapsis_radius"]:
			if not finite(state.get(field)) or absf(state[field]) > 2.0e15: return false
		if state.planet_radius <= 0 or state.altitude <= -state.planet_radius or state.surface_speed < 0 or state.periapsis_radius < 0: return false
		if state.get("orbit_classification") not in ["stable", "decaying", "impact", "escape"] or not state.has("apoapsis_radius"): return false
		if state.apoapsis_radius != null and (not finite(state.apoapsis_radius) or state.apoapsis_radius < state.periapsis_radius or state.apoapsis_radius > 2.0e15): return false
	var resources: Variant = state.get("resources", {})
	if not resources is Dictionary or typeof(resources.get("selected", false)) != TYPE_BOOL: return false
	if resources.get("selected", false):
		if not finite(resources.get("fraction")) or resources.fraction < 0 or resources.fraction > 1 or typeof(resources.get("jump_charges")) != TYPE_INT or resources.jump_charges < 0 or resources.jump_charges > 3: return false
	for sign_name in ["positive", "negative"]:
		var force: Variant = state.get(sign_name + "_force_body")
		var ratings: Variant = state.get(sign_name + "_force_ratings")
		if not force is PackedFloat64Array or force.size() != 3 or not ratings is PackedFloat64Array or ratings.size() != 3: return false
		for axis in 3:
			if not is_finite(force[axis]) or force[axis] < 0 or not is_finite(ratings[axis]) or ratings[axis] <= 0: return false
	return true

static func distance(metres: float) -> String:
	return "%.2e km" % (metres / 1000.0) if absf(metres) > 1.0e12 else "%.1f km" % (metres / 1000.0)

static func rows(state: Dictionary) -> Array[PackedStringArray]:
	if not valid_state(state): return []
	var nav: PackedStringArray
	if state.get("body_kind") == "minor":
		nav = PackedStringArray(["NAV / ASTEROID INSPECTION", "Spherical source proxy / no landing", "Local drive: " + str(state.jump.phase).capitalize(), "Transfer: %.1f s" % float(state.jump.remaining_seconds), "Home route uses one jump charge", "Stand-off gap: %.1f m" % state.stand_off_gap])
	elif state.get("body_kind") == "moon":
		nav = PackedStringArray(["NAV / LOCAL MOON", "Airless surface", "Local drive: " + str(state.jump.phase).capitalize(), "Transfer: %.1f s" % float(state.jump.remaining_seconds), "Home route uses one jump charge", "Reference altitude is not gear clearance"])
	else:
		nav = PackedStringArray(["NAV / CENTRAL-BODY FORECAST", "Orbit: " + state.orbit_classification.capitalize(), "Periapsis: " + distance(state.periapsis_radius - state.planet_radius), "Apoapsis: " + (distance(state.apoapsis_radius - state.planet_radius) if state.apoapsis_radius != null else "unavailable"), "Thrust and drag change this forecast", "Not terrain or landing clearance"])
	var flight := PackedStringArray(["FLIGHT / CURRENT OBSERVATIONS", "Reference altitude: " + distance(state.altitude), "Surface speed: %.1f m/s" % state.surface_speed, "Radial rate: %+.1f m/s" % state.radial_rate, "Attached" if state.attached else "Free craft", "C++ tick " + state.tick])
	if state.get("body_kind") == "moon": flight[4] = MoonState.gear_clearance_text(state)
	if state.get("body_kind") == "minor":
		flight[1] = "Proxy clearance: " + distance(state.altitude)
		flight[4] = "Inspection stand-off / no landing"
	var resources: Dictionary = state.get("resources", {})
	var systems := PackedStringArray(["SYSTEMS / APPLIED PROPULSION", "Fuel: %.1f%%" % (resources.fraction * 100.0) if resources.get("selected", false) else "Fuel tracking unavailable", "Jump charges: %d/3" % resources.jump_charges if resources.get("selected", false) else "Historical resource selection", "Main %.0f%% / Retro %.0f%%" % [clampf(state.negative_force_body[2] / state.negative_force_ratings[2], 0, 1) * 100, clampf(state.positive_force_body[2] / state.positive_force_ratings[2], 0, 1) * 100], "Flight assistance: " + ("ON" if state.assistance else "OFF"), "Actual C++ readings / no flight commands"])
	return [nav, flight, systems]

func bind_screens(screens: Array) -> bool:
	if not viewports.is_empty() or not valid_screens(screens): return false
	for screen: Dictionary in screens:
		var viewport := SubViewport.new()
		var longest: float = maxf(screen["size"][0], screen["size"][1])
		viewport.size = Vector2i(roundi(512.0 * screen["size"][0] / longest), roundi(512.0 * screen["size"][1] / longest))
		viewport.disable_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
		add_child(viewport)
		var face := InstrumentFace.new()
		face.mouse_filter = Control.MOUSE_FILTER_IGNORE
		face.size = Vector2(viewport.size)
		viewport.add_child(face)
		var panel := MeshInstance3D.new()
		var quad := QuadMesh.new()
		quad.size = Vector2(screen["size"][0], screen["size"][1])
		panel.mesh = quad
		var columns: Array = screen.transform
		panel.transform = Transform3D(Basis(Vector3(columns[0][0], columns[0][1], columns[0][2]), Vector3(columns[1][0], columns[1][1], columns[1][2]), Vector3(columns[2][0], columns[2][1], columns[2][2])), Vector3(columns[3][0], columns[3][1], columns[3][2]))
		# One millimetre toward the pilot avoids fighting the baked screen face.
		panel.position += panel.basis.z * 0.001
		panel.layers = 2
		panel.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_texture = viewport.get_texture()
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
		panel.material_override = material
		add_child(panel)
		viewports.append(viewport)
		faces.append(face)
		panels.append(panel)
	return true

func refresh(state: Dictionary, delta: float = 0.0, force: bool = false) -> bool:
	if not valid_state(state) or not is_finite(delta) or delta < 0 or viewports.size() != 3 or faces.size() != 3 or panels.size() != 3: return false
	for index in 3:
		if not is_instance_valid(viewports[index]) or not is_instance_valid(faces[index]) or not is_instance_valid(panels[index]): return false
	var next := rows(state)
	if next == presented_rows: return true
	_elapsed += delta
	if not force and not presented_rows.is_empty() and _elapsed < REFRESH_SECONDS: return true
	_elapsed = 0.0
	presented_rows = next
	for index in 3:
		faces[index].lines = next[index]
		faces[index].queue_redraw()
		viewports[index].render_target_update_mode = SubViewport.UPDATE_ONCE
	return true
