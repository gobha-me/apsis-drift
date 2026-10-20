extends SceneTree
## Real station Continue, input and unchanged journey. No staged film actors.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const Crew = preload("res://scripts/native/native_station_crew.gd")
var failures := 0

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error(message)
	return value

func _initialize() -> void:
	call_deferred("run")

func save(owner: Variant, path: String) -> PackedByteArray:
	if not check(owner.save_freedom_as(path), "Crew contract Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func key(view: Control, code: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	view._input(event)

func pad(view: Control, device: int, code: int, pressed: bool) -> void:
	var event := InputEventJoypadButton.new()
	event.device = device
	event.button_index = code
	event.pressed = pressed
	# The owning view filters the selected device before GUI dispatch. Forward
	# once here; feeding InputMap too would navigate the same focused button twice.
	view._input(event)

func packet_refusals(packet: Dictionary, walking: Dictionary) -> void:
	for damage in ["version", "version_type", "station", "tick", "ordinal", "short", "nan", "heading", "height", "available", "topics", "duplicate", "topic_text", "extra"]:
		var bad := packet.duplicate(true)
		match damage:
			"version": bad.version = 2
			"version_type": bad.version = 1.0
			"station": bad.station_id = "0"
			"tick": bad.tick = str(int(packet.tick) + 1)
			"ordinal": bad.ordinal = 2
			"short": bad.foot_position_metres = PackedFloat64Array([1, 2])
			"nan": bad.foot_position_metres = PackedFloat64Array([NAN, 0, 0])
			"heading": bad.heading_radians = INF
			"height": bad.height_metres = 2.0
			"available": bad.available = 1
			"topics": bad.topics = []
			"duplicate": bad.topics[1].id = bad.topics[0].id
			"topic_text": bad.topics[0].text = 0
			"extra": bad.extra = true
		check(not Crew.valid_packet(bad, walking), "Malformed crew packet accepted: " + damage)

func asset_refusals(assets: String, output: String) -> void:
	var layout: Variant = JSON.parse_string(FileAccess.get_file_as_string(assets.path_join("crew/idle-frames.json")))
	check(Crew.valid_frames(layout), "Actual selected idle frame registration refused")
	for damage in ["missing", "duplicate", "rect", "fraction", "anchor", "height"]:
		var bad: Dictionary = layout.duplicate(true)
		match damage:
			"missing": bad.views.pop_back()
			"duplicate": bad.views[1].view = "front"
			"rect": bad.views[0].rect[2] = 1537
			"fraction": bad.views[0].rect[0] = 121.5
			"anchor": bad.views[0].anchor = [0]
			"height": bad.height_metres = NAN
		check(not Crew.valid_frames(bad), "Malformed idle frame registration accepted: " + damage)
	var refused := Crew.new()
	check(not refused.configure("relative-assets") and refused.get_child_count() == 0, "Relative crew asset path created a visual")
	var bad_assets := output.path_join("bad-assets")
	check(DirAccess.make_dir_recursive_absolute(bad_assets.path_join("crew")) == OK, "Bad asset fixture directory unavailable")
	var file := FileAccess.open(bad_assets.path_join("crew/casual-male-idle.png"), FileAccess.WRITE)
	if check(file != null, "Bad image fixture unavailable"):
		file.store_string("not a PNG")
		file.close()
	check(not refused.configure(bad_assets) and refused.get_child_count() == 0 and not refused.error.is_empty(), "Unauthenticated image reached the crew decoder/visual")
	refused.free()

func idle_views(view: Control, packet: Dictionary) -> void:
	var actor: Node3D = view.crew_actor
	var origin: Vector3 = actor.position
	var h: float = packet.heading_radians
	var front := Vector3(-sin(h), 0, -cos(h))
	var right := Vector3(cos(h), 0, -sin(h))
	# Pure adapter cameras, not player/camera or C++ pose changes. Verify each
	# original idle drawing and its own sole anchor under the real station basis.
	var views := [["front", front], ["right", right], ["rear", -front], ["left", -right]]
	for entry in views:
		check(actor.present(packet, origin + entry[1] * 3.0 + Vector3.UP * 1.7), "Authored crew idle view refused")
		var frame: Dictionary = actor.frames[entry[0]]
		check(actor.last_view == entry[0] and actor.position == origin and actor.get_parent() == view.station, "Crew idle changed physical root, source view or station parent")
		check(actor.card.region_rect == Rect2(frame.rect[0], frame.rect[1], frame.rect[2], frame.rect[3]) and is_equal_approx(actor.card.pixel_size, 1.85 / 487.0), "Crew idle changed registered source crop/scale")
		check(is_zero_approx(actor.card.position.y + (float(frame.rect[3]) * 0.5 - float(frame.anchor[1])) * actor.card.pixel_size), "Crew idle sole anchor left the source floor datum")
		var normal: Vector3 = actor.plane.transform.basis.z
		check(normal.dot(entry[1]) > 0.999 and actor.plane.rotation.x == 0 and actor.plane.rotation.z == 0, "Crew facing used world-Y or tilted the authored idle plane")
	check(not actor.card.no_depth_test and actor.card.shaded and actor.card.texture_filter == BaseMaterial3D.TEXTURE_FILTER_NEAREST and actor.card.billboard == BaseMaterial3D.BILLBOARD_DISABLED, "Crew art lost depth/nearest/station-frame presentation")
	check(actor.card.texture.get_width() == 1536 and actor.card.texture.get_height() == 1024, "Crew adapter replaced the selected unchanged sheet")
	view.refresh_crew()

func reader_layout(view: Control, owner: Variant, baseline: PackedByteArray, output: String) -> void:
	var old_size := root.size
	for dimensions in [Vector2i(640, 480), Vector2i(1280, 720)]:
		root.size = dimensions
		await process_frame
		await process_frame
		check(view.crew_open() and view.paused and view.crew_reader.readable_theme.default_font_size >= 20, "Crew reader resize lost paused readable modal")
		for button in view.crew_reader.topics + [view.crew_reader.back]:
			check(button.custom_minimum_size.y >= 44 and button.get_global_rect().size.x > 0 and button.get_global_rect().end.x <= view.size.x + 1.0, "Crew reader controls escaped resized viewport")
	root.size = old_size
	await process_frame
	check(save(owner, output.path_join("reader-resized.json")) == baseline, "Crew reader resize changed complete journey")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	var source := args[0]
	var assets := args[1]
	var output := args[2]
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Crew output directory unavailable"): quit(1); return
	var source_bytes := FileAccess.get_file_as_bytes(source)
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Crew native bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Crew source Continue failed: " + shell.error): shell.free(); quit(1); return
	var view: Control = shell.current_view
	view.set_process(false)
	view.set_process_input(false)
	view.preferences_provider.persist = false
	var packet: Dictionary = owner.get_freedom_station_crew()
	if not check(Crew.valid_packet(packet, view.state) and view.paused and packet.name == "Bay attendant" and packet.foot_position_metres == PackedFloat64Array([1.3, 0, 0]), "Actual station crew binding unavailable"): shell.free(); quit(1); return
	packet_refusals(packet, view.state)
	asset_refusals(assets, output)
	idle_views(view, packet)
	var initial := save(owner, output.path_join("continued.json"))
	check(initial == source_bytes, "Station crew staging/reading changed source Save")
	view.request_crew_conversation()
	check(not view.crew_open() and save(owner, output.path_join("paused-refusal.json")) == initial, "Paused interaction changed state or opened crew")
	view.resume_requested()
	# Actual mouse-look input turns the existing walker to face the attendant.
	# A real neutral walking batch commits that heading at the shared clock.
	var motion := InputEventMouseMotion.new()
	motion.relative = Vector2(PI / (0.003 * float(view.preferences_provider.settings.look_speed) / 1.6), 0)
	view._input(motion)
	view._process(0.1)
	packet = owner.get_freedom_station_crew()
	if not check(packet.available and not view.paused and view.crew_actor.visible and "Talk to" in view.boarding_prompt.text, "Real mouse turn/neutral tick did not make the nearby crew reachable"): shell.free(); quit(1); return
	check(view.crew_actor.bubble.visible and view.crew_actor.bubble.text == packet.remark and view.crew_remark_tick == int(packet.tick), "Actual visible nearby crew did not emit its source-bound passing remark")
	var first_remark: int = view.crew_remark_tick
	for heading in [PI / 2, -PI / 2]:
		check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, 0, heading])), "Ordinary neutral crew visibility turn refused")
		view.update_view()
	check(view.crew_remark_tick == first_remark, "Immediate visible proximity reentry bypassed the 30-second shared-clock chatter cooldown")
	packet = owner.get_freedom_station_crew()
	var before: Dictionary = owner.get_freedom_walk_state()
	var frozen := save(owner, output.path_join("before-talk.json"))
	var remark_tick: int = view.crew_remark_tick
	var remark: String = view.crew_actor.bubble.text
	for repeat in 3: view.update_view()
	check(view.crew_remark_tick == remark_tick and view.crew_actor.bubble.text == remark and save(owner, output.path_join("remark-read.json")) == frozen, "Repeated crew display spammed chatter or advanced journey")
	# Held custom movement, modal and focus must retain ordinary neutral gates.
	var settings: Dictionary = view.preferences_provider.settings.duplicate(true)
	var custom := settings.duplicate(true)
	custom.merge({"walk_forward": KEY_T, "walk_backward": KEY_G, "walk_left": KEY_F, "walk_right": KEY_H}, true)
	check(view.preferences_provider.apply_preferences(custom), "Crew custom walking keys refused")
	key(view, KEY_T, true)
	view.request_crew_conversation()
	check(not view.crew_open(), "Held custom movement opened conversation")
	key(view, KEY_T, false)
	check(view.preferences_provider.apply_preferences(settings), "Crew walking preferences restore refused")
	view.observe_neutral_controls()
	view.save_dialog.show()
	view.request_crew_conversation()
	check(not view.crew_open(), "Save modal permitted crew interaction")
	view.save_dialog.hide()
	view.journey_dialog_open = true
	view.request_crew_conversation()
	check(not view.crew_open(), "Journey modal permitted crew interaction")
	view.journey_dialog_open = false
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.request_crew_conversation()
	check(view.paused and not view.crew_open() and not view.crew_actor.bubble.visible, "Focus loss retained crew input/bubble authority")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.resume_requested()
	key(view, KEY_E, true)
	key(view, KEY_E, false)
	if not check(view.crew_open() and view.paused and not view.mouse_look.active and not view.hud_scroll.visible, "Actual E did not open optional paused conversation"): shell.free(); quit(1); return
	for topic in packet.topics:
		view.select_crew_topic(topic.id)
		check(view.crew_reader.text.text == topic.text, "Crew reader did not present the actual selected C++ topic")
	var text: String = view.crew_reader.text.text
	view.select_crew_topic(999)
	check(view.crew_reader.text.text == text, "Unknown topic changed accepted dialogue")
	view.resume_requested()
	view._process(0.5)
	check(view.crew_open() and view.paused and not view.crew_actor.bubble.visible and owner.get_freedom_walk_state() == before, "Conversation resumed or stepped the underlying player")
	await reader_layout(view, owner, frozen, output)
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.select_crew_topic(packet.topics[0].id)
	view.crew_reader.back.pressed.emit()
	check(view.crew_open() and view.crew_reader.text.text == text, "Unfocused reader accepted topic/close")
	view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
	key(view, KEY_ESCAPE, true)
	key(view, KEY_ESCAPE, false)
	check(not view.crew_open() and view.paused and not view.controls_armed, "Escape closed dialogue and auto-resumed walking")
	key(view, KEY_W, true)
	view.resume_requested()
	check(view.paused, "Held movement bypassed conversation Resume")
	key(view, KEY_W, false)
	view.resume_requested()
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	view._input(click)
	check(view.crew_open(), "Captured contextual left click did not open crew")
	view.close_crew_conversation()
	Input.joy_connection_changed.emit(0, true)
	Input.joy_connection_changed.emit(7, true)
	pad(view, 0, JOY_BUTTON_START, true)
	pad(view, 0, JOY_BUTTON_START, false)
	view.resume_requested()
	pad(view, 7, JOY_BUTTON_A, true)
	pad(view, 7, JOY_BUTTON_A, false)
	check(not view.crew_open() and view.selected_pad == 0, "Foreign controller opened crew or stole selection")
	pad(view, 0, JOY_BUTTON_A, true)
	pad(view, 0, JOY_BUTTON_A, false)
	check(view.crew_open(), "Selected controller A did not open crew")
	pad(view, 0, JOY_BUTTON_DPAD_DOWN, true)
	pad(view, 0, JOY_BUTTON_DPAD_DOWN, false)
	pad(view, 0, JOY_BUTTON_A, true)
	pad(view, 0, JOY_BUTTON_A, false)
	check(view.crew_reader.text.text == packet.topics[1].text, "Selected D-pad/A did not choose actual second topic")
	pad(view, 7, JOY_BUTTON_B, true)
	pad(view, 7, JOY_BUTTON_B, false)
	check(view.crew_open(), "Foreign controller closed reader")
	pad(view, 0, JOY_BUTTON_B, true)
	pad(view, 0, JOY_BUTTON_B, false)
	var conversation_unchanged := save(owner, output.path_join("after-talk.json")) == frozen
	check(not view.crew_open() and view.paused and conversation_unchanged, "Controller close changed journey or auto-resumed")
	Input.joy_connection_changed.emit(7, false)
	Input.joy_connection_changed.emit(0, false)
	# Actual Continue restores the same actor/clock/crew, with no conversation
	# progression. Then traverse the unchanged route and board through C++.
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("after-talk.json"), "assets": assets}), "Talk Save Continue refused"): shell.free(); quit(1); return
	view = shell.current_view
	view.set_process(false)
	view.set_process_input(false)
	check(view.paused and not view.crew_open() and save(owner, output.path_join("talk-continued.json")) == frozen and owner.get_freedom_station_crew() == packet, "Crew Save Continue changed world or invented dialogue progression")
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Original station route reset refused"): shell.free(); quit(1); return
	view = shell.current_view
	view.set_process(false)
	view.set_process_input(false)
	var walk_ticks := 0
	for tick in 1194:
		if not check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, -1, 0])), "Crew obstructed original supported route"): break
		walk_ticks += 1
	# Direct C++ replay does not advance the view's 0.1 s presentation timers.
	view.boarding_query_elapsed = 0.1
	view.crew_query_elapsed = 0.1
	view.update_view()
	check(view.boarding_availability.available and not owner.get_freedom_station_crew().available and not view.crew_actor.bubble.visible, "D1 arrival lost boarding priority or retained remote chatter")
	view.resume_requested()
	key(view, KEY_E, true)
	key(view, KEY_E, false)
	check(view.boarding_transition() and not view.crew_open() and not view.crew_actor.visible, "D1 contextual input failed boarding/crew exclusion")
	var boarding_ticks := 0
	for tick in 1440:
		if not check(owner.advance_freedom_walk(1.0 / 120.0, PackedFloat64Array([0, 0, 0])), "Original boarding tick refused with crew selected"): break
		boarding_ticks += 1
	check(owner.get_freedom_walk_state().is_empty() and owner.get_freedom_boarding_state().state == "seated" and owner.get_freedom_station_crew().is_empty(), "Crew remained active after actual original seat transition")
	check(not save(owner, output.path_join("seated.json")).is_empty() and FileAccess.get_file_as_bytes(source) == source_bytes, "Crew route failed complete Save or rewrote source")
	var report := {"schema": "apsis.station-crew-native/1", "failures": failures, "crew_ordinal": packet.ordinal, "station_id": packet.station_id, "interaction_tick": packet.tick, "conversation_zero_time": conversation_unchanged, "topics": packet.topics, "original_walk_ticks": walk_ticks, "original_board_ticks": boarding_ticks, "scope": "Headless software input/real native owner; no GPU or physical-controller acceptance"}
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "Crew report unavailable"):
		file.store_string(JSON.stringify(report, "\t") + "\n")
		file.close()
	shell.free()
	await process_frame
	print("Native station crew: %d failures" % failures)
	quit(0 if failures == 0 else 1)
