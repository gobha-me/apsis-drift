extends SceneTree
## Production first-person consumer against independent C++ journey save bytes.
const WalkView = preload("res://native_walk_view.gd")
var failures := 0


func check(ok: bool, message: String) -> void:
	if not ok:
		push_error(message)
		failures += 1


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		push_error("Expected C++ fresh seed42 journey, walking trace, prepared assets")
		quit(1)
		return
	GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var neutral := PackedFloat64Array([0.0, 0.0, 0.0])
	check(owner.get_freedom_walk_state().is_empty() and not owner.advance_freedom_walk(1.0 / 120.0, neutral), "Uninitialized walking accepted")
	var originals := [FileAccess.get_file_as_bytes(args[0]), FileAccess.get_file_as_bytes(args[1])]
	if not owner.initialize_freedom_new_game("42"):
		push_error("Fresh walking New Game failed: " + str(owner.get_last_error()))
		quit(1)
		return
	var start: Dictionary = owner.get_freedom_walk_state()
	if not WalkView.valid_state(start):
		push_error("Fresh walking view invalid")
		quit(1)
		return
	check(not start.continued and start.tick == "0" and start.actor_id == "1", "New Game has no valid fresh station actor")
	check(owner.get_freedom_start().is_empty(), "New walking state also claims a frozen legacy station start")
	var flight: Dictionary = owner.get_freedom_flight_state()
	check(flight.attached and flight.target_port == 1 and flight.station_id == start.station_id and flight.tick == start.tick, "Fresh actor/Wayfarer do not share D1 and time")
	var path := args[0].get_base_dir().path_join("native-walking.json")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[0], "Ordinary New Game bytes differ from independent C++ fresh journey")
	for buffer in [PackedFloat64Array(), PackedFloat64Array([0.0, 0.0]), PackedFloat64Array([0.0, 0.0, 0.0, 0.0]), PackedFloat64Array([NAN, 0.0, 0.0]), PackedFloat64Array([0.0, INF, 0.0]), PackedFloat64Array([1.01, 0.0, 0.0]), PackedFloat64Array([0.0, -1.01, 0.0]), PackedFloat64Array([0.0, 0.0, PI + 0.01])]:
		check(not owner.advance_freedom_walk(1.0 / 120.0, buffer) and owner.get_freedom_walk_state() == start, "Malformed walk buffer changed state/clock")
	for elapsed in [NAN, INF, -0.01, 60.01]:
		check(not owner.advance_freedom_walk(elapsed, neutral) and owner.get_freedom_walk_state() == start, "Malformed walk time changed state")
	check(not owner.release_freedom_port() and not owner.set_freedom_assistance(true) and owner.get_freedom_walk_state() == start, "Outside actor gained flight authority")
	var refusal: String = owner.get_last_error()
	owner.get_freedom_walk_state()
	owner.get_freedom_flight_state()
	owner.get_freedom_station_geometry()
	check(owner.get_last_error() == refusal, "Read-only query erased command refusal")
	for key in ["foot_position_metres", "actor_global_position_metres"]:
		var bad := start.duplicate(true)
		bad[key] = PackedFloat64Array([NAN, 0.0, 0.0])
		check(not WalkView.valid_state(bad), "Walk view accepted nonfinite " + key)
	var malformed := start.duplicate(true)
	malformed["station_basis"] = Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO)
	check(not WalkView.valid_state(malformed), "Walk view accepted collapsed frame")
	var view := WalkView.new()
	root.add_child(view)
	view.set_process(false)
	if not view.initialize(owner, args[2]):
		push_error("Production walking view failed: " + view.error)
		view.free()
		quit(1)
		return
	check(view.station != null and view.station.camera == null and view.ship != null, "Walking lost real ship/station or used an inspection camera")
	check(view.camera.position == start.actor_eye_position and view.station.transform == Transform3D(start.station_basis, start.station_position), "Actor camera/station differs from authoritative projection")
	view.set_paused(true)
	check(view.advance_requested(60.0, neutral) and owner.get_freedom_walk_state() == start, "Paused view advanced shared time")
	view.set_paused(false)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	check(view.paused and not view.focused and view.advance_requested(1.0, neutral) and owner.get_freedom_walk_state() == start, "Lost-focus input advanced walking")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and view.focused, "Focus return resumed time without player action")
	view.set_paused(false)
	for axis in [-1.0, 1.0]:
		for n in 960:
			check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0.0, axis, 0.0])), "Supported walking trace refused")
	view.update_view()
	var final: Dictionary = owner.get_freedom_walk_state()
	check(final.tick == "1920" and owner.get_freedom_flight_state().tick == final.tick, "Walk/ship shared clock diverged")
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == originals[1], "Native route bytes differ from independent C++ collision/motion trace")
	check(owner.initialize_freedom_continue(path), "Walking Continue refused")
	var continued: Dictionary = owner.get_freedom_walk_state()
	final.continued = true
	check(continued == final, "Walking Continue changed actor pose, frame, motion or time")
	view.free()
	view = WalkView.new()
	root.add_child(view)
	view.set_process(false)
	check(view.initialize(owner, args[2]) and view.paused, "Continued walking view resumed without player action")
	check(view.advance_requested(1.0, neutral) and owner.get_freedom_walk_state() == continued, "Continued view advanced while initially paused")
	var other: Variant = ClassDB.instantiate("FreedomBridge")
	check(other.initialize_freedom_new_game("42"), "Cadence walking New Game refused")
	for axis in [-1.0, 1.0]:
		for n in 480:
			check(other.advance_freedom_walk(1.0 / 60.0, PackedFloat64Array([0.0, axis, 0.0])), "Cadence walking refused")
	continued.continued = false
	check(other.get_freedom_walk_state() == continued, "Presentation cadence changed supported walking")
	check(FileAccess.get_file_as_bytes(args[0]) == originals[0] and FileAccess.get_file_as_bytes(args[1]) == originals[1], "Walking modified independent source saves")
	view.free()
	print("Native station walking: %d failures" % failures)
	quit(0 if failures == 0 else 1)
