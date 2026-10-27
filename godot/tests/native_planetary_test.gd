extends "res://studies/captures/native_planetary_capture.gd"

const Navigation = preload("res://scripts/native/home_navigation.gd")

func tracking_save(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Planet tracking Save failed: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func tracking_refusals(packet: Dictionary, state: Dictionary, targets: Dictionary) -> void:
	for field in ["version", "selected", "available", "system_id", "planet_id", "reference_planet_id", "craft_id", "craft_frame_id", "craft_frame_version", "world_frame", "world_body_id", "tick", "name", "position_metres", "range_metres", "note"]:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check(not FlightView.valid_planet_tracking(missing, state), "Missing tracking field accepted: " + field)
	for damage in ["extra", "version_float", "bool", "id", "foreign", "reference", "owner", "recipe", "frame_version", "frame", "frame_stringname", "world_body", "tick", "name", "control", "note", "array", "short", "nan", "infinite", "zero", "range_mismatch", "vector_mismatch", "coincident", "unavailable_leak"]:
		var bad := packet.duplicate(true)
		match damage:
			"extra": bad.extra = true
			"version_float": bad.version = 1.0
			"bool": bad.selected = 1
			"id": bad.planet_id = "01"
			"foreign": bad.system_id = "1" if state.system_id != "1" else "2"
			"reference": bad.reference_planet_id = "1" if state.planet_id != "1" else "2"
			"owner": bad.craft_id = "1" if state.craft_id != "1" else "2"
			"recipe": bad.craft_frame_id = "9"
			"frame_version": bad.craft_frame_version = 4294967296
			"frame": bad.world_frame = "body_fixed"
			"frame_stringname": bad.world_frame = StringName(packet.world_frame)
			"world_body": bad.world_body_id = "1" if packet.world_body_id != "1" else "2"
			"tick": bad.tick = "0" if state.tick != "0" else "1"
			"name": bad.name = "é".repeat(129)
			"control": bad.name = "Planet\nInjected line"
			"note": bad.note = ""
			"array": bad.position_metres = Vector3(1, 2, 3)
			"short": bad.position_metres = PackedFloat64Array([1, 2])
			"nan": bad.position_metres[0] = NAN
			"infinite": bad.range_metres = INF
			"zero": bad.range_metres = 0.0
			"range_mismatch": bad.range_metres *= 2.0
			"vector_mismatch":
				for index in 3: bad.position_metres[index] *= 2.0
			"coincident": bad.position_metres = PackedFloat64Array([0, 0, 0])
			"unavailable_leak":
				bad.available = false
				bad.selected = false
		check(not FlightView.valid_planet_tracking(bad, state), "Malformed tracking reading accepted: " + damage)
	for damage in ["extra", "version", "system", "tick", "overflow", "row_extra", "duplicate", "name", "id"]:
		var bad := targets.duplicate(true)
		match damage:
			"extra": bad.extra = true
			"version": bad.version = 1.0
			"system": bad.system_id = "0"
			"tick": bad.tick = "0" if state.tick != "0" else "1"
			"overflow":
				while bad.targets.size() < 7: bad.targets.append(bad.targets[0].duplicate(true))
			"row_extra": bad.targets[0].extra = true
			"duplicate": bad.targets.append(bad.targets[0].duplicate(true))
			"name": bad.targets[0].name = ""
			"id": bad.targets[0].planet_id = "18446744073709551616"
		check(not FlightView.valid_planet_tracking_targets(bad, state), "Malformed known target list accepted: " + damage)
	for damage in ["walking", "jump", "loss", "companion"]:
		var bad_state := state.duplicate(true)
		match damage:
			"walking": bad_state.surface_walk = {"supported": true}
			"jump": bad_state.jump.phase = "spool"
			"loss": bad_state.recovery_pending = true
			"companion": bad_state.body_kind = "moon"
		check(not FlightView.valid_planet_tracking(packet, bad_state), "Unavailable tracking context accepted: " + damage)

func tracking_contract(args: PackedStringArray) -> void:
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Tracking native owner unavailable"): return
	var directory: String = args[2].path_join("known-planet-tracking")
	if not check(DirAccess.make_dir_recursive_absolute(directory) == OK, "Tracking proof directory unavailable"): return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.size = Vector2i(1280, 720)
	var source: String = args[0].path_join("seated.json")
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": args[1]}), "Tracking seated source refused"): shell.free(); return
	var view: Control = shell.current_view
	freeze(view)
	var before: Dictionary = owner.get_freedom_flight_state()
	var baseline := tracking_save(owner, directory.path_join("seated-before.json"))
	view.pause_controls("Review known-planet HUD tracking.")
	var targets: Dictionary = owner.get_freedom_planet_tracking_targets()
	if not check(FlightView.valid_planet_tracking_targets(targets, before) and not targets.targets.is_empty(), "Genuine planetary source omitted its admitted current planet"): shell.free(); return
	var index := -1
	for i in targets.targets.size():
		if targets.targets[i].planet_id == before.planet_id: index = i
	if not check(index >= 0 and view.planet_tracking_buttons[index].visible and not view.planet_tracking_buttons[index].disabled, "Current planet was gated by jump selectability"): shell.free(); return
	view.focused = false
	view.planet_tracking_buttons[index].pressed.emit()
	check(not owner.get_freedom_planet_tracking().selected, "Unfocused planet tracking action changed selection")
	view.focused = true
	view.journey_dialog_open = true
	view.planet_tracking_buttons[index].pressed.emit()
	check(not owner.get_freedom_planet_tracking().selected, "Journey modal accepted a hidden tracking action")
	view.journey_dialog_open = false
	view.planet_tracking_buttons[index].pressed.emit()
	var packet: Dictionary = owner.get_freedom_planet_tracking()
	if not check(FlightView.valid_planet_tracking(packet, before) and packet.available and packet.planet_id == before.planet_id, "Real paused current-planet tracking failed"): shell.free(); return
	check(view.hud_tracking_mode == "planet" and view.home_marker.visible and view.home_marker.display_name == packet.name + " · centre" and view.home_marker.detail_text.begins_with(Navigation.range_text(packet.range_metres)), "HUD lost C++ name/body-centre F64 range")
	tracking_refusals(packet, before, targets)
	for repeat in 3: view.update_view(0.0)
	check(owner.get_freedom_flight_state() == before and tracking_save(owner, directory.path_join("seated-tracked.json")) == baseline, "Tracking read/selection changed complete knowledge, jump, resources or Save")
	for mode in ["home", "port", "off"]:
		view.planet_tracking_buttons[index].pressed.emit()
		var button: Button = view.home_tracking_button if mode == "home" else view.port_tracking_button if mode == "port" else view.clear_tracking_button
		button.pressed.emit()
		check(not owner.get_freedom_planet_tracking().selected and view.hud_tracking_mode == mode, "Origin/port/off retained a C++ planet selection: " + mode)
	check(tracking_save(owner, directory.path_join("seated-cleared.json")) == baseline, "Clearing tracking changed complete Save")
	# A disclosed resource boundary, not a flown depletion/earning claim. Keep
	# every identifier, pose, clock and knowledge byte outside these two scalars.
	var resource_text: String = FileAccess.get_file_as_string(source)
	var original_resource_text: String = resource_text
	for field in ["flight_quanta", "jump_charges"]:
		var pattern := RegEx.new()
		pattern.compile('"' + field + '"\\s*:\\s*[0-9]+')
		if not check(pattern.search_all(resource_text).size() == 1, "Zero-resource fixture requires one unambiguous scalar: " + field): shell.free(); return
		resource_text = pattern.sub(resource_text, '"' + field + '":0', false)
	var original_mask: String = original_resource_text
	var boundary_mask: String = resource_text
	for field in ["flight_quanta", "jump_charges"]:
		var pattern := RegEx.new()
		pattern.compile('"' + field + '"\\s*:\\s*[0-9]+')
		original_mask = pattern.sub(original_mask, '"' + field + '":RESOURCE_BOUNDARY', false)
		boundary_mask = pattern.sub(boundary_mask, '"' + field + '":RESOURCE_BOUNDARY', false)
	if not check(original_mask == boundary_mask, "Resource boundary changed identifier, pose, tick, knowledge or other raw Save data"): shell.free(); return
	var zero_path: String = directory.path_join("initialized-zero-resources.json")
	var file := FileAccess.open(zero_path, FileAccess.WRITE)
	if not check(file != null, "Resource boundary output unavailable"): shell.free(); return
	file.store_string(resource_text); file.close()
	if not check(shell.select_start(owner, {"mode": "continue", "value": zero_path, "assets": args[1]}), "Typed zero-resource boundary failed C++ admission"): shell.free(); return
	view = shell.current_view
	freeze(view)
	view.pause_controls("Resource boundary tracking check.")
	var zero_before := tracking_save(owner, directory.path_join("zero-before.json"))
	var zero: Dictionary = owner.get_freedom_flight_state()
	check(zero.resources.jump_charges == 0 and zero.resources.fraction == 0.0, "Disclosed fixture did not initialize zero resources")
	view.planet_tracking_buttons[0].pressed.emit()
	check(owner.get_freedom_planet_tracking().available and view.hud_tracking_mode == "planet" and tracking_save(owner, directory.path_join("zero-tracked.json")) == zero_before, "Zero fuel/charges gated pure tracking or tracking billed resources")
	# Reset/Continue uses genuine sources, and a real moving fixed-step reading.
	var moving: String = args[0].path_join("departed.json")
	if not check(shell.select_start(owner, {"mode": "continue", "value": moving, "assets": args[1]}), "Moving tracking source refused"): shell.free(); return
	view = shell.current_view
	freeze(view)
	check(not owner.get_freedom_planet_tracking().selected and view.hud_tracking_mode == "home", "Continue inherited a transient target")
	view.pause_controls("Moving current-planet tracking.")
	view.planet_tracking_buttons[0].pressed.emit()
	var old: Dictionary = owner.get_freedom_planet_tracking()
	if not check(FlightView.valid_planet_tracking(old, owner.get_freedom_flight_state()) and old.available, "Moving source tracking was unavailable before advancement"): shell.free(); return
	var neutral := PackedFloat64Array()
	neutral.resize(12)
	for tick in 3: check(owner.advance_freedom_flight(1.0 / 120.0, neutral, false), "Actual neutral moving tracking tick refused")
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	var current: Dictionary = owner.get_freedom_planet_tracking()
	check(current.available and FlightView.valid_planet_tracking(current, view.state) and current.tick != old.tick and current.position_metres != old.position_metres and not FlightView.valid_planet_tracking(old, view.state), "Moving HUD retained stale tick/vector or refused the fresh actual reading")
	var moving_before := tracking_save(owner, directory.path_join("moving-before-refusal.json"))
	check(not owner.select_freedom_planet_tracking(current.system_id, current.planet_id, old.tick), "Stale tracking token was accepted")
	check(not owner.select_freedom_planet_tracking("1" if current.system_id != "1" else "2", current.planet_id, current.tick), "Foreign tracking identity was accepted")
	check(owner.get_freedom_planet_tracking() == current and tracking_save(owner, directory.path_join("moving-after-refusal.json")) == moving_before, "Refused stale/foreign tracking changed target or Save")
	view.clear_tracking_button.pressed.emit()
	check(not owner.get_freedom_planet_tracking().available and not view.home_marker.visible, "Clearing moving tracking retained stale geometry")
	check(shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("station-start.json"), "assets": args[1]}), "Walking reset source refused")
	check(not owner.get_freedom_planet_tracking().available, "Walking retained a flight target")
	check(FileAccess.get_file_as_string(source) == original_resource_text, "Tracking tests rewrote the genuine seated source")
	var report := FileAccess.open(directory.path_join("report.json"), FileAccess.WRITE)
	if check(report != null, "Planet tracking report unavailable"):
		report.store_string(JSON.stringify({"schema": "apsis.known-planet-tracking-native/1", "completed": not failed, "source_sha256": FileAccess.get_sha256(source), "source_unchanged": FileAccess.get_file_as_string(source) == original_resource_text, "resource_boundary_only": original_mask == boundary_mask, "current_planet_id": before.planet_id, "moving_before": old, "moving_after": current, "scope": "Actual paused commands/reads, real moving ticks, Save-neutral tracking. Zero resources explicitly initialized on a copy; no flown depletion, route clearance, or manual/GPU acceptance."}, "\t") + "\n")
		report.close()
	shell.free()
	await process_frame

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	await tracking_contract(args)
	if failed: quit(1); return
	await super.run()
