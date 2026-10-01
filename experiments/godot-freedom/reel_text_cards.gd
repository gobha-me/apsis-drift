extends SceneTree
## Editorial dialogue/title artwork. This does not implement game conversations.


func _initialize() -> void:
	call_deferred("run")


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2 or not args[0].is_absolute_path() or not args[1].is_absolute_path():
		push_error("Expected absolute reel plan and existing output directory")
		quit(1)
		return
	var plan: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[0]))
	if not plan is Dictionary or not plan.get("cards") is Array or not DirAccess.dir_exists_absolute(args[1]):
		push_error("Invalid reel cards plan/output")
		quit(1)
		return
	root.size = Vector2i(1920, 1080)
	var viewport := SubViewport.new()
	viewport.size = root.size
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var images := {}
	for card in plan.cards:
		if not card is Dictionary or not card.get("id") is String or not card.id.is_valid_filename():
			push_error("Invalid card identifier")
			quit(1)
			return
		var canvas := Control.new()
		canvas.size = Vector2(1920, 1080)
		viewport.add_child(canvas)
		if card.get("kind", "dialogue") == "badge":
			var placement: String = str(card.get("placement", "bottom"))
			if placement in ["hero_pair", "hero_other"]:
				var badge_y := 18.0 if placement == "hero_pair" else 220.0
				add_panel(canvas, Rect2(1460, badge_y, 420, 74), Color(0.015, 0.025, 0.04, 0.9))
				add_label(canvas, str(card.get("text", "")), Rect2(1478, badge_y + 10, 384, 56), 20, Color(0.8, 0.87, 0.9), HORIZONTAL_ALIGNMENT_RIGHT)
			else:
				add_panel(canvas, Rect2(1080, 28, 812, 72), Color(0.015, 0.025, 0.04, 0.9))
				add_label(canvas, str(card.get("text", "")), Rect2(1100, 41, 772, 54), 21, Color(0.8, 0.87, 0.9), HORIZONTAL_ALIGNMENT_RIGHT)
		elif card.get("kind", "dialogue") == "title":
			add_panel(canvas, Rect2(110, 365, 1700, 325), Color(0.015, 0.025, 0.04, 0.82))
			add_label(canvas, str(card.get("speaker", "APSIS DRIFT")), Rect2(150, 398, 1620, 48), 26, Color(0.39, 0.8, 0.91), HORIZONTAL_ALIGNMENT_CENTER)
			add_label(canvas, str(card.get("text", "")), Rect2(150, 458, 1620, 100), 64, Color.WHITE, HORIZONTAL_ALIGNMENT_CENTER)
			add_label(canvas, str(card.get("detail", "")), Rect2(170, 585, 1580, 72), 25, Color(0.8, 0.87, 0.9), HORIZONTAL_ALIGNMENT_CENTER)
		else:
			var placement: String = str(card.get("placement", "bottom"))
			if placement == "top":
				add_panel(canvas, Rect2(120, 130, 1200, 155), Color(0.015, 0.025, 0.04, 0.9))
				add_label(canvas, str(card.get("speaker", "")), Rect2(148, 146, 1144, 30), 22, Color(0.39, 0.8, 0.91), HORIZONTAL_ALIGNMENT_LEFT)
				add_label(canvas, str(card.get("text", "")), Rect2(148, 184, 1144, 84), 34, Color.WHITE, HORIZONTAL_ALIGNMENT_LEFT)
			elif placement == "left":
				add_panel(canvas, Rect2(50, 220, 620, 210), Color(0.015, 0.025, 0.04, 0.9))
				add_label(canvas, str(card.get("speaker", "")), Rect2(75, 238, 570, 30), 22, Color(0.39, 0.8, 0.91), HORIZONTAL_ALIGNMENT_LEFT)
				add_label(canvas, str(card.get("text", "")), Rect2(75, 278, 570, 128), 30, Color.WHITE, HORIZONTAL_ALIGNMENT_LEFT)
			else:
				add_panel(canvas, Rect2(120, 860, 1680, 173), Color(0.015, 0.025, 0.04, 0.9))
				add_label(canvas, str(card.get("speaker", "")), Rect2(150, 878, 1600, 34), 22, Color(0.39, 0.8, 0.91), HORIZONTAL_ALIGNMENT_LEFT)
				add_label(canvas, str(card.get("text", "")), Rect2(150, 922, 1600, 94), 34, Color.WHITE, HORIZONTAL_ALIGNMENT_LEFT)
		for frame in 3:
			await process_frame
			await RenderingServer.frame_post_draw
		var image := viewport.get_texture().get_image()
		if image == null or image.is_empty() or image.save_png(args[1].path_join(card.id + ".png")) != OK:
			push_error("Could not write reel card")
			quit(1)
			return
		images[card.id + ".png"] = FileAccess.get_sha256(args[1].path_join(card.id + ".png"))
		canvas.free()
	viewport.free()
	var receipt := FileAccess.open(args[1].path_join("cards-receipt.json"), FileAccess.WRITE)
	if receipt == null:
		push_error("Could not write card provenance")
		quit(1)
		return
	receipt.store_string(JSON.stringify({"schema_version": 1, "narrative_sha256": FileAccess.get_sha256(args[0]), "narrative": plan, "images_sha256": images, "script_sha256": FileAccess.get_sha256("res://reel_text_cards.gd"), "engine": Engine.get_version_info()}, "\t") + "\n")
	quit(0)


func add_panel(parent: Control, rect: Rect2, color: Color) -> void:
	var panel := ColorRect.new()
	panel.position = rect.position
	panel.size = rect.size
	panel.color = color
	parent.add_child(panel)


func add_label(parent: Control, text: String, rect: Rect2, font_size: int, color: Color, alignment: HorizontalAlignment) -> void:
	var label := Label.new()
	label.position = rect.position
	label.size = rect.size
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.horizontal_alignment = alignment
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", color)
	parent.add_child(label)
