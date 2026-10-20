extends RefCounted
## Real home service and existing local-jump UI, with fixed saved history.
const MoonView = preload("res://scripts/native/native_moon_view.gd")
const MinorView = preload("res://scripts/native/native_minor_view.gd")
var tree: SceneTree
var shell: Control
var owner: Variant
var assets := ""
var output := ""
var failures := 0
var neutral := PackedFloat64Array()
var original_knowledge := {}
var legacy := {}
var report := {"version": 1, "completed": false, "services": [], "legs": []}

static func run(test: SceneTree, live_shell: Control, bridge: Variant, package: String, destination: String, first: String) -> int:
	var helper: RefCounted = load("res://tests/native_local_route_reuse.gd").new()
	helper.tree = test
	helper.shell = live_shell
	helper.owner = bridge
	helper.assets = package
	helper.output = destination
	await helper.exercise(first)
	return int(helper.failures)

func check(value: bool, message: String) -> bool:
	if not value:
		failures += 1
		push_error("Local route reuse: " + message)
	return value

func saved(name: String) -> PackedByteArray:
	var path := output.path_join(name + ".json")
	if not check(owner.save_freedom_as(path), "Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

func component(bytes: PackedByteArray, key: String) -> Dictionary:
	var value: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	for depth in 16:
		if not value is Dictionary: break
		if value.get(key) is Dictionary: return value[key]
		value = value.get("voyage", value.get("base"))
	check(false, "Saved component unavailable: " + key)
	return {}

func refresh() -> Control:
	var view: Control = shell.current_view
	view.state = owner.get_freedom_flight_state()
	view.update_view(0.0)
	return view

func continued(name: String, staged: bool = false) -> bool:
	var before := saved(name)
	if not check(not before.is_empty() and before.size() <= 1048576 and component(before, "knowledge") == original_knowledge, "Continue checkpoint changed personal knowledge or exceeded save bound"): return false
	var routes: Dictionary = component(before, "travel").get("origin_routes", {})
	if not routes.is_empty():
		if legacy.is_empty(): legacy = routes.legacy.duplicate(true)
		if not check(routes.size() == 5 and routes.version == 1 and routes.legacy == legacy, "Fixed route shape or immutable legacy changed"): return false
	var path := output.path_join(name + ".json")
	var copy_path := output.path_join(name + "-continued.json")
	if staged:
		# One ordinary shell Continue covers full presentation staging. Other
		# phases exercise actual codec/session Continue without repeatedly
		# rebuilding the unchanged Moon cover during this bounded UI case.
		if not check(shell.select_start(owner, {"mode": "continue", "value": path, "assets": assets}), "Ordinary staged Continue refused: " + shell.error): return false
		shell.current_view.set_process(false)
		return check(shell.current_view.paused and saved(name + "-continued") == before, "Staged Continue changed complete bytes or resumed flight")
	# As in first_jump, this peer owns C++ fixture state only. It makes no
	# renderer/model-readiness claim; the live shell uses normal admission.
	var peer: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(peer.stage_freedom_continue(path), "Fixture Continue could not stage: " + str(peer.get_last_error())): return false
	var pending: Dictionary = peer.get_pending_freedom_start()
	if not check(not pending.is_empty() and peer.commit_pending_freedom_start(pending.candidate_id), "Fixture Continue could not commit: " + str(peer.get_last_error())): return false
	return check(peer.save_freedom_as(copy_path) and FileAccess.get_file_as_bytes(copy_path) == before, "Independent fixture Continue changed complete saved bytes")

func advance(demand: PackedFloat64Array) -> bool:
	return check(owner.advance_freedom_flight(1.0 / 120.0, demand, false), "Actual flight tick refused: " + str(owner.get_last_error()))

func service() -> bool:
	var view := refresh()
	var start: Dictionary = owner.get_freedom_flight_state()
	if not check(start.attached and start.resources.jump_charges == 1 and start.resources.fraction < 1.0, "Fixture is not the actual captured return with spent resources"): return false
	var capture_tick: String = str(start.tick)
	view.service_button.pressed.emit()
	if not check(view.state.attached and str(view.state.tick) == capture_tick and view.state.resources.jump_charges == 3 and view.state.resources.fraction == 1.0, "Existing service button failed same-tick replenishment"): return false
	report.services.append({"capture_tick": capture_tick, "charges_before": start.resources.jump_charges, "charges_after": 3})
	if not continued("serviced-attached"): return false
	view = refresh()
	view.port_command("release_freedom_port")
	var withdraw := neutral.duplicate()
	withdraw[4] = 0.25
	for tick in 720:
		if not advance(withdraw): return false
	view = refresh()
	var frozen := saved("free-service-before")
	return check(not view.state.attached and view.state.resources.jump_charges == 3 and not owner.replenish_freedom_resources() and saved("free-service-refused") == frozen, "Actual release/withdrawal changed charges or allowed remote service")

func leg(label: String, destination: String) -> bool:
	var view := refresh()
	var start: Dictionary = owner.get_freedom_flight_state()
	view.observe_neutral_controls()
	if destination == "home": view.select_home()
	elif destination == "moon": view.local_moon_button.pressed.emit()
	else: view.local_minor_button.pressed.emit()
	var selected: Dictionary = owner.get_freedom_flight_state()
	if not check(selected.jump.phase == "idle" and not str(selected.jump.selected).is_empty() and selected.resources == start.resources and selected.tick == start.tick, "Existing destination button refused or billed time/resources"): return false
	view.jump_requested()
	var spool := saved(label + "-spool")
	if not check(owner.get_freedom_flight_state().jump.phase == "spool", "Actual local spool failed"): return false
	# Active destination changes are refused atomically by the C++ transaction.
	if not check(not owner.select_freedom_moon_jump() and not owner.select_freedom_minor_jump() and saved(label + "-switch-refused") == spool, "Active spool permitted a destination switch"): return false
	if not continued(label + "-spool"): return false
	for tick in 600:
		if not advance(neutral): return false
		if tick == 358 and not check(owner.get_freedom_flight_state().resources.jump_charges == start.resources.jump_charges, "Spool billed before commitment"): return false
		if tick == 359:
			if not check(owner.get_freedom_flight_state().resources.jump_charges == start.resources.jump_charges - 1, "Commit did not bill exactly one charge"): return false
			if not continued(label + "-transit", destination != "home"): return false
	var arrived: Dictionary = owner.get_freedom_flight_state()
	if not check(arrived.jump.phase == "idle" and int(arrived.tick) == int(start.tick) + 600 and arrived.resources.jump_charges == start.resources.jump_charges - 1, "Local arrival clock/charge differs"): return false
	shell.switch_journey_view()
	view = shell.current_view
	view.set_process(false)
	if not check(view.paused and view.error.is_empty() and ((view is MinorView) if destination == "minor" else ((view is MoonView and not view is MinorView) if destination == "moon" else not view is MoonView)), "Actual arrival selected the wrong ordinary paused view"): return false
	report.legs.append({"label": label, "destination": destination, "departure_tick": start.tick, "arrival_tick": arrived.tick, "charges_before": start.resources.jump_charges, "charges_after": arrived.resources.jump_charges})
	return continued(label + "-arrival")

func exercise(first: String) -> void:
	if not check(first in ["moon", "minor"] and DirAccess.make_dir_recursive_absolute(output) == OK, "Invalid bounded route/output"): return
	neutral.resize(12)
	original_knowledge = component(saved("start"), "knowledge").duplicate(true)
	if await service():
		var view := refresh()
		# The new excursion returns using its actual outgoing home assistance.
		var assistance: bool = not bool(view.state.assistance)
		view.controls_menu.assistance_requested.emit(assistance)
		if check(owner.get_freedom_flight_state().assistance == assistance, "Actual home assistance change failed"):
			if await leg("reuse-out", first):
				var before := saved("direct-before")
				var refused: bool = not owner.select_freedom_minor_jump() if first == "moon" else not owner.select_freedom_moon_jump()
				if check(refused and saved("direct-after") == before, "Direct companion transfer changed Save"):
					if await leg("reuse-home", "home"):
						check(owner.get_freedom_flight_state().assistance == assistance and owner.get_freedom_flight_state().resources.jump_charges == 1, "Return lost actual outgoing home profile or reserve charge")
	report.completed = failures == 0 and report.legs.size() == 2 and report.services.size() == 1 and not legacy.is_empty()
	check(report.completed, "Actual service and reused local round trip did not finish")
	report.failures = failures
	var file := FileAccess.open(output.path_join("report.json"), FileAccess.WRITE)
	if check(file != null, "Route report unavailable"):
		file.store_string(JSON.stringify(report, "  ") + "\n")
		file.close()
	print("Actual local route reuse: %d failures; %d services, %d charged legs" % [failures, report.services.size(), report.legs.size()])
