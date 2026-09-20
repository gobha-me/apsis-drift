extends SceneTree
## Real input resolution and native physics; synthetic rig, no art qualification.
const Controls = preload("res://player_input.gd")
const Motion = preload("res://pilot_motion.gd")
const Fixture = preload("res://pilot_motion_test.gd")
var failures := 0

func check(value: bool, reason: String) -> void:
	if not value:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	call_deferred("run")

func joy_axis(axis: int, value: float) -> void:
	var event := InputEventJoypadMotion.new()
	event.device = 0
	event.axis = axis
	event.axis_value = value
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func key(code: int, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)
	Input.flush_buffered_events()

func run() -> void:
	var snapshot := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--snapshot="):
			snapshot = argument.trim_prefix("--snapshot=")
	if not snapshot.is_absolute_path():
		push_error("Requires --snapshot=/absolute/native-snapshot.json")
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		quit(1)
		return
	var controls := Controls.new()
	controls.persist = false
	controls.thrust_mode = true
	root.add_child(controls)
	controls.device = 0
	controls.install()
	controls.sample()
	var rig: Node3D = Fixture.fixture()
	root.add_child(rig)
	var motion := Motion.new()
	root.add_child(motion)
	check(motion.configure(rig), "Configure synthetic seated fixture")
	joy_axis(JOY_AXIS_LEFT_Y, 1.0)
	var pad: PackedFloat64Array = controls.sample().thrust_axes
	check(pad[2] == 1.0, "Controller pitch did not resolve")
	check(motion.update_thrust_axes(pad, true, 0.1), "Controller semantic demand refused")
	var pad_pose: Vector4 = motion.diagnostics().blend
	joy_axis(JOY_AXIS_LEFT_Y, 0.0)
	controls.sample()
	motion.reset_neutral()
	key(KEY_I, true)
	var keyboard: PackedFloat64Array = controls.sample().thrust_axes
	check(keyboard == pad, "Equivalent keyboard/controller demands differ")
	check(motion.update_thrust_axes(keyboard, true, 0.1), "Keyboard semantic demand refused")
	check(motion.diagnostics().blend == pad_pose, "Device identity changed presentation pose")
	key(KEY_I, false)
	controls.sample()
	check(controls.rebind("pitch_up", "key", {"kind": "key", "code": KEY_J}), "Remap fixture rejected")
	controls.sample()
	motion.reset_neutral()
	key(KEY_J, true)
	var remapped: PackedFloat64Array = controls.sample().thrust_axes
	check(remapped == pad and motion.update_thrust_axes(remapped, true, 0.1), "Remapped key lost semantic pitch")
	check(motion.diagnostics().blend == pad_pose, "Remapping changed arm/control pose")
	key(KEY_J, false)
	controls.sample()
	# Head-look has already suppressed yaw/heave in resolved input. Animation
	# must not peek behind that decision at raw controller axis values.
	joy_axis(JOY_AXIS_RIGHT_X, 0.7)
	joy_axis(JOY_AXIS_RIGHT_Y, -0.6)
	Input.action_press("pilot_look_hold", 1.0)
	motion.reset_neutral()
	var looking: Dictionary = controls.sample()
	check(looking.look.length() > 0 and looking.thrust_axes[3] == 0 and looking.thrust_axes[6] == 0, "Look fixture did not suppress flight")
	check(motion.update_thrust_axes(looking.thrust_axes, true, 0.1), "Look neutral demand refused")
	check(motion.diagnostics().blend == Vector4.ZERO, "Head-look moved flight grips")
	Input.action_release("pilot_look_hold")
	check(controls.sample().recenter, "Look release did not recenter")
	joy_axis(JOY_AXIS_RIGHT_X, 0)
	joy_axis(JOY_AXIS_RIGHT_Y, 0)
	controls.sample()
	# Disabling controls covers menu/focus safety; disconnect also requires an
	# explicit resume in the application. Held controls cannot animate through it.
	joy_axis(JOY_AXIS_LEFT_Y, 1)
	check(motion.update_thrust_axes(controls.sample().thrust_axes, true, 0.1), "Pre-pause motion")
	controls.set_enabled(false)
	var before_pause: Vector4 = motion.diagnostics().blend
	check(motion.update_thrust_axes(controls.sample().thrust_axes, false, 0.1), "Inactive presentation rejected")
	check(motion.diagnostics().blend.length() < before_pause.length(), "Inactive pose moved away from neutral")
	check(motion.update_thrust_axes(controls.sample().thrust_axes, false, 0.1), "Bounded neutral return rejected")
	check(motion.diagnostics().blend == Vector4.ZERO, "Menu/focus gate retained arm command")
	controls.connection_changed(0, false)
	check(motion.update_thrust_axes(controls.sample().thrust_axes, false, 0.1), "Disconnect neutral rejected")
	check(motion.diagnostics().blend == Vector4.ZERO, "Disconnect retained pose demand")
	joy_axis(JOY_AXIS_LEFT_Y, 0)
	# Native simulation sees identical resolved demands with or without the
	# presentation consumer. Pose work is never a throttle or simulation owner.
	var source := FileAccess.get_file_as_string(snapshot)
	var animated: Variant = ClassDB.instantiate("FreedomBridge")
	var reference: Variant = ClassDB.instantiate("FreedomBridge")
	for bridge in [animated, reference]:
		check(bridge.initialize(source) and bridge.enable_orbit_practice(), "Native fixture rejected")
	var demand := PackedFloat64Array([0.3, 0.1, 0.2, -0.3, 0.4, -0.1, 0.2])
	for tick in 120:
		var before: Dictionary = animated.get_state()
		check(motion.update_thrust_axes(demand, true, 1.0/120), "Continuous semantic demand refused")
		check(animated.get_state() == before, "Arm presentation mutated authoritative state")
		check(animated.advance_thrust(1.0/120, demand, true), "Animated native step failed")
		check(reference.advance_thrust(1.0/120, demand, true), "Reference native step failed")
	check(animated.get_state() == reference.get_state(), "Pilot presentation changed deterministic flight")
	motion.free()
	rig.free()
	controls.free()
	print("Pilot motion input/native integration: %d failures; synthetic rig, remapping/head-look/safety and unchanged flight" % failures)
	quit(0 if failures == 0 else 1)
