extends SceneTree
## Calls the production helpers with actual Godot images and scene objects.
const Capture = preload("res://studies/captures/native_frame_import_capture.gd")
const SIZES = [Vector2i(800, 450), Vector2i(1920, 1080), Vector2i(3840, 2160)]
var failures := 0
var checks := 0


func check(value: bool, reason: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error(reason)


func _initialize() -> void:
	call_deferred("run")


func dimensions() -> void:
	for size in SIZES:
		check(Capture.valid_dimensions(size.x, size.y), "Supported dimensions refused: " + str(size))
		check(not Capture.valid_dimensions(size.y, size.x), "Transposed dimensions accepted: " + str(size))
		# GDScript integers are 64-bit; Vector2i components are only 32-bit.
		# These must fail before a narrowing conversion can alias a supported size.
		for shift in [-4294967296, 4294967296]:
			check(not Capture.valid_dimensions(size.x + shift, size.y), "Width wrapped into supported dimensions: " + str(size.x + shift))
			check(not Capture.valid_dimensions(size.x, size.y + shift), "Height wrapped into supported dimensions: " + str(size.y + shift))
	for size in [Vector2i.ZERO, Vector2i(0, 450), Vector2i(800, 0), Vector2i(-800, 450), Vector2i(800, -450), Vector2i(799, 450), Vector2i(800, 451), Vector2i(1280, 720)]:
		check(not Capture.valid_dimensions(size.x, size.y), "Invalid dimensions accepted: " + str(size))
	check(not Capture.valid_dimensions(9223372036854775807, 450), "Maximum signed integer accepted as width")


func images() -> void:
	check(not Capture.valid_image(null, SIZES[0]), "Null readback accepted")
	check(not Capture.valid_image(Image.new(), SIZES[0]), "Empty readback accepted")
	for size in SIZES:
		var image := Image.create(size.x, size.y, false, Image.FORMAT_RGBA8)
		for color in [Color.BLACK, Color.WHITE, Color.RED, Color.TRANSPARENT]:
			image.fill(color)
			check(not Capture.valid_image(image, size), "Uniform readback accepted: " + str(size) + " " + str(color))
		# Exercise all edge/midpoint sample positions, including the final row
		# and column. These are real Image buffers, not substituted readbacks.
		for y in [0, size.y >> 1, size.y - 1]:
			for x in [0, size.x >> 1, size.x - 1]:
				image.fill(Color.BLACK)
				image.set_pixel(x, y, Color.WHITE)
				check(Capture.valid_image(image, size), "Visible sample variation refused: " + str(Vector2i(x, y)))
		check(not Capture.valid_image(image, Vector2i(800, 451)), "Unsupported expected dimensions accepted")
		check(not Capture.valid_image(image, Vector2i(size.x + 1, size.y)), "Mismatched buffer dimensions accepted")
		for invalid in [Vector2i.ZERO, Vector2i(-800, 450), Vector2i(800, -450)]:
			check(not Capture.valid_image(image, invalid), "Nonpositive expected image dimensions accepted: " + str(invalid))
		# RGB variation at alpha zero cannot provide any visible evidence.
		image.fill(Color.TRANSPARENT)
		image.set_pixel(size.x - 1, size.y - 1, Color(1.0, 0.0, 0.0, 0.0))
		check(not Capture.valid_image(image, size), "Entirely transparent readback accepted because invisible RGB differs: " + str(size))
	var unsupported := Image.create(799, 450, false, Image.FORMAT_RGB8)
	unsupported.fill(Color.BLACK)
	unsupported.set_pixel(798, 449, Color.WHITE)
	check(not Capture.valid_image(unsupported, Vector2i(799, 450)), "Unsupported actual image dimensions accepted")


func camera_metadata() -> void:
	var viewport := SubViewport.new()
	viewport.size = SIZES[0]
	root.add_child(viewport)
	var pivot := Node3D.new()
	viewport.add_child(pivot)
	pivot.position = Vector3(10.0, 20.0, 30.0)
	pivot.rotation.y = PI / 2.0
	var camera := Camera3D.new()
	pivot.add_child(camera)
	camera.position = Vector3(2.0, 3.0, 4.0)
	camera.fov = 61.0
	camera.near = 0.125
	camera.far = 12345.0
	camera.keep_aspect = Camera3D.KEEP_WIDTH
	var record: Dictionary = Capture.camera_record(camera)
	var position: Array = record.get("position_metres", [])
	check(position.size() == 3 and Vector3(position[0], position[1], position[2]).is_equal_approx(Vector3(14.0, 23.0, 28.0)), "Camera record lost parent world transform")
	check(record.get("basis") == [Capture.coordinates(camera.global_basis.x), Capture.coordinates(camera.global_basis.y), Capture.coordinates(camera.global_basis.z)], "Camera record lost world basis")
	check(record.get("viewport") == [800.0, 450.0], "Camera record lost actual viewport size")
	check(record.get("fov_degrees") == camera.fov and record.get("keep_aspect") == Camera3D.KEEP_WIDTH and record.get("projection") == Camera3D.PROJECTION_PERSPECTIVE, "Camera record lost projection")
	check(record.get("near_metres") == camera.near and record.get("far_metres") == camera.far, "Camera record lost clipping planes")
	check(Capture.coordinates(Vector3(-2.0, 0.0, 3.5)) == [-2.0, 0.0, 3.5], "Coordinate order changed")
	viewport.free()


func audit_png(path: String, expected: Vector2i) -> Image:
	var image := Image.load_from_file(path)
	if image == null or image.is_empty():
		check(false, "PNG unavailable: " + path)
		return null
	check(image.get_size() == expected, "PNG dimensions differ from requested output: " + path)
	image.convert(Image.FORMAT_RGBA8)
	var pixels := image.get_data()
	var first := PackedInt32Array()
	var visible := false
	var varied := false
	for offset in range(0, pixels.size(), 4):
		var alpha: int = pixels[offset + 3]
		if alpha == 0:
			continue
		visible = true
		var color := PackedInt32Array([pixels[offset] * alpha, pixels[offset + 1] * alpha, pixels[offset + 2] * alpha, alpha])
		if first.is_empty():
			first = color
		elif color != first:
			varied = true
			break
	check(visible and varied, "PNG has no visible color variation: " + path)
	return image


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		dimensions()
		images()
		camera_metadata()
	elif args.size() == 4:
		var expected := Vector2i(args[2].to_int(), args[3].to_int())
		var first := audit_png(args[0], expected)
		var second := audit_png(args[1], expected)
		check(first != null and second != null and first.get_data() == second.get_data(), "Decoded import/canonical PNG pixel buffers differ")
	else:
		check(false, "Expected no arguments, or two PNGs plus width/height")
	await process_frame
	print("Native frame import capture helpers: %d checks, %d failures" % [checks, failures])
	quit(1 if failures else 0)
