extends SceneTree
## Source-sized station cinema: C++ player and explicitly cinematic clerk.
const Operating = preload("res://scripts/ships/wayfarer_operating_view.gd")
const Walk = preload("res://scripts/native/native_walk_view.gd")
const FEMALE_PATH := "res://trials/walk-04/pilot_presentation.gd"
const MALE_PATH := "res://trials/casual-male-01/pilot.gd"
const NPC_FOOT := Vector3(-1.0, 0.0, 0.4)
var owner: Variant
var view: Node3D
var female: Node3D
var male: Node3D
var camera: Camera3D
var label: Label
var walk_distance := 0.0
var previous_foot := Vector3.ZERO
var previous_art_camera := Vector3.FORWARD
var step_number := 0
var cinema := false
var npc_foot := NPC_FOOT
var npc_heading := PI
var npc_moving := false
var npc_greeting := false
var npc_distance := 0.0
var npc_previous := NPC_FOOT
var npc_view := 0
const FPS := 24
const CHAPTER_FRAMES := 40 * FPS
const CHAPTER_IDS := ["hub-meeting", "go-west", "d1-approach"]

func _initialize() -> void:
	call_deferred("run")

func fail(message: String) -> void:
	push_error(message)
	quit(1)

func character_files_valid() -> bool:
	if not FileAccess.file_exists("res://character-resources.json"):
		return false
	var receipt: Variant = JSON.parse_string(FileAccess.get_file_as_string("res://character-resources.json"))
	if not receipt is Dictionary or receipt.get("schema") != "apsis-reel-character-resources-v1" or not receipt.get("files") is Array:
		return false
	var seen := {}
	for item in receipt.files:
		if not item is Dictionary or not item.get("path") is String or seen.has(item.path) or item.path.is_absolute_path() or item.path.contains(".."):
			return false
		seen[item.path] = true
		if item.get("role") == "runtime":
			if not item.get("sha256") is String or FileAccess.get_sha256("res://" + item.path) != item.sha256:
				return false
	return true

func state_snapshot() -> Dictionary:
	var actor: Dictionary = owner.get_freedom_walk_state()
	var flight: Dictionary = owner.get_freedom_flight_state()
	return {"tick": actor.tick, "body_tick": flight.tick, "checksum": flight.checksum, "foot_metres": Array(actor.foot_position_metres), "heading_radians": actor.heading_radians, "dropped_seconds": actor.dropped_seconds}

func card_face_camera(card: Sprite3D, to_camera: Vector3) -> void:
	card.billboard = BaseMaterial3D.BILLBOARD_DISABLED
	if Vector2(to_camera.x,to_camera.z).length_squared() > 0.000001:
		card.basis = Basis(Vector3.UP, atan2(to_camera.x, to_camera.z))

func update_characters() -> bool:
	var actor: Dictionary = owner.get_freedom_walk_state()
	var flight: Dictionary = owner.get_freedom_flight_state()
	if not Walk.valid_state(actor) or actor.tick != flight.tick or actor.dropped_seconds != 0.0:
		return false
	view.station.transform = Transform3D(actor.station_basis, actor.station_position)
	view.ship.transform = Transform3D(flight.body_basis, Vector3.ZERO)
	var foot := Vector3(actor.foot_position_metres[0], actor.foot_position_metres[1], actor.foot_position_metres[2])
	walk_distance += previous_foot.distance_to(foot)
	previous_foot = foot
	female.position = foot
	# C++ forward at heading0 is -Z; supplied art heading0 is +Z.
	var art_heading := wrapf(actor.heading_radians + PI, -PI, PI)
	var local_camera: Vector3 = view.station.to_local(camera.global_position)
	var female_camera: Vector3 = local_camera - foot
	var velocity: PackedFloat64Array = actor.velocity_metres_per_second
	var moving := Vector3(velocity[0], velocity[1], velocity[2]).length_squared() > 0.000001
	# The actual first-person eye has no horizontal camera direction. Keep the
	# last valid artwork view while its card intersects the near plane; the
	# camera and C++ foot remain unchanged, and the source API stays strict.
	if Vector2(female_camera.x,female_camera.z).length_squared() > 0.000001:
		previous_art_camera = female_camera
	if not female.present_movement(art_heading, previous_art_camera, walk_distance, moving):
		return false
	card_face_camera(female.locomotion._card, female_camera)
	male.position = npc_foot
	var male_camera := local_camera - npc_foot
	if cinema:
		npc_distance += npc_previous.distance_to(npc_foot)
		npc_previous = npc_foot
		npc_view = male.get_script().select_view(npc_heading - atan2(male_camera.x,male_camera.z),npc_view)
		var direction: String = ["front","right","rear","left"][npc_view]
		var family := "idle"
		var index := 0
		if npc_moving:
			family = "walk_side" if direction in ["right","left"] else "walk_axial"
			index = male.get_script().frame_for(npc_distance / 1.4,8 if family == "walk_side" else 4)
		elif npc_greeting and direction in ["right","left"]:
			family = "gesture"
			index = 0
		if not male.present(family,index,direction):
			return false
	elif not male.present("idle",0,"front"):
		return false
	card_face_camera(male.card, male_camera)
	var cpp_world_foot: Vector3 = actor.actor_eye_position - actor.station_basis.y * 1.70
	return female.global_position.distance_to(cpp_world_foot) < 0.00002 and not female.locomotion._card.no_depth_test and not male.card.no_depth_test

func negative_requests_preserved() -> bool:
	var pc_before := [female.transform, female.mode, female.locomotion.current_family, female.locomotion.current_frame, female.locomotion.current_view, female.locomotion._card.transform]
	if female.present_movement(NAN, Vector3(1,0,0), 0.0, false) or female.present_movement(0.0, Vector3.ZERO, 0.0, true):
		return false
	var pc_after := [female.transform, female.mode, female.locomotion.current_family, female.locomotion.current_frame, female.locomotion.current_view, female.locomotion._card.transform]
	var npc_before := [male.transform, male.current_family, male.current_frame, male.current_view, male.card.transform]
	if male.present("walk_side", 999, "right") or male.present("unknown", 0, "front"):
		return false
	return pc_before == pc_after and npc_before == [male.transform, male.current_family, male.current_frame, male.current_view, male.card.transform]

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 3 or args.size() > 4 or (args.size()==4 and args[3] not in ["capture","review"]) or DisplayServer.get_name() == "headless":
		fail("Expected prepared starter, operating, empty output; optional capture/review; rendering display")
		return
	for path in args.slice(0,3):
		if not path.is_absolute_path() or not DirAccess.dir_exists_absolute(path):
			fail("Proof paths must be existing absolute directories")
			return
	var out := DirAccess.open(args[2])
	if not out.get_files().is_empty() or not out.get_directories().is_empty() or not character_files_valid():
		fail("Expected unused output and unchanged staged Hero resources")
		return
	if not ResourceLoader.exists(FEMALE_PATH) or not ResourceLoader.exists(MALE_PATH):
		fail("Stage the selected private character resource closure first")
		return
	var female_script := load(FEMALE_PATH) as GDScript
	var male_script := load(MALE_PATH) as GDScript
	if female_script == null or male_script == null:
		fail("Selected character scripts could not load")
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	owner = ClassDB.instantiate("FreedomBridge")
	if owner == null or not owner.initialize_freedom_new_game("42"):
		fail("Actual C++ station New Game is unavailable")
		return
	root.size = Vector2i(1920,1080)
	view = Operating.new()
	root.add_child(view)
	if not view.initialize(owner, args[0], args[1]):
		fail(view.error)
		return
	camera = Camera3D.new()
	camera.near = 0.025
	camera.far = 180.0
	camera.fov = 76.0
	view.add_child(camera)
	camera.current = true
	var world := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.004,0.009,0.02)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.6,0.75,0.95)
	environment.ambient_light_energy = 0.7
	world.environment = environment
	view.add_child(world)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-40,-30,0)
	light.light_energy = 1.5
	view.add_child(light)
	female = female_script.new()
	female.name = "CppPlayerPresentation"
	view.station.add_child(female)
	male = male_script.new()
	male.name = "CinematicStationClerk"
	view.station.add_child(male)
	if not male.configure():
		fail("Current casual male resources refused")
		return
	label = Label.new()
	label.position = Vector2(24,24)
	label.add_theme_font_size_override("font_size",20)
	label.add_theme_color_override("font_shadow_color",Color.BLACK)
	label.add_theme_constant_override("shadow_offset_x",1)
	label.add_theme_constant_override("shadow_offset_y",1)
	var plate := Panel.new()
	plate.position = Vector2(18,18)
	plate.size = Vector2(810,38)
	var background := StyleBoxFlat.new()
	background.bg_color = Color(0.015,0.025,0.04,0.83)
	plate.add_theme_stylebox_override("panel",background)
	root.add_child(plate)
	root.add_child(label)
	if args.size()==4:
		cinema = true
		await capture_chapters(args,args[3]=="review")
		return
	var initial := state_snapshot()
	var records := []
	for frame in 20:
		if frame > 0:
			for tick in 5:
				var controls := PackedFloat64Array([0.0,0.0,PI/2.0])
				if step_number < 24:
					controls[1] = 1.0
				elif step_number < 84:
					controls[0] = 1.0
				if not owner.advance_freedom_walk(1.0/120.0,controls):
					fail("Actual C++ proof walk refused: " + str(owner.get_last_error()))
					return
				step_number += 1
		var actor: Dictionary = owner.get_freedom_walk_state()
		view.station.transform = Transform3D(actor.station_basis,actor.station_position)
		var camera_local := Vector3(-4.0,1.55,0.2) if frame < 10 else Vector3(-3.6,1.4,-0.25)
		if frame == 18:
			camera_local = Vector3(-8.0,1.2,2.0)
		camera.position = view.station.transform * camera_local
		camera.look_at(view.station.transform * Vector3(-0.6,0.92,0),actor.station_basis.y)
		if not update_characters() or not negative_requests_preserved():
			fail("Character scale/frame/occlusion or atomic negative API control failed")
			return
		label.text = "Integrated station proof · C++ pilot / cinematic clerk" if frame != 18 else "Source wall occlusion negative control · depth testing retained"
		for settle in 2:
			await process_frame
			await RenderingServer.frame_post_draw
		var image := root.get_texture().get_image()
		var filename := "proof-%02d.png" % frame
		var path := args[2].path_join(filename)
		if image == null or image.get_width() != 1920 or image.get_height() != 1080 or image.save_png(path) != OK:
			fail("Integrated station image failed")
			return
		records.append({"file":filename,"sha256":FileAccess.get_sha256(path),"state":state_snapshot(),"camera":Operating.columns(camera.transform),"female_foot_station_metres":[female.position.x,female.position.y,female.position.z],"female_height_metres":female.locomotion._card.pixel_size*144.0,"female_pose":[female.locomotion.current_family,female.locomotion.current_frame,female.locomotion.current_view],"male_foot_station_metres":[male.position.x,male.position.y,male.position.z],"male_height_metres":male.card.pixel_size*160.0,"male_pose":[male.current_family,male.current_frame,male.current_view],"female_card_station_transform":Operating.columns(female.locomotion._card.transform),"male_card_station_transform":Operating.columns(male.card.transform),"wall_occlusion_control":frame==18})
	var final := state_snapshot()
	if final.tick != "95" or final.body_tick != final.tick or abs(final.foot_metres[0]+1.0)>0.000001 or abs(final.foot_metres[2]+0.4)>0.000001:
		fail("Actual short proof path did not match the input trace")
		return
	var checkpoint := args[2].path_join("proof-journey.json")
	if not owner.save_freedom_as(checkpoint):
		fail("Proof journey Save As failed")
		return
	var inputs := {}
	for filename in ["prepared.json","wayfarer-operating-02.json","wayfarer-operating-02.glb","contact.json","station-closure.json","qualification.json","station-d1-clearance-01.glb"]:
		inputs[filename] = FileAccess.get_sha256(args[1].path_join(filename))
	var report := {"schema":"apsis.reel-integrated-station-proof/1","scope":"Real C++ fresh seed42 player foot/heading/shared clock, source-sized female/male in the same registered station; male is cinematic presentation, no NPC AI/collision or action-success claims; no boarding","engine":Engine.get_version_info(),"renderer":RenderingServer.get_video_adapter_name(),"initial_state":initial,"final_state":final,"character_receipt_sha256":FileAccess.get_sha256("res://character-resources.json"),"consumer_sha256":FileAccess.get_sha256("res://studies/film/reel_integrated_station.gd"),"bridge_sha256":FileAccess.get_sha256("res://bin/libapsis_freedom_bridge.so"),"operating_inputs_sha256":inputs,"journey_save_sha256":FileAccess.get_sha256(checkpoint),"manual_station_local_billboards":true,"negative_requests_preserved":true,"captures":records}
	var file := FileAccess.open(args[2].path_join("proof.json"),FileAccess.WRITE)
	file.store_string(JSON.stringify(report,"\t")+"\n")
	print("Integrated station proof: both characters, 20 images, actual shared tick95")
	quit(0)

func chapter_input(chapter: int, frame: int, substep: int) -> PackedFloat64Array:
	var tick := frame * 5 + substep
	var control := PackedFloat64Array([0.0,0.0,PI/2.0])
	if chapter == 0:
		if tick < 24:
			control[1] = 1.0
		elif tick < 84:
			control[0] = 1.0
		else:
			control[2] = lerpf(PI/2.0,PI,smoothstep(84.0,240.0,float(tick)))
	elif chapter == 1:
		if tick >= 120 and tick < 144:
			control[1] = -1.0
		elif tick >= 144 and tick < 1044:
			control[0] = 1.0
	elif chapter == 2 and tick >= 240 and tick < 600:
		control[0] = 1.0
	return control

func position_camera(position_local: Vector3, target_local: Vector3) -> void:
	camera.position = view.station.transform * position_local
	camera.look_at(view.station.transform * target_local,view.station.basis.y)

func cinema_camera(chapter: int, time: float) -> void:
	var actor: Dictionary = owner.get_freedom_walk_state()
	var foot := Vector3(actor.foot_position_metres[0],actor.foot_position_metres[1],actor.foot_position_metres[2])
	view.station.transform = Transform3D(actor.station_basis,actor.station_position)
	if chapter == 0:
		var knots := [
			[0.0,Vector3(-4,1.55,.2),Vector3(-.6,.92,0)],
			[4.0,Vector3(-2.6,1.5,-1.8),Vector3(-1,.85,.4)],
			[10.0,Vector3(-2.6,1.5,1.8),Vector3(-1,.85,-.4)],
			[16.0,Vector3(-2.6,1.5,-1.8),Vector3(-1,.85,.4)],
			[24.0,Vector3(-2,1.55,1.75),Vector3(-1,.7,-.4)],
			[30.0,Vector3(-2.6,1.5,-1.8),Vector3(-1,.85,.4)],
			[38.0,Vector3(-4,1.6,.05),Vector3(-1,1,0)]
		]
		var from: Array = knots[0]
		var to: Array = knots[0]
		for knot in knots:
			if time >= float(knot[0]):
				from = to
				to = knot
		var blend := smoothstep(float(to[0]),float(to[0])+2.0,time)
		position_camera(from[1].lerp(to[1],blend),from[2].lerp(to[2],blend))
	elif chapter == 1:
		position_camera(foot+Vector3(1.65,1.8,.22),foot+Vector3(-2,1.4,0))
		var follow := camera.transform
		var first_person := Transform3D(actor.station_basis * Basis(Vector3.UP,actor.heading_radians),actor.actor_eye_position)
		# Pass beside the card before reaching the true eye, rather than pushing
		# the camera through an enlarged face during the viewpoint transition.
		position_camera(foot+Vector3(0,1.7,.72),foot+Vector3(-2,1.7,0))
		var side := camera.transform
		camera.transform = follow.interpolate_with(side,smoothstep(8.0,8.5,time)).interpolate_with(first_person,smoothstep(8.5,9.0,time))
	else:
		var pitch := lerpf(0.0,-.58,smoothstep(5.0,8.0,time))
		var first_person := Transform3D(actor.station_basis * Basis(Vector3.UP,actor.heading_radians) * Basis(Vector3.RIGHT,pitch),actor.actor_eye_position)
		var lateral := lerpf(.35,-.3,smoothstep(24.0,39.0,time))
		position_camera(foot+Vector3(2.0,1.75,lateral),Vector3(-23.12,-.2,0))
		camera.transform = first_person.interpolate_with(camera.transform,smoothstep(12.0,14.0,time))

func player_card_intersects_camera() -> bool:
	var card: Sprite3D = female.locomotion._card
	var point := card.to_local(camera.global_position)
	var half_extent := 80.0 * card.pixel_size
	return abs(point.z) <= camera.near + 0.02 and abs(point.x) <= half_extent and abs(point.y) <= half_extent

func checkpoint(directory: String, name: String) -> Dictionary:
	var path := directory.path_join(name+".json")
	var before := state_snapshot()
	if FileAccess.file_exists(path) or not owner.save_freedom_as(path) or not owner.initialize_freedom_continue(path):
		return {"error":"Actual Save As/reopen failed: "+str(owner.get_last_error())}
	if state_snapshot()!=before:
		return {"error":"Save/reopen changed actual actor/body state"}
	return {"file":name+".json","sha256":FileAccess.get_sha256(path),"reopened_state":before}

func cinema_sample(chapter: int, frame: int) -> bool:
	var seconds: Array = [0.0,4.0,6.0,11.0,18.0,27.0,33.0,39.0] if chapter==0 else ([0.0,2.0,4.0,7.5,8.75,9.0,14.0,20.0,30.0,39.0] if chapter==1 else [0.0,3.0,5.0,8.0,12.0,13.0,14.0,20.0,28.0,35.0,39.0])
	for second in seconds:
		if frame==int(float(second)*FPS):
			return true
	return false

func capture_chapters(args: PackedStringArray, review: bool) -> void:
	var started := Time.get_ticks_usec()
	var chapters := []
	var images := []
	for chapter in CHAPTER_IDS.size():
		var start := checkpoint(args[2],CHAPTER_IDS[chapter]+"-start")
		if start.has("error"):
			fail(start.error)
			return
		var record := {"id":CHAPTER_IDS[chapter],"start_frame":chapter*CHAPTER_FRAMES,"frames":CHAPTER_FRAMES,"seconds":40,"start_save":start,"samples":[]}
		label.text = "C++ station walking · cinematic clerk / editorial cameras"
		for frame in CHAPTER_FRAMES:
			for substep in 5:
				if not owner.advance_freedom_walk(1.0/120.0,chapter_input(chapter,frame,substep)):
					fail("Authoritative cinema movement failed: "+str(owner.get_last_error()))
					return
			var time := float(frame)/FPS
			npc_greeting = chapter==0 and time>=2.2 and time<3.8
			npc_moving = chapter==1 and time<1.0
			npc_heading = PI/2.0 if chapter>=1 else PI
			npc_foot = NPC_FOOT if chapter==0 else Vector3(lerpf(-1.0,0.0,smoothstep(0.0,1.0,time)) if chapter==1 else 0.0,0.0,.4)
			cinema_camera(chapter,time)
			if not update_characters() or not negative_requests_preserved():
				fail("Cinema character/authority/atomic pose validation failed")
				return
			# Hold the source card plane during the final sideways camera approach.
			# It then intersects that real plane before reaching the C++ eye, so
			# near-plane hiding avoids an enlarged billboard sweeping the lens.
			if chapter==1 and time>=8.0:
				card_face_camera(female.locomotion._card,Vector3.RIGHT)
			female.visible = not player_card_intersects_camera()
			var sample := cinema_sample(chapter,frame)
			if review and not sample:
				continue
			for settle in 2:
				await process_frame
				await RenderingServer.frame_post_draw
			var image := root.get_texture().get_image()
			var absolute := chapter*CHAPTER_FRAMES+frame
			var filename := "frame-%06d.png" % absolute
			var path := args[2].path_join(filename)
			if image==null or image.get_width()!=1920 or image.get_height()!=1080 or FileAccess.file_exists(path) or image.save_png(path)!=OK:
				fail("Cinema source image failed")
				return
			images.append({"frame":absolute,"file":filename,"sha256":FileAccess.get_sha256(path)})
			if sample:
				record.samples.append({"frame":absolute,"time_seconds":time,"state":state_snapshot(),"camera":Operating.columns(camera.transform),"npc_foot_metres":[npc_foot.x,npc_foot.y,npc_foot.z],"npc_pose":[male.current_family,male.current_frame,male.current_view],"player_pose":[female.locomotion.current_family,female.locomotion.current_frame,female.locomotion.current_view],"player_card_intersects_camera":not female.visible,"female_height_metres":female.locomotion._card.pixel_size*144.0,"male_height_metres":male.card.pixel_size*160.0})
			if frame%FPS==0:
				print("Integrated station %s frame%d/%d" % [CHAPTER_IDS[chapter],frame,CHAPTER_FRAMES])
		var end := checkpoint(args[2],CHAPTER_IDS[chapter]+"-end")
		if end.has("error") or int(end.reopened_state.tick)-int(start.reopened_state.tick)!=4800:
			fail("Cinema checkpoint or single shared clock failed")
			return
		record["end_save"] = end
		chapters.append(record)
	var last := state_snapshot()
	if last.tick!="14400" or abs(last.foot_metres[0]+22.0)>0.000001 or abs(last.foot_metres[2])>0.000001:
		fail("Supported narrative route did not reach the expected D1 approach")
		return
	var inputs := {}
	for filename in ["prepared.json","wayfarer-operating-02.json","wayfarer-operating-02.glb","contact.json","station-closure.json","qualification.json","station-d1-clearance-01.glb"]:
		inputs[filename] = FileAccess.get_sha256(args[1].path_join(filename))
	var sources := {}
	for filename in ["studies/film/reel_integrated_station.gd","scripts/ships/wayfarer_operating_view.gd","scripts/native/native_station_view.gd","scripts/native/native_walk_view.gd","character-resources.json","bin/libapsis_freedom_bridge.so"]:
		sources[filename] = FileAccess.get_sha256("res://"+filename)
	var report := {"schema":"apsis.reel-integrated-station-capture/1","review_only":review,"fps":FPS,"width":1920,"height":1080,"seconds":120,"timeline_frames":2880,"captured_frames":images.size(),"capture_wall_seconds":(Time.get_ticks_usec()-started)/1000000.0,"scope":"Same registered station and two source-sized identities; authoritative C++ player motion/shared tick, cinematic NPC choreography/editorial cameras; no production NPC dialogue/AI, contact success or boarding","engine":Engine.get_version_info(),"renderer":RenderingServer.get_video_adapter_name(),"sources_sha256":sources,"operating_inputs_sha256":inputs,"chapters":chapters,"images":images,"final_state":last}
	var output := FileAccess.open(args[2].path_join("render.json"),FileAccess.WRITE)
	output.store_string(JSON.stringify(report,"\t")+"\n")
	print("Integrated station cinema complete: %d images, actual tick14400" % images.size())
	quit(0)
