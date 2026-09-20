extends MarginContainer
## BSD-3-Clause. Read-only paused reference for the native thrust lab, not a
## tutorial state machine or flight authority. Bindings come from input owner.
signal back_requested
const TITLES := ["Thrust and momentum", "Space is not orbit", "Flight assistance", "Look without steering", "Guidance and prototype limits"]
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
	page = posmod(page + direction, TITLES.size())
	scroll.scroll_vertical = 0
	refresh()

func refresh() -> void:
	if not is_instance_valid(heading) or not is_instance_valid(controls):
		return
	heading.text = "FLIGHT BASICS  %d / %d  —  %s" % [page+1, TITLES.size(), TITLES[page]]
	body.text = page_text(page, controls, rotational_coasting, orbit_preserving_assist)

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
