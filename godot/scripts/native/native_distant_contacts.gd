extends RefCounted
## C++ far-field optical projections only; no placement, clock or flight target.
const SkyShader = preload("res://shaders/native_navigation_sky.gdshader")
const MAX_CONTACTS := 2

static func decimal(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value[0] == "0": return false
	for byte in value.to_ascii_buffer():
		if byte < 48 or byte > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

static func unit_vector(value: Variant) -> bool:
	return value is Vector3 and value.is_finite() and absf(value.length_squared() - 1.0) < 0.0001

static func valid(state: Dictionary) -> bool:
	if not state.has("distant_contacts"): return true
	var data: Variant = state.distant_contacts
	if not data is Dictionary or data.size() != 5 or typeof(data.get("version")) != TYPE_INT or data.version != 1 or typeof(data.get("selected")) != TYPE_BOOL: return false
	for field in ["system_id", "tick"]:
		if not decimal(data.get(field)) or data[field] != state.get(field): return false
	if not data.get("contacts") is Array or data.contacts.size() > MAX_CONTACTS or (not data.selected and not data.contacts.is_empty()): return false
	var ids := {}
	for contact in data.contacts:
		if not contact is Dictionary or contact.size() != 9 or not decimal(contact.get("id")) or ids.has(contact.id): return false
		ids[contact.id] = true
		for field in ["label", "reason"]:
			if not contact.get(field) is String or contact[field].length() > (64 if field == "label" else 160): return false
		if not contact.get("status") is String or contact.status not in ["visible", "unresolved", "occluded", "close_unavailable"]: return false
		for field in ["direction", "light_direction"]:
			if not unit_vector(contact.get(field)): return false
		if not contact.get("angular_radius") is float or not is_finite(contact.angular_radius) or contact.angular_radius < 0.0 or contact.angular_radius > 0.2: return false
		if not contact.get("spin_radians") is float or not is_finite(contact.spin_radians) or absf(contact.spin_radians) > PI: return false
		var tint: Variant = contact.get("tint")
		if not tint is Color or not is_finite(tint.r) or not is_finite(tint.g) or not is_finite(tint.b) or tint.r < 0 or tint.r > 1 or tint.g < 0 or tint.g > 1 or tint.b < 0 or tint.b > 1 or tint.a != 1.0: return false
	return true

static func selected_contact(state: Dictionary, id: String) -> Dictionary:
	if not valid(state) or id.is_empty(): return {}
	for contact: Dictionary in state.get("distant_contacts", {}).get("contacts", []):
		if contact.id == id: return contact
	return {}

static func apply(state: Dictionary, materials: Array[ShaderMaterial]) -> bool:
	# Validate the complete owner and every destination before the first write.
	if not valid(state): return false
	for material in materials:
		if material == null or material.shader != SkyShader: return false
	var directions := PackedVector3Array()
	var angular_radii := PackedFloat32Array()
	var lights := PackedVector3Array()
	var tints := PackedVector4Array()
	var spins := PackedFloat32Array()
	directions.resize(MAX_CONTACTS)
	angular_radii.resize(MAX_CONTACTS)
	lights.resize(MAX_CONTACTS)
	tints.resize(MAX_CONTACTS)
	spins.resize(MAX_CONTACTS)
	var count := 0
	for contact: Dictionary in state.get("distant_contacts", {}).get("contacts", []):
		if contact.status != "visible" or contact.angular_radius <= 0.0: continue
		directions[count] = contact.direction
		angular_radii[count] = contact.angular_radius
		lights[count] = contact.light_direction
		var tint: Color = contact.tint
		tints[count] = Vector4(tint.r, tint.g, tint.b, tint.a)
		spins[count] = contact.spin_radians
		count += 1
	for material in materials:
		material.set_shader_parameter("distant_contact_directions", directions)
		material.set_shader_parameter("distant_contact_angular_radii", angular_radii)
		material.set_shader_parameter("distant_contact_light_directions", lights)
		material.set_shader_parameter("distant_contact_tints", tints)
		material.set_shader_parameter("distant_contact_spins", spins)
		material.set_shader_parameter("distant_contact_count", count)
	return true
