extends "res://scripts/flight/flight_basics.gd"
## One paused snapshot. Only an explicitly configured exposure offers collection;
## the owning flight view rechecks the authoritative command before committing.
var reference_text := ""
var reference_title := "SURFACE CONDITIONS"
var collect_button: Button
var collection_status: Label
var collection_context: Dictionary = {}
var collection_command: Callable
var feedback_scroll_frames := 0
var opened_frame := -1
var opening_input_pending := false

func _ready() -> void:
	super._ready()
	collect_button = _button(_buttons, "Collect hand specimen", collect)
	_buttons.move_child(collect_button, 0)
	collect_button.hide()
	# Feedback shares the existing scroll budget with the description. Keeping
	# it outside the scroller can squeeze the readable body away at 640x450.
	var content := VBoxContainer.new()
	content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.remove_child(body)
	scroll.add_child(content)
	content.add_child(body)
	collection_status = Label.new()
	collection_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	content.add_child(collection_status)
	collection_status.hide()
	refresh()

func clear_collection() -> void:
	collection_context.clear()
	collection_command = Callable()
	opening_input_pending = false
	feedback_scroll_frames = 0
	if is_instance_valid(collection_status):
		collection_status.text = ""
		collection_status.hide()
	if is_instance_valid(collect_button):
		collect_button.disabled = true
		collect_button.hide()

func configure_collection(context: Dictionary, command: Callable) -> void:
	collection_context = context.duplicate(true)
	collection_command = command
	refresh()

func activation_held() -> bool:
	return Input.is_physical_key_pressed(KEY_E) or Input.is_physical_key_pressed(KEY_ENTER) or Input.is_physical_key_pressed(KEY_SPACE) or Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT) or (is_instance_valid(controls) and controls.device >= 0 and Input.is_joy_button_pressed(controls.device, JOY_BUTTON_A))

func open() -> void:
	page = 0
	scroll.scroll_vertical = 0
	opened_frame = Engine.get_process_frames()
	opening_input_pending = activation_held()
	refresh()
	show()
	# Opening inspection is never a collection gesture.
	back_button.grab_focus()

func collection_ready() -> bool:
	return is_visible_in_tree() and not collection_context.is_empty() and collection_command.is_valid() and Engine.get_process_frames() > opened_frame and not opening_input_pending

func collect() -> void:
	if collection_ready() and not collect_button.disabled:
		collection_command.call()

func refresh_collection(available: bool, reason: String) -> void:
	collect_button.disabled = not available
	collect_button.tooltip_text = reason
	collection_status.text = reason

func show_collection_result(result := "") -> void:
	if not result.is_empty(): collection_status.text = result + "\n" + collect_button.tooltip_text
	# The changed text needs container layout before it can be brought into
	# view. Owned frames cannot outlive a rapid Back/Continue transition.
	feedback_scroll_frames = 2

func refresh() -> void:
	if not is_instance_valid(heading): return
	heading.text = reference_title
	var exposure: bool = not collection_context.is_empty()
	_paused.text = "PAUSED / INSPECTED EXPOSURE" if exposure else "PAUSED / READ-ONLY REFERENCE"
	_hint.text = "Left / right or Tab: select · Enter / A: activate\nUp / down: scroll text · B / Circle / Esc / Start: back to controls" if exposure else "Up / down: scroll text\nB / Circle / Esc / Start: back to controls"
	body.text = reference_text
	previous_button.hide()
	next_button.hide()
	if is_instance_valid(collect_button): collect_button.visible = exposure
	if is_instance_valid(collection_status): collection_status.visible = exposure
	var other: Control = collect_button if exposure and is_instance_valid(collect_button) else back_button
	for property in ["focus_neighbor_left", "focus_neighbor_right", "focus_previous", "focus_next"]:
		back_button.set(property, back_button.get_path_to(other))
		if other != back_button: other.set(property, other.get_path_to(back_button))

func _process(delta: float) -> void:
	super._process(delta)
	if visible and opening_input_pending and not activation_held(): opening_input_pending = false
	if is_instance_valid(collect_button) and is_visible_in_tree():
		collect_button.add_theme_font_size_override("font_size", roundi(24*_readable_scale))
		collect_button.custom_minimum_size.y = 48*_readable_scale
		collection_status.add_theme_font_size_override("font_size", roundi(20*_readable_scale))
		if feedback_scroll_frames > 0:
			feedback_scroll_frames -= 1
			if feedback_scroll_frames == 0: scroll.ensure_control_visible(collection_status)

func _input(event: InputEvent) -> void:
	if not is_visible_in_tree(): return
	if not collection_context.is_empty():
		var pad: bool = event is InputEventJoypadButton or event is InputEventJoypadMotion
		if not is_instance_valid(controls) or not controls.focused or not controls.waiting_action.is_empty() or (pad and event.device != controls.device) or (event is InputEventKey and event.echo):
			get_viewport().set_input_as_handled()
			return
		if opening_input_pending and (event.is_action("ui_accept") or (event is InputEventKey and event.physical_keycode == KEY_E) or (event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT)):
			if not activation_held(): opening_input_pending = false
			get_viewport().set_input_as_handled()
			return
	super._input(event)
