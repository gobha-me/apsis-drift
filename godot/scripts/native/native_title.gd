extends Control
## Startup presentation only. No world exists until C++ accepts a selection.
signal start_requested(selection: Dictionary)
signal quit_requested
const Controls = preload("res://scripts/ui/player_input.gd")
const Basics = preload("res://scripts/flight/flight_basics.gd")
const Settings = preload("res://scripts/ui/control_settings.gd")
const ProfileBrowser = preload("res://scripts/ui/profile_browser.gd")
const CHART_HELP = [
	"Home and Nearby system fixes; local reference bodies remain known. No remote planetary charts or commission office.",
	"Four system fixes; local reference bodies remain known. No remote planetary charts or commission office.",
	"Four systems with planet names and orbital locations. Optional orbital and geological commissions are available at Origin.",
	"Four systems with planetary charts and regional stellar search for further system fixes. Optional commissions are available at Origin.",
	"Four systems with planetary charts and limited-reach stellar search. Search from charted waypoints for farther contacts. Optional commissions are available at Origin."
]
const DRIVE_HELP = [
	"Standard arrival rules, with no recovery cycle. Each committed jump costs one charge.",
	"Arrival starts 45 seconds of recovery. Valid early jumps still cost one charge, with up to 2 km of extra arrival uncertainty that fades as recovery clears.",
	"Arrival recovery lasts 45–90 seconds as unfinished recovery carries forward. Valid early jumps still cost one charge, with up to 2 km of fading extra arrival uncertainty."
]
var catalog_owner: Variant
var catalog_continue_button: Button
var catalog_load_button: Button
var catalog_browser: Control
var catalog_snapshot: Dictionary = {}
var catalog_invoker: Button
var persist_controls := true
var controls: Node
var panel: MarginContainer
var column: VBoxContainer
var scroll: ScrollContainer
var heading: Label
var seed_label: Label
var seed: LineEdit
var new_button: Button
var network_choice: OptionButton
var recovery_choice: OptionButton
var chart_help: Label
var drive_help: Label
var continue_button: Button
var reference_button: Button
var settings_button: Button
var settings_view: Control
var quit_button: Button
var status: Label
var reference: Control
var chooser: FileDialog
var theme_owner: Theme
var origin_service_choice: CheckButton
var derelict_recovery_choice: CheckButton
var busy := false
var focused := true
var readable_scale := -1.0
# -2 discovers a launch-time owner; a journey return supplies its live selection.
var initial_controller_device := -2

static func valid_seed(value: String) -> bool:
	if value.is_empty() or value.length() > 20: return false
	if value.length() > 1 and value.begins_with("0"): return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57: return false
	return value.length() < 20 or value <= "18446744073709551615"

func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var background := ColorRect.new()
	background.color = Color(0.015, 0.025, 0.04)
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	controls = Controls.new()
	controls.persist = persist_controls
	controls.thrust_mode = true
	controls.initial_device = initial_controller_device
	if initial_controller_device >= 0: controls.available_pads[initial_controller_device] = true
	add_child(controls)
	controls.set_enabled(false)
	controls.pause_requested.connect(back_to_title)
	panel = MarginContainer.new()
	panel.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(panel)
	theme_owner = Theme.new()
	panel.theme = theme_owner
	scroll = ScrollContainer.new()
	scroll.follow_focus = true
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	column = VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(column)
	heading = Label.new()
	heading.text = "APSIS DRIFT"
	column.add_child(heading)
	var note := Label.new()
	note.text = "FREEDOM · EXPLORATION BEFORE PROGRESSION\nBegin on Origin Station. Your Wayfarer is waiting at D1."
	note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(note)
	seed_label = Label.new()
	seed_label.text = "Universe seed · 0 to 18446744073709551615"
	seed_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(seed_label)
	seed = LineEdit.new()
	seed.text = "42"
	seed.max_length = 64
	seed.accessibility_name = "Universe seed"
	column.add_child(seed)
	seed.text_submitted.connect(func(_value: String): begin_new())
	var chart_label := Label.new()
	chart_label.text = "Starting charts"
	column.add_child(chart_label)
	network_choice = OptionButton.new()
	network_choice.add_item("Nearby systems")
	network_choice.add_item("Four charted systems")
	network_choice.add_item("Four systems + planetary charts")
	network_choice.add_item("Four charted + two uncharted systems")
	network_choice.add_item("Outer frontier: four charted systems")
	network_choice.tooltip_text = "Planetary charts grant names and orbital locations; surfaces remain unexplored. The original uncharted choice keeps its regional search. Outer frontier starts with four charted systems and a declared limited-reach stellar search; search from charted waypoints for new contacts. Scan local space can find an exterior derelict. Three jump charges; free replenishment requires Origin Station docking. Existing saves retain their charts."
	network_choice.focus_mode = Control.FOCUS_ALL
	column.add_child(network_choice)
	chart_help = add_choice_help(network_choice)
	var drive_label := Label.new()
	drive_label.text = "Jump drive"
	column.add_child(drive_label)
	recovery_choice = OptionButton.new()
	recovery_choice.add_item("Standard")
	recovery_choice.add_item("Recovery · 45 seconds")
	recovery_choice.add_item("Recovery + temporary strain · 45–90 seconds")
	recovery_choice.tooltip_text = "Recovery adds up to 2 km arrival uncertainty to early jumps. Temporary strain also carries unfinished recovery into the next cycle, up to 90 seconds. Waiting clears it. Each jump costs one charge. Existing saves keep their drive rules."
	column.add_child(recovery_choice)
	drive_help = add_choice_help(recovery_choice)
	origin_service_choice = CheckButton.new()
	origin_service_choice.text = "Origin fuel service · prototype"
	origin_service_choice.tooltip_text = "One service Wayfarer, with finite fuel and jump charges. Request fuel while seated in a supported ship on the home moon. D2 is reserved for it. Existing saves keep their current rules."
	column.add_child(origin_service_choice)
	derelict_recovery_choice = CheckButton.new()
	derelict_recovery_choice.text = "Derelict recovery · prototype"
	derelict_recovery_choice.tooltip_text = "For either uncharted exploration profile. After exterior inspection, hold 100–150 m at matched speed for 30 seconds to recover one canister. Sell it explicitly at Origin; loss or sale leaves the source depleted."
	derelict_recovery_choice.disabled = true
	column.add_child(derelict_recovery_choice)
	network_choice.item_selected.connect(func(index: int):
		derelict_recovery_choice.disabled = index not in [3, 4]
		if index not in [3, 4]: derelict_recovery_choice.button_pressed = false
		refresh_chart_help())
	recovery_choice.item_selected.connect(func(_index: int): refresh_drive_help())
	refresh_chart_help()
	refresh_drive_help()
	new_button = add_button("New Game", begin_new)
	catalog_continue_button = add_button("Continue", continue_catalog)
	continue_button = add_button("Open save file…", open_continue)
	catalog_load_button = add_button("Load saved journey…", open_catalog)
	reference_button = add_button("Flight basics", open_reference)
	settings_button = add_button("Settings", open_settings)
	quit_button = add_button("Quit", func(): quit_requested.emit())
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status.text = "Choose a seed for New Game, or select a saved journey. Quit does not autosave."
	column.add_child(status)
	chooser = FileDialog.new()
	chooser.access = FileDialog.ACCESS_FILESYSTEM
	chooser.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	chooser.filters = PackedStringArray(["*.json ; Apsis Drift save"])
	chooser.exclusive = true
	chooser.theme = theme_owner
	add_child(chooser)
	chooser.file_selected.connect(choose_continue)
	chooser.canceled.connect(func():
		controls.set_process_input(true)
		status.text = "Continue canceled. No journey was opened."
		if focused: continue_button.grab_focus())
	reference = Basics.new()
	reference.controls = controls
	reference.saved_flight = true
	reference.saved_wayfarer = true
	reference.title_reference = true
	reference.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(reference)
	reference.back_requested.connect(back_to_title)
	settings_view = Settings.new()
	settings_view.controls = controls
	add_child(settings_view)
	settings_view.back_requested.connect(back_to_title)
	catalog_browser = ProfileBrowser.new()
	catalog_browser.provider = catalog_owner
	add_child(catalog_browser)
	# Controls owns controller adoption before this title delegates catalog input.
	# Otherwise the browser consumes an absent owner's backup Start first.
	catalog_browser.set_process_input(false)
	controls.bindings_changed.connect(func(): catalog_browser.device = controls.device)
	catalog_browser.selected.connect(choose_catalog)
	catalog_browser.canceled.connect(back_from_catalog)
	refresh_catalog()
	layout_title()
	new_button.grab_focus()

func add_button(text: String, action: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.pressed.connect(action)
	column.add_child(button)
	return button

func add_choice_help(choice: OptionButton) -> Label:
	var help := Label.new()
	help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	help.focus_mode = Control.FOCUS_NONE
	help.mouse_filter = Control.MOUSE_FILTER_IGNORE
	column.add_child(help)
	choice.focus_entered.connect(func(): reveal_choice_help(choice, help))
	help.resized.connect(func(): reveal_choice_help(choice, help))
	return help

func refresh_chart_help() -> void:
	chart_help.text = CHART_HELP[network_choice.selected] + "\nPlanetary charts add names and orbits, not surface findings. Paid operations require separate enrollment; accepting work does not enroll you."
	reveal_choice_help(network_choice, chart_help)

func refresh_drive_help() -> void:
	drive_help.text = DRIVE_HELP[recovery_choice.selected] + "\nThree starting charges; free replenishment requires Origin docking until explicit paid enrollment. Pause freezes recovery. Continue keeps saved choices."
	reveal_choice_help(recovery_choice, drive_help)

func reveal_choice_help(choice: OptionButton, help: Label) -> void:
	# Focus-following runs first; then reveal the selector and wrapped text as a
	# single block after the container has applied selection/resize changes.
	await get_tree().process_frame
	if not panel.visible or not focused or not choice.has_focus() or choice.get_popup().visible: return
	var viewport := scroll.get_global_rect()
	var block := choice.get_global_rect().merge(help.get_global_rect())
	if block.size.y > viewport.size.y:
		scroll.ensure_control_visible(choice)
	elif block.position.y < viewport.position.y:
		scroll.scroll_vertical += floori(block.position.y - viewport.position.y)
	elif block.end.y > viewport.end.y:
		scroll.scroll_vertical += ceili(block.end.y - viewport.end.y)

func begin_new() -> void:
	if busy or not focused or chooser.visible or reference.visible or settings_view.visible: return
	if not valid_seed(seed.text):
		status.text = "Enter a whole unsigned seed from 0 to 18446744073709551615, without signs, spaces or leading zeros."
		seed.grab_focus()
		return
	busy = true
	start_requested.emit({"mode": "new_game", "value": seed.text, "network": network_choice.selected > 0, "planetary": network_choice.selected == 2, "uncharted": network_choice.selected == 3, "frontier": network_choice.selected == 4, "drive_recovery": recovery_choice.selected > 0, "drive_strain": recovery_choice.selected == 2, "origin_service": origin_service_choice.button_pressed, "derelict_recovery": network_choice.selected in [3, 4] and derelict_recovery_choice.button_pressed})

func open_continue() -> void:
	if busy or not focused or reference.visible or settings_view.visible: return
	# Let the exclusive dialog receive Escape rather than the pause binding.
	controls.set_process_input(false)
	chooser.popup_centered_ratio(0.75)

func choose_continue(path: String) -> void:
	chooser.hide()
	controls.set_process_input(true)
	if busy or not focused: return
	if not path.is_absolute_path() or path.is_empty() or path.length() > 4096:
		refuse("Choose a bounded absolute save path.")
		return
	busy = true
	start_requested.emit({"mode": "continue", "value": path})

func refuse(message: String) -> void:
	busy = false
	status.text = "Journey could not open: " + message
	if catalog_browser != null and catalog_browser.visible:
		catalog_browser.status.text = status.text
		if focused: catalog_browser.back.grab_focus()
	elif focused: continue_button.grab_focus()

func open_reference() -> void:
	if busy or not focused or chooser.visible or settings_view.visible: return
	panel.hide()
	reference.open()

func back_to_title() -> void:
	if catalog_browser != null and catalog_browser.visible:
		back_from_catalog()
		return
	if settings_view.visible:
		settings_view.cancel()
		return
	if not panel.visible and not reference.visible:
		panel.show()
		if focused: settings_button.grab_focus()
		return
	if not reference.visible: return
	reference.hide()
	panel.show()
	if focused: reference_button.grab_focus()

func open_settings() -> void:
	if busy or not focused or chooser.visible or reference.visible: return
	panel.hide()
	settings_view.open()

func layout_title() -> void:
	var pixels := Vector2(get_window().size)
	if not pixels.is_finite() or not size.is_finite() or minf(pixels.x, pixels.y) <= 0 or minf(size.x, size.y) <= 0: return
	var scale := maxf(1.0, maxf(size.x / pixels.x, size.y / pixels.y))
	if is_equal_approx(scale, readable_scale): return
	readable_scale = scale
	theme_owner.default_font_size = roundi(20 * scale)
	for kind in ["Label", "Button", "LineEdit", "ItemList", "Tree", "PopupMenu"]:
		theme_owner.set_font_size("font_size", kind, roundi(20 * scale))
	theme_owner.set_font_size("title_font_size", "Window", roundi(20 * scale))
	for side in ["left", "right", "top", "bottom"]:
		panel.add_theme_constant_override("margin_" + side, roundi(32 * scale))
	column.add_theme_constant_override("separation", roundi(12 * scale))
	heading.add_theme_font_size_override("font_size", roundi(36 * scale))
	for button in [seed, network_choice, recovery_choice, origin_service_choice, derelict_recovery_choice, new_button, catalog_continue_button, continue_button, catalog_load_button, reference_button, settings_button, quit_button]:
		button.custom_minimum_size.y = 44 * scale
	if chooser.visible: chooser.popup_centered_ratio(0.75)

func _process(_delta: float) -> void:
	layout_title()

func _input(event: InputEvent) -> void:
	if focused and catalog_browser != null and catalog_browser.visible:
		catalog_browser._input(event)

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		focused = false
		if controls != null: controls.focused = false
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		focused = true
		if controls != null: controls.focused = true

func refresh_catalog() -> void:
	if catalog_owner == null or not catalog_owner.has_method("list_freedom_profiles"):
		catalog_continue_button.disabled = true
		catalog_continue_button.focus_mode = Control.FOCUS_NONE
		catalog_load_button.disabled = true
		return
	catalog_snapshot = catalog_owner.list_freedom_profiles()
	catalog_continue_button.disabled = int(catalog_snapshot.get("continue_index", -1)) < 0
	catalog_continue_button.focus_mode = Control.FOCUS_NONE if catalog_continue_button.disabled else Control.FOCUS_ALL
	catalog_continue_button.tooltip_text = str(catalog_snapshot.get("diagnostic", ""))

func continue_catalog() -> void:
	if busy or not focused or not panel.visible: return
	refresh_catalog()
	var index := int(catalog_snapshot.get("continue_index", -1))
	if index < 0: return
	busy = true
	start_requested.emit({"mode": "profile", "value": index})

func open_catalog() -> void:
	if busy or not focused or not panel.visible or chooser.visible: return
	catalog_invoker = catalog_load_button
	panel.hide()
	catalog_browser.device = controls.device
	catalog_browser.open()

func choose_catalog(index: int) -> void:
	if busy or not focused: return
	busy = true
	start_requested.emit({"mode": "profile", "value": index})

func back_from_catalog() -> void:
	catalog_browser.hide()
	panel.show()
	controls.set_process_input(true)
	refresh_catalog()
	if focused: catalog_load_button.grab_focus()
