extends Node3D
## Runtime displays sit inside existing bezels; source GLBs remain unchanged.
var screens: Array[Control] = []
var elapsed := 1.0

func _ready() -> void:
	for i in 3:
		var viewport := SubViewport.new()
		viewport.size = Vector2i(896, 560)
		viewport.disable_3d = true
		viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
		add_child(viewport)
		var screen := preload("res://instrument_screen.gd").new()
		screen.page = i
		screen.size = Vector2(896, 560)
		viewport.add_child(screen)
		screens.append(screen)
		var panel := MeshInstance3D.new()
		var quad := QuadMesh.new()
		quad.size = Vector2(0.465, 0.335)
		panel.mesh = quad
		# Flight-cell revision 5 (build_flight_cell.py), ahead of baked lettering.
		panel.position = Vector3((i - 1) * 0.57, 0.835, -0.531)
		panel.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_texture = viewport.get_texture()
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR
		panel.material_override = material
		add_child(panel)

func refresh(state: Dictionary, delta: float) -> void:
	elapsed += delta
	if elapsed < 0.1:
		return
	elapsed = 0
	for screen in screens:
		screen.state = state
		screen.queue_redraw()
		(screen.get_parent() as SubViewport).render_target_update_mode = SubViewport.UPDATE_ONCE
