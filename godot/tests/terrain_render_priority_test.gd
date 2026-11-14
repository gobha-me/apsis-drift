extends SceneTree
## Properties of the real key parser/priority function, independent of its formula.
const StreamView = preload("res://scripts/world/planet_stream.gd")
var checks := 0
var failures := 0


func check(value: bool, reason: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error(reason)


func _initialize() -> void:
	call_deferred("run")


func priority(face: int, lod: int, x: int, y: int) -> int:
	return StreamView.tile_render_priority("%d:%d:%d:%d" % [face, lod, x, y])


func legal_keys() -> void:
	var bands := {}
	var recorded := {}
	for face in 6:
		for lod in 14:
			var edge := (1 << lod) - 1
			var coordinates := [0] if lod == 0 else [0, 1, maxi(0, edge - 1), edge]
			var local_bands := {}
			for x: int in coordinates:
				for y: int in coordinates:
					var key := "%d:%d:%d:%d" % [face, lod, x, y]
					var value := StreamView.tile_render_priority(key)
					recorded[key] = value
					bands[value] = true
					local_bands[value] = true
					check(value >= -128 and value <= 39, "Legal key escaped reserved material range: " + key)
					check(value >= Material.RENDER_PRIORITY_MIN and value <= Material.RENDER_PRIORITY_MAX, "Legal key exceeds engine material limits: " + key)
					# Opposite faces cannot touch; every other face shares a seam.
					check(value == priority(face ^ 1, lod, x, y), "Opposite face lost its shared band: " + key)
					for other in 6:
						if other != face and other != (face ^ 1):
							for seam_x: int in [0, edge]:
								for seam_y: int in [0, edge]:
									check(value != priority(other, lod, seam_x, seam_y), "Touching faces share a priority: " + key)
					if x < edge:
						check(value != priority(face, lod, x + 1, y), "Horizontal neighbors tie: " + key)
					if y < edge:
						check(value != priority(face, lod, x, y + 1), "Vertical neighbors tie: " + key)
					if x < edge and y < edge:
						check(value != priority(face, lod, x + 1, y + 1), "Corner neighbors tie: " + key)
					if x + 2 <= edge:
						check(value == priority(face, lod, x + 2, y), "Even X translation changed parity band: " + key)
					if y + 2 <= edge:
						check(value == priority(face, lod, x, y + 2), "Even Y translation changed parity band: " + key)
					# All lower levels, including multi-level ancestors, must be
					# separate even when the two coordinate parities differ.
					for coarse in lod:
						var limit := mini(2, 1 << coarse)
						for cx in limit:
							for cy in limit:
								check(value != priority(face, coarse, cx, cy), "Cross-LOD priorities collide: " + key)
			check(local_bands.size() == (1 if lod == 0 else 4), "Coordinate parity collapsed at face/LOD %d/%d" % [face, lod])
	check(bands.size() == 159, "Legal representatives must reach exactly 159 distinct bands")
	check(priority(0, 0, 0, 0) == -128, "First legal band changed")
	check(priority(5, 13, 8191, 8191) == 39, "Inclusive LOD13/8191 boundary lost its final band")
	# Reversed call order must have no allocation/history-dependent state.
	var keys := recorded.keys()
	keys.reverse()
	for key in keys:
		check(StreamView.tile_render_priority(key) == recorded[key], "Priority depends on call order: " + key)


func invalid_keys() -> void:
	for value in [null, false, true, 0, -128, 39, 1.0, NAN, INF, -INF,
		[], {}, PackedStringArray(["0", "0", "0", "0"]), PackedInt32Array([0, 0, 0, 0]),
		Vector4i.ZERO, &"0:0:0:0", "", "incoming", "retained", "surface",
		"0", "0:0:0", "0:0:0:0:0", ":0:0:0", "0:0:0:",
		"6:0:0:0", "5:14:0:0", "0:0:1:0", "0:0:0:1",
		"5:13:8192:8191", "5:13:8191:8192", "5:12:4096:0",
		"5:13:9999:0", "0:9999:0:0", "0:0:0:0\n", "0:0:0:0�"]:
		check(StreamView.tile_render_priority(value) == 0, "Malformed/nonkey value did not use legacy default: " + str(value))
	# Exercise every field rather than only the face or final coordinate parser.
	for field in 4:
		for token in ["", "-1", "+1", "00", "01", "1.0", "1e0", "nan", "NaN", "inf", "-inf",
			" 0", "0 ", "0\t", "0\n", "０", "١", "0x0", "10000", "4294967296",
			"9223372036854775808", "18446744073709551616", "9".repeat(1024)]:
			var parts := ["0", "1", "0", "0"]
			parts[field] = token
			check(StreamView.tile_render_priority(":".join(parts)) == 0, "Malformed field accepted: %d/%s" % [field, token])


func run() -> void:
	invalid_keys()
	legal_keys()
	print("Terrain render priority properties: %d checks, %d failures" % [checks, failures])
	quit(1 if failures else 0)
