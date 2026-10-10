extends Node
## One optional playback owner for saved play. No world or save mutation.
const RecordedAudio = preload("res://scripts/audio/recorded_ship_audio.gd")
const Preferences = preload("res://scripts/audio/audio_preferences.gd")
const FlightView = preload("res://scripts/native/native_flight_view.gd")
const Applied = preload("res://scripts/native/native_main_exhaust.gd")
var audio: Node
var preferences: RefCounted
var view: Control
var closing := false


func configure(hum: String, propulsion: String, persist: bool) -> bool:
	if audio != null: return false
	var candidate := RecordedAudio.new()
	if not candidate.configure_recordings(hum, propulsion):
		candidate.free()
		return false
	audio = candidate
	preferences = Preferences.new()
	preferences.persist = persist
	preferences.load_settings()
	add_child(audio)
	apply_preferences()
	return true


func apply_preferences() -> void:
	if audio == null: return
	var levels: Dictionary = preferences.levels
	audio.set_mix_levels(levels.master, levels.machinery, levels.propulsion, levels.atmosphere)
	audio.set_muted(preferences.muted)


static func telemetry(state: Dictionary) -> Dictionary:
	if not Applied.valid_applied(state, 0.0): return {}
	for key in ["air_density", "dynamic_pressure"]:
		var value: Variant = state.get(key)
		if not (value is float or value is int) or not is_finite(float(value)) or value < 0.0: return {}
	var jump: Variant = state.get("jump", {})
	if not jump is Dictionary or jump.get("phase", "idle") not in ["idle", "spool", "transit"]: return {}
	if jump.get("phase", "idle") == "transit":
		# The committed bubble performs no propulsion or airflow work.
		return {"main_thrust": 0.0, "retro_thrust": 0.0, "effective_air_density": 0.0, "dynamic_pressure": 0.0}
	var positive: PackedFloat64Array = state.positive_force_body
	var negative: PackedFloat64Array = state.negative_force_body
	var positive_ratings: PackedFloat64Array = state.positive_force_ratings
	var negative_ratings: PackedFloat64Array = state.negative_force_ratings
	var main := negative[2] / negative_ratings[2]
	for axis in 2:
		main = maxf(main, maxf(positive[axis] / positive_ratings[axis], negative[axis] / negative_ratings[axis]))
	return {"main_thrust": clampf(main, 0.0, 1.0), "retro_thrust": clampf(positive[2] / positive_ratings[2], 0.0, 1.0), "effective_air_density": float(state.air_density), "dynamic_pressure": float(state.dynamic_pressure)}


func refresh() -> void:
	if audio == null or closing: return
	if not is_instance_valid(view) or not view is FlightView or not view.activated or not view.error.is_empty():
		audio.update_telemetry({}, false, false)
		return
	var active: bool = not view.paused and view.focused and not view.save_dialog.visible
	active = active and not view.controls_menu.panel.visible
	audio.update_telemetry(telemetry(view.state), view.cockpit, active)


func _process(_delta: float) -> void:
	refresh()


func begin_shutdown() -> float:
	closing = true
	set_process(false)
	return audio.prepare_shutdown() if audio != null else 0.0


func shutdown_drained() -> bool:
	return audio == null or audio.shutdown_drained()


func _exit_tree() -> void:
	if audio != null and not closing: audio.prepare_shutdown()
