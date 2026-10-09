extends SceneTree
## Accelerated deterministic integration playback, not a gameplay autopilot.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const WalkView = preload("res://scripts/native/native_walk_view.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const Checkpoints = preload("res://studies/captures/native_planetary_capture.gd")
const PlayerInput = preload("res://scripts/ui/player_input.gd")
const MAX_BYTES = 33554432
const MAX_ROWS = 100000
var failed := false

class RecordedInput extends PlayerInput:
	var axes := PackedFloat64Array([0, 0, 0, 0, 0, 0, 0])
	func sample() -> Dictionary:
		observe_neutral()
		return {"thrust_axes": axes, "look": Vector2.ZERO, "recenter": false}

func _initialize() -> void:
	call_deferred("run")

func check(value: bool, reason: String) -> bool:
	if not value:
		push_error(reason)
		failed = true
	return value

static func decoded_values(args: Array) -> PackedFloat64Array:
	var result := PackedFloat64Array()
	for value in args:
		if not value is String or value.length() != 16: return PackedFloat64Array()
		for c in value.to_ascii_buffer():
			if not (c >= 48 and c <= 57 or c >= 97 and c <= 102): return PackedFloat64Array()
		result.append(value.hex_decode().decode_double(0))
	return result

static func valid_record(value: Variant) -> bool:
	if not value is Array or value.size() != 3 or not value[0] is String or not Checkpoints.finite_number(value[1]) or value[1] != int(value[1]) or not value[2] is Array: return false
	var op: String = value[0]
	var count: int = int(value[1])
	var args: Array = value[2]
	if op in ["walk", "flight"]:
		if count < 1 or count > 15 or args.size() != (3 if op == "walk" else 12): return false
		var values := decoded_values(args)
		if values.size() != args.size(): return false
		for i in values.size():
			if not is_finite(values[i]): return false
			if op == "flight" and (values[i] < 0 or values[i] > 1): return false
			if op == "walk" and absf(values[i]) > (PI if i == 2 else 1.0): return false
		return true
	if count != 0: return false
	if op == "checkpoint": return args.size() == 1 and args[0] is String and args[0] in Checkpoints.LABELS
	return op in ["board", "unboard", "assistance_off", "release", "approach", "capture"] and args.is_empty()

static func valid_stream(path: String, trace: Dictionary) -> bool:
	var metadata: Variant = trace.get("command_stream")
	if not metadata is Dictionary or metadata.get("file") != "commands.jsonl" or metadata.get("cadence_ticks") != 15 or metadata.get("float_encoding") != "ieee754-le-hex": return false
	for pair in [["bytes", MAX_BYTES], ["rows", MAX_ROWS]]:
		var n: Variant = metadata.get(pair[0])
		if not Checkpoints.finite_number(n) or n != int(n) or n < 1 or n > pair[1]: return false
	var stream := FileAccess.open(path, FileAccess.READ)
	if stream == null or stream.get_length() != int(metadata.bytes): return false
	var rows := 0
	var tick := 0
	var phase := 0
	while stream.get_position() < stream.get_length():
		var line := stream.get_line()
		if line.to_utf8_buffer().size() > 4096 or rows >= MAX_ROWS: return false
		var record: Variant = JSON.parse_string(line)
		if not valid_record(record): return false
		rows += 1
		if record[0] in ["walk", "flight"]:
			tick += int(record[1])
			if tick > 750000: return false
		elif record[0] == "checkpoint":
			if phase >= Checkpoints.LABELS.size() or record[2][0] != Checkpoints.LABELS[phase] or str(tick) != trace.checkpoints[phase].tick: return false
			phase += 1
	stream.close()
	return phase == Checkpoints.LABELS.size() and rows == int(metadata.rows) and str(tick) == trace.final_tick

func resume(view: Control) -> bool:
	if view.paused:
		if view is WalkView: view.resume_requested()
		else: view.toggle_pause()
	return check(not view.paused and view.focused and view.error.is_empty(), "Voyage view could not resume")

func install_recorded_input(view: Control) -> void:
	if not view is FlightView or view.player_input is RecordedInput: return
	var previous: Node = view.player_input
	var recorded := RecordedInput.new()
	recorded.persist = false
	recorded.thrust_mode = true
	recorded.assistance_request_only = true
	recorded.assist = view.state.assistance
	view.add_child(recorded)
	recorded.set_enabled(false)
	view.player_input = recorded
	view.controls_menu.controls = recorded
	previous.free()

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3 or not args[0].is_absolute_path() or not args[1].is_absolute_path() or not args[2].is_absolute_path(): quit(1); return
	var source := FileAccess.open(args[0].path_join("trace.json"), FileAccess.READ)
	if source == null or source.get_length() > 32768: quit(1); return
	var trace: Variant = JSON.parse_string(source.get_as_text())
	source.close()
	if not check(Checkpoints.valid_trace(trace), "Malformed voyage manifest") or not check(valid_stream(args[0].path_join("commands.jsonl"), trace), "Malformed bounded voyage stream"): quit(1); return
	var zero := "0000000000000000"
	check(valid_record(["walk", 15, [zero, zero, zero]]) and decoded_values(["000000000000f03f"])[0] == 1.0, "Valid exact control buffer refused")
	for bad in [["flight", 0, []], ["walk", 16, [zero, zero, zero]], ["walk", 1.5, [zero, zero, zero]], ["flight", 1, [zero, zero, zero]], ["walk", 1, ["000000000000f87f", zero, zero]], ["walk", 1, ["000000000000f07f", zero, zero]], ["walk", 1, ["0000000000000040", zero, zero]], ["walk", 1, ["z000000000000000", zero, zero]], ["walk", 1, [zero + "00", zero, zero]], ["checkpoint", 0, ["../other"]], ["release", 0, [1]], ["teleport", 0, []]]:
		check(not valid_record(bad), "Invalid command accepted")
	if failed: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native C++ bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	# Explicit playback owns cadence. Deferred handoff signals still run; frame
	# waits and newly activated views cannot advance C++ on their own.
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK, "Voyage output unavailable") or not check(shell.select_start(owner, {"mode": "new_game", "value": "42", "assets": args[1]}), "New Game staging refused: " + shell.error): shell.free(); quit(1); return
	var model_id: int = shell.current_view.staged_model.get_instance_id()
	var rendered := DisplayServer.get_name() != "headless"
	var stream := FileAccess.open(args[0].path_join("commands.jsonl"), FileAccess.READ)
	var phase := 0
	var tick := 0
	var rows := 0
	var captures: Array = []
	while stream.get_position() < stream.get_length() and not failed:
		var record: Array = JSON.parse_string(stream.get_line())
		var view: Control = shell.current_view
		install_recorded_input(view)
		if not check(view.staged_model.get_instance_id() == model_id and view.error.is_empty() and shell.error.is_empty(), "Voyage replaced the Wayfarer or lost its view"): break
		var op: String = record[0]
		var count: int = int(record[1])
		if op == "checkpoint":
			var row: Dictionary = trace.checkpoints[phase]
			var state: Dictionary = owner.get_freedom_flight_state()
			check(state.tick == row.tick and state.checksum == row.checksum and state.attached == row.attached and (view is WalkView) == row.walking, "Continuous phase differs: " + row.label)
			var save: String = args[2].path_join(row.label + "-native.json")
			check(owner.save_freedom_as(save) and FileAccess.get_sha256(save) == FileAccess.get_sha256(args[0].path_join(row.file)), "Continuous full Save differs: " + row.label)
			if rendered and not failed: await capture_phase(view, owner, args[2], row, captures)
			print("Continuous phase: %s tick=%s" % [row.label, row.tick])
			phase += 1
		elif op in ["walk", "flight"]:
			if not resume(view): break
			var demand := decoded_values(record[2])
			if op == "walk":
				if not check(view is WalkView and view.advance_requested(float(count) / 120.0, demand), "Continuous walk refused"): break
			else:
				if not check(view is FlightView, "Flight demand reached walking view"): break
				# Replay semantic controller output through the real _process path,
				# actuator mapping, clock, terrain and exhaust update. OS/controller
				# sampling itself remains covered separately, not simulated here.
				view.player_input.axes = PackedFloat64Array([demand[5], demand[2], demand[6] - demand[9], demand[10] - demand[7], demand[11] - demand[8], demand[0] - demand[3], demand[1] - demand[4]])
				if not check(FlightView.actuator_fractions(view.player_input.axes) == demand, "Recorded fractions lost semantic input precision"): break
				view._process(float(count) / 120.0)
			tick += count
			var after: Dictionary = owner.get_freedom_flight_state()
			check(after.tick == str(tick) and view.error.is_empty(), "Native batch dropped ticks or refused: " + view.error)
			if view.mode_change_pending:
				await process_frame
				await process_frame
				check(owner.get_freedom_flight_state() == after, "Handoff frame wait advanced authoritative time")
		elif op in ["board", "unboard"]:
			if view is FlightView: view.player_input.axes.fill(0.0)
			if not resume(view): break
			if op == "board":
				if not check(view is WalkView, "Board action reached flight view"): break
				view.board_requested()
			else:
				if not check(view is FlightView, "Unboard action reached walking view"): break
				view.unboard_requested()
				await process_frame
				await process_frame
			check(owner.get_freedom_boarding_state().get("state") == ("boarding" if op == "board" else "disembarking"), "Native boarding action refused")
		else:
			if not check(view is FlightView, "Port action reached walking view"): break
			view.player_input.axes.fill(0.0)
			if not resume(view): break
			if op == "assistance_off": view.request_assistance(false)
			else: view.port_command({"release": "release_freedom_port", "approach": "begin_freedom_port_approach", "capture": "capture_freedom_port"}[op])
			var committed: Dictionary = owner.get_freedom_flight_state()
			check(view.error.is_empty() and (not committed.assistance if op == "assistance_off" else (not committed.attached if op == "release" else (committed.port_approach.active if op == "approach" else committed.attached))), "Native flight action refused: " + op)
		rows += 1
		if rows % 2000 == 0:
			var frozen: Dictionary = owner.get_freedom_flight_state()
			await process_frame
			check(owner.get_freedom_flight_state() == frozen, "Presentation cadence changed the voyage")
	stream.close()
	check(phase == 11 and str(tick) == trace.final_tick, "Continuous voyage did not complete")
	var report := FileAccess.open(args[2].path_join("voyage.json"), FileAccess.WRITE)
	if report != null:
		var hashes := {}
		for name in ["studies/captures/native_voyage_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_walk_view.gd", "scripts/native/native_flight_view.gd", "scripts/native/native_main_exhaust.gd", "scripts/native/home_navigation.gd", "scripts/ui/player_input.gd", "scripts/world/planet_stream.gd", "studies/captures/native_planetary_capture.gd", "shaders/native_main_exhaust.gdshader", "shaders/native_close_composite.gdshader", "shaders/terrain.gdshader", "bin/libapsis_freedom_bridge.so"]: hashes[name] = FileAccess.get_sha256("res://" + name)
		report.store_string(JSON.stringify({"scope": "One accelerated native session from New Game, semantic input replay; not manual/controller or performance acceptance", "pass": not failed, "rows": rows, "final_tick": str(tick), "model_retained": not failed, "trace_sha256": FileAccess.get_sha256(args[0].path_join("trace.json")), "commands_sha256": FileAccess.get_sha256(args[0].path_join("commands.jsonl")), "sources_sha256": hashes, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if rendered else "headless", "license_records": "assets/native/freedom-starter-01/licenses", "captures": captures}, "\t") + "\n")
		report.close()
	else: check(false, "Could not write continuous voyage report")
	shell.free()
	print("Native continuous voyage: %d failures" % int(failed))
	quit(1 if failed else 0)

func capture_phase(view: Control, owner: Variant, output: String, row: Dictionary, captures: Array) -> void:
	var frozen: Dictionary = owner.get_freedom_flight_state()
	if view is FlightView:
		view.controls_menu.hide_menu()
		view.cockpit = false
		view.update_view(0.0)
		for frame in 3000:
			view.terrain.tick(0.02, view.camera.position)
			await process_frame
			await create_timer(0.02).timeout
			if view.terrain.is_ready or not view.terrain.error.is_empty(): break
		if not check(view.terrain.is_ready and view.terrain.error.is_empty(), "Continuous terrain did not become ready"): return
	for frame in 3:
		await process_frame
		await RenderingServer.frame_post_draw
	check(owner.get_freedom_flight_state() == frozen, "Continuous render advanced time")
	var picture := root.get_texture().get_image()
	var name: String = row.label + ".png"
	if check(picture != null and not picture.is_empty() and picture.save_png(output.path_join(name)) == OK, "Could not save continuous image"):
		captures.append({"label": row.label, "file": name, "sha256": FileAccess.get_sha256(output.path_join(name)), "tick": frozen.tick, "checksum": frozen.checksum, "width": picture.get_width(), "height": picture.get_height(), "terrain": view.terrain.report() if view is FlightView else {}, "exhaust": {"phase": view.exhaust.phase, "main": view.exhaust.intensity, "withdrawal": view.exhaust.withdrawal_intensity} if view is FlightView else {}})
