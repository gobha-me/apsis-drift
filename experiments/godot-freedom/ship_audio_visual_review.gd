extends SceneTree
## Silent GPU inspection of the controller-accessible session mute setting.
var study: Variant

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	if AudioServer.get_driver_name() != "Dummy":
		push_error("Audio menu review requires --audio-driver Dummy")
		quit(1)
		return
	study = load("res://main.tscn").instantiate()
	root.add_child(study)
	for frame in 20:
		await process_frame
	if not is_instance_valid(study.ship_audio) or not is_instance_valid(study.pause_menu.audio_toggle):
		push_error("Review requires --ship-audio=true and native thrust flight")
		quit(1)
		return
	study.set_player_paused(true, "Ship audio study: start at a comfortable low system volume.")
	study.pause_menu.audio_toggle.grab_focus()
	for frame in 12:
		await process_frame
	await RenderingServer.frame_post_draw
	var directory: String = study.options.get("--review-dir", "")
	if directory.is_empty():
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(directory)
	if root.get_texture().get_image().save_png(directory.path_join("ship-audio-menu.png")) != OK:
		quit(1)
		return
	var metadata := FileAccess.open(directory.path_join("ship-audio-menu.json"), FileAccess.WRITE)
	if metadata == null:
		quit(1)
		return
	metadata.store_string(JSON.stringify({"source": "Original project UI rendered in Godot; paused menu inspection", "license": "BSD-3-Clause", "license_terms": "LICENSE.md", "attribution": "Apsis Drift contributors", "audio_output": "Dummy; no speaker output or listening qualification", "diagnostics": study.ship_audio.diagnostics()}, "\t"))
	print("Ship audio menu GPU inspection complete; no listening qualification")
	quit(0)
