extends Node3D
## One provisional gear control on the authenticated current SYSTEMS console.
## No authoring asset, physical input, simulation or hand animation ownership.
const Displays = preload("res://scripts/native/native_cockpit_displays.gd")
const EXTENT := Vector2(0.16, 0.065)
const MAX_REACH_METRES := 2.0
var face: MeshInstance3D
var label: Label3D
var aimed := false
var bound := false

func bind_screens(screens: Variant) -> bool:
	if bound or not Displays.valid_screens(screens): return false
	var screen: Dictionary = screens[2]
	var columns: Array = screen.transform
	transform = Transform3D(Basis(Vector3(columns[0][0], columns[0][1], columns[0][2]), Vector3(columns[1][0], columns[1][1], columns[1][2]), Vector3(columns[2][0], columns[2][1], columns[2][2])), Vector3(columns[3][0], columns[3][1], columns[3][2]))
	# A labelled patch on the lower screen bezel, not an invented old-layout
	# switch pivot. Retain the immutable current ship and all display datums.
	position += basis * Vector3(0, -float(screen["size"][1]) * 0.5 - EXTENT.y * 0.5, 0.006)
	face = MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = EXTENT
	face.mesh = quad
	face.layers = 2
	face.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var ink := StandardMaterial3D.new()
	ink.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ink.albedo_color = Color("14242d")
	face.material_override = ink
	add_child(face)
	label = Label3D.new()
	label.pixel_size = 0.0007
	label.font_size = 28
	label.outline_size = 4
	label.position.z = 0.001
	label.layers = 2
	label.no_depth_test = false
	add_child(label)
	bound = true
	return true

func ray_hits(eye: Vector3, direction: Vector3) -> bool:
	if not bound or not eye.is_finite() or not direction.is_finite() or not global_transform.is_finite() or absf(direction.length_squared() - 1.0) > 0.001: return false
	var local_eye := global_transform.affine_inverse() * eye
	var local_ray := global_basis.inverse() * direction
	if local_eye.z <= 0 or local_ray.z >= -0.00001: return false
	var distance := -local_eye.z / local_ray.z
	if distance < 0 or distance > MAX_REACH_METRES: return false
	var point := local_eye + local_ray * distance
	return absf(point.x) <= EXTENT.x * 0.5 and absf(point.y) <= EXTENT.y * 0.5

func present(deployed: bool, shown: bool, enabled: bool, camera: Camera3D) -> void:
	visible = shown
	aimed = shown and enabled and is_instance_valid(camera) and ray_hits(camera.global_position, -camera.global_basis.z)
	if not bound: return
	label.text = "GEAR DOWN" if deployed else "GEAR UP"
	label.modulate = Color("b7ffcb") if aimed else Color("c4d6de")
	face.material_override.albedo_color = Color("235641") if aimed else Color("14242d")

func clear() -> void:
	aimed = false
	visible = false
