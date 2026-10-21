extends RefCounted
## Existing actual companion stages; resizing/focus never owns voyage time.
const ViewportSize = preload("res://scripts/native/native_viewport_size.gd")

static func check_inputs(check: Callable) -> void:
	# Refuse dimensions before touching an actual viewport or loading a world.
	var container := SubViewportContainer.new()
	container.position = Vector2(3, 4)
	container.size = Vector2(320, 180)
	container.scale = Vector2(2, 2)
	var before := [container.position, container.size, container.scale]
	for pair in [[Vector2.ZERO, Vector2.ONE], [Vector2(-1, 720), Vector2.ONE], [Vector2(NAN, 720), Vector2.ONE], [Vector2(1280, INF), Vector2.ONE], [Vector2(2147483648.0, 720), Vector2.ONE], [Vector2(1280, 720), Vector2.ZERO], [Vector2(1280, 720), Vector2(-1, 1)], [Vector2(1280, 720), Vector2(INF, 1)], [Vector2(1280, 720), Vector2(1, NAN)]]:
		check.call(ViewportSize.target(pair[0], pair[1]) == Vector2i.ZERO and not ViewportSize.fit(container, pair[0], pair[1]) and [container.position, container.size, container.scale] == before, "Invalid companion target dimensions changed accepted destination")
	check.call(not ViewportSize.fit(null, Vector2(1280, 720), Vector2.ONE), "Missing companion target accepted")
	container.free()

static func save(owner: Variant, path: String, check: Callable) -> PackedByteArray:
	if not check.call(owner.save_freedom_as(path), "Companion layout complete Save refused: " + str(owner.get_last_error())): return PackedByteArray()
	return FileAccess.get_file_as_bytes(path)

static func run(tree: SceneTree, view: Control, owner: Variant, output: String, label: String, check: Callable) -> void:
	if not check.call(DirAccess.make_dir_recursive_absolute(output) == OK and view.paused and not view.is_processing(), "Companion layout needs its existing paused fixture stage"): return
	if not check.call(view.hud_scroll is ScrollContainer and view.hud_column is VBoxContainer and view.hud_theme is Theme and view.companion_panel is PanelContainer, "Companion layout lacks its bounded native HUD owners"): return
	var state: Dictionary = owner.get_freedom_flight_state()
	var path := output.path_join(label + ".json")
	var original := save(owner, path, check)
	var previous_size := tree.root.size
	var controls_visible: bool = view.controls_menu.panel.visible
	var previous_focus: Control = tree.root.gui_get_focus_owner()
	view.controls_menu.hide_menu()
	var measurements := []
	for pixels in [Vector2i(1280, 720), Vector2i(640, 450)]:
		tree.root.size = pixels
		for frame in 3: await tree.process_frame
		view.layout_hud()
		view.update_compact_hud()
		for frame in 3: await tree.process_frame
		var bounds: Rect2 = view.get_global_rect()
		var density: Vector2 = view.get_viewport().get_final_transform().get_scale().abs()
		check.call(density.is_finite() and density.x > 0 and density.y > 0, "Companion layout has invalid actual canvas density")
		var text_pixels: float = view.primary_status.get_theme_font_size("normal_font_size") * density.y
		check.call(text_pixels >= 17.5 and view.save_status.get_theme_font_size("normal_font_size") * density.y >= 17.5, "Companion HUD body text shrank below18physical pixels: " + label + str(pixels))
		check.call(view.hud_scroll.follow_focus and view.hud_scroll.horizontal_scroll_mode == ScrollContainer.SCROLL_MODE_DISABLED and view.hud_column.size.x <= view.hud_scroll.size.x + 1.0, "Companion HUD lost bounded focus-follow or gained horizontal overflow")
		check.call(bounds.grow(1.0).encloses(view.companion_panel.get_global_rect()) and bounds.grow(1.0).encloses(view.hud_scroll.get_global_rect()) and bounds.grow(1.0).encloses(view.pause_button.get_global_rect()), "Companion panel, scroll or Pause escaped the physical window: " + label + str(pixels))
		check.call(not view.companion_panel.is_ancestor_of(view.pause_button) and not view.companion_panel.get_global_rect().intersects(view.pause_button.get_global_rect()), "Companion Pause became part of or covered by the overflow panel")
		for text: RichTextLabel in [view.primary_status, view.save_status]:
			if text.text.is_empty(): continue
			check.call(text.is_visible_in_tree() and text.get_content_height() > 0 and text.get_content_height() <= text.size.y + 1.0 and text.size.x <= view.hud_column.size.x + 1.0, "Companion wrapped status text clipped or overflowed horizontally: " + label)
		var buttons := [view.context_action, view.assist_button, view.landing_button, view.stow_gear_button, view.liftoff_button, view.moon_select_button, view.moon_jump_button]
		var reached := 0
		for button: Button in buttons:
			if not button.is_visible_in_tree(): continue
			check.call(button.size.y * density.y >= 43.5 and button.get_theme_font_size("font_size") * density.y >= 17.5, "Companion action shrank below44physical pixels or readable type: " + button.text)
			if button.disabled: continue
			button.grab_focus()
			for frame in 2: await tree.process_frame
			check.call(tree.root.gui_get_focus_owner() == button and view.hud_scroll.get_global_rect().grow(1.0).encloses(button.get_global_rect()), "Companion keyboard/controller focus did not reveal enabled action: " + button.text)
			reached += 1
		view.pause_button.grab_focus()
		check.call(tree.root.gui_get_focus_owner() == view.pause_button and view.pause_button.size.y * density.y >= 43.5 and view.pause_button.get_theme_font_size("font_size") * density.y >= 17.5, "Companion fixed Pause lost focus/readability")
		var expected := ViewportSize.target(view.size, density)
		for container: SubViewportContainer in [view.far_container, view.near_container]:
			check.call(is_instance_valid(container) and (container.size * container.scale).is_equal_approx(view.size), "Companion depth pass does not cover its logical view")
		check.call(expected != Vector2i.ZERO and view.far_viewport.size == expected and view.near_viewport.size == expected and expected.x <= pixels.x and expected.y <= pixels.y, "Companion depth targets exceed physical output or diverge: " + label + str(pixels))
		check.call(view.paused and owner.get_freedom_flight_state() == state and save(owner, path, check) == original, "Companion resize/focus changed actual state, clock or full Save: " + label)
		measurements.append({"window": [pixels.x, pixels.y], "targets": [expected.x, expected.y], "body_font_pixels": text_pixels, "focused_actions": reached})
	tree.root.size = previous_size
	for frame in 3: await tree.process_frame
	view.layout_hud()
	view.hud_scroll.scroll_vertical = 0
	if controls_visible: view.controls_menu.show_menu()
	if is_instance_valid(previous_focus) and previous_focus.is_visible_in_tree(): previous_focus.grab_focus()
	check.call(view.paused and owner.get_freedom_flight_state() == state and save(owner, path, check) == original, "Companion layout did not restore its paused source")
	var report := FileAccess.open(output.path_join(label + "-layout.json"), FileAccess.WRITE)
	if check.call(report != null, "Companion layout measurement report unavailable"):
		report.store_string(JSON.stringify({"scope": "Actual paused companion resize/focus/layout; no gameplay tick or GPU/FPS claim", "body_kind": state.body_kind, "tick": state.tick, "save_sha256": FileAccess.get_sha256(path), "measurements": measurements}, "\t") + "\n")
