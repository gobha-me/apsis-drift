extends RefCounted
## One existing continuous Moon landing, one nearest site, real bounded walking.
const Shell = preload("res://scripts/native/native_start_shell.gd")
const MoonView = preload("res://scripts/native/native_moon_view.gd")
const Landmarks = preload("res://scripts/native/native_surface_landmarks.gd")
const FieldSamples = preload("res://tests/native_field_samples.gd")
const WALK_CAP := 24000
const EYE_HEIGHT := 1.70 # Existing canonical origin/surface walker eye height.
var failures := 0
var tree: SceneTree
var output := ""
var report := {"scope": "Actual supported Moon exposure interaction; no route/whole-footprint certificate", "outward_cap": WALK_CAP, "return_cap": WALK_CAP, "outward_ticks": 0, "return_ticks": 0, "target_id": "", "initial_gap_metres": 0.0, "inspection_gap_metres": 0.0, "return_gap_metres": 0.0, "completed": false}

static func run(test: SceneTree, phases: String, assets: String, destination: String) -> int:
	var helper: RefCounted = load("res://tests/native_moon_exposures.gd").new()
	helper.tree = test
	helper.output = destination
	await helper.run_actual(phases, assets)
	return int(helper.failures)

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error("Moon exposures: " + message)
	return value

func save_bytes(owner: Variant, name: String) -> PackedByteArray:
	var path := output.path_join(name + ".json")
	if not check(owner.save_freedom_as(path), "Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func knowledge(bytes: PackedByteArray) -> Dictionary:
	var value: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	for depth in 16:
		if not value is Dictionary: break
		if value.get("knowledge") is Dictionary: return value.knowledge
		value = value.get("voyage", value.get("base"))
	check(false, "Saved knowledge owner was not found")
	return {}

func without_landmarks(state: Dictionary) -> Dictionary:
	var copy := state.duplicate(true)
	copy.erase("surface_landmarks")
	return copy

func metadata(bytes: PackedByteArray) -> Dictionary:
	return knowledge(bytes).get("moon_surface_landmarks", {})

func key(code: int, down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func context_event(view: Control, down: bool, echo := false) -> void:
	# Deliver an actual physical interaction event while the disabled shell
	# prevents any automatic simulation ticks during this test-owned clock.
	var view_mode := view.process_mode
	var input_mode: int = view.player_input.process_mode
	view.process_mode = Node.PROCESS_MODE_ALWAYS
	view.player_input.process_mode = Node.PROCESS_MODE_ALWAYS
	view.set_process(false)
	var event := InputEventKey.new()
	event.physical_keycode = KEY_E
	event.keycode = KEY_E
	event.pressed = down
	event.echo = echo
	Input.parse_input_event(event)
	Input.flush_buffered_events()
	view.player_input.process_mode = input_mode
	view.process_mode = view_mode
	view.set_process(false)

func activate(view: Control, button: Button, controller := false) -> void:
	var mode := button.process_mode
	button.process_mode = Node.PROCESS_MODE_ALWAYS
	await tree.process_frame
	button.grab_focus()
	view.controls_menu.left_scroll.ensure_control_visible(button)
	if check(button.visible and not button.disabled and button.has_focus() and button.can_process(), "Actual paused action is unavailable to focused input"):
		if controller:
			for down in [true, false]:
				var event := InputEventJoypadButton.new()
				event.device = view.player_input.device
				event.button_index = JOY_BUTTON_A
				event.pressed = down
				Input.parse_input_event(event)
				Input.flush_buffered_events()
				await tree.process_frame
		else:
			key(KEY_ENTER, true)
			await tree.process_frame
			key(KEY_ENTER, false)
	button.process_mode = mode
	button.release_focus()

func resume(view: Control) -> bool:
	view.observe_neutral_controls()
	view.toggle_pause()
	return check(not view.paused and view.controls_armed, "Explicit neutral Resume refused")

func refresh(view: Control, owner: Variant) -> void:
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)

func find_site(state: Dictionary, id: String) -> Dictionary:
	for site: Dictionary in state.get("surface_landmarks", {}).get("sites", []):
		if site.id == id: return site
	return {}

func material_bytes(material: ShaderMaterial) -> PackedByteArray:
	return var_to_bytes([material.get_shader_parameter("surface_landmarks_enabled"), material.get_shader_parameter("surface_landmark_count"), material.get_shader_parameter("surface_landmark_centres"), material.get_shader_parameter("surface_landmark_radii"), material.get_shader_parameter("surface_landmark_tints")])

func packet_refusals(state: Dictionary, material: ShaderMaterial) -> void:
	var presenter := Landmarks.new()
	if not check(Landmarks.valid(state) and presenter.apply(state, [material]), "Genuine typed Moon packet refused by actual lunar material"): return
	check(material.get_shader_parameter("surface_landmarks_enabled") == true and material.get_shader_parameter("surface_landmark_count") == state.surface_landmarks.sites.size(), "Lunar material lost actual bounded footprint count")
	for i in state.surface_landmarks.sites.size():
		var site: Dictionary = state.surface_landmarks.sites[i]
		check(material.get_shader_parameter("surface_landmark_centres")[i] == site.position and is_equal_approx(material.get_shader_parameter("surface_landmark_radii")[i], site.radius_metres), "Actual lunar centre/radius was not used by its tint material")
	var accepted := material_bytes(material)
	var malformed: Array[Dictionary] = []
	for field in state.surface_landmarks:
		var bad := state.duplicate(true)
		bad.surface_landmarks[field] = null
		malformed.append(bad)
	for field in state.surface_landmarks.sites[0]:
		var bad := state.duplicate(true)
		bad.surface_landmarks.sites[0][field] = null
		malformed.append(bad)
	for pair in [["body_kind", "planet"], ["body_version", 999], ["body_id", "0" if state.body_id != "0" else "1"], ["system_id", "0" if state.system_id != "0" else "1"], ["tick", "0"], ["inspection_count", 65], ["version", 2.0], ["planet_id", state.body_id]]:
		var bad := state.duplicate(true)
		bad.surface_landmarks[pair[0]] = pair[1]
		malformed.append(bad)
	for pair in [["position", Vector3(NAN, 0, 0)], ["radius_metres", 27.999], ["radius_metres", 52.001], ["distance_metres", INF], ["tint", Color(NAN, 0, 0, 1)], ["id", "00"], ["kind", 3]]:
		var bad := state.duplicate(true)
		bad.surface_landmarks.sites[0][pair[0]] = pair[1]
		malformed.append(bad)
	var duplicate := state.duplicate(true)
	duplicate.surface_landmarks.sites.append(duplicate.surface_landmarks.sites[0].duplicate(true))
	malformed.append(duplicate)
	var oversized := state.duplicate(true)
	oversized.surface_landmarks.sites = []
	for index in 9: oversized.surface_landmarks.sites.append(state.surface_landmarks.sites[0].duplicate(true))
	malformed.append(oversized)
	for bad in malformed:
		var original := var_to_bytes(bad)
		check(not Landmarks.valid(bad) and not presenter.apply(bad, [material]) and material_bytes(material) == accepted and var_to_bytes(bad) == original, "Malformed Moon packet changed accepted material or input")
	var invalid_material := ShaderMaterial.new()
	check(not presenter.apply(state, [material, invalid_material]) and material_bytes(material) == accepted, "Invalid second destination partially changed lunar material")

func walk_to_site(owner: Variant, id: String) -> Array:
	var commands: Array = []
	var stalled := 0
	var previous := INF
	for tick in WALK_CAP:
		var state: Dictionary = owner.get_freedom_flight_state()
		var site := find_site(state, id)
		if not check(not site.is_empty(), "Chosen initial site left the bounded ordinary query; no alternate selected"): return []
		report.inspection_gap_metres = site.distance_metres
		if site.can_inspect and site.distance_metres <= 7.5 and not state.surface_walk.return_access.within_reach:
			report.outward_ticks = commands.size()
			return commands
		if site.distance_metres >= previous - 0.000001: stalled += 1
		else: stalled = 0
		if not check(stalled < 240, "Actual direct foot route stalled against support/slope/craft clearance at gap %.3f m" % site.distance_metres): return []
		previous = site.distance_metres
		var walking: Dictionary = state.surface_walk
		var basis: Basis = walking.basis * Basis(Vector3.UP, float(walking.heading))
		var foot: Vector3 = walking.eye_position - walking.basis.y * EYE_HEIGHT
		var delta: Vector3 = site.position - foot
		var demand := Vector2(delta.dot(basis.x), -delta.dot(basis.z))
		if not check(demand.is_finite() and demand.length() > 0.000001, "Actual bearing is unavailable"): return []
		demand = demand.normalized()
		var controls := PackedFloat64Array([demand.y, demand.x, float(walking.heading)])
		if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, controls, false), "Actual outward foot tick refused: " + str(owner.get_last_error())): return []
		commands.append(controls)
		report.outward_ticks = commands.size()
		if tick % 120 == 0: await tree.process_frame
	report.outward_ticks = commands.size()
	check(false, "Nearest fixed site not reached within24000 actual120Hz foot ticks")
	return []

func exercise(shell: Control, owner: Variant, assets: String) -> void:
	var view: Control = shell.current_view
	var initial: Dictionary = owner.get_freedom_flight_state()
	var initial_records: Dictionary = owner.get_freedom_observations()
	var frozen := save_bytes(owner, "historical-outside")
	if not check(view is MoonView and view.paused and view.surface_walking() and initial.surface_landmarks.version == 2 and initial.surface_landmarks.can_enable and not initial.surface_landmarks.selected and metadata(frozen).is_empty(), "Actual historical outside source lacks explicit Enable selection"): return
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.landmark_enable_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.controls_menu.show_settings()
	view.landmark_enable_button.pressed.emit()
	view.controls_menu.close_settings()
	if not check(save_bytes(owner, "enable-refused") == frozen, "Unfocused/nested Enable changed complete Save"): return
	await activate(view, view.landmark_enable_button)
	var enabled: Dictionary = owner.get_freedom_flight_state()
	var enabled_bytes := save_bytes(owner, "enabled-outside")
	if not check(enabled.surface_landmarks.selected and enabled.surface_landmarks.inspection_count == 0 and without_landmarks(enabled) == without_landmarks(initial) and owner.get_freedom_observations() == initial_records, "Enable changed something beyond the zero-time Moon metadata selection"): return
	if not check(not enabled.surface_landmarks.sites.is_empty(), "No genuine exposure exists in the initial384m ordinary query; no seed or centre fallback"): return
	var target: Dictionary = enabled.surface_landmarks.sites[0]
	report.target_id = target.id
	report.initial_gap_metres = target.distance_metres
	var initial_sites: Array = []
	for site: Dictionary in enabled.surface_landmarks.sites:
		initial_sites.append({"id": site.id, "distance_metres": site.distance_metres})
	report.initial_observation = {"body_kind": enabled.body_kind, "body_version": enabled.body_version, "body_id": enabled.body_id, "system_id": enabled.system_id, "tick": enabled.tick, "checksum": enabled.checksum, "sites": initial_sites}
	if not check(target.distance_metres <= 384.0 and not target.inspected, "Initial nearest actual site is outside the selected query or already recorded"): return
	for candidate: Dictionary in enabled.surface_landmarks.sites:
		if not check(candidate.distance_metres >= target.distance_metres, "Ordinary C++ nearby query is not nearest-first"): return
	packet_refusals(enabled, view.terrain.material)
	if failures: return
	# The original genuine outside source is at its hatch. Demonstrate that it
	# retains priority, then Continue the exact enabled outside Save for walking.
	if not check(enabled.surface_walk.return_access.within_reach, "Retained actual outside source is not at its hatch"): return
	if not resume(view): return
	context_event(view, true)
	context_event(view, false)
	if not check(not view.surface_walking() and view.paused and owner.get_freedom_flight_state().tick == enabled.tick and metadata(save_bytes(owner, "hatch-priority")) == metadata(enabled_bytes), "Reachable hatch lost priority or changed exposure history"): return
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("enabled-outside.json"), "assets": assets}), "Enabled actual outside Continue refused"): return
	view = shell.current_view
	view.set_process(false)
	if not check(save_bytes(owner, "enabled-continued") == enabled_bytes, "Enable Continue changed complete source bytes"): return
	var commands: Array = await walk_to_site(owner, target.id)
	if failures: return
	if not check(not commands.is_empty(), "Exposure route did not execute a genuine foot segment"): return
	refresh(view, owner)
	var near: Dictionary = view.state
	var near_site := find_site(near, target.id)
	if not check(not near_site.is_empty() and near_site.can_inspect and not near.surface_walk.return_access.within_reach and near.resources == enabled.resources, "Bounded actual walk did not reach a supported inspectable centre away from hatch"): return
	var near_bytes := save_bytes(owner, "reached-before-inspection")
	if not check(FieldSamples.uninspected(owner, target.id, output), "Uninspected actual Moon foot admitted a specimen or read adopted sample state"): return
	var records: Dictionary = owner.get_freedom_observations()
	for gate in ["movement", "look", "unarmed", "modal", "echo"]:
		if view.paused and not resume(view): return
		if gate == "movement": key(KEY_W, true)
		elif gate == "look": key(KEY_RIGHT, true)
		elif gate == "unarmed": view.controls_armed = false
		elif gate == "modal": view.journey_dialog_open = true
		context_event(view, true, gate == "echo")
		context_event(view, false)
		if gate == "movement": key(KEY_W, false)
		elif gate == "look": key(KEY_RIGHT, false)
		elif gate == "modal": view.journey_dialog_open = false
		check(save_bytes(owner, "inspect-refused-" + gate) == near_bytes and not view.controls_menu.surface_reference_view.visible, "Context gate changed actual Save or opened reader: " + gate)
		view.observe_neutral_controls()
	view.pause_controls("Check paused Moon inspection guards")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view.landmark_inspect_button.pressed.emit()
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	view.controls_menu.show_settings()
	view.landmark_inspect_button.pressed.emit()
	view.controls_menu.close_settings()
	if not check(save_bytes(owner, "inspect-refused-ui") == near_bytes, "Unfocused/nested paused Inspect changed full Save"): return
	if not resume(view): return
	context_event(view, true)
	context_event(view, false)
	var inspected: Dictionary = owner.get_freedom_flight_state()
	var reader: Control = view.controls_menu.surface_reference_view
	if not check(view.paused and reader.visible and near_site.description in reader.body.text and inspected.surface_landmarks.inspection_count == 1 and find_site(inspected, target.id).get("inspected", false), "Actual E did not inspect the chosen supported site and open its description"): return
	check(without_landmarks(inspected) == without_landmarks(near) and owner.get_freedom_observations() == records, "Zero-time inspection changed actor/clock/resources/personal marker or unrelated facts")
	var first := save_bytes(owner, "first-inspection")
	var first_metadata := metadata(first)
	if not check(first_metadata.get("version") == 1 and first_metadata.get("inspections") is Array and first_metadata.inspections.size() == 1 and int(first_metadata.inspections[0].tick) == int(near.tick) and first_metadata.inspections[0].target.kind == "moon" and first_metadata.inspections[0].target.identity == near.body_id and first_metadata.inspections[0].terrain_recipe == 1, "Saved inspection lost typed owner, terrain recipe or authoritative first tick"): return
	view.controls_menu.close_surface_reference()
	if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([0, 0, float(near.surface_walk.heading)]), false), "Actual neutral post-inspection tick refused"): return
	refresh(view, owner)
	var repeat_before := save_bytes(owner, "repeat-before")
	view.player_input.device = 19
	view.player_input.available_pads[19] = true
	view.player_input.install()
	await activate(view, view.landmark_inspect_button, true)
	view.player_input.available_pads.erase(19)
	view.player_input.device = -2
	view.player_input.install()
	if not check(reader.visible and "already recorded" in reader.body.text and save_bytes(owner, "repeat-after") == repeat_before and metadata(repeat_before) == first_metadata, "Selected-pad paused repeat changed full Save or first inspection tick"): return
	view.controls_menu.close_surface_reference()
	if not check(shell.select_start(owner, {"mode": "continue", "value": output.path_join("repeat-before.json"), "assets": assets}), "Inspected outside Continue refused"): return
	view = shell.current_view
	view.set_process(false)
	if not check(view.paused and save_bytes(owner, "inspected-continued") == repeat_before, "Inspected outside Continue changed full saved bytes"): return
	failures += await FieldSamples.run(tree, shell, owner, assets, output, target.id, Callable(self, "activate"))
	if failures: return
	view = shell.current_view
	view.set_process(false)
	var samples: Dictionary = owner.get_freedom_field_samples()
	# Reverse only the commands actually executed on the genuine outward path.
	for i in mini(commands.size(), WALK_CAP):
		var state: Dictionary = owner.get_freedom_flight_state()
		if state.surface_walk.return_access.within_reach: break
		var old: PackedFloat64Array = commands[commands.size() - 1 - i]
		if not check(owner.advance_freedom_surface_walk(1.0 / 120.0, PackedFloat64Array([-old[0], -old[1], old[2]]), false), "Actual recorded reverse foot tick refused"): return
		report.return_ticks += 1
		if i % 120 == 0: await tree.process_frame
	refresh(view, owner)
	report.return_gap_metres = view.state.surface_walk.return_access.distance_metres
	if not check(view.state.surface_walk.return_access.within_reach and metadata(save_bytes(owner, "returned-foot")) == first_metadata and view.state.resources == enabled.resources, "Recorded actual walk did not return to hatch with metadata/resources retained"): return
	if not check(owner.get_freedom_field_samples() == samples, "Actual return walk lost carried specimen or first collection notes"): return
	if not resume(view): return
	context_event(view, true)
	context_event(view, false)
	var aboard: Dictionary = owner.get_freedom_flight_state()
	check(not view.surface_walking() and view.paused and aboard.surface.landed and metadata(save_bytes(owner, "reboarded")) == first_metadata, "Actual contextual reboarding lost first inspection record")
	check(owner.get_freedom_field_samples() == samples, "Actual contextual reboarding lost carried specimen or first collection notes")
	report.completed = failures == 0

func run_actual(phases: String, assets: String) -> void:
	if not check(DirAccess.make_dir_recursive_absolute(output) == OK, "Output directory unavailable"): return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	tree.root.add_child(shell)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if check(shell.select_start(owner, {"mode": "continue", "value": phases.path_join("outside.json"), "assets": assets}), "Actual continuous outside Continue failed: " + shell.error):
		shell.current_view.set_process(false)
		await exercise(shell, owner, assets)
	report.failures = failures
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "Report output unavailable"):
		file.store_string(JSON.stringify(report, "  ") + "\n")
		file.close()
	shell.free()
	await tree.process_frame
	print("Actual Moon exposure walk: %d failures; outward %d, return %d ticks; target %s, gap %.3f m" % [failures, report.outward_ticks, report.return_ticks, report.target_id, report.inspection_gap_metres])
