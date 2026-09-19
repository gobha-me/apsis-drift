extends Node
## Two depth ranges, one C++ world: distant terrain/sky behind near ship/cabin.
var viewport: SubViewport
var camera: Camera3D
var near_environment: Environment
var near_viewport: SubViewport
var near_camera: Camera3D

func install(study: Node3D) -> void:
	viewport = SubViewport.new()
	viewport.size = Vector2i(study.get_viewport().get_visible_rect().size)
	viewport.world_3d = study.get_world_3d()
	viewport.msaa_3d = Viewport.MSAA_4X
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(viewport)
	camera = Camera3D.new()
	camera.cull_mask = 2
	camera.environment = study.scene_environment
	viewport.add_child(camera)
	camera.make_current()
	var canvas := CanvasLayer.new()
	canvas.layer = -1
	add_child(canvas)
	var background := TextureRect.new()
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	background.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	background.mouse_filter = Control.MOUSE_FILTER_IGNORE
	background.texture = viewport.get_texture()
	canvas.add_child(background)
	near_environment = study.scene_environment.duplicate()
	near_environment.background_mode = Environment.BG_COLOR
	near_environment.background_color = Color(0, 0, 0, 0)
	near_environment.fog_enabled = false
	near_viewport = SubViewport.new()
	near_viewport.size = viewport.size
	near_viewport.world_3d = study.get_world_3d()
	near_viewport.transparent_bg = true
	near_viewport.msaa_3d = Viewport.MSAA_4X
	near_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(near_viewport)
	near_camera = Camera3D.new()
	near_camera.cull_mask = 1
	near_camera.environment = near_environment
	near_camera.near = 0.10
	near_camera.far = 300
	near_viewport.add_child(near_camera)
	near_camera.make_current()
	var foreground := TextureRect.new()
	foreground.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	foreground.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	foreground.mouse_filter = Control.MOUSE_FILTER_IGNORE
	foreground.texture = near_viewport.get_texture()
	canvas.add_child(foreground)
	# Composite explicitly; BG_CANVAS camera overrides are renderer-dependent.
	study.get_viewport().disable_3d = true
	study.camera.cull_mask = 1
	study.camera.near = 0.10
	study.camera.far = 300
	if study.planet_stream != null:
		study.planet_stream.render_layer = 2

func refresh(study: Node3D, altitude: float, radius: float) -> void:
	var size := Vector2i(study.get_viewport().get_visible_rect().size)
	if size != viewport.size:
		viewport.size = size
		near_viewport.size = size
	camera.transform = study.camera.transform
	camera.fov = study.camera.fov
	near_camera.transform = study.camera.transform
	near_camera.fov = study.camera.fov
	camera.far = maxf(200000, maxf(0, altitude) * 8 + 2 * sqrt(maxf(0, 2 * radius * altitude)))
	# Bound far/near ratio to avoid float frustum degeneration in light culling.
	camera.near = maxf(0.75, camera.far / 100000)
	study.camera.near = 0.10
	study.camera.far = 300
	near_environment.ambient_light_energy = study.scene_environment.ambient_light_energy
