extends SceneTree
## Real station Continue, input and unchanged journey. No staged film actors.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const Crew = preload("res://scripts/native/native_station_crew.gd")
const Notes = preload("res://scripts/native/exploration_notes.gd")
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
	for damage in ["version", "version_type", "future_version", "station", "tick", "ordinal", "short", "nan", "heading", "height", "available", "topics", "two_topics", "six_topics", "duplicate", "topic_id", "topic_text", "extra"]:
		var bad := packet.duplicate(true)
		match damage:
			"version": bad.version = 1
			"version_type": bad.version = 3.0
			"future_version": bad.version = 4
			"station": bad.station_id = "0"
			"tick": bad.tick = str(int(packet.tick) + 1)
			"ordinal": bad.ordinal = 2
			"short": bad.foot_position_metres = PackedFloat64Array([1, 2])
			"nan": bad.foot_position_metres = PackedFloat64Array([NAN, 0, 0])
			"heading": bad.heading_radians = INF
			"height": bad.height_metres = 2.0
			"available": bad.available = 1
			"topics": bad.topics = []
			"two_topics": bad.topics.resize(2)
			"six_topics":
				while bad.topics.size() < 6: bad.topics.append({"id": bad.topics.size() + 1, "title": "Extra", "text": "Out of scope"})
			"duplicate": bad.topics[1].id = bad.topics[0].id
			"topic_id": bad.topics[0].id = 1.0
			"topic_text": bad.topics[0].text = 0
			"extra": bad.extra = true
		check(not Crew.valid_packet(bad, walking), "Malformed crew packet accepted: " + damage)

func topic_layouts(packet: Dictionary, walking: Dictionary) -> void:
	for optional_ids in [[], [4], [5], [4, 5]]:
		var candidate := packet.duplicate(true)
		candidate.topics = candidate.topics.slice(0, 3)
		for id in optional_ids: candidate.topics.append({"id": id, "title": "Synthetic bounded topic", "text": "Presentation validation only."})
		check(Crew.valid_packet(candidate, walking), "Independent optional topic IDs refused: " + str(optional_ids))
	for ids in [[5, 4], [4, 4], [6], [0]]:
		var candidate := packet.duplicate(true)
		candidate.topics = candidate.topics.slice(0, 3)
		for id in ids: candidate.topics.append({"id": id, "title": "Synthetic invalid topic", "text": "Presentation validation only."})
		check(not Crew.valid_packet(candidate, walking), "Malformed optional topic order/identity accepted: " + str(ids))

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

func reader_layout(view: Control, owner: Variant, baseline: PackedByteArray, output: String, label: String = "", existing_save: String = "") -> void:
	var old_size := root.size
	var reader: Control = view.crew_reader
	var original_text: String = reader.text.text
	for dimensions in [Vector2i(640, 450), Vector2i(1280, 720)]:
		root.size = dimensions
		for frame in 4: await process_frame
		var card: Rect2 = reader.panel.get_global_rect()
		var bounds := Rect2(Vector2.ZERO, view.size)
		var detail := "window=%s logical=%s card=%s reply=%s choices=%s" % [dimensions, view.size, card, reader.response_scroll.get_global_rect(), reader.scroll.get_global_rect()]
		check(view.crew_open() and view.paused and bounds.encloses(card) and card.position.x > view.size.x * 0.5 and card.size.x <= view.size.x * 0.44 + 1.0 and card.size.y <= view.size.y * 0.8 + 1.0, "Crew card covers centre/world or escapes viewport: " + detail)
		check(card.size.x * dimensions.x / view.size.x <= 561.0 and reader.text.get_theme_font_size("font_size") * dimensions.x / view.size.x >= 19.5, "Crew card lost its readable physical width/font: " + detail)
		for child in reader.get_children():
			if child is ColorRect and child.visible:
				check(child.color.a <= 0.15 or not child.get_global_rect().has_point(view.size * 0.5), "Crew conversation restored an opaque screen-wide shade")
		var face: Vector3 = view.crew_actor.global_transform * Vector3(0, 1.65, 0)
		var face_pixel: Vector2 = view.camera.unproject_position(face) * view.size / Vector2(view.viewport.size)
		check(view.crew_actor.visible and not view.camera.is_position_behind(face) and bounds.has_point(face_pixel) and not card.has_point(face_pixel), "Actual attendant face was hidden by the conversation: " + detail + " face=" + str(face_pixel))
		var fixed_title: Rect2 = reader.title.get_global_rect()
		var fixed_back: Rect2 = reader.back.get_global_rect()
		check(card.encloses(fixed_title) and card.encloses(fixed_back), "Conversation name/Back escaped the fixed card")
		check(reader.response_scroll.get_global_rect().size.y > 0 and reader.scroll.get_global_rect().size.y > 0 and reader.text.text == original_text, "Reply/choice panes lost space or changed actual C++ text")
		reader.reply_down.grab_focus()
		reader.accept()
		await process_frame
		var maximum: float = reader.response_scroll.get_v_scroll_bar().max_value - reader.response_scroll.get_v_scroll_bar().page
		var reply_position: int = reader.response_scroll.scroll_vertical
		check(maximum < 1.0 or reply_position > 0, "Long C++ reply could not scroll through the controller-accessible page action")
		for button in reader.topics + [reader.keep] + reader.services + [reader.reply_up, reader.reply_down, reader.back]:
			if not button.visible: continue
			button.grab_focus()
			await process_frame
			await process_frame
			var clip: Rect2 = card if button in [reader.reply_up, reader.reply_down, reader.back] else reader.scroll.get_global_rect()
			var rect: Rect2 = button.get_global_rect()
			check(button.has_focus() and button.size.y * dimensions.y / view.size.y >= 43.5 and rect.size.x > 0 and rect.position.x >= clip.position.x - 1.0 and rect.end.x <= clip.end.x + 1.0 and rect.position.y >= clip.position.y - 1.0 and rect.end.y <= clip.end.y + 1.0, "Focused crew control escaped its pane: " + detail + " control=" + button.text + " rect=" + str(rect))
			check(reader.title.get_global_rect() == fixed_title and reader.back.get_global_rect() == fixed_back and reader.response_scroll.scroll_vertical == reply_position, "Topic focus moved the speaker/Back or reset the reply scroll")
		reader.reply_up.grab_focus()
		reader.accept()
		check(reader.response_scroll.scroll_vertical <= reply_position and reader.text.text == original_text, "Reading earlier reply lines changed the C++ response")
		reader.response_scroll.scroll_vertical = 0
	root.size = old_size
	for frame in 4: await process_frame
	var proof_path: String = output.path_join(label + "reader-resized.json") if existing_save.is_empty() else existing_save
	check(save(owner, proof_path) == baseline, "Crew reader resize/scroll changed complete journey")

func stop_talk_regressions(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	# Start from real Continue each time, then move with physical input. These
	# cases deliberately do not insert the old test's neutral frame before E.
	var interaction := output.path_join("interaction")
	if not check(DirAccess.make_dir_recursive_absolute(interaction) == OK, "Stop-talk proof directory unavailable"): return
	for scenario in ["key", "click", "movement", "pause", "focus", "save_modal", "journey_modal"]:
		if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Stop-talk Continue refused: " + scenario): return
		var view: Control = shell.current_view
		view.set_process(false)
		view.set_process_input(false)
		view.resume_requested()
		var motion := InputEventMouseMotion.new()
		var sensitivity := 0.003 * float(view.preferences_provider.settings.look_speed) / 1.6
		motion.relative = Vector2(PI / sensitivity, (0.35 if scenario == "click" else -0.35) / sensitivity)
		view._input(motion)
		key(view, KEY_W, true)
		view.crew_query_elapsed = 0.1
		view._process(0.025)
		var moving: Dictionary = owner.get_freedom_walk_state()
		check(moving.velocity_metres_per_second != PackedFloat64Array([0, 0, 0]) and not view.crew_packet.available, "Stop-talk fixture did not actually move or retain unavailable cached crew: " + scenario)
		key(view, KEY_E, true)
		key(view, KEY_E, false)
		check(not view.crew_open() and view.pending_crew_talk.is_empty() and view.active_card.visible and "Release walking" in view.active_status.text, "Held Talk did not refuse visibly: " + scenario)
		key(view, KEY_W, false)
		var path := interaction.path_join("stop-talk-" + scenario + ".json")
		var moving_save := save(owner, path)
		if scenario == "click":
			var click := InputEventMouseButton.new()
			click.button_index = MOUSE_BUTTON_LEFT
			click.pressed = true
			view._input(click)
		else:
			key(view, KEY_E, true)
			key(view, KEY_E, false)
		check(not view.crew_open() and not view.pending_crew_talk.is_empty() and "Stopping to talk" in view.active_status.text and save(owner, path) == moving_save, "Immediate released Talk was lost, invisible or stepped the owner: " + scenario)
		match scenario:
			"movement":
				# A press and release between frames must still cancel the request.
				key(view, KEY_W, true)
				key(view, KEY_W, false)
			"pause":
				view.pause_controls("Stop-talk cancellation")
				view.resume_requested()
			"focus":
				view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
				view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
				view.resume_requested()
			"save_modal": view.save_dialog.show()
			"journey_modal": view.journey_dialog_open = true
		if scenario in ["save_modal", "journey_modal"]:
			view._process(0.0)
			check(view.pending_crew_talk.is_empty(), "Modal retained pending Talk: " + scenario)
			view.save_dialog.hide()
			view.journey_dialog_open = false
		var tick := int(owner.get_freedom_walk_state().tick)
		view._process(0.1)
		if scenario in ["key", "click"]:
			check(view.crew_open() and view.paused and view.pending_crew_talk.is_empty() and not view.mouse_look.active, "Released Talk did not open after the ordinary stopping step: " + scenario)
			var stopped: Dictionary = owner.get_freedom_walk_state()
			check(stopped.velocity_metres_per_second == PackedFloat64Array([0, 0, 0]) and stopped.foot_position_metres == moving.foot_position_metres and int(stopped.tick) - tick == 12, "Deferred Talk injected movement or extra ticks: " + scenario)
			var stopped_save := save(owner, path)
			view._process(0.5)
			check(save(owner, path) == stopped_save, "Conversation advanced the stopped complete journey: " + scenario)
		else:
			check(not view.crew_open() and view.pending_crew_talk.is_empty(), "Canceled Talk opened later: " + scenario)
			view._process(0.1)
			check(not view.crew_open(), "Canceled Talk survived a second ordinary frame: " + scenario)
			save(owner, path)
	print("Station stop-talk regressions: 7 scenarios checked")

func mouse_topic(button: Button) -> void:
	# Entry can construct/show this reader and invoke the test in one frame.
	# Let its wrapped text and panes settle before focus-follow scroll uses the
	# button rectangle; otherwise the later centre can lie below the pane.
	await process_frame
	await process_frame
	button.grab_focus()
	await process_frame
	await process_frame
	var pane: ScrollContainer = button.get_parent().get_parent() as ScrollContainer
	var rect: Rect2 = button.get_global_rect()
	var clip: Rect2 = pane.get_global_rect()
	var detail := "viewport=%s button=%s rect=%s pane=%s scroll=%d focus=%s" % [root.get_visible_rect(), button.text, rect, clip, pane.scroll_vertical, button.has_focus()]
	if not check(button.is_visible_in_tree() and not button.disabled and button.has_focus() and rect.has_area() and clip.grow(1.0).encloses(rect) and root.get_visible_rect().encloses(rect), "Physical crew mouse target is clipped or unfocused after layout: " + detail): return
	for down in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.position = button.get_global_rect().get_center()
		event.global_position = event.position
		event.pressed = down
		# Button rectangles use logical viewport coordinates; Input's global
		# parser expects physical window coordinates under canvas_items stretch.
		root.push_input(event, true)
		await process_frame

func earned_contact_topics(shell: Control, owner: Variant, phases: String, assets: String, output: String, original_topics: Array) -> void:
	# These are genuine earned-record + classified-recovery producer saves.
	# Only real mouse look and ordinary neutral walking ticks face the worker.
	var rows: Array = []
	var responses: Array[String] = []
	for label in ["contact-recovered", "recovered"]:
		var source := phases.path_join(label + ".json")
		var source_hash := FileAccess.get_sha256(source)
		var source_bytes := FileAccess.get_file_as_bytes(source)
		var prefix := output.path_join("earned-" + label)
		# Keep one player shell, as in normal play. A second live shell would
		# retain device callbacks and steal GUI focus on controller connection.
		if not check(not source_bytes.is_empty() and shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Earned crew recovery Continue failed: " + label):
			return
		var view: Control = shell.current_view
		view.set_process(false)
		view.set_process_input(false)
		view.preferences_provider.persist = false
		if not check(view.paused and save(owner, prefix + "-source.json") == source_bytes, "Earned crew Continue changed complete source Save: " + label):
			return
		var record: Dictionary = owner.get_freedom_flight_state().derelict
		check(record.known and record.inspected == (label == "recovered"), "Crew fixture lacks the actual retained contact/inspection distinction: " + label)
		var walking: Dictionary = owner.get_freedom_walk_state()
		var packet: Dictionary = owner.get_freedom_station_crew()
		if not check(Crew.valid_packet(packet, walking) and packet.topics.size() in [4, 5] and packet.topics[3].id == 4 and packet.topics[3].title == "Can you look at my contact log?", "Earned contact omitted the explicit fourth topic: " + label):
			return
		packet_refusals(packet, walking)
		var response: String = packet.topics[3].text
		if record.inspected:
			check("no active power or thrust" in response and "unreadable registry plate" in response and "Ownership and whether anyone is aboard remain unknown" in response, "Inspected crew response lost recorded findings or invented ownership")
		else:
			check("Your log records a contact" in response and "Inspect derelict exterior" in response and not "no active power or thrust" in response, "Contact-only crew response invented inspection findings or omitted the manual next action")
		for index in 3: check(packet.topics[index] == original_topics[index], "Earned contact changed an original crew topic: " + str(index))
		var delta: Vector2 = Vector2(packet.foot_position_metres[0] - walking.foot_position_metres[0], packet.foot_position_metres[2] - walking.foot_position_metres[2])
		var heading := atan2(-delta.x, -delta.y)
		view.resume_requested()
		var motion := InputEventMouseMotion.new()
		var sensitivity := 0.003 * float(view.preferences_provider.settings.look_speed) / 1.6
		motion.relative = Vector2(wrapf(float(walking.heading_radians) - heading, -PI, PI) / sensitivity, 0)
		view._input(motion)
		view._process(0.1)
		var faced: Dictionary = owner.get_freedom_walk_state()
		packet = owner.get_freedom_station_crew()
		if not check(not view.paused and packet.available and faced.foot_position_metres == walking.foot_position_metres and int(faced.tick) == int(walking.tick) + 12, "Actual neutral walking/mouse look did not make recovered attendant eligible: " + label):
			return
		var frozen := save(owner, prefix + "-before.json")
		var before: Dictionary = owner.get_freedom_flight_state()
		key(view, KEY_E, true)
		key(view, KEY_E, false)
		if not check(view.crew_open() and view.paused, "Real recovered crew input failed to open paused reader: " + label):
			return
		if label == "contact-recovered":
			for index in 3:
				key(view, KEY_DOWN, true)
				key(view, KEY_DOWN, false)
			key(view, KEY_ENTER, true)
			key(view, KEY_ENTER, false)
		else:
			Input.joy_connection_changed.emit(0, true)
			# Continue retains the selected controller. Select only if needed;
			# Start on an already selected pad normally closes this reader.
			if view.selected_pad != 0:
				pad(view, 0, JOY_BUTTON_START, true)
				pad(view, 0, JOY_BUTTON_START, false)
			check(view.selected_pad == 0 and view.crew_open(), "Controller selection stole the earned reader's focus or closed it")
			for index in 3:
				pad(view, 0, JOY_BUTTON_DPAD_DOWN, true)
				pad(view, 0, JOY_BUTTON_DPAD_DOWN, false)
			pad(view, 0, JOY_BUTTON_A, true)
			pad(view, 0, JOY_BUTTON_A, false)
		check(view.crew_open() and view.crew_reader.text.text == packet.topics[3].text, "Keyboard/selected controller did not select actual fourth response: " + label)
		responses.append(packet.topics[3].text)
		# Exercise the actual fourth GUI button with mouse input as well.
		view.select_crew_topic(1)
		await mouse_topic(view.crew_reader.topics[3])
		check(view.crew_reader.text.text == packet.topics[3].text, "Mouse did not choose the earned fourth topic: " + label)
		await reader_layout(view, owner, frozen, output, "earned-" + label + "-")
		var barrier_text: String = view.crew_reader.text.text
		view.journey_dialog_open = true
		view.select_crew_topic(1)
		check(view.crew_reader.text.text == barrier_text, "Journey modal accepted an earned topic change")
		view.journey_dialog_open = false
		view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
		view.select_crew_topic(1)
		view.crew_reader.back.pressed.emit()
		check(view.crew_open() and view.crew_reader.text.text == barrier_text, "Unfocused earned reader accepted a topic or close")
		view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
		key(view, KEY_ESCAPE, true)
		key(view, KEY_ESCAPE, false)
		check(not view.crew_open() and view.paused and not view.controls_armed and owner.get_freedom_flight_state() == before and save(owner, prefix + "-after.json") == frozen, "Earned conversation changed complete Save or auto-resumed: " + label)
		if label == "recovered": Input.joy_connection_changed.emit(0, false)
		if check(shell.select_start(owner, {"mode": "continue", "value": prefix + "-after.json", "assets": assets}), "Earned talk Save Continue refused: " + label):
			view = shell.current_view
			view.set_process(false)
			view.set_process_input(false)
			check(view.paused and not view.crew_open() and owner.get_freedom_station_crew() == packet and save(owner, prefix + "-continued.json") == frozen, "Earned crew Continue lost retained records or invented dialogue progress: " + label)
		rows.append({"source": source, "source_sha256": source_hash, "topic_count": packet.topics.size(), "topic_id": packet.topics[3].id, "inspection_recorded": record.inspected, "tick": packet.tick, "read_save_sha256": FileAccess.get_sha256(prefix + "-after.json")})
		check(FileAccess.get_sha256(source) == source_hash, "Earned crew read rewrote the C++ source fixture: " + label)
		await process_frame
	check(responses.size() == 2 and responses[0] != responses[1], "Contact-only and inspected retained logs received the same response")
	var file := FileAccess.open(output.path_join("earned-contact-report.json"), FileAccess.WRITE)
	if check(file != null, "Earned contact report unavailable"):
		file.store_string(JSON.stringify({"schema": "apsis.crew-earned-contact-native/1", "completed": failures == 0, "rows": rows, "scope": "Actual earned contact/inspection retained by classified recovery, real facing ticks and paused input/read/Continue; no new facts, NPC memory or manual-controller acceptance"}, "\t") + "\n")
		file.close()

func service_interactions(shell: Control, owner: Variant, phases: String, assets: String, output: String) -> void:
	# A real planetary-chart station Save, already facing the attendant. The
	# existing office owns all work, sales and financial commands after entry.
	var source := phases.path_join("lead-station.json")
	var rows: Array = []
	for gesture in ["keyboard", "mouse", "pad"]:
		if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Service entry Continue refused: " + gesture): return
		var view: Control = shell.current_view
		view.set_process(false)
		view.set_process_input(false)
		view.preferences_provider.persist = false
		view.observe_neutral_controls()
		view.resume_requested()
		view.request_crew_conversation()
		var historical := save(owner, output.path_join("services-" + gesture + "-historical.json"))
		if not check(view.crew_open() and not view.crew_reader.services[0].disabled and view.crew_reader.services[1].disabled, "Historical condition-free journey lost office access or invented repair support"): return
		view.crew_reader.services[1].pressed.emit()
		check(view.crew_open() and not view.origin_repair_open() and save(owner, output.path_join("services-" + gesture + "-unavailable.json")) == historical, "Unavailable historical repair changed the Save or opened service")
		view.close_crew_conversation()
		# Explicit public mode selection on a test clone supplies a genuine
		# pristine condition owner. It is not a flown damage/repair claim.
		if not check(owner.enable_freedom_impact_consequences(), "Public condition selection refused for service clone"): return
		view.update_view()
		if gesture == "pad":
			Input.joy_connection_changed.emit(0, true)
			pad(view, 0, JOY_BUTTON_START, true)
			pad(view, 0, JOY_BUTTON_START, false)
		var baseline := save(owner, output.path_join("services-" + gesture + "-initial.json"))
		for service in ["office", "repair"]:
			var prefix := output.path_join("services-" + gesture + "-" + service)
			view.observe_neutral_controls()
			view.resume_requested()
			key(view, KEY_E, true)
			key(view, KEY_E, false)
			if not check(view.crew_open() and view.paused, "Service conversation did not open: " + service): return
			var button: Button = view.crew_reader.services[0 if service == "office" else 1]
			if not check(not button.disabled, "Actual supported station service disabled: " + service): return
			key(view, KEY_W, true)
			button.pressed.emit()
			key(view, KEY_W, false)
			view.save_dialog.show()
			button.pressed.emit()
			view.save_dialog.hide()
			view.journey_dialog_open = true
			button.pressed.emit()
			view.journey_dialog_open = false
			view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
			button.pressed.emit()
			view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
			view.open_crew_service("unknown")
			check(view.crew_open() and not view.commissions_open() and not view.origin_repair_open() and save(owner, prefix + "-guarded.json") == baseline, "Service entry bypassed demand/focus/modal guards: " + service)
			if gesture != "mouse": button.grab_focus()
			if gesture == "mouse": await mouse_topic(button)
			elif gesture == "pad":
				pad(view, 0, JOY_BUTTON_A, true)
				pad(view, 0, JOY_BUTTON_A, false)
			else:
				key(view, KEY_ENTER, true)
				key(view, KEY_ENTER, false)
			var opened: bool = view.commissions_open() if service == "office" else view.origin_repair_open()
			check(opened and not view.crew_open() and view.paused and not view.mouse_look.active and save(owner, prefix + "-opened.json") == baseline, "Service entry changed journey or failed exclusive paused routing: " + service + "/" + gesture)
			if not opened: return
			if service == "office": view.close_survey_commissions()
			else: view.close_origin_repair()
			var return_button: Button = view.commissions_button if service == "office" else view.origin_repair_button
			check(view.paused and not view.crew_open() and not view.commissions_open() and not view.origin_repair_open() and not view.controls_armed and return_button.has_focus() and save(owner, prefix + "-closed.json") == baseline, "Service Back lost paused focus or changed Save: " + service)
			rows.append({"gesture": gesture, "service": service, "save_sha256": FileAccess.get_sha256(prefix + "-closed.json")})
		if gesture == "pad": Input.joy_connection_changed.emit(0, false)
	var file := FileAccess.open(output.path_join("services.json"), FileAccess.WRITE)
	if check(file != null, "Service entry report unavailable"):
		file.store_string(JSON.stringify({"schema": "apsis.crew-service-entry/1", "completed": failures == 0, "rows": rows}, "\t") + "\n")
		file.close()

func knowledge_in(document: Variant) -> Dictionary:
	var value: Variant = document
	for depth in 16:
		if not value is Dictionary: return {}
		if value.get("knowledge") is Dictionary: return value.knowledge
		value = value.get("voyage", value.get("base"))
	return {}

func lead_interactions(shell: Control, owner: Variant, phases: String, assets: String, output: String) -> void:
	var source := phases.path_join("lead-station.json")
	for gesture in ["keyboard", "mouse", "pad"]:
		var prefix := output.path_join("lead-" + gesture)
		if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Actual lead station Continue refused: " + gesture): return
		var view: Control = shell.current_view
		view.set_process(false)
		view.set_process_input(false)
		view.preferences_provider.persist = false
		await process_frame
		await process_frame
		var initial := save(owner, prefix + "-initial.json")
		var flight: Dictionary = owner.get_freedom_flight_state()
		var facts: Dictionary = owner.get_freedom_observations()
		var offer: Dictionary = owner.get_freedom_station_stellar_lead()
		if not check(Notes.station_lead_valid(offer) and offer.can_keep and not offer.kept and initial == FileAccess.get_file_as_bytes(source), "Lead fixture lacks an authentic unkept zero-time offer: " + gesture): return
		check(owner.get_freedom_station_stellar_lead() == offer and save(owner, prefix + "-read.json") == initial, "Reading the offer received or rerolled it")
		# A wrong same-context clock token must refuse before any receipt exists.
		var stale := "0" if offer.offer_tick != "0" else "1"
		check(not owner.receive_freedom_station_stellar_lead(stale) and save(owner, prefix + "-stale.json") == initial, "Stale lead clock token changed the voyage")
		if gesture == "pad":
			Input.joy_connection_changed.emit(0, true)
			if view.selected_pad != 0:
				pad(view, 0, JOY_BUTTON_START, true)
				pad(view, 0, JOY_BUTTON_START, false)
		view.observe_neutral_controls()
		view.resume_requested()
		key(view, KEY_E, true)
		key(view, KEY_E, false)
		if not check(view.crew_open() and view.paused, "Actual lead conversation failed focused neutral entry: " + gesture): return
		var topic: Button
		for button in view.crew_reader.topics:
			if button.visible and button.get_meta("topic_id") == 5: topic = button
		if not check(topic != null and not view.crew_reader.keep.visible, "Lead topic absent or Keep visible before explicit topic selection"): return
		topic.grab_focus()
		if gesture == "pad":
			pad(view, 0, JOY_BUTTON_A, true)
			pad(view, 0, JOY_BUTTON_A, false)
		else:
			key(view, KEY_ENTER, true)
			key(view, KEY_ENTER, false)
		if not check(view.crew_reader.keep.visible and not view.crew_reader.keep.disabled and view.crew_reader.offer_tick == offer.offer_tick and save(owner, prefix + "-topic.json") == initial, "Selecting lead text recorded it or lost the current offer"): return
		var dialogue: String = ""
		for response in owner.get_freedom_station_crew().topics:
			if response.id == 5: dialogue = response.text
		check(not dialogue.is_empty() and dialogue in view.crew_reader.text.text, "Actual lead reader replaced the fresh C++ conversation with generic note text")
		if gesture == "keyboard":
			# Reuse the existing topic proof file: no new fixture or saved-state row.
			await reader_layout(view, owner, initial, output, "", prefix + "-topic.json")
		# The only mutation is the explicit Keep; direct signal calls retain all
		# the owning station view's focus, modal and independent-demand guards.
		key(view, KEY_W, true)
		view.crew_reader.keep.pressed.emit()
		key(view, KEY_W, false)
		view.save_dialog.show()
		view.crew_reader.keep.pressed.emit()
		view.save_dialog.hide()
		view.journey_dialog_open = true
		view.crew_reader.keep.pressed.emit()
		view.journey_dialog_open = false
		view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_OUT)
		view.crew_reader.keep.pressed.emit()
		view._notification(Control.NOTIFICATION_APPLICATION_FOCUS_IN)
		check(not owner.get_freedom_station_stellar_lead().kept and save(owner, prefix + "-guarded.json") == initial, "Held demand/focus/modal bypassed Keep guards")
		if gesture != "mouse": view.crew_reader.keep.grab_focus()
		if gesture == "mouse":
			await mouse_topic(view.crew_reader.keep)
		elif gesture == "pad":
			pad(view, 7, JOY_BUTTON_A, true)
			pad(view, 7, JOY_BUTTON_A, false)
			check(not owner.get_freedom_station_stellar_lead().kept, "Foreign controller received the report")
			pad(view, 0, JOY_BUTTON_A, true)
			pad(view, 0, JOY_BUTTON_A, false)
		else:
			key(view, KEY_ENTER, true)
			key(view, KEY_ENTER, false)
		var kept: Dictionary = owner.get_freedom_station_stellar_lead()
		var bytes := save(owner, prefix + "-kept.json")
		if not check(Notes.station_lead_valid(kept) and kept.kept and not kept.can_keep and kept.status == "unverified" and view.paused and not view.crew_reader.keep.visible and view.crew_reader.back.has_focus(), "Explicit Keep failed or resumed the journey: " + gesture): return
		for response in owner.get_freedom_station_crew().topics:
			if response.id == 5: check(response.text in view.crew_reader.text.text, "Keep retained the old offer instead of the fresh C++ confirmation")
		var before_document: Variant = JSON.parse_string(initial.get_string_from_utf8())
		var after_document: Variant = JSON.parse_string(bytes.get_string_from_utf8())
		var knowledge := knowledge_in(after_document)
		check(knowledge.has("station_stellar_lead"), "Explicit Keep failed to retain its typed knowledge component")
		knowledge.erase("station_stellar_lead")
		check(after_document == before_document and owner.get_freedom_flight_state() == flight and owner.get_freedom_observations() == facts, "Keep changed more than its one note: clock, resources, actor, chart or facts")
		view.crew_reader.keep.pressed.emit()
		check(not owner.receive_freedom_station_stellar_lead(offer.offer_tick) and save(owner, prefix + "-duplicate.json") == bytes, "Duplicate Keep changed the first receipt or voyage")
		key(view, KEY_ESCAPE, true)
		key(view, KEY_ESCAPE, false)
		check(view.paused and not view.crew_open(), "Lead reader Back resumed walking")
		if gesture == "pad": Input.joy_connection_changed.emit(0, false)
		if not check(shell.select_start(owner, {"mode": "continue", "value": prefix + "-kept.json", "assets": assets}), "Kept lead Continue refused"): return
		check(owner.get_freedom_station_stellar_lead() == kept and save(owner, prefix + "-continued.json") == bytes and shell.current_view.paused and not shell.current_view.crew_open(), "Continue changed the retained report or reopened a transient reader")
	# Each source below is produced by real C++ receipt/search/arrival/recovery.
	# Reading these snapshots does not claim the Godot reader flew those legs.
	for phase in ["lead-kept", "lead-confirmed", "lead-visited", "lead-recovered"]:
		var path := phases.path_join(phase + ".json")
		if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Lead archive phase Continue refused: " + phase): return
		var view: Control = shell.current_view
		view.set_process(false)
		view.set_process_input(false)
		var bytes := save(owner, output.path_join(phase + "-archive-before.json"))
		var lead: Dictionary = owner.get_freedom_station_stellar_lead()
		var status: String = {"lead-kept": "unverified", "lead-confirmed": "sensor_fix_acquired", "lead-visited": "visited", "lead-recovered": "visited"}[phase]
		if not check(Notes.station_lead_valid(lead) and lead.kept and lead.status == status, "Actual lead phase lacks authoritative retained status: " + phase): return
		var menu: CanvasLayer = null
		var reader: Control
		if view.has_method("open_observations"):
			view.open_observations()
			reader = view.observations_view
		else:
			menu = view.controls_menu
			menu.show_observations()
			reader = menu.observations_view
		var found := false
		for index in reader.titles().size():
			var page: Dictionary = reader.titles()[index]
			if page.get("title") == "STATION LEAD":
				reader.page = index
				reader.refresh()
				found = lead.note in reader.body.text and "Origin bay attendant" in reader.body.text
		check(found and reader.visible and view.paused and save(owner, output.path_join(phase + "-archive-after.json")) == bytes, "Archive omitted/changed the retained note or complete Save: " + phase)
		reader.back_button.pressed.emit()
		check(not reader.visible and view.paused, "Lead archive Back resumed the journey")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 4: quit(1); return
	var source := args[0]
	var assets := args[1]
	var output := args[2]
	var derelict_phases := args[3]
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Crew output directory unavailable"): quit(1); return
	# Headless defaults to 64x64. Exercise GUI clicks at the ordinary supported
	# window size; reader_layout restores this after its smaller-window check.
	root.size = Vector2i(1280, 720)
	await process_frame
	var source_bytes := FileAccess.get_file_as_bytes(source)
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Crew native bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	await service_interactions(shell, owner, derelict_phases, assets, output)
	if failures != 0: shell.free(); quit(1); return
	if not check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Crew source Continue failed: " + shell.error): shell.free(); quit(1); return
	var view: Control = shell.current_view
	view.set_process(false)
	view.set_process_input(false)
	view.preferences_provider.persist = false
	var packet: Dictionary = owner.get_freedom_station_crew()
	if not check(Crew.valid_packet(packet, view.state) and view.paused and packet.name == "Bay attendant" and packet.foot_position_metres == PackedFloat64Array([1.3, 0, 0]), "Actual station crew binding unavailable"): shell.free(); quit(1); return
	check(packet.version == 3 and packet.topics.size() in [3, 4] and (packet.topics.size() == 3 or packet.topics[3].id == 5), "Unacquired contact exposed an extra crew topic")
	packet_refusals(packet, view.state)
	topic_layouts(packet, view.state)
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
	view.select_crew_topic(1)
	var text: String = view.crew_reader.text.text
	check(not view.crew_reader.keep.visible and not view.crew_reader.topics[4].visible and (not view.crew_reader.topics[3].visible or view.crew_reader.topics[3].get_meta("topic_id") == 5), "Unknown-contact reader invented topic4 or showed Keep without selecting a report")
	view.select_crew_topic(4)
	check(view.crew_reader.text.text == text, "Stale contact topic fabricated a response without earned knowledge")
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
	# Keep the original nine proof saves/report unchanged; additional desk-input
	# regressions run independently before the existing original-route reset.
	stop_talk_regressions(shell, owner, source, assets, output)
	await earned_contact_topics(shell, owner, derelict_phases, assets, output, packet.topics)
	await lead_interactions(shell, owner, derelict_phases, assets, output)
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
