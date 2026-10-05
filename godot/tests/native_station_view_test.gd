extends SceneTree
const StationView = preload("res://scripts/native/native_station_view.gd")
var failures := 0


func commit_fixture_new_game(owner: Variant, seed: String) -> bool:
	if not owner.stage_freedom_new_game(seed): return false
	var pending: Dictionary = owner.get_pending_freedom_start()
	return owner.commit_pending_freedom_start(pending.candidate_id)

func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.get_freedom_station_geometry().is_empty(), "Uninitialized geometry exposed")
	check(commit_fixture_new_game(bridge, "42"), "New Game refused")
	var start: Dictionary = bridge.get_freedom_walk_state()
	if start.is_empty():
		check(false, "New Game station actor is unavailable")
		quit(1)
		return
	var geometry: Dictionary = bridge.get_freedom_station_geometry()
	check(StationView.valid_geometry(geometry, start.station_id), "C++ geometry refused")
	check(not bridge.initialize_freedom_new_game("01"), "Invalid seed accepted")
	var error: String = bridge.get_last_error()
	check(bridge.get_freedom_station_geometry() == geometry and bridge.get_last_error() == error, "Pure geometry query cleared refusal or changed selection")
	check(not StationView.valid_geometry(geometry, "0"), "Foreign station accepted")
	for dimensions in [Vector2i.ZERO, Vector2i(-1, 720), Vector2i(640, 0), Vector2i(4097, 720), Vector2i(2147483647, 2147483647)]:
		check(not StationView.valid_dimensions(dimensions), "Invalid viewport accepted")
	for key in ["asset_offset_metres", "bounds_minimum_metres", "bounds_maximum_metres"]:
		for vector in [PackedFloat64Array([0, 0]), PackedFloat64Array([NAN, 0, 0]), PackedFloat64Array([INF, 0, 0])]:
			var invalid := geometry.duplicate(true)
			invalid[key] = vector
			check(not StationView.valid_geometry(invalid, start.station_id), "Invalid geometry vector accepted")
	var invalid := geometry.duplicate(true)
	invalid.bounds_maximum_metres = invalid.bounds_minimum_metres
	check(not StationView.valid_geometry(invalid, start.station_id), "Inside-out bounds accepted")
	for key in ["ordinal", "station_id", "outward_normal", "bore_metres", "withdrawal_metres"]:
		invalid = geometry.duplicate(true)
		invalid.ports[0][key] = {"ordinal": 2, "station_id": "0", "outward_normal": PackedFloat64Array([0, -2, 0]), "bore_metres": NAN, "withdrawal_metres": -1.0}[key]
		check(not StationView.valid_geometry(invalid, start.station_id), "Invalid port accepted")
	var rejected := StationView.new()
	check(not rejected.initialize(start, geometry, "relative"), "Relative asset path accepted")
	check(rejected.get_child_count() == 0, "Refused assets created geometry")
	rejected.free()
	rejected = StationView.new()
	check(not rejected.initialize(start, geometry, args[0].path_join("missing")), "Missing prepared asset accepted")
	check(rejected.get_child_count() == 0, "Missing asset created a proxy")
	rejected.free()
	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	root.add_child(viewport)
	var view := StationView.new()
	viewport.add_child(view)
	check(view.initialize(start, geometry, args[0]), "Selected station import refused: " + view.error)
	if view.camera == null:
		viewport.free()
		quit(1)
		return
	var pose := view.camera.transform
	for values in [[NAN, 0.0, 0.0], [INF, 0.0, 0.0], [1.99, 0.0, 0.0], [1000.01, 0.0, 0.0], [100.0, NAN, 0.0], [100.0, 0.0, INF], [100.0, 0.0, 1.41]]:
		check(not view.inspect(values[0], values[1], values[2]), "Invalid camera accepted")
		check(view.camera.transform == pose, "Invalid camera changed view")
	var model: Node3D = view.get_node("OriginStationAsset")
	check(model.position.is_equal_approx(Vector3(-0.97, 0, 0.978)) and model.scale == Vector3.ONE, "Station scaled or datum applied twice")
	check(model.find_children("*", "MeshInstance3D", true, false).size() == 5009, "Selected station silhouette changed")
	for port in geometry.ports:
		var marker: Node3D = view.port_markers[port.ordinal - 1]
		check(marker.position.is_equal_approx(StationView.vector(port.position_metres)), "Port visual differs from C++ collar")
		check(marker.get_child(0).mesh is TorusMesh, "Port aperture cue missing")
	check(view.port_cue(0).is_empty() and view.port_cue(3).is_empty(), "Invalid port index accepted")
	for dimensions in [Vector2i(640, 360), Vector2i(1920, 1080)]:
		check(StationView.valid_dimensions(dimensions), "Supported viewport refused")
		viewport.size = dimensions
		for distance in [2.0, 30.0, 100.0, 500.0, 1000.0]:
			for angle in [0.0, PI / 2, PI, -PI / 2]:
				for pitch in [-1.4, 0.0, 1.4]:
					check(view.inspect(distance, angle, pitch), "Supported view refused")
					check(view.camera.transform.is_finite(), "Nonfinite camera transform")
					for ordinal in [1, 2]:
						var cue: Dictionary = view.port_cue(ordinal)
						check(cue.station_id == start.station_id and cue.label == "D%d" % ordinal, "Cue lost port identity")
						check(cue.pixel.is_finite() and is_finite(cue.range_metres) and cue.range_metres > 0, "Unbounded projection cue")
						check(cue.visibility == "projection_only", "Projection pretends to prove visibility")
	check(bridge.get_freedom_walk_state() == start and bridge.get_freedom_station_geometry() == geometry, "Inspection advanced simulation or changed geometry")
	viewport.free()
	print("Native station view: %d failures; physical geometry, LOD, camera and projection contracts; no GPU claim" % failures)
	quit(0 if failures == 0 else 1)
