extends "res://studies/captures/native_voyage_capture.gd"
## A near-ground C++ fixture exercises the real staged model and native actions.
## It is not a station-to-site voyage or manual pilot acceptance.
func check_deployed_gear(model: Node3D) -> void:
	# The displayed mesh's soles must match the C++ registered uncompressed
	# rectangles. This checks composed asset geometry, not a copied gear flag.
	var expected := {"HopperGear05": Vector3(0, -2.08, -0.7), "HopperGear16": Vector3(-2.5, -2.08, 5.07), "HopperGear27": Vector3(2.5, -2.08, 5.07)}
	for name in expected:
		var node: Variant = model.gear_nodes.get(name)
		if not check(node is MeshInstance3D, "Missing deployed pad mesh " + name): return
		var box: AABB = node.get_aabb()
		var transform: Transform3D = model.global_transform.affine_inverse() * node.global_transform
		var minimum := Vector3(INF, INF, INF)
		var maximum := Vector3(-INF, -INF, -INF)
		for mask in 8:
			var corner := transform * (box.position + Vector3(box.size.x if mask & 1 else 0.0, box.size.y if mask & 2 else 0.0, box.size.z if mask & 4 else 0.0))
			minimum = minimum.min(corner)
			maximum = maximum.max(corner)
		var center: Vector3 = expected[name]
		check(absf(minimum.y - center.y) < 0.001 and absf((minimum.x + maximum.x) / 2.0 - center.x) < 0.001 and absf((minimum.z + maximum.z) / 2.0 - center.z) < 0.001 and absf(maximum.x - minimum.x - 0.64) < 0.001 and absf(maximum.z - minimum.z - 0.52) < 0.001, "Displayed deployed sole differs from physical rectangle " + name)

func check_last_gear_gap(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	var view: Control = shell.current_view
	install_recorded_input(view)
	check(not view.state.surface.gear_clearance.available, "Continue invented a historical gear gap")
	view.request_assistance(false)
	view.port_command("request_freedom_landing")
	check(not view.state.surface.gear_clearance.available, "A gear command invented a contact sample")
	if not resume(view): return
	view._process(1.0 / 120.0)
	var sampled: Dictionary = owner.get_freedom_flight_state()
	var gap: Dictionary = sampled.surface.gear_clearance
	check(view.error.is_empty() and not sampled.surface.landed and gap.available and gap.tick == sampled.tick and gap.source_checksum == sampled.checksum and gap.minimum_gap_metres > 0.0 and gap.minimum_gap_metres <= gap.maximum_gap_metres and gap.age_seconds == 0.0, "Actual airborne post-step query lost its last pad gap/provenance")
	check("Last gear gap" in view.primary_status.text and "Reference altitude" in view.primary_status.text, "Compact HUD omitted last gear gap or confused its datum")
	view.pause_controls("Inspecting last gear sample")
	var paused: Dictionary = owner.get_freedom_flight_state()
	check(paused.tick == sampled.tick and paused.surface.gear_clearance == gap, "Pausing changed the last contact observation")
	sampled = paused
	var saved := output.path_join("last-gear-gap.json")
	check(owner.save_freedom_as(saved), "Sampled airborne Save As refused")
	var bytes := FileAccess.get_file_as_bytes(saved)
	for repeat in 3:
		view._process(0.125)
		check(owner.get_freedom_flight_state() == sampled, "Paused reads sampled terrain or advanced the voyage")
	var short := PackedFloat64Array([0, 0])
	check(not owner.advance_freedom_flight(0.125, short, false) and owner.get_freedom_flight_state() == sampled and owner.save_freedom_as(saved) and FileAccess.get_file_as_bytes(saved) == bytes, "Refused advance or sample inspection changed diagnostics/full save")
	var continued: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(continued.stage_freedom_continue(saved), "Sampled assembled Continue staging refused"): return
	var pending: Dictionary = continued.get_pending_freedom_start()
	check(not pending.is_empty() and continued.commit_pending_freedom_start(pending.candidate_id) and not continued.get_freedom_flight_state().surface.gear_clearance.available and continued.save_freedom_as(output.path_join("last-gear-gap-continued.json")) and FileAccess.get_file_as_bytes(output.path_join("last-gear-gap-continued.json")) == bytes, "Continue fabricated a last sample or changed canonical saved bytes")
	view.port_command("stow_freedom_landing_gear")
	view.port_command("request_freedom_landing")
	check(not view.state.surface.gear_clearance.available, "Same-tick stow/redeploy revived an old gear sample")
	check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Gear-gap probe did not restore canonical surface source")


func check_surface_aid_feedback(shell: Control, owner: Variant, source: String, assets: String, output: String) -> void:
	var view: Control = shell.current_view
	install_recorded_input(view)
	if not resume(view): return
	view.port_command("request_freedom_landing")
	check(view.state.surface.maneuver == "landing" and view.state.surface.note in view.primary_status.text and not view.hud_scroll.visible, "Actual landing aid note requires opening the paused instruments")
	# Actual manual input, rather than a synthetic outcome packet, cancels aid.
	view.player_input.axes[6] = 1.0
	view._process(1.0 / 120.0)
	view.player_input.axes[6] = 0.0
	var canceled: Dictionary = owner.get_freedom_flight_state()
	var note: String = canceled.surface.note
	check(view.error.is_empty() and canceled.surface.maneuver == "off" and "canceled by manual control" in note and note in view.primary_status.text, "Actual manual-input aid handoff disappeared from compact flight")
	var path := output.path_join("surface-aid-feedback.json")
	check(owner.save_freedom_as(path), "Aid feedback control save refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	# Liftoff while airborne is a genuine refusal, with no physical mutation.
	view.port_command("liftoff_freedom_surface")
	var refusal: String = view.save_status.text.left(180)
	check(not refusal.is_empty() and refusal in view.primary_status.text and view.primary_status.text.find(refusal) < view.primary_status.text.find(note) and owner.get_freedom_flight_state() == canceled and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Aid result hid command refusal or changed the authoritative voyage")
	for repeat in 3: view.update_view(0.0)
	check(owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Quiet aid feedback reads changed a complete save")
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	view._process(0.125)
	view._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	check(view.paused and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Focus handling advanced flight while reviewing aid feedback")
	check(shell.select_start(owner, {"mode": "continue", "value": source, "assets": assets}), "Aid feedback probe did not restore canonical surface source")
	view = shell.current_view
	for repeat in 3: view.update_view(0.0)
	check(view.paused and not note in view.primary_status.text, "Quiet Continue replayed another view's aid result")

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	var source := FileAccess.open(args[0].path_join("manifest.json"), FileAccess.READ)
	if source == null or source.get_length() > 16384: quit(1); return
	var manifest: Variant = JSON.parse_string(source.get_as_text())
	source.close()
	if not check(manifest is Dictionary and manifest.get("schema_version") == 1 and manifest.get("source") == "source.json" and manifest.get("phases") is Array and manifest.phases.size() == 5, "Invalid surface manifest"): quit(1); return
	var labels := ["landed", "idle", "released", "airborne", "ascent"]
	for i in labels.size():
		var row: Variant = manifest.phases[i]
		if not check(row is Dictionary and row.get("name") == labels[i] and row.get("file") == labels[i] + ".json" and Checkpoints.finite_number(row.get("ticks")) and row.ticks == int(row.ticks) and row.ticks >= 0 and row.ticks <= 7200 and Shell.decimal_text(row.get("checksum")), "Invalid surface phase"): quit(1); return
	if not ClassDB.class_exists("FreedomBridge"): GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"): quit(1); return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := Shell.new()
	shell.presentation_only = true
	shell.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(shell)
	root.size = Vector2i(1280, 720)
	shell.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if not check(DirAccess.make_dir_recursive_absolute(args[2]) == OK and shell.select_start(owner, {"mode": "continue", "value": args[0].path_join("source.json"), "assets": args[1]}), "Near-ground source did not stage: " + shell.error): shell.free(); quit(1); return
	check_last_gear_gap(shell, owner, args[0].path_join("source.json"), args[1], args[2])
	if failed: shell.free(); quit(1); return
	check_surface_aid_feedback(shell, owner, args[0].path_join("source.json"), args[1], args[2])
	if failed: shell.free(); quit(1); return
	var view: Control = shell.current_view
	install_recorded_input(view)
	var model_id: int = view.staged_model.get_instance_id()
	var roots: Array = view.staged_model.replacement_nodes.duplicate()
	var rendered := DisplayServer.get_name() != "headless"
	var captures: Array = []
	check(not view.state.surface.gear_deployed and not view.state.surface.landed, "Initial saved craft silently acquired gear or landing")
	if not resume(view): shell.free(); quit(1); return
	view.port_command("request_freedom_landing")
	check(view.state.surface.gear_deployed and view.state.surface.maneuver == "landing", "Native landing action did not deploy and request maneuver")
	for row in manifest.phases:
		if failed: break
		if row.name == "released": view.port_command("liftoff_freedom_surface")
		if row.name == "ascent":
			view.port_command("stow_freedom_landing_gear")
			view.player_input.axes[6] = 1.0
		var remaining: int = int(row.ticks)
		while remaining > 0 and not failed:
			var count := mini(remaining, 15)
			view._process(float(count) / 120.0)
			check(view.error.is_empty(), "Native surface step refused: " + view.error)
			if view.state.surface.maneuver in ["landing", "liftoff"]:
				check(view.state.surface.note in view.primary_status.text and not view.hud_scroll.visible, "Active C++ aid detail disappeared from compact flight")
			remaining -= count
		var state: Dictionary = owner.get_freedom_flight_state()
		if row.name in ["landed", "airborne"]:
			check(state.surface.maneuver == "off" and view.surface_aid_cue.text() == state.surface.note and state.surface.note in view.primary_status.text, "Actual aid completion did not briefly retain its compact result: " + row.name)
		var path: String = args[2].path_join(row.file)
		check(state.checksum == row.checksum and owner.save_freedom_as(path) and FileAccess.get_sha256(path) == FileAccess.get_sha256(args[0].path_join(row.file)), "Full surface Save differs at " + row.name)
		check(view.staged_model.get_instance_id() == model_id and view.staged_model.replacement_nodes == roots and view.staged_model.valid_current_pose(), "Surface lifecycle replaced or damaged the craft model")
		check(view.liftoff_button.disabled == not state.surface.landed and view.stow_gear_button.disabled == (state.surface.landed or not state.surface.gear_deployed), "Surface HUD actions disagree with C++ state")
		var lift_fraction := clampf(state.positive_force_body[1] / state.positive_force_ratings[1], 0.0, 1.0)
		check(is_equal_approx(view.exhaust.lift_intensity, lift_fraction) and view.exhaust.lift_plumes[0].visible == (lift_fraction > 0.0) and view.exhaust.lift_plumes[1].visible == (lift_fraction > 0.0), "Real surface/liftoff exhaust differs from applied positive-Y force")
		if state.surface.gear_deployed: check_deployed_gear(view.staged_model)
		var menu: Button = view.controls_menu.saved_port_buttons["Liftoff with thrusters"]
		check(menu.focus_mode == Control.FOCUS_ALL and menu.disabled == not state.surface.landed, "Controller landing action lost focus or authority")
		if row.name == "idle":
			var second_owner: Variant = ClassDB.instantiate("FreedomBridge")
			var second := Shell.new()
			second.presentation_only = true
			second.process_mode = Node.PROCESS_MODE_DISABLED
			root.add_child(second)
			check(second.select_start(second_owner, {"mode": "continue", "value": path, "assets": args[1]}), "Landed Continue did not stage: " + second.error)
			if second.current_view != null:
				check(second.current_view.paused and second.current_view.state.surface.landed and second.current_view.state.surface.gear_deployed and second.current_view.staged_model.valid_current_pose(), "Landed Continue lost constraint, gear or paused readiness")
				check(second.current_view.surface_aid_cue.text().is_empty(), "Quiet landed Continue invented an aid completion notification")
			second.free()
		if rendered and not failed:
			await capture_phase(view, owner, args[2], {"label": row.name}, captures)
			if not captures.is_empty(): captures[-1].exhaust["lift"] = view.exhaust.lift_intensity
		print("Surface phase: %s checksum=%s" % [row.name, state.checksum])
	var output := FileAccess.open(args[2].path_join("surface-report.json"), FileAccess.WRITE)
	if output != null:
		var hashes := {}
		for name in ["studies/captures/native_surface_capture.gd", "studies/captures/native_voyage_capture.gd", "scripts/native/native_start_shell.gd", "scripts/native/native_flight_view.gd", "scripts/native/native_main_exhaust.gd", "scripts/characters/hopper_presentation.gd", "scripts/ui/pause_menu.gd", "scripts/world/planet_stream.gd", "shaders/terrain.gdshader", "shaders/native_close_composite.gdshader", "bin/libapsis_freedom_bridge.so"]:
			hashes[name] = FileAccess.get_sha256("res://" + name)
		output.store_string(JSON.stringify({"sources_sha256": hashes, "manifest_sha256": FileAccess.get_sha256(args[0].path_join("manifest.json")), "source_sha256": FileAccess.get_sha256(args[0].path_join("source.json")), "asset_license_records": "assets/native/freedom-starter-01/licenses", "scope": manifest.scope, "pass": not failed, "model_retained": not failed, "engine": Engine.get_version_info(), "renderer": RenderingServer.get_video_adapter_name() if rendered else "headless", "captures": captures}, "\t") + "\n")
		output.close()
	else: check(false, "Surface report unavailable")
	shell.free()
	print("Native surface lifecycle: %d failures" % int(failed))
	quit(1 if failed else 0)
