extends SceneTree
## Editorial source-workshop composition. C++ world/player state stays unchanged.
var Pilot: Variant
const FIT_HASH := "f2a724f53b973b3324c1df3e447909a039c4ace7cde90f5749f3f7bfabd521e5"
const DESCRIPTOR_HASHES := {"station-workshop":"52d62249594473aca4ab5c431a7e34d96c34416c7ac96ba5c45c87f9a53e80d9","station-service-instrument-fit":"479c115f672e0ceac4b062919b522eefb657c2427103a2bb162e966afe9d64a4"}
const WORKSHOP_HASH := "e8633bbb8381a3359174175ab63c9f38e051fb3782c946395d75c2891c932f47"
const FPS := 24
const SECONDS := 60
var owner: Variant
var room: Node3D
var frame_root: Node3D
var pilot: Node3D
var camera: Camera3D
var tool: Node3D
var output := ""
var base := ""
var review := false
var work_grip := Vector3.ZERO
var foot := Vector3.ZERO
var target := Vector3.ZERO
var source_hashes := {}
var samples: Array = []
var mesh_nodes: Array[MeshInstance3D] = []
var failures: Array[String] = []

func _initialize() -> void:
	call_deferred("run")

func require_ok(value: bool, message: String) -> bool:
	if value:
		return true
	push_error(message)
	failures.append(message)
	quit(1)
	return false

func vec(value: Vector3) -> Array:
	return [value.x, value.y, value.z]

func collect_meshes(node: Node) -> void:
	if node is MeshInstance3D and node.mesh != null:
		mesh_nodes.append(node)
	for child in node.get_children():
		collect_meshes(child)

# Actual imported triangle ray, in normalized source-workshop Godot metres.
func hit_mesh(node: MeshInstance3D, origin: Vector3, direction: Vector3) -> Dictionary:
	var result := {}
	var local_to_room := room.global_transform.affine_inverse() * node.global_transform
	var inverse := local_to_room.affine_inverse()
	var ray_origin := inverse * origin
	var ray_direction := inverse.basis * direction
	var nearest := INF
	for surface in node.mesh.get_surface_count():
		var arrays: Array = node.mesh.surface_get_arrays(surface)
		var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var count := indices.size() if not indices.is_empty() else vertices.size()
		if count % 3 != 0:
			return {"error": "Malformed triangle buffer"}
		for i in range(0, count, 3):
			var corners: Array[Vector3] = []
			for j in 3:
				var index := indices[i + j] if not indices.is_empty() else i + j
				if index < 0 or index >= vertices.size() or not vertices[index].is_finite():
					return {"error": "Malformed vertex/index"}
				corners.append(vertices[index])
			var contact: Variant = Geometry3D.ray_intersects_triangle(ray_origin, ray_direction, corners[0], corners[1], corners[2])
			if contact is Vector3:
				var point: Vector3 = local_to_room * contact
				var distance := (point - origin).dot(direction)
				if distance >= 0 and distance < nearest:
					nearest = distance
					result = {"point": point, "distance": distance, "node": str(node.name), "surface": surface, "triangle": i / 3}
	return result

func screen_hit(height: float, lateral: float) -> Dictionary:
	for node in mesh_nodes:
		if str(node.name).contains("bench device") and str(node.name).contains("replaceable screen"):
			return hit_mesh(node, Vector3(0.3, height, lateral), Vector3.LEFT)
	return {}

func floor_hit(position: Vector3) -> Dictionary:
	var result := {}
	var nearest := INF
	for node in mesh_nodes:
		if not str(node.name).contains("deck"):
			continue
		var hit := hit_mesh(node, position + Vector3.UP * 0.4, Vector3.DOWN)
		if hit.has("error"):
			return hit
		if not hit.is_empty() and hit.distance < nearest:
			nearest = hit.distance
			result = hit
	return result

func box(parent: Node3D, at: Vector3, size: Vector3, color: Color) -> void:
	var item := MeshInstance3D.new()
	var shape := BoxMesh.new()
	shape.size = size
	item.mesh = shape
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	item.material_override = material
	item.position = at
	parent.add_child(item)

func set_pose(seconds: float) -> bool:
	if not require_ok(pilot.present(4, "right"), "Male authored held service pose refused"):
		return false
	# This new source has a held service key, not an animated work cycle.
	tool.position = work_grip
	tool.rotation = Vector3.ZERO
	var local_seconds := fmod(seconds, 20.0)
	var t := local_seconds / 20.0
	if seconds < 20:
		camera.position = Vector3(0.72, 1.70, 3.0).lerp(Vector3(0.46, 1.56, 2.65), smoothstep(0, 1, t))
		camera.look_at(room.global_transform * Vector3(-0.43, 1.12, 1.1), room.global_basis.y)
		camera.fov = 61.0
	elif seconds < 40:
		camera.position = Vector3(0.31, 1.40, 2.1).lerp(Vector3(0.15, 1.37, 1.98), smoothstep(0, 1, t))
		camera.look_at(room.global_transform * Vector3(-0.69, 1.13, 1.1), room.global_basis.y)
		camera.fov = 51.0
	else:
		camera.position = Vector3(-0.16, 1.33, 1.73).lerp(Vector3(-0.27, 1.29, 1.58), smoothstep(0, 1, t))
		camera.look_at(room.global_transform * target, room.global_basis.y)
		camera.fov = 44.0
	return true

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if not require_ok(args.size() in [2, 3] and (args.size() == 2 or args[2] == "review"), "Expected source-art-root empty-output [review]"):
		return
	base = args[0]
	output = args[1]
	review = args.size() == 3
	if not require_ok(base.is_absolute_path() and output.is_absolute_path() and DirAccess.dir_exists_absolute(base) and DirAccess.dir_exists_absolute(output) and DirAccess.get_files_at(output).is_empty() and DirAccess.get_directories_at(output).is_empty() and DisplayServer.get_name() != "headless", "Require existing absolute source/empty output and graphical display"):
		return
	for stem in ["station-workshop", "station-service-instrument-fit"]:
		var descriptor: String = "assets/visual/" + stem + ".json"
		var geometry: String = "assets/visual/" + stem + ".glb"
		var metadata: Variant = JSON.parse_string(FileAccess.get_file_as_string(base.path_join(descriptor)))
		var expected := WORKSHOP_HASH if stem == "station-workshop" else FIT_HASH
		if not require_ok(metadata is Dictionary and FileAccess.get_sha256(base.path_join(descriptor)) == DESCRIPTOR_HASHES[stem] and metadata.get("glb_sha256") == expected and FileAccess.get_sha256(base.path_join(geometry)) == expected, "Source descriptor/GLB changed: " + stem):
			return
		source_hashes[descriptor] = FileAccess.get_sha256(base.path_join(descriptor))
		source_hashes[geometry] = expected
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not require_ok(ClassDB.class_exists("FreedomBridge"), "Matching C++ bridge not staged"):
		return
	if not require_ok(ResourceLoader.exists("res://trials/casual-actions-01/pilot.gd"), "Stage the source-bound Hero runtime closure for this optional film"):
		return
	Pilot = load("res://trials/casual-actions-01/pilot.gd")
	owner = ClassDB.instantiate("FreedomBridge")
	if not require_ok(owner.initialize_freedom_new_game("42"), "C++ New Game refused"):
		return
	var initial_actor: Dictionary = owner.get_freedom_walk_state()
	var initial_flight: Dictionary = owner.get_freedom_flight_state()
	if not require_ok(not initial_actor.is_empty() and initial_actor.tick == "0", "C++ registered station actor missing"):
		return
	root.size = Vector2i(1920, 1080)
	frame_root = Node3D.new()
	root.add_child(frame_root)
	frame_root.transform = Transform3D(initial_actor.station_basis, initial_actor.station_position)
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if not require_ok(document.append_from_file(base.path_join("assets/visual/station-service-instrument-fit.glb"), state) == OK, "Actual fit GLB import refused"):
		return
	room = document.generate_scene(state, 24.0)
	if not require_ok(room != null, "Actual fit GLB scene missing"):
		return
	frame_root.add_child(room)
	# Exact descriptor inverse plus source canonical x,z,-y and offset.
	var descriptor: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(base.path_join("assets/visual/station-workshop.json")))
	var m: Array = descriptor.source_to_local_matrix
	var normalized := Transform3D(Basis(Vector3(m[0][0],m[1][0],m[2][0]),Vector3(m[0][1],m[1][1],m[2][1]),Vector3(m[0][2],m[1][2],m[2][2])),Vector3(m[0][3],m[1][3],m[2][3]))
	var axes := Basis(Vector3.RIGHT,Vector3(0,0,-1),Vector3.UP)
	room.transform = Transform3D(Basis.IDENTITY,Vector3(-0.97,0,0.978)) * Transform3D(axes,Vector3.ZERO) * normalized.affine_inverse() * Transform3D(axes.inverse(),Vector3.ZERO)
	collect_meshes(room)
	pilot = Pilot.new()
	room.add_child(pilot)
	if not require_ok(pilot.configure("male") and pilot.present(4, "right"), "Male casual held service pose unavailable"):
		return
	work_grip = pilot.anchor_local("grip")
	if not require_ok(work_grip.is_finite(), "Male painted grip landmark unavailable"):
		return
	var contact := screen_hit(work_grip.y, 1.1)
	if not require_ok(not contact.is_empty() and not contact.has("error"), "Actual screen ray missed at authored grip height"):
		return
	target = contact.point
	foot = Vector3(target.x + work_grip.x + 0.23, 0, target.z + work_grip.z)
	var support := floor_hit(foot)
	if not require_ok(not support.is_empty() and not support.has("error") and absf(support.point.y) <= 0.05, "Exact source deck support unavailable"):
		return
	foot.y = support.point.y
	contact = screen_hit(foot.y + work_grip.y, target.z)
	if not require_ok(not contact.is_empty() and not contact.has("error"), "Floor-adjusted screen ray missed"):
		return
	target = contact.point
	foot.x = target.x + work_grip.x + 0.23
	var floor_probes: Array = []
	for offset in [Vector3.ZERO,Vector3(0.28,0,0),Vector3(-0.28,0,0),Vector3(0,0,0.28),Vector3(0,0,-0.28)]:
		var probe := floor_hit(foot + offset)
		if not require_ok(not probe.is_empty() and not probe.has("error") and absf(probe.point.y-foot.y)<0.001, "Bounded source floor probe missed"):
			return
		floor_probes.append({"offset":vec(offset),"point":vec(probe.point),"node":probe.node})
	if not require_ok(screen_hit(3.0,1.1).is_empty() and screen_hit(foot.y+work_grip.y,3.0).is_empty(), "Out-of-bounds real-screen negative control failed"):
		return
	pilot.position = foot
	pilot.rotation.y = PI
	tool = Node3D.new()
	pilot.add_child(tool)
	box(tool, Vector3.ZERO, Vector3(0.13, 0.035, 0.035), Color("#4e9aaa"))
	box(tool, Vector3(0.14, 0, 0), Vector3(0.18, 0.012, 0.012), Color("#c4ced2"))
	box(tool, Vector3(0.217, 0, 0), Vector3(0.026, 0.022, 0.022), Color("#6dcad5"))
	camera = Camera3D.new()
	room.add_child(camera)
	camera.current = true
	camera.near = 0.02
	camera.far = 100
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_COLOR
	environment.environment.background_color = Color(0.01, 0.015, 0.025)
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.environment.ambient_light_color = Color(0.8, 0.88, 1)
	environment.environment.ambient_light_energy = 0.6
	room.add_child(environment)
	var light := OmniLight3D.new()
	light.position = Vector3(0.5, 2.1, 1.6)
	light.omni_range = 5
	light.light_energy = 0.8
	room.add_child(light)
	var label := Label.new()
	label.position = Vector2(24, 24)
	label.add_theme_font_size_override("font_size", 20)
	label.text = "Scripted crew / real retained instrument fixture"
	label.add_theme_color_override("font_shadow_color", Color.BLACK)
	label.add_theme_constant_override("shadow_offset_x", 1)
	label.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(label)
	var frames: Array = []
	if review:
		for i in 20:
			frames.append(int(round(float(i) / 19.0 * (FPS * SECONDS - 1))))
	else:
		frames = range(FPS * SECONDS)
	for frame in frames:
		var seconds := float(frame) / FPS
		if not set_pose(seconds):
			return
		await process_frame
		await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		var filename := "frame-%06d.png" % frame
		if not require_ok(image != null and image.get_size() == Vector2i(1920, 1080) and image.save_png(output.path_join(filename)) == OK, "Actual image capture failed"):
			return
		if review or frame % FPS == 0:
			var tip: Vector3 = room.global_transform.affine_inverse() * (tool.global_transform * Vector3(0.23, 0, 0))
			if not require_ok(tip.distance_to(target)<0.0001, "Fixed authored-grip tip no longer matches measured screen"):
				return
			samples.append({"frame": frame, "seconds": seconds, "pose": pilot.current_frame, "foot": vec(foot), "tip": vec(tip), "target": vec(target), "tip_target_distance": tip.distance_to(target), "camera": vec(camera.position), "png_sha256": FileAccess.get_sha256(output.path_join(filename))})
		if frame % (FPS * 6) == 0:
			print("INTEGRATED_WORKSHOP ", frame, "/", FPS * SECONDS)
	var final_actor: Dictionary = owner.get_freedom_walk_state()
	var final_flight: Dictionary = owner.get_freedom_flight_state()
	if not require_ok(final_actor == initial_actor and final_flight == initial_flight, "Editorial scene changed authoritative C++ state"):
		return
	for path in source_hashes:
		if not require_ok(FileAccess.get_sha256(base.path_join(path)) == source_hashes[path], "Original source changed during capture"):
			return
	if not require_ok(owner.save_freedom_as(output.path_join("unchanged-journey.json")), "Unchanged authoritative journey Save As failed"):
		return
	var report := {"schema_version": 1, "passed": true, "review": review, "frames": frames.size(), "fps": FPS, "seconds": SECONDS, "renderer":RenderingServer.get_current_rendering_method(), "adapter":RenderingServer.get_video_adapter_name(), "godot":Engine.get_version_info().string, "pixels": [1920,1080], "source_sha256": source_hashes, "script_sha256": FileAccess.get_sha256("res://studies/film/reel_integrated_workshop.gd"), "bridge_sha256": FileAccess.get_sha256("res://bin/libapsis_freedom_bridge.so"), "initial_tick": initial_actor.tick, "final_tick": final_actor.tick, "initial_checksum": initial_flight.checksum, "final_checksum": final_flight.checksum, "player_state_unchanged": true, "unchanged_save_sha256":FileAccess.get_sha256(output.path_join("unchanged-journey.json")), "held_grip_landmark": vec(work_grip), "screen_contact": {"node": contact.node, "surface": contact.surface, "triangle": contact.triangle, "point": vec(target)}, "floor_support": {"node": support.node, "point": vec(support.point)}, "floor_probes":floor_probes, "negative_controls":{"above_screen_ray_missed":true,"outside_screen_lateral_ray_missed":true}, "character_closure_sha256":FileAccess.get_sha256("res://character-resources.json"), "samples": samples, "limits": "Source-workshop derivative composition registered in the existing C++ station frame; full room/retained instrument remain visible. Cinematic same male clerk/technician in one held service key with already-held primitive probe; no animated work cycle. Actual screen ray and fixed work-tip registration, no body/continuous collision or anatomical IK qualification, NPC AI, tool inventory, diagnostics, repair success, manual retention release or native fixture admission. Source action art inherited rights remain owner-review-only. No C++ state advanced, no second simulated world, no performance/audio claim."}
	FileAccess.open(output.path_join("capture.json"), FileAccess.WRITE).store_string(JSON.stringify(report, "\t") + "\n")
	print("INTEGRATED_WORKSHOP PASS frames=", frames.size(), " checksum=", final_flight.checksum)
	room.free()
	quit(0)
