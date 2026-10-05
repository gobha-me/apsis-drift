extends SceneTree
## Model-two bridge contract; existing model-one fixtures remain unchanged.
var failures := 0
const NEUTRAL := [0, 0, 0, 0, 0, 0, 0]

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func create(snapshot: String, native: bool) -> Variant:
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	check(bridge.initialize(snapshot), "Rotation fixture snapshot initialization failed")
	check(bridge.enable_surface_practice() if native else bridge.enable_thrust_flight(), "Rotation model selection failed")
	return bridge

func identity(state: Dictionary, version: int) -> void:
	check(state.flight_model == "thrust-lab-%d" % version and str(state.checksum).begins_with("lab%d:" % version), "Angular model/checksum identity mismatch")
	check(state.angular_model == version, "Explicit angular model missing")
	check(state.angular_velocity_body.is_finite() and state.applied_torque_body.is_finite(), "Angular telemetry is nonfinite")
	check(state.body_basis.is_finite() and absf(state.body_basis.determinant() - 1) < 0.00001, "Quaternion-derived presentation basis invalid")

func _initialize() -> void:
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 1 or not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var snapshot := FileAccess.get_file_as_string(arguments[0])
	var bridge: Variant = create(snapshot, true)
	var original: Dictionary = bridge.get_state()
	identity(original, 2)
	check(original.attitude_stabilized and original.assist, "Native start is not assisted model two")
	for malformed in [[], [0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0]]:
		check(not bridge.advance_thrust(1.0 / 120, PackedFloat64Array(malformed), false), "Malformed buffer accepted by model two")
		check(bridge.get_state() == original, "Malformed model-two buffer mutated state")
	for channel in 7:
		for value in [NAN, INF, -1.01, 1.01]:
			var axes := PackedFloat64Array(NEUTRAL)
			axes[channel] = value
			check(not bridge.advance_thrust(1.0 / 120, axes, false), "Malformed model-two scalar accepted")
			check(bridge.get_state() == original, "Malformed model-two scalar mutated clock/state")
	for elapsed in [-1, NAN, INF, 61]:
		check(not bridge.advance_thrust(elapsed, PackedFloat64Array(NEUTRAL), false), "Malformed model-two elapsed time accepted")
		check(bridge.get_state() == original, "Malformed elapsed time mutated model two")
	check(not bridge.enable_thrust_flight() and not bridge.enable_surface_practice() and bridge.get_state() == original, "Late model selection replaced native state")
	check(not bridge.initialize("{}") and bridge.get_state() == original, "Malformed reset replaced quaternion state")
	check(bridge.enable_streaming() and bridge.start_practice(false), "Model-two orbit practice failed")
	identity(bridge.get_state(), 2)
	check(not bridge.get_state().attitude_stabilized, "Orbit practice does not expose rotational coast")
	for tick in 120:
		check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array([0, 0, 0, 0, 0.4, 0, 0]), false), "Model-two commanded roll failed")
	var spinning: Dictionary = bridge.get_state()
	check(spinning.angular_velocity_body.length() > 0.5, "Rotation fixture did not acquire appreciable spin")
	for tick in 240:
		check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array(NEUTRAL), false), "Model-two rotational coast failed")
	var coasting: Dictionary = bridge.get_state()
	identity(coasting, 2)
	check(not coasting.attitude_stabilized and not coasting.assist and coasting.applied_torque_body.length() == 0, "OFF coast secretly stabilizes attitude")
	check(coasting.angular_velocity_body.distance_to(spinning.angular_velocity_body) < 0.00001, "Native principal-axis rotation damped after look/control release")
	check(not coasting.body_basis.is_equal_approx(spinning.body_basis), "Coasting spin is not reflected in presentation basis")
	check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array(NEUTRAL), true), "ON rotational recovery failed")
	var first_recovery: Dictionary = bridge.get_state()
	check(first_recovery.attitude_stabilized and first_recovery.angular_velocity_body.length() > 0.4, "ON recovery snapped spin to zero instead of applying torque")
	for tick in 240:
		check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array(NEUTRAL), true), "ON stabilizing tick failed")
	check(bridge.get_state().angular_velocity_body.length() < 0.001, "ON stabilization did not recover angular motion")
	check(bridge.start_practice(true), "Model-two reentry practice failed")
	identity(bridge.get_state(), 2)
	check(not bridge.get_state().assist and not bridge.get_state().attitude_stabilized, "Reentry practice lost coast mode")
	check(bridge.set_survey_pose(0.25, 0.4, 30000), "Model-two survey relocation failed")
	identity(bridge.get_state(), 2)
	check(bridge.get_state().angular_velocity_body.length() == 0, "Fresh survey retained stale spin")
	check(bridge.initialize(snapshot) and bridge.enable_surface_practice(), "Explicit native model-two reset failed")
	check(bridge.get_state() == original, "Explicit native reset changed initial model-two state")
	var legacy: Variant = create(snapshot, false)
	identity(legacy.get_state(), 1)
	for tick in 120:
		check(legacy.advance_thrust(1.0 / 120, PackedFloat64Array([0, 0, 0, 0, 0.4, 0, 0]), false), "Legacy rotation command failed")
	for tick in 120:
		check(legacy.advance_thrust(1.0 / 120, PackedFloat64Array(NEUTRAL), false), "Legacy damping tick failed")
	identity(legacy.get_state(), 1)
	check(legacy.get_state().attitude_stabilized and legacy.get_state().angular_velocity_body.length() == 0, "Standalone model-one historical damping changed")
	var expected := {}
	for fps in [30, 60, 144]:
		var replay: Variant = create(snapshot, true)
		for frame in fps * 2:
			check(replay.advance_thrust(1.0 / fps, PackedFloat64Array([0.7, 0.1, 0.3, -0.2, 0.4, 0.15, -0.1]), false), "Native model-two cadence step failed")
		var state: Dictionary = replay.get_state()
		identity(state, 2)
		check(state.tick == 240, "Model-two cadence consumed incorrect tick count")
		if expected.is_empty():
			expected = state
		else:
			check(state == expected, "Renderer cadence changed native quaternion state/checksum")
	print("Rotation-coast bridge: %d failures; model-two cadence %s" % [failures, expected.get("checksum", "missing")])
	quit(0 if failures == 0 else 1)
