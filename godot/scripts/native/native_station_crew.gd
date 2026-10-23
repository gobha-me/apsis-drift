extends Node3D
## Four unchanged idle views. C++ alone owns the station worker and eligibility.
const PNG_SHA := "007bc821141b58995d13834d45baef7996c61000ca61760a9f755ca47a577503"
const FRAMES_SHA := "78174bf54ac8ff4224557b90761593475c3dc22222e94f695b4fecfeb46d716e"
const NativeEnvironment = preload("res://scripts/native/native_environment.gd")
var error := ""
var plane: Node3D
var card: Sprite3D
var bubble: Label3D
var frames: Dictionary = {}
var last_view := ""

static func valid_packet(value: Variant, walking: Dictionary) -> bool:
	if not value is Dictionary or value.size() != 12 or value.get("version") != 2 or not value.get("version") is int or value.get("ordinal") != 1 or not value.get("ordinal") is int: return false
	for field in ["station_id", "tick"]:
		if not NativeEnvironment.decimal(value.get(field)) or value[field] != walking.get(field): return false
	for field in ["name", "reason", "remark"]:
		if not value.get(field) is String or value[field].is_empty() or value[field].length() > 512: return false
	if not value.get("available") is bool or not value.get("foot_position_metres") is PackedFloat64Array or value.foot_position_metres.size() != 3: return false
	for component in value.foot_position_metres:
		if not is_finite(component) or absf(component) > 1000.0: return false
	if not value.get("heading_radians") is float or not is_finite(value.heading_radians) or absf(value.heading_radians) > PI or value.get("height_metres") != 1.85 or not value.get("height_metres") is float: return false
	if not value.get("topics") is Array or not value.topics.size() in [3, 4]: return false
	var ids := {}
	for index in value.topics.size():
		var topic: Variant = value.topics[index]
		if not topic is Dictionary or topic.size() != 3 or not topic.get("id") is int or topic.id != index + 1 or ids.has(topic.id): return false
		if not topic.get("title") is String or topic.title.is_empty() or topic.title.length() > 96 or not topic.get("text") is String or topic.text.is_empty() or topic.text.length() > 2048: return false
		ids[topic.id] = true
	return true

static func valid_frames(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 4 or value.get("schema") != "apsis.origin-crew-idle/1" or value.get("height_metres") != 1.85 or value.get("reference_height_pixels") != 487 or not value.get("views") is Array or value.views.size() != 4: return false
	var names := ["front", "right", "rear", "left"]
	for index in 4:
		var view: Variant = value.views[index]
		if not view is Dictionary or view.size() != 3 or view.get("view") != names[index]: return false
		for field in ["rect", "anchor"]:
			if not view.get(field) is Array or view[field].size() != (4 if field == "rect" else 2): return false
			for component in view[field]:
				if not (component is int or component is float) or not is_finite(float(component)) or component != int(component): return false
		var r: Array = view.rect
		var a: Array = view.anchor
		if r[0] < 0 or r[1] < 0 or r[2] <= 0 or r[3] <= 0 or r[0] + r[2] > 1536 or r[1] + r[3] > 1024 or a[0] < 0 or a[1] < 0 or a[0] >= r[2] or a[1] >= r[3]: return false
	return true

func configure(assets: String) -> bool:
	if card != null or not assets.is_absolute_path(): return false
	var folder := assets.path_join("crew")
	for item in [["casual-male-idle.png", PNG_SHA, 2 * 1024 * 1024], ["idle-frames.json", FRAMES_SHA, 4096]]:
		var path := folder.path_join(item[0])
		var file := FileAccess.open(path, FileAccess.READ)
		if file == null:
			error = "Prepare the selected Origin crew assets."
			return false
		var length := file.get_length()
		file.close()
		if length <= 0 or length > item[2] or FileAccess.get_sha256(path) != item[1]:
			error = "Origin crew asset bytes changed."
			return false
	var layout: Variant = JSON.parse_string(FileAccess.get_file_as_string(folder.path_join("idle-frames.json")))
	if not valid_frames(layout):
		error = "Origin crew idle registration is unavailable."
		return false
	var image := Image.load_from_file(folder.path_join("casual-male-idle.png"))
	if image == null or image.get_size() != Vector2i(1536, 1024) or FileAccess.get_sha256(folder.path_join("casual-male-idle.png")) != PNG_SHA:
		error = "Origin crew image is unavailable."
		return false
	for entry in layout.views: frames[entry.view] = entry
	plane = Node3D.new()
	add_child(plane)
	card = Sprite3D.new()
	card.texture = ImageTexture.create_from_image(image)
	card.texture_filter = BaseMaterial3D.TEXTURE_FILTER_NEAREST
	card.pixel_size = 1.85 / 487.0
	card.region_enabled = true
	card.shaded = true
	card.no_depth_test = false
	card.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	card.alpha_cut = SpriteBase3D.ALPHA_CUT_DISCARD
	card.alpha_scissor_threshold = 0.5
	card.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	plane.add_child(card)
	bubble = Label3D.new()
	bubble.position.y = 2.1
	bubble.font_size = 24
	bubble.pixel_size = 0.0025
	bubble.width = 400
	bubble.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bubble.no_depth_test = false
	bubble.outline_size = 8
	plane.add_child(bubble)
	bubble.hide()
	hide()
	return true

func present(packet: Dictionary, camera_station: Vector3) -> bool:
	if card == null or not camera_station.is_finite(): return false
	var foot: PackedFloat64Array = packet.foot_position_metres
	var delta := camera_station - Vector3(foot[0], foot[1], foot[2])
	if Vector2(delta.x, delta.z).length_squared() < 0.000001: return false
	var h: float = packet.heading_radians
	# C++ heading0 faces -Z. Select genuine front/rear/left/right art; never
	# mirror the sheet or apply a world-Y billboard to the tilted station.
	var front := delta.dot(Vector3(-sin(h), 0, -cos(h)))
	var right := delta.dot(Vector3(cos(h), 0, -sin(h)))
	last_view = ("front" if front >= 0 else "rear") if absf(front) >= absf(right) else ("right" if right >= 0 else "left")
	var entry: Dictionary = frames[last_view]
	var rect: Array = entry.rect
	var anchor: Array = entry.anchor
	card.region_rect = Rect2(rect[0], rect[1], rect[2], rect[3])
	card.position = Vector3((float(rect[2]) * 0.5 - float(anchor[0])) * card.pixel_size, (float(anchor[1]) - float(rect[3]) * 0.5) * card.pixel_size, 0)
	position = Vector3(foot[0], foot[1], foot[2])
	plane.rotation.y = atan2(delta.x, delta.z)
	show()
	return true
