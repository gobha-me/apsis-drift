extends SceneTree
## Policy-two bridge acceptance, not a rendered/controller playtest.
## Arguments: atmospheric snapshot, independently generated airless snapshot.
var failures := 0
const NEUTRAL := [0, 0, 0, 0, 0, 0, 0]
const TRANSLATION_FIELDS := ["latitude", "longitude", "altitude", "clearance", "speed",
	"periapsis", "apoapsis", "horizontal_speed", "climb_rate", "air_density",
	"effective_air_density", "dynamic_pressure", "acceleration", "bound_orbit",
	"clear_orbit", "floor_guard"]

func check(ok: bool, message: String) -> bool:
	if not ok:
		failures += 1
		push_error(message)
	return ok

func create(snapshot: String, model: int = 3) -> Variant:
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(bridge.initialize(snapshot), "Orbit-assist snapshot initialization failed"):
		return null
	var enabled: bool = bridge.enable_orbit_practice() if model == 3 else (bridge.enable_surface_practice() if model == 2 else bridge.enable_thrust_flight())
	if not check(enabled, "Orbit-assist model selection failed"):
		return null
	return bridge

func identity(state: Dictionary, model: int = 3) -> void:
	check(state.flight_model == "thrust-lab-%d" % model and str(state.checksum).begins_with("lab%d:" % model), "Flight/checksum model identity mismatch")
	check(state.angular_model == (1 if model == 1 else 2), "Orbit assist changed angular model")
	check(state.translation_policy == (2 if model == 3 else 1), "Translation policy identity mismatch")
	check(is_finite(state.translation_assist_weight) and state.translation_assist_weight >= 0 and state.translation_assist_weight <= 1, "Invalid translation-assist envelope")
	check(is_finite(state.effective_air_density) and state.effective_air_density >= 0, "Invalid effective aerodynamic density")

func translation(state: Dictionary) -> Dictionary:
	var result := {}
	for key in TRANSLATION_FIELDS:
		result[key] = state[key]
	return result

func step(bridge: Variant, axes: Array, assist: bool, count: int = 1) -> void:
	for tick in count:
		if not check(bridge.advance_thrust(1.0 / 120, PackedFloat64Array(axes), assist), "Policy-two bridge step failed: %s" % bridge.get_last_error()):
			return

func transaction_contract(snapshot: String) -> void:
	var bridge: Variant = create(snapshot)
	if bridge == null:
		return
	var before: Dictionary = bridge.get_state()
	identity(before)
	for malformed in [[], [0], [0, 0, 0, 0, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0]]:
		check(not bridge.advance_thrust(1.0 / 120, PackedFloat64Array(malformed), true), "Malformed policy-two demand size accepted")
		check(bridge.get_state() == before, "Refused demand mutated flight or clock")
	for channel in 7:
		for value in [NAN, INF, -1.01, 1.01]:
			var axes := PackedFloat64Array(NEUTRAL)
			axes[channel] = value
			check(not bridge.advance_thrust(1.0 / 120, axes, true), "Malformed policy-two scalar accepted")
			check(bridge.get_state() == before, "Refused scalar mutated policy-two state")
	for elapsed in [-1, NAN, INF, 61]:
		check(not bridge.advance_thrust(elapsed, PackedFloat64Array(NEUTRAL), true), "Malformed policy-two elapsed time accepted")
		check(bridge.get_state() == before, "Refused elapsed time mutated state")
	check(not bridge.enable_orbit_practice() and not bridge.enable_surface_practice() and not bridge.enable_thrust_flight(), "Late model selection replaced policy two")
	check(bridge.get_state() == before, "Late model selection mutated flight")
	check(not bridge.initialize("{}") and bridge.get_state() == before, "Malformed reset replaced policy-two state")
	check(bridge.enable_streaming() and bridge.start_practice(true), "Reentry relocation failed")
	identity(bridge.get_state())
	check(bridge.set_survey_pose(0.25, 0.4, 30000), "Policy-two survey relocation failed")
	identity(bridge.get_state())
	check(bridge.initialize(snapshot) and bridge.enable_orbit_practice(), "Policy-two explicit reset failed")
	check(bridge.get_state() == before, "Policy-two reset changed original state")
	for model in [1, 2]:
		var old: Variant = create(snapshot, model)
		if old != null:
			identity(old.get_state(), model)
			check(old.enable_streaming() and old.start_practice(false), "Historical practice relocation failed")
			identity(old.get_state(), model)

func orbital_coast_contract(snapshot: String) -> void:
	var assisted: Variant = create(snapshot)
	var manual: Variant = create(snapshot)
	if assisted == null or manual == null:
		return
	for bridge in [assisted, manual]:
		check(bridge.enable_streaming() and bridge.start_practice(false), "Policy-two orbit practice failed")
		check(bridge.get_state().dynamic_pressure == 0, "Orbit relocation retained aerodynamic pressure before the first tick")
		step(bridge, [0, 0, 0.35, -0.2, 0.3, 0, 0], false, 120)
	var acquired: Dictionary = assisted.get_state()
	check(acquired.angular_velocity_body.length() > 0.3, "Orbit fixture did not acquire mixed-axis spin")
	check(acquired.clear_orbit, "Orbit fixture is not a clear orbit")
	for second in 10:
		step(assisted, NEUTRAL, true, 120)
		step(manual, NEUTRAL, false, 120)
		var a: Dictionary = assisted.get_state()
		var m: Dictionary = manual.get_state()
		check(translation(a) == translation(m), "Neutral-space assist changed authoritative translation at second %d" % second)
		check(a.translation_assist_weight == 0 and a.effective_air_density == 0 and a.dynamic_pressure == 0, "Space retained a hidden assist/drag tail")
		check(a.rcs_acceleration.length() == 0 and m.rcs_acceleration.length() == 0, "Neutral-space translation used thrusters")
		check(a.clear_orbit and not a.floor_guard, "Neutral assist lost clear orbit or touched floor guard")
	var settled: Dictionary = assisted.get_state()
	var spinning: Dictionary = manual.get_state()
	check(settled.attitude_stabilized and settled.angular_velocity_body.length() < 0.001, "Space assist did not settle attitude")
	check(not spinning.attitude_stabilized and spinning.angular_velocity_body.length() > 0.3, "Manual space attitude lost angular coast")
	check(not settled.body_basis.is_equal_approx(spinning.body_basis), "Different attitudes were not reflected in presentation")
	var before_guidance: Dictionary = assisted.get_state()
	var guidance: Dictionary = assisted.get_flight_guidance(1)
	check(guidance.ok and guidance.assist and not guidance.thrust_active, "Guidance confuses attitude assist with trajectory-changing thrust")
	check(assisted.get_state() == before_guidance, "Guidance polling changed orbital state")
	# A pilot-requested burn is intentionally outside the neutral-coast promise.
	step(assisted, [0.7, 0, 0, 0, 0, 0, 0], true, 120)
	step(manual, NEUTRAL, false, 120)
	check(translation(assisted.get_state()) != translation(manual.get_state()), "Explicit main thrust was ignored to preserve an orbit artificially")
	check(assisted.get_flight_guidance(1).thrust_active, "Guidance hides a real powered maneuver")

func atmosphere_contract(snapshot: String) -> void:
	var bridge: Variant = create(snapshot)
	if bridge == null:
		return
	var surface: Dictionary = bridge.get_state()
	check(surface.atmosphere_edge > 0 and surface.translation_assist_weight > 0, "Atmospheric fixture lacks near-surface support")
	check(bridge.enable_streaming(), "Atmospheric survey streaming setup failed")
	var edge: float = surface.atmosphere_edge
	var previous := -1.0
	for altitude in [edge + 1000, edge + 1, edge, edge - 1, edge - 1000, edge - 20000, 30000.0]:
		check(bridge.set_survey_pose(0.25, 0.4, altitude), "Atmosphere-boundary survey failed")
		var state: Dictionary = bridge.get_state()
		identity(state)
		var weight: float = state.translation_assist_weight
		check(weight >= previous, "Assist envelope reverses while descending into atmosphere")
		previous = weight
		if altitude > edge:
			check(weight == 0 and state.effective_air_density == 0, "Above-edge assist/drag is not exactly zero")
		if absf(altitude - edge) <= 1:
			check(weight < 0.00000001, "Atmosphere boundary has a discontinuous assist step")
		check(state.effective_air_density <= state.air_density + 0.000000001, "Taper invents extra aerodynamic density")
	check(previous == 1, "Dense-air translational assistance was lost")

func airless_contract(snapshot: String) -> void:
	var assisted: Variant = create(snapshot)
	var manual: Variant = create(snapshot)
	if assisted == null or manual == null:
		return
	var initial: Dictionary = assisted.get_state()
	if not check(initial.atmosphere_edge == 0 and initial.air_density == 0, "Second snapshot must be an actually generated airless planet"):
		return
	check(initial.translation_assist_weight == 0 and initial.effective_air_density == 0, "Airless near-surface state silently enables hover")
	step(assisted, NEUTRAL, true, 120)
	step(manual, NEUTRAL, false, 120)
	check(translation(assisted.get_state()) == translation(manual.get_state()), "Airless near-surface assist brakes ballistic translation")
	check(assisted.get_state().climb_rate < -0.1 and not assisted.get_state().floor_guard, "Airless test did not demonstrate unsupported free fall")
	for bridge in [assisted, manual]:
		check(bridge.enable_streaming() and bridge.start_practice(false), "Airless orbit practice failed")
	step(assisted, NEUTRAL, true, 240)
	step(manual, NEUTRAL, false, 240)
	check(translation(assisted.get_state()) == translation(manual.get_state()), "Airless orbital assist changed translation")

func cadence_contract(snapshot: String) -> void:
	var expected := {}
	for fps in [30, 60, 144]:
		var bridge: Variant = create(snapshot)
		if bridge == null:
			return
		check(bridge.enable_streaming() and bridge.start_practice(false), "Cadence orbit fixture failed")
		for frame in fps * 2:
			check(bridge.advance_thrust(1.0 / fps, PackedFloat64Array([0.2, 0, 0.3, -0.2, 0.4, 0.15, -0.1]), true), "Policy-two cadence tick failed")
		var state: Dictionary = bridge.get_state()
		identity(state)
		check(state.tick == 240, "Policy-two cadence consumed incorrect tick count")
		if expected.is_empty():
			expected = state
		else:
			check(state == expected, "Renderer cadence changed orbit-assist simulation state")
	print("Orbit-assist cadence checksum: ", expected.get("checksum", "unavailable"))

func _initialize() -> void:
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	var arguments := OS.get_cmdline_user_args()
	if arguments.size() != 2 or not ClassDB.class_exists("FreedomBridge"):
		push_error("Expected atmospheric and airless snapshot paths plus FreedomBridge")
		quit(1)
		return
	var atmospheric := FileAccess.get_file_as_string(arguments[0])
	var airless := FileAccess.get_file_as_string(arguments[1])
	transaction_contract(atmospheric)
	orbital_coast_contract(atmospheric)
	atmosphere_contract(atmospheric)
	airless_contract(airless)
	cadence_contract(atmospheric)
	print("Orbit-preserving assist bridge: %d failures" % failures)
	quit(0 if failures == 0 else 1)
