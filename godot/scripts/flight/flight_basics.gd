extends MarginContainer
## BSD-3-Clause. Read-only paused reference for native lab/saved flight, not a
## tutorial state machine or flight authority. Bindings come from input owner.
signal back_requested
const TITLES := ["Thrust and momentum", "Space is not orbit", "Flight assistance", "Look without steering", "Guidance and prototype limits"]
const SAVED_TITLES := ["Physical thrust and momentum", "Air and orbital observations", "Physical assistance", "Look without steering", "Origin ports", "Committed saves and limits"]
var saved_flight := false
var saved_wayfarer := false
var controls: Node
var rotational_coasting := false
var orbit_preserving_assist := false
var page := 0
var heading: Label
var body: Label
var scroll: ScrollContainer
var previous_button: Button
var next_button: Button
var back_button: Button
var _layout: VBoxContainer
var _buttons: HBoxContainer
var _paused: Label
var _hint: Label
var _readable_scale := -1.0

static func binding(controls_ref: Node, action: String) -> String:
	return "%s  /  %s" % [controls_ref.binding_label(action, "pad"), controls_ref.binding_label(action, "key")]

static func page_text(index: int, controls_ref: Node, rotation_coasts: bool, translation_coasts: bool) -> String:
	match index:
		0:
			return "Main thrust: %s\nWeak retro thrust: %s\n\nThrust changes velocity; releasing it is not a stop command. There is no forward speed hold. Retro is weaker than the main engines: allow time and room to slow down.\n\nYour ship can point one way while travelling another. Turning alone does not redirect its momentum. Air resistance falls away as the atmosphere thins." % [binding(controls_ref, "forward"), binding(controls_ref, "backward")]
		1:
			return "SPACE describes the surrounding air, not a safe trajectory. You can be in space and still fall back. Build speed along the horizon, not only height.\n\nORBIT ESTABLISHED requires a closed, bound trajectory with periapsis strictly above BOTH the atmosphere edge and 20 km above the planet's reference radius. Periapsis is the predicted lowest point—not current height or terrain clearance.\n\nWatch periapsis on NAV. An escape path is not a closed orbit. Thrust changes the prediction; a clear sky is not proof of safety."
		2:
			var text := "Toggle assist: %s\n\n" % binding(controls_ref, "assist")
			text += "SPACE: with translation controls released, ON and OFF both preserve orbital motion. ON stabilizes rotation; it is not a space brake.\n\n" if translation_coasts else "This older lab model can apply translation support with assist ON. Use OFF for unassisted coasting.\n\n"
			text += "OFF: released sticks allow rotational coasting too. Counter-steer or enable assist to settle a spin. Held sticks request bounded turn rates.\n\n" if rotation_coasts else "This older lab model retains attitude damping even with assist OFF.\n\n"
			text += "Atmospheric support fades through trace air. Airless worlds have no automatic hover support." if translation_coasts else "Atmospheric drag still acts; assist is not invulnerability or autopilot."
			return text
		3:
			return "Hold to look: %s\nCockpit / chase: %s\n\nWhile looking, use your configured look directions. The same hold action orbits the outside camera in chase view.\n\nRelease to recenter. Center the right stick before yaw / rise-fall resumes, so looking does not become an accidental flight command.\n\nThe flight mapping stays the same in atmosphere and space. The remap column shows all current bindings; these examples update after remapping too." % [binding(controls_ref, "look_hold"), binding(controls_ref, "camera")]
		4:
			return "Flight guidance is advisory—not autopilot. Choose it from the controls menu; flight continues while the guidance selector is open. You still steer and apply thrust.\n\nOrbit / Re-entry practice relocates the unsaved flight. It is a test setup, not travel or a checkpoint.\n\nLanding gear is not a landing system. This native lab has no landing/contact collision; its 16 m test floor guard is not a touchdown.\n\nFuel use, jumps and flight saves are not implemented in this native slice. Settings persistence is separate."
	return ""

static func saved_page_text(index: int, controls_ref: Node, wayfarer: bool) -> String:
	match index:
		0:
			return "Main thrust: %s\nWeak retro: %s\nRoll left / right: %s · %s\n\nYour controls request independent physical thrust and torque. Releasing thrust is not a stop command. Retro is weaker than main: allow time and room to slow down.\n\nTurning the ship does not redirect its momentum. Opposed engines can both fire even when their net force cancels. Air resistance changes motion; gravity remains in space." % [binding(controls_ref, "forward"), binding(controls_ref, "backward"), binding(controls_ref, "roll_left"), binding(controls_ref, "roll_right")]
		1:
			return "Altitude is height above the planet's reference sphere, not terrain clearance. Surface speed is relative to the rotating planet; radial rate says whether you are rising or falling. Air density describes the environment, not a safe trajectory.\n\nPeriapsis is a predicted lowest radius from the planet's centre; subtract the reference radius to express its altitude. Apoapsis may be unavailable. Being in space does not mean you are in orbit.\n\nA stable closed orbit is bound, with periapsis above the actual atmosphere boundary. Escape is not a closed orbit. These are central-body observations, not terrain avoidance, landing clearance or autopilot. Thrust changes the prediction."
		2:
			return "Toggle assistance: %s\n\nON uses bounded real actuator torque to stabilize rotation toward the requested angular rate. OFF removes that active stabilization; released rotation can coast. Passive air effects still act.\n\nAssistance is not a translation brake, forward-speed hold or automatic hover. Assistance alone does not brake orbital coasting. Held attitude controls request physical torque fractions; available actuators limit the response.\n\nA saved orbit-hold request is separate from assistance and remains subject to your flight inputs and flight conditions. No hold selector is available in this view." % binding(controls_ref, "assist")
		3:
			return "Hold to look: %s\nCockpit / chase: %s\n\nHold and use your configured look directions to look around or orbit the exterior camera. Release to recenter; center the shared yaw / rise-fall controls before steering resumes. Other independent flight controls remain available during look.\n\nThe mapping stays the same in atmosphere and space. Examples update with remapping; the controls menu lists every binding. A camera choice does not board or seat a pilot." % [binding(controls_ref, "look_hold"), binding(controls_ref, "camera")]
		4:
			if not wayfarer:
				return "This historical saved craft has no supported Wayfarer docking controls. Its placeholder presentation preserves the original craft configuration.\n\nContinue and Save As preserve that craft. This reference does not substitute a Wayfarer, relocate it or board a pilot."
			return "Target D1 / D2 selects a port without moving the craft. Release an attachment before changing target.\n\nCapture needs the current port assessment: outward approach, collar separation at most 0.15 m, full attitude within 3°, inward closure 0–0.3 m/s, lateral motion at most 0.2 m/s and angular speed at most 0.02 rad/s. The complete stowed hull must fit the reserved column. Read the current readiness or refusal in the flight view or controls menu.\n\nCapture locks your craft to the station and matches its motion. Thrusters remain off while attached. Release unlocks the craft without changing its position or velocity. %s withdraws below the dock; %s brakes. Clear the 12 m column before forward thrust.\n\nApproach port with thrusters helps an aligned craft below the selected port within 150 m. It uses real propulsion, yields to manual input and has a 60-second limit. Capture remains your action. Pause/focus loss cancels; Continue never arms it. Docking does not board or seat the pilot." % [binding(controls_ref, "fall"), binding(controls_ref, "rise")]
		5:
			return "Continue opens paused. Save As records the last committed flight state, including your craft, journey and any port attachment. Held controls and camera offsets are separate from that save.\n\nCancel or failed Save As stays paused. Quit does not autosave. Release every mapped control before explicitly resuming; focus return alone never resumes. Control preferences are separate from world saves.\n\nThis saved view offers no practice relocation or reset. The current boarding route is a prototype. Planetary touchdown, recovery and the complete planetary journey remain incomplete. Fuel accounting and jump travel are not implemented here. Landing gear appearance is not a qualified landing system."
	return ""

func titles() -> Array:
	return SAVED_TITLES if saved_flight else TITLES

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		add_theme_constant_override("margin_" + side, 32)
	var layout := VBoxContainer.new()
	_layout = layout
	layout.add_theme_constant_override("separation", 16)
	add_child(layout)
	heading = Label.new()
	heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	heading.add_theme_font_size_override("font_size", 30)
	layout.add_child(heading)
	var paused := Label.new()
	_paused = paused
	paused.text = "FLIGHT PAUSED  /  READ-ONLY REFERENCE"
	paused.add_theme_font_size_override("font_size", 21)
	paused.modulate = Color(0.65, 0.86, 0.9)
	layout.add_child(paused)
	scroll = ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.follow_focus = true
	layout.add_child(scroll)
	body = Label.new()
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_theme_font_size_override("font_size", 26)
	scroll.add_child(body)
	var buttons := HBoxContainer.new()
	_buttons = buttons
	buttons.add_theme_constant_override("separation", 16)
	layout.add_child(buttons)
	previous_button = _button(buttons, "Previous", func(): change_page(-1))
	next_button = _button(buttons, "Next", func(): change_page(1))
	back_button = _button(buttons, "Back to controls", func(): back_requested.emit())
	# A single horizontal ring avoids dropping focus into the hidden menu.
	var ordered := [previous_button, next_button, back_button]
	for i in ordered.size():
		ordered[i].focus_neighbor_left = ordered[i].get_path_to(ordered[posmod(i-1, ordered.size())])
		ordered[i].focus_neighbor_right = ordered[i].get_path_to(ordered[(i+1)%ordered.size()])
		ordered[i].focus_previous = ordered[i].focus_neighbor_left
		ordered[i].focus_next = ordered[i].focus_neighbor_right
	var hint := Label.new()
	_hint = hint
	hint.text = "Left / right or Tab: select  ·  Up / down: scroll text\nA / Cross / Enter: open  ·  B / Circle / Esc / Start: resume"
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.add_theme_font_size_override("font_size", 20)
	layout.add_child(hint)
	refresh()
	_apply_readable_scale()
	hide()

func _button(parent: Node, text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size.y = 48
	button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	button.add_theme_font_size_override("font_size", 24)
	button.pressed.connect(action)
	parent.add_child(button)
	return button

func open() -> void:
	page = 0
	refresh()
	show()
	next_button.grab_focus()

func change_page(direction: int) -> void:
	page = posmod(page + direction, titles().size())
	scroll.scroll_vertical = 0
	refresh()

func refresh() -> void:
	if not is_instance_valid(heading) or not is_instance_valid(controls):
		return
	heading.text = "FLIGHT BASICS  %d / %d  —  %s" % [page+1, titles().size(), titles()[page]]
	body.text = saved_page_text(page, controls, saved_wayfarer) if saved_flight else page_text(page, controls, rotational_coasting, orbit_preserving_assist)

func _apply_readable_scale() -> void:
	# The study normally draws a 1920 logical canvas into a 1280 window. Keep
	# this reference's body at approximately 26 physical pixels, not 17, and
	# let short windows scroll instead of shrinking text to fit.
	var pixels := Vector2(get_window().size)
	var logical := get_viewport_rect().size
	if pixels.x <= 0 or pixels.y <= 0 or logical.x <= 0 or logical.y <= 0:
		return
	var factor := maxf(1.0, maxf(logical.x/pixels.x, logical.y/pixels.y))
	if is_equal_approx(factor, _readable_scale):
		return
	_readable_scale = factor
	for side in ["left", "right", "top", "bottom"]:
		add_theme_constant_override("margin_" + side, roundi(32*factor))
	_layout.add_theme_constant_override("separation", roundi(16*factor))
	_buttons.add_theme_constant_override("separation", roundi(16*factor))
	heading.add_theme_font_size_override("font_size", roundi(30*factor))
	_paused.add_theme_font_size_override("font_size", roundi(21*factor))
	body.add_theme_font_size_override("font_size", roundi(26*factor))
	_hint.add_theme_font_size_override("font_size", roundi(20*factor))
	for button in [previous_button, next_button, back_button]:
		button.add_theme_font_size_override("font_size", roundi(24*factor))
		button.custom_minimum_size.y = 48*factor

func _process(_delta: float) -> void:
	if is_visible_in_tree():
		_apply_readable_scale()

func _input(event: InputEvent) -> void:
	# Text remains controller-readable on short viewports without reducing font
	# size. Horizontal/Tab navigation retains the three ordinary focus buttons.
	if not is_visible_in_tree() or not is_instance_valid(controls) or not controls.focused or not controls.waiting_action.is_empty():
		return
	if event is InputEventJoypadButton and event.device != controls.device:
		return
	if event.is_action_pressed("ui_up", true):
		scroll.scroll_vertical -= roundi(64*_readable_scale)
		get_viewport().set_input_as_handled()
	elif event.is_action_pressed("ui_down", true):
		scroll.scroll_vertical += roundi(64*_readable_scale)
		get_viewport().set_input_as_handled()
