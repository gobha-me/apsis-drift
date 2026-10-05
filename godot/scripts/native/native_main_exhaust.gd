extends Node3D
## Visual only; actual gross propulsion comes from C++, never key state.
const PlumeShader = preload("res://shaders/native_main_exhaust.gdshader")
const ApertureShader = preload("res://shaders/native_withdrawal_aperture.gdshader")
const MODEL_SHA256 = "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8"
# Measured installed ENGINE04 +/-1 aft mouth rims in source hopper-craft-09.
# Blender (x,y,z) -> Godot (x,z,-y); centre of outer aft face plus 30 mm.
const ANCHORS = [Vector3(-2.25, 1.23, 7.65), Vector3(2.25, 1.23, 7.65)]
# New assembled-craft exterior interfaces, distinct from internal lifeboat RCS.
# The exact source/chart/datum qualification is recorded in WITHDRAWAL_EXHAUST.md.
const WITHDRAWAL_ANCHORS = [Vector3(-1.309999942779541, 2.055159091949463, -1.4500000476837158), Vector3(1.309999942779541, 2.0589799880981445, -1.4500000476837158)]
# Outward float32 conversion of anchor Y + half the actual float32 mesh height.
const WITHDRAWAL_CENTRES = [Vector3(-1.309999942779541, 2.355159282684326, -1.4500000476837158), Vector3(1.309999942779541, 2.358980178833008, -1.4500000476837158)]
const WITHDRAWAL_RADIUS := 0.018
const WITHDRAWAL_LENGTH := 0.6
const SKIN_SURFACE := 18
var plumes: Array[MeshInstance3D] = []
var withdrawal_plumes: Array[MeshInstance3D] = []
var material: ShaderMaterial
var withdrawal_material: ShaderMaterial
var phase := 0.0
var intensity := 0.0
var withdrawal_intensity := 0.0
var bound_skin: MeshInstance3D
var bound_surface := -1
var bound_original: StandardMaterial3D
var bound_coating: StandardMaterial3D
var bound_aperture: ShaderMaterial
var bound_coating_state: Array = []


func _init() -> void:
	material = ShaderMaterial.new()
	material.shader = PlumeShader
	for anchor in ANCHORS:
		var plume := MeshInstance3D.new()
		var mesh := CylinderMesh.new()
		mesh.top_radius = 0.44
		mesh.bottom_radius = 0.10
		mesh.height = 5.0
		mesh.radial_segments = 24
		plume.mesh = mesh
		plume.material_override = material
		plume.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		plume.rotation.x = -PI / 2.0
		plume.position = anchor + Vector3(0.0, 0.0, 2.5)
		plume.visible = false
		add_child(plume)
		plumes.append(plume)
	withdrawal_material = ShaderMaterial.new()
	withdrawal_material.shader = PlumeShader
	for centre in WITHDRAWAL_CENTRES:
		var plume := MeshInstance3D.new()
		var mesh := CylinderMesh.new()
		mesh.top_radius = WITHDRAWAL_RADIUS
		mesh.bottom_radius = 0.002
		mesh.height = WITHDRAWAL_LENGTH
		mesh.radial_segments = 24
		plume.mesh = mesh
		plume.material_override = withdrawal_material
		plume.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		# Cylinder side UV0/top is the bright base. Flip it toward the skin.
		plume.basis = Basis(Vector3.RIGHT, Vector3.DOWN, Vector3.FORWARD)
		plume.position = centre
		plume.visible = false
		add_child(plume)
		withdrawal_plumes.append(plume)


func bind_skin(model: Node3D, selected_surface: int = SKIN_SURFACE) -> bool:
	if bound_skin != null: return false
	var skin := model.find_child("HopperStructure", true, false) as MeshInstance3D
	if skin == null or skin.mesh == null or skin.mesh.get_surface_count() <= selected_surface or selected_surface not in [14, 18]:
		return false
	var original := skin.mesh.surface_get_material(selected_surface) as StandardMaterial3D
	if original == null or original.resource_name != "WF08 | factory-new exterior atlas" or original.next_pass != null:
		return false
	var aperture := ShaderMaterial.new()
	aperture.shader = ApertureShader
	var coated_skin: StandardMaterial3D = original.duplicate()
	coated_skin.next_pass = aperture
	# Conformed zero-volume mark: reuse the original vertices and material,
	# with an extra pass only inside the two bound exterior chart disks.
	skin.set_surface_override_material(selected_surface, coated_skin)
	bound_skin = skin
	bound_surface = selected_surface
	bound_original = original
	bound_coating = coated_skin
	bound_aperture = aperture
	bound_coating_state = coating_state(coated_skin)
	return valid_bound_skin()


static func coating_state(coating: StandardMaterial3D) -> Array:
	return [coating.albedo_color, coating.albedo_texture, coating.normal_texture, coating.normal_scale, coating.metallic, coating.roughness, coating.cull_mode, coating.texture_filter]


func valid_bound_skin() -> bool:
	return is_instance_valid(bound_skin) and bound_skin.mesh != null and bound_skin.mesh.get_surface_count() > bound_surface and bound_skin.mesh.surface_get_material(bound_surface) == bound_original and bound_skin.get_surface_override_material(bound_surface) == bound_coating and bound_original.next_pass == null and bound_coating.next_pass == bound_aperture and bound_aperture.shader == ApertureShader and coating_state(bound_coating) == bound_coating_state and material.shader == PlumeShader and withdrawal_material.shader == PlumeShader


static func valid_applied(state: Dictionary, elapsed: float) -> bool:
	var valid := is_finite(elapsed) and elapsed >= 0.0 and elapsed <= 60.0
	valid = valid and state.get("mode") is String and state.get("frame_id") is String and state.get("attached") is bool
	if not valid:
		return false
	if state.mode != "freedom_flight" or state.frame_id != "2":
		return false
	for sign_name in ["positive", "negative"]:
		var force: Variant = state.get(sign_name + "_force_body")
		var ratings: Variant = state.get(sign_name + "_force_ratings")
		if not force is PackedFloat64Array or not ratings is PackedFloat64Array or force.size() != 3 or ratings.size() != 3:
			return false
		for axis in 3:
			if not is_finite(force[axis]) or force[axis] < 0.0 or not is_finite(ratings[axis]) or ratings[axis] <= 0.0:
				return false
	return true


func update_applied(state: Dictionary, elapsed: float, paused: bool) -> bool:
	intensity = 0.0
	withdrawal_intensity = 0.0
	var valid := valid_applied(state, elapsed)
	if valid and not paused and not state.attached:
		var force: PackedFloat64Array = state.negative_force_body
		var ratings: PackedFloat64Array = state.negative_force_ratings
		intensity = clampf(force[2] / ratings[2], 0.0, 1.0)
		withdrawal_intensity = clampf(force[1] / ratings[1], 0.0, 1.0)
		phase = fmod(phase + elapsed, 100.0)
	material.set_shader_parameter("phase", phase)
	material.set_shader_parameter("intensity", intensity)
	for plume in plumes:
		plume.visible = intensity > 0.0
	withdrawal_material.set_shader_parameter("phase", phase)
	withdrawal_material.set_shader_parameter("intensity", withdrawal_intensity)
	for plume in withdrawal_plumes:
		plume.visible = withdrawal_intensity > 0.0
	return valid
