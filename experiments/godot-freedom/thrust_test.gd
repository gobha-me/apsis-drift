extends SceneTree
## Boundary validation first; then deterministic C++ force/attitude integration.
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func make_bridge(snapshot: String) -> Variant:
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.initialize(snapshot), "Initialize failed")
	check(bridge.enable_thrust_flight(), "Enable thrust failed")
	return bridge

func _initialize() -> void:
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var snapshot := FileAccess.get_file_as_string(arguments[0])
	var bridge: Variant = make_bridge(snapshot)
	var original: Dictionary = bridge.get_state()
	check(original.flight_model == "thrust-lab-1" and str(original.checksum).begins_with("lab1:"), "Version identity missing")
	for axes in [[], [0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0], [-0.1, 0, 0, 0, 0, 0, 0], [0, -0.1, 0, 0, 0, 0, 0]]:
		check(not bridge.advance_thrust(1.0 / 120, PackedFloat64Array(axes), true), "Invalid buffer accepted")
		check(bridge.get_state() == original, "Rejected buffer mutated state")
	for channel in 7:
		for value in [NAN, INF, -1.01, 1.01]:
			var axes := PackedFloat64Array([0, 0, 0, 0, 0, 0, 0])
			axes[channel] = value
			check(not bridge.advance_thrust(1.0 / 120, axes, true), "Invalid channel accepted")
			check(bridge.get_state() == original, "Rejected channel mutated state")
	for elapsed in [-1, NAN, INF, 61]:
		check(not bridge.advance_thrust(elapsed, PackedFloat64Array([0, 0, 0, 0, 0, 0, 0]), true), "Invalid time accepted")
		check(bridge.get_state() == original, "Rejected time mutated clock/state")
	check(not bridge.advance(1.0 / 120, 1), "Legacy controls accepted by new session")
	check(not bridge.advance_analog(1.0 / 120, PackedFloat64Array([1, 0, 0, 0])), "Legacy analog accepted by new session")
	check(not bridge.enable_thrust_flight(), "Double enable reset lab")
	check(not bridge.initialize("{}"), "Invalid snapshot reset lab")
	check(bridge.get_state() == original, "Rejected operation replaced session")
	check(not bridge.start_practice(false) and bridge.get_state() == original, "Practice without streaming mutated state")
	var catalog: Array = bridge.get_sky_catalog()
	check(catalog.size() == 1536 and catalog == bridge.get_sky_catalog(), "Catalog nondeterministic or unbounded")
	var expected := {}
	for fps in [30, 60, 144]:
		var session: Variant = make_bridge(snapshot)
		for frame in fps * 2:
			check(session.advance_thrust(1.0 / fps, PackedFloat64Array([0.7, 0.1, 0.3, -0.2, 0.4, 0.15, -0.1]), true), "Force step failed")
		var state: Dictionary = session.get_state()
		check(state.tick == 240 and state.body_basis.is_finite() and absf(state.body_basis.determinant() - 1) < 0.00001, "Invalid attitude")
		check(not state.body_basis.is_equal_approx(original.body_basis), "Pitch/roll not presented")
		if expected.is_empty():
			expected = state
		else:
			check(state == expected, "Presentation cadence changed thrust session")
	var legacy: Variant = ClassDB.instantiate("FreedomBridge")
	check(legacy.initialize(snapshot) and legacy.advance(1.0 / 120, 1), "Legacy session failed")
	var legacy_state: Dictionary = legacy.get_state()
	check(not legacy.enable_thrust_flight() and legacy.get_state() == legacy_state, "Late model switch mutated old replay")
	var practice: Variant = make_bridge(snapshot)
	check(practice.enable_streaming() and practice.start_practice(false), "Orbit practice failed")
	var unchanged: Dictionary = practice.get_state()
	for offset in [Vector3(NAN, 0, 0), Vector3(0, INF, 0), Vector3(5001, 0, 0)]:
		check(is_nan(practice.camera_clearance(offset)) and practice.get_state() == unchanged, "Bad camera probe mutated flight")
	check(absf(practice.camera_clearance(Vector3.ZERO) - practice.get_state().clearance) < 0.01, "Camera probe inconsistent with terrain")
	check(practice.get_state().clear_orbit and not practice.get_state().assist, "Orbit practice not coasting orbit")
	check(practice.start_practice(true), "Re-entry practice failed")
	check(practice.get_state().climb_rate < -600 and not practice.get_state().clear_orbit, "Entry practice not descending")
	check(practice.set_survey_pose(0.25, 0.4, 30000), "Survey setup failed")
	check(practice.get_state().air_density > 0.01 and practice.get_state().speed == 0, "Paused survey has stale atmospheric telemetry")
	print("Thrust bridge contracts: %d failures; cadence checksum %s" % [failures, expected.get("checksum", "missing")])
	quit(0 if failures == 0 else 1)
