extends Node3D
## Physical station presentation from C++ geometry and verified starter bytes.
const STATION_HASH = "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80"
var camera: Camera3D
var port_markers: Array[Node3D] = []
var geometry: Dictionary = {}
var selected: Dictionary = {}
var error := ""
var distance := 100.0
var yaw := 0.65
var pitch := 0.3


static func finite_vector(value: Variant) -> bool:
	if not value is PackedFloat64Array or value.size() != 3:
		return false
	for component in value:
		if not is_finite(component) or abs(component) > 1000.0:
			return false
	return true


static func vector(value: PackedFloat64Array) -> Vector3:
	return Vector3(value[0], value[1], value[2])


static func valid_geometry(value: Variant, station_id: String) -> bool:
	if not value is Dictionary or not value.get("version") is int or value.get("version") != 1 or value.get("station_id") != station_id:
		return false
	for key in ["asset_offset_metres", "bounds_minimum_metres", "bounds_maximum_metres"]:
		if not finite_vector(value.get(key)):
			return false
	for axis in 3:
		if value.bounds_maximum_metres[axis] <= value.bounds_minimum_metres[axis]:
			return false
	var ports: Variant = value.get("ports")
	if not ports is Array or ports.size() != 2:
		return false
	var ordinals := {}
	for port in ports:
		if not port is Dictionary or port.get("station_id") != station_id:
			return false
		var ordinal: Variant = port.get("ordinal")
		if not ordinal is int or ordinal != ordinals.size() + 1 or ordinals.has(ordinal):
			return false
		ordinals[ordinal] = true
		if not finite_vector(port.get("position_metres")) or not finite_vector(port.get("outward_normal")):
			return false
		if abs(vector(port.outward_normal).length_squared() - 1.0) > 0.000001:
			return false
		for key in ["withdrawal_metres", "bore_metres"]:
			var dimension: Variant = port.get(key)
			if not dimension is float or not is_finite(dimension) or dimension <= 0.0 or dimension > 100.0:
				return false
	return true


static func valid_walk_access(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4: return false
	for key in ["minimum_x", "maximum_x", "minimum_z", "maximum_z"]:
		var coordinate: Variant = value.get(key)
		if not (coordinate is int or coordinate is float) or not is_finite(float(coordinate)) or absf(coordinate) > 1000.0: return false
	return value.minimum_x < value.maximum_x and value.minimum_z < value.maximum_z


func initialize(start: Dictionary, contract: Dictionary, assets: String, inspection_camera: bool = true) -> bool:
	if start.has("walk_access") and not valid_walk_access(start.walk_access):
		error = "Station access limits are unavailable or invalid"
		return false
	if not valid_geometry(contract, str(start.get("station_id", ""))):
		error = "C++ station geometry is unavailable or invalid"
		return false
	var path := assets.path_join("station-reference.glb")
	if not assets.is_absolute_path() or FileAccess.get_sha256(path) != STATION_HASH:
		error = "Prepare the selected native starter assets; station bytes are missing or changed"
		return false
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		error = "The verified station export could not be imported"
		return false
	for mesh in state.get_meshes():
		mesh.get_mesh().generate_lods(60.0, 25.0, [])
	var model: Node3D = document.generate_scene(state)
	if model == null:
		error = "The station export produced no native scene"
		return false
	selected = start.duplicate(true)
	geometry = contract.duplicate(true)
	model.name = "OriginStationAsset"
	model.position = vector(contract.asset_offset_metres)
	add_child(model)
	if start.has("walk_access"): install_walk_access(start.walk_access)
	for port in contract.ports:
		var marker := Node3D.new()
		marker.name = "OriginPortD%d" % port.ordinal
		marker.position = vector(port.position_metres)
		add_child(marker)
		port_markers.append(marker)
		var ring := MeshInstance3D.new()
		var mesh := TorusMesh.new()
		mesh.inner_radius = port.bore_metres * 0.5
		mesh.outer_radius = mesh.inner_radius + 0.05
		mesh.rings = 24
		mesh.ring_segments = 8
		ring.mesh = mesh
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = Color(1.0, 0.66, 0.2) if port.ordinal == 1 else Color(0.35, 0.7, 1.0)
		ring.material_override = material
		marker.add_child(ring)
		var label := Label3D.new()
		label.text = "D%d" % port.ordinal
		label.position.y = -0.8
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.font_size = 32
		label.pixel_size = 0.015
		marker.add_child(label)
	if not inspection_camera:
		return true
	camera = Camera3D.new()
	camera.name = "StationInspectionCamera"
	camera.near = 0.05
	camera.far = 2000.0
	camera.fov = 75.0
	add_child(camera)
	camera.current = true
	update_camera()
	return true


func install_walk_access(access: Dictionary) -> void:
	# A visible guard for the existing C++ crop; this never supplies collision.
	var guides := Node3D.new()
	guides.name = "WalkAccess"
	add_child(guides)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(1.0, 0.64, 0.12)
	var width: float = access.maximum_z - access.minimum_z
	var junction_length := minf(1.5, access.maximum_x - access.minimum_x)
	var x: float = access.maximum_x - junction_length * 0.5
	var z: float = (access.minimum_z + access.maximum_z) * 0.5
	var limits := [
		[Vector3(access.maximum_x, 0.0, z), Vector3(0.035, 0.07, width), "ARRIVALS\nAccess closed in this build"],
		[Vector3(x, 0.0, access.minimum_z), Vector3(junction_length, 0.07, 0.035), "ACCESS CLOSED\nFollow D1 to the workshop"],
		[Vector3(x, 0.0, access.maximum_z), Vector3(junction_length, 0.07, 0.035), "ACCESS CLOSED\nFollow D1 to the workshop"]]
	for limit in limits:
		var gate := Node3D.new()
		gate.position = limit[0]
		guides.add_child(gate)
		for height in [0.55, 1.05]:
			var bar := MeshInstance3D.new()
			var box := BoxMesh.new()
			box.size = limit[1]
			bar.mesh = box
			bar.material_override = material
			bar.position.y = height
			gate.add_child(bar)
		var label := Label3D.new()
		label.text = limit[2]
		# The arrival guard is close to the spawn: keep both lines at eye level
		# rather than below the camera's vertical field of view.
		label.position.y = 1.7
		label.pixel_size = 0.0015
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.font_size = 32
		gate.add_child(label)


func update_camera() -> void:
	if camera == null:
		return
	camera.position = Vector3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)) * distance
	camera.basis = Basis.looking_at(Vector3(0, 1, 10) - camera.position, Vector3.UP)


func inspect(range_metres: float, yaw_radians: float, pitch_radians: float) -> bool:
	if not is_finite(range_metres) or range_metres < 2.0 or range_metres > 1000.0 or not is_finite(yaw_radians) or not is_finite(pitch_radians) or abs(yaw_radians) > 1000000.0 or abs(pitch_radians) > 1.4:
		return false
	distance = range_metres
	yaw = yaw_radians
	pitch = pitch_radians
	update_camera()
	return true


static func valid_dimensions(value: Vector2i) -> bool:
	return value.x >= 64 and value.y >= 64 and value.x <= 4096 and value.y <= 4096


func port_cue(ordinal: int) -> Dictionary:
	if camera == null or ordinal < 1 or ordinal > port_markers.size():
		return {}
	var marker := port_markers[ordinal - 1]
	var behind := camera.is_position_behind(marker.global_position)
	var pixel := camera.unproject_position(marker.global_position) if not behind else Vector2.ZERO
	var viewport := camera.get_viewport().get_visible_rect()
	return {"label": "D%d" % ordinal, "station_id": selected.station_id, "range_metres": camera.global_position.distance_to(marker.global_position),
		"behind": behind, "offscreen": behind or not viewport.has_point(pixel), "pixel": pixel, "visibility": "projection_only", "status": "Inspection"}


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_RIGHT):
		inspect(distance, yaw - event.relative.x * 0.005, clampf(pitch + event.relative.y * 0.005, -1.4, 1.4))
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			inspect(clampf(distance * 0.85, 2, 1000), yaw, pitch)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			inspect(clampf(distance / 0.85, 2, 1000), yaw, pitch)
