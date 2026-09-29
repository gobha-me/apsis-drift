extends Control
## Docked-state presentation from a selected C++ Freedom save or recipe.

var bridge: Variant = null
var selected: Dictionary = {}


func fail(message: String) -> void:
	push_error("Native start rejected: " + message)
	get_tree().quit(1)


func parse_selection(arguments: PackedStringArray) -> Dictionary:
	var selection := {}
	for argument in arguments:
		if argument == "--validate-only":
			if selection.has("validate_only"):
				return {}
			selection["validate_only"] = true
		elif argument.begins_with("--new-game="):
			if selection.has("mode"):
				return {}
			selection["mode"] = "new_game"
			selection["value"] = argument.trim_prefix("--new-game=")
		elif argument.begins_with("--continue="):
			if selection.has("mode"):
				return {}
			selection["mode"] = "continue"
			selection["value"] = argument.trim_prefix("--continue=")
		else:
			return {}
	if not selection.has("mode") or selection.value.is_empty():
		return {}
	return selection


func decimal_text(value: Variant) -> bool:
	if not value is String or value.is_empty() or value.length() > 20:
		return false
	if value.length() > 1 and value.begins_with("0"):
		return false
	for digit in value.to_utf8_buffer():
		if digit < 48 or digit > 57:
			return false
	return true


func valid_start(start: Dictionary) -> bool:
	if start.get("mode") != "freedom":
		return false
	for key in ["universe_seed", "system_seed", "system_id", "home_planet_id", "station_id", "craft_id", "tick", "cycle_tick"]:
		if not decimal_text(start.get(key)):
			return false
	for key in ["host_position_metres", "host_velocity_metres_per_second", "station_position_metres", "station_velocity_metres_per_second", "station_relative_position_metres", "station_relative_velocity_metres_per_second"]:
		var coordinates: Variant = start.get(key)
		if not coordinates is PackedFloat64Array or coordinates.size() != 3:
			return false
		for component in coordinates:
			if not is_finite(component):
				return false
	return start.get("continued") is bool and start.get("discovery_count") is int and start.discovery_count >= 0 and start.get("world_delta_count") is int and start.world_delta_count >= 0 and start.get("station_phase_radians") is float and is_finite(start.station_phase_radians)


func _ready() -> void:
	var options := parse_selection(OS.get_cmdline_user_args())
	if options.is_empty():
		fail("supply exactly one --new-game=SEED or --continue=ABSOLUTE_PATH")
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		fail("native C++ bridge is unavailable")
		return
	bridge = ClassDB.instantiate("FreedomBridge")
	var accepted: bool = bridge.initialize_freedom_new_game(options.value) if options.mode == "new_game" else bridge.initialize_freedom_continue(options.value)
	if not accepted:
		fail(str(bridge.get_last_error()))
		return
	selected = bridge.get_freedom_start()
	if not valid_start(selected):
		fail("C++ bridge returned an invalid docked-state view")
		return
	if options.get("validate_only", false):
		print("Freedom native shell validated: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count])
		get_tree().quit(0)
		return
	build_view()


func build_view() -> void:
	var background := ColorRect.new()
	background.color = Color(0.025, 0.04, 0.08)
	background.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(background)
	var margin := MarginContainer.new()
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	add_child(margin)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	margin.add_child(column)
	var title := Label.new()
	title.text = "APSIS DRIFT"
	title.add_theme_font_size_override("font_size", 40)
	column.add_child(title)
	var location := Label.new()
	location.text = "Docked at Origin Station"
	location.add_theme_font_size_override("font_size", 25)
	column.add_child(location)
	var details := Label.new()
	details.text = "Universe %s  •  System %s\nStation %s  •  Craft %s\nSimulation tick %s  •  Discoveries %d" % [selected.universe_seed, selected.system_id, selected.station_id, selected.craft_id, selected.tick, selected.discovery_count]
	details.add_theme_font_size_override("font_size", 18)
	column.add_child(details)
	var note := Label.new()
	note.text = "Station start is available. Flight from this save is still in development."
	column.add_child(note)
	var quit_button := Button.new()
	quit_button.text = "Quit"
	quit_button.pressed.connect(func() -> void: get_tree().quit(0))
	column.add_child(quit_button)
	quit_button.grab_focus()
	print("Freedom native shell opened: seed=%s tick=%s system=%s planet=%s station=%s craft=%s discoveries=%d deltas=%d" % [selected.universe_seed, selected.tick, selected.system_id, selected.home_planet_id, selected.station_id, selected.craft_id, selected.discovery_count, selected.world_delta_count])
