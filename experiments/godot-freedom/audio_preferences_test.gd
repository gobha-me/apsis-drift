extends SceneTree
const Preferences = preload("res://audio_preferences.gd")
var failures := 0

func check(ok: bool, reason: String) -> void:
	if not ok:
		failures += 1
		push_error(reason)

func _initialize() -> void:
	var prefs := Preferences.new()
	prefs.persist = false
	var original: Dictionary = prefs.levels.duplicate()
	for bad in [NAN, INF, -INF, -0.1, 1.1, true, "0.5", null]:
		check(not prefs.set_level("master", bad), "Invalid level accepted")
		check(prefs.levels == original, "Invalid level mutated settings")
	check(not prefs.set_level("flight", 0.5), "Unknown category accepted")
	for value in [0, 0.5, 1]:
		check(prefs.set_level("master", value), "Valid bounded level rejected")
	check(prefs.save_settings(), "Nonpersistent settings failed")
	var good := {"version": 1, "muted": false, "levels": original}
	check(Preferences.valid_document(good), "Default document rejected")
	for bad in [null, [], {}, {"version": true, "muted": false, "levels": original},
		{"version": 2, "muted": false, "levels": original},
		{"version": 1, "muted": 0, "levels": original},
		{"version": 1, "muted": false, "levels": {"master": 0.5}},
		{"version": 1, "muted": false, "levels": original, "extra": true}]:
		check(not Preferences.valid_document(bad), "Malformed document accepted")
	# Unique test file only; never touch the actual user's preferences.
	var test_path := "user://audio-preferences-test-%d-%d.json" % [OS.get_process_id(), Time.get_ticks_usec()]
	prefs.path = test_path
	prefs.persist = true
	prefs.set_level("master", 0.55)
	prefs.set_level("machinery", 0.2)
	prefs.muted = true
	check(prefs.save_settings(), "Atomic settings save failed")
	var restored := Preferences.new()
	restored.path = test_path
	check(restored.load_settings(), "Settings load failed")
	check(restored.levels == prefs.levels and restored.muted, "Saved settings changed")
	var file := FileAccess.open(test_path, FileAccess.WRITE)
	file.store_string("invalid")
	file.close()
	var before: Dictionary = restored.levels.duplicate()
	check(not restored.load_settings(), "Corrupt file accepted")
	check(restored.levels == before and restored.muted, "Corrupt file mutated previous state")
	file = FileAccess.open(test_path, FileAccess.WRITE)
	file.store_string(" ".repeat(Preferences.MAX_BYTES + 1))
	file.close()
	check(not restored.load_settings(), "Oversized file accepted")
	prefs.reset_defaults()
	check(prefs.levels == original and not prefs.muted, "Defaults did not restore approved balance")
	check(DirAccess.remove_absolute(test_path) == OK, "Could not remove own temporary fixture")
	print("Ship audio preferences: %d failures; invalid/bounds/transactional load/persistence/defaults" % failures)
	quit(0 if failures == 0 else 1)
