extends Node3D
## Visual only; actual gross main propulsion comes from C++, never key state.
const PlumeShader = preload("res://native_main_exhaust.gdshader")
const MODEL_SHA256 = "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8"
# Measured installed ENGINE04 +/-1 aft mouth rims in source hopper-craft-09.
# Blender (x,y,z) -> Godot (x,z,-y); centre of outer aft face plus 30 mm.
const ANCHORS = [Vector3(-2.25, 1.23, 7.65), Vector3(2.25, 1.23, 7.65)]
var plumes: Array[MeshInstance3D] = []
var material: ShaderMaterial
var phase := 0.0
var intensity := 0.0


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


func update_applied(state: Dictionary, elapsed: float, paused: bool) -> bool:
	intensity = 0.0
	var force: Variant = state.get("negative_force_body")
	var ratings: Variant = state.get("negative_force_ratings")
	var valid := is_finite(elapsed) and elapsed >= 0.0 and elapsed <= 60.0
	valid = valid and force is PackedFloat64Array and ratings is PackedFloat64Array
	if valid:
		valid = force.size() == 3 and ratings.size() == 3
	if valid:
		for axis in 3:
			valid = valid and is_finite(force[axis]) and force[axis] >= 0.0 and is_finite(ratings[axis]) and ratings[axis] > 0.0
	if valid and not paused:
		intensity = clampf(force[2] / ratings[2], 0.0, 1.0)
		phase = fmod(phase + elapsed, 100.0)
	material.set_shader_parameter("phase", phase)
	material.set_shader_parameter("intensity", intensity)
	for plume in plumes:
		plume.visible = intensity > 0.0
	return valid
