extends RefCounted
## Presentation preferences only; never part of deterministic world/save state.
const DEFAULT_PATH := "user://freedom_ship_audio_v1.json"
const KEYS := ["master", "machinery", "propulsion", "atmosphere"]
const DEFAULTS := {"master": 1.0, "machinery": 1.0, "propulsion": 1.0, "atmosphere": 1.0}
const MAX_BYTES := 4096
var levels: Dictionary = DEFAULTS.duplicate()
var muted := false
var persist := true
var path := DEFAULT_PATH
var status := ""

static func valid_level(value: Variant) -> bool:
	return (value is int or value is float) and is_finite(float(value)) and value >= 0 and value <= 1

static func valid_document(value: Variant) -> bool:
	if not value is Dictionary or value.size() != 3:
		return false
	var version: Variant = value.get("version")
	if not (version is int or version is float) or version != 1 or not value.get("muted") is bool:
		return false
	if not value.get("levels") is Dictionary or value.levels.size() != KEYS.size():
		return false
	for key in KEYS:
		if not valid_level(value.levels.get(key)):
			return false
	return true

func set_level(key: String, value: Variant) -> bool:
	if key not in KEYS or not valid_level(value):
		return false
	levels[key] = float(value)
	return true

func load_settings() -> bool:
	if not persist or not FileAccess.file_exists(path):
		return true
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		status = "Audio settings unreadable; current/default levels retained."
		return false
	if file.get_length() > MAX_BYTES:
		status = "Audio settings too large; current/default levels retained."
		return false
	var parser := JSON.new()
	if parser.parse(file.get_as_text()) != OK or not valid_document(parser.data):
		status = "Invalid audio settings; current/default levels retained."
		return false
	var value: Dictionary = parser.data
	levels = value.levels.duplicate()
	muted = value.muted
	status = ""
	return true

func save_settings() -> bool:
	if not persist:
		return true
	var document := {"version": 1, "levels": levels, "muted": muted}
	if not valid_document(document):
		status = "Invalid audio settings; previous file retained."
		return false
	var file := FileAccess.open(path + ".tmp", FileAccess.WRITE)
	if file == null:
		status = "Could not save audio settings; changes are session-only."
		return false
	file.store_string(JSON.stringify(document, "\t"))
	file.flush()
	var error := file.get_error()
	file.close()
	if error != OK or DirAccess.rename_absolute(path + ".tmp", path) != OK:
		status = "Could not replace audio settings; previous file retained."
		return false
	status = ""
	return true

func reset_defaults() -> void:
	levels = DEFAULTS.duplicate()
	muted = false
