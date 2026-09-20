extends SceneTree
## Inherited main frame/input/view paths + real C++ bridge, generated rig only.
## Dummy/headless: no authored-asset fit, GPU pixels or hardware qualification.
class HeadlessMain extends "res://main.gd":
	var errors: Array[String] = []
	func _ready() -> void:
		set_process(false)
	func fail(message: String) -> void:
		errors.append(message)

class NoWorldDrawing extends Node:
	func refresh(_study: Node, _altitude: float, _radius: float) -> void:
		pass

class TerrainGate extends Node3D:
	var is_ready := false
	var error := ""
	var resident_ids := []
	var swaps := 0
	var retired_total := 0
	func tick(_delta: float, _camera: Vector3) -> void:
		pass

const Fixture = preload("res://pilot_motion_test.gd")
const DEVICE := 77
var failures := 0
var study: Variant

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func key(code: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func group(parent: Node3D, title: String) -> Node3D:
	var node := Node3D.new()
	node.name = title
	parent.add_child(node)
	var mesh := MeshInstance3D.new()
	mesh.mesh = BoxMesh.new()
	node.add_child(mesh)
	return node

func pose() -> Vector4:
	return study.pilot_motion_hook.diagnostics().motion.blend

func frame(delta := 0.05) -> void:
	var count: int = study.pilot_motion_hook.diagnostics().updates
	study._process(delta)
	check(study.pilot_motion_hook.diagnostics().updates == count + 1, "Main updates motion once per presentation frame")
	check(study.errors.is_empty(), "Main rejected valid integration frame: " + str(study.errors))

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var snapshot := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot = argument.trim_prefix("--snapshot=")
	if not snapshot.is_absolute_path():
		push_error("Requires --snapshot=/absolute/native-snapshot.json")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	study = HeadlessMain.new()
	root.add_child(study)
	study.snapshot_text = FileAccess.get_file_as_string(snapshot)
	study.data = JSON.parse_string(study.snapshot_text)
	study.options = {"--flight-model": "thrust", "--controls-persist": "false"}
	study.live_bridge = ClassDB.instantiate("FreedomBridge")
	var reference: Variant = ClassDB.instantiate("FreedomBridge")
	for bridge in [study.live_bridge, reference]:
		if not bridge.initialize(study.snapshot_text) or not bridge.enable_orbit_practice() or not bridge.enable_streaming() or not bridge.start_practice(false):
			push_error("Real native flight fixture initialization failed")
			quit(1)
			return
	study.live_presentation = true
	study.streaming_requested = true
	study.pilot_view = true
	study.surface = Node3D.new()
	study.exhibit = Node3D.new()
	study.sun_light = DirectionalLight3D.new()
	for node in [study.surface, study.exhibit, study.sun_light]:
		study.add_child(node)
	study.ship = Node3D.new()
	study.add_child(study.ship)
	study.pilot_cockpit = Node3D.new()
	study.ship.add_child(study.pilot_cockpit)
	study.pilot_cockpit.transform = study.CockpitLayout.mount()
	study.camera = Camera3D.new()
	study.add_child(study.camera)
	study.scene_environment = Environment.new()
	study.navigation_sky = ShaderMaterial.new()
	study.navigation_sky.shader = preload("res://navigation_sky.gdshader")
	study.world_view = NoWorldDrawing.new()
	study.add_child(study.world_view)
	study.label = Label.new()
	study.caption = Label.new()
	study.debug_backdrop = ColorRect.new()
	for node in [study.label, study.caption, study.debug_backdrop]:
		study.add_child(node)
	study.flight_displays = preload("res://flight_displays.gd").new()
	study.pilot_cockpit.add_child(study.flight_displays)
	study.setup_player_controls()
	study.player_input.device = DEVICE
	study.player_input.install()
	study.player_input.assist = study.live_bridge.get_state().assist
	study.player_input.sample()
	check(study.setup_pilot_motion() and study.pilot_motion_hook == null, "Pilot motion is default-off")
	study.options["--pilot-motion"] = "true"
	check(not study.setup_pilot_motion() and not study.errors.is_empty(), "Explicit missing assets fail clearly")
	study.errors.clear()
	study.options["--pilot-asset"] = "/synthetic/rig.glb"
	study.options["--pilot-cabin"] = "/synthetic/cabin.glb"
	study.seated_pilot = Fixture.fixture()
	study.pilot_cockpit.add_child(study.seated_pilot)
	var head := group(study.seated_pilot, "PilotHead")
	var body := group(study.seated_pilot, "PilotBody")
	check(not study.setup_pilot_motion(), "Missing authored neutral clip rejected")
	study.errors.clear()
	var player := AnimationPlayer.new()
	var library := AnimationLibrary.new()
	var animation := Animation.new()
	animation.length = 1.0
	library.add_animation("Scene", animation)
	player.add_animation_library("", library)
	study.seated_pilot.add_child(player)
	var skeleton: Skeleton3D = study.seated_pilot.find_children("*", "Skeleton3D", true, false)[0]
	skeleton.reparent(head)
	check(not study.setup_pilot_motion(), "Shared skeleton under hidden own-head group accepted")
	study.errors.clear()
	skeleton.reparent(study.seated_pilot)
	# Equality is not is_ancestor_of: a Skeleton named as the own-head group
	# must also refuse even though its child mesh satisfies the static contract.
	var original_skeleton_name := skeleton.name
	head.name = "TemporaryHead"
	var head_geometry := head.get_child(0)
	head_geometry.reparent(skeleton)
	skeleton.name = "PilotHead"
	check(not study.setup_pilot_motion(), "Shared skeleton itself used as hidden head group accepted")
	study.errors.clear()
	skeleton.name = original_skeleton_name
	head_geometry.reparent(head)
	head.name = "PilotHead"
	var before: Dictionary = study.live_bridge.get_state()
	check(study.setup_pilot_motion() and not player.is_playing(), "Authored neutral clip paused before driver configuration")
	check(study.live_bridge.get_state() == before, "Motion configuration wrote flight state")
	if study.pilot_motion_hook == null:
		quit(1)
		return
	var hook: Node = study.pilot_motion_hook
	check(study.setup_pilot_motion() and study.pilot_motion_hook == hook, "Repeated setup duplicates driver")
	key(KEY_I, true)
	frame()
	check(pose().x > 0 and pose().y == 0 and pose().z == 0 and pose().w == 0, "Real resolved pitch did not move corresponding grip")
	check(reference.advance_thrust(0.05, PackedFloat64Array([0,0,1,0,0,0,0]), study.player_input.assist), "Reference step failed")
	check(reference.get_state() == study.live_bridge.get_state(), "Live motion changed authoritative flight result")
	before = study.live_bridge.get_state()
	study.update_pilot_motion({"thrust_axes": PackedFloat64Array([0,0,1,0,0,0,0])}, 0.05)
	check(study.live_bridge.get_state() == before, "Presentation update advances flight")
	var held := pose()
	study.toggle_pilot()
	check(head.visible and body.visible and pose() == held, "Exterior transition lost pose or hid body")
	study.toggle_pilot()
	check(not head.visible and body.visible and pose() == held, "Cockpit transition lost pose/head masking")
	study.set_player_paused(true)
	check(pose() == Vector4.ZERO, "Pause did not reset stale demand immediately")
	before = study.live_bridge.get_state()
	frame()
	check(pose() == Vector4.ZERO and study.live_bridge.get_state() == before, "Pause menu leaked pose or physics")
	study.set_player_paused(false)
	frame()
	check(pose() == Vector4.ZERO and study.player_input.needs_neutral, "Resume replayed held demand")
	key(KEY_I, false)
	frame()
	key(KEY_I, true)
	frame()
	check(pose().x > 0, "Rearmed flight did not restore motion")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_OUT)
	check(pose() == Vector4.ZERO and study.live_paused, "Focus loss failed safe neutral")
	study._notification(Node.NOTIFICATION_WM_WINDOW_FOCUS_IN)
	frame()
	check(pose() == Vector4.ZERO, "Focus regain resumed stale demand")
	key(KEY_I, false)
	study.set_player_paused(false)
	frame()
	study.flight_plan_menu.open(0)
	key(KEY_TAB, true)
	key(KEY_TAB, false)
	frame()
	check(pose() == Vector4.ZERO, "Guidance navigation animated flight grips")
	key(KEY_I, true)
	frame()
	check(study.flight_plan_menu.active and pose().x > 0, "Live guidance incorrectly froze allowed flight motion")
	study.flight_plan_menu.close()
	key(KEY_I, false)
	var gate := TerrainGate.new()
	study.add_child(gate)
	study.planet_stream = gate
	before = study.live_bridge.get_state()
	frame(0.2)
	check(pose() == Vector4.ZERO and study.live_bridge.get_state() == before, "Terrain readiness gate leaked stale demand")
	study.planet_stream = null
	gate.free()
	frame()
	key(KEY_I, true)
	frame()
	study.player_input.connection_changed(DEVICE, false)
	check(study.live_paused and pose() == Vector4.ZERO, "Disconnect did not safety-pause and reset motion")
	key(KEY_I, false)
	study.set_player_paused(false)
	frame()
	before = study.live_bridge.get_state()
	check(not hook.update_command({"thrust_axes": PackedFloat64Array([0,0,NAN,0,0,0,0])}, true, 0.1), "Nonfinite demand accepted")
	check(pose() == Vector4.ZERO and study.live_bridge.get_state() == before, "Invalid motion demand corrupted state")
	check(not hook.update_command({}, true, NAN), "Nonfinite presentation delta accepted")
	study.mode = 3
	study.update_pilot_motion({"thrust_axes": PackedFloat64Array([0,0,1,0,0,0,0])}, 1.0)
	check(pose() == Vector4.ZERO and not hook.active, "Inspection mode retained flight motion")
	study.queue_free()
	await process_frame
	print("PILOT MOTION MAIN INTEGRATION: ", failures, " failures; synthetic rig, real flight provider")
	quit(0 if failures == 0 else 1)
