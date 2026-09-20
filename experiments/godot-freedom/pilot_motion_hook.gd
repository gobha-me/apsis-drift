extends Node
## Explicit study adapter. No input polling, camera access or simulation writes.
const Motion = preload("res://pilot_motion.gd")
const Pilot = preload("res://pilot_presentation.gd")
var driver: Node
var last_error := ""
var updates := 0
var active := false

func configure(model: Node3D) -> bool:
	if is_instance_valid(driver) or not is_instance_valid(model) or not model.is_inside_tree():
		last_error = "Motion requires one tree-mounted, unconfigured pilot rig"
		return false
	if not Pilot.valid(model):
		last_error = "Motion requires disjoint PilotHead / PilotBody mesh groups"
		return false
	var heads: Array[Node3D] = []
	Pilot.groups(model, "PilotHead", heads)
	var skeletons := model.find_children("*", "Skeleton3D", true, false)
	if skeletons.size() != 1 or heads[0] == skeletons[0] or heads[0].is_ancestor_of(skeletons[0]):
		last_error = "Shared skeleton must remain outside the hidden own-head hierarchy"
		return false
	var players := model.find_children("*", "AnimationPlayer", true, false)
	if players.size() != 1 or not players[0].has_animation("Scene"):
		last_error = "Motion study requires exactly one authored Scene neutral clip"
		return false
	var player: AnimationPlayer = players[0]
	player.play("Scene")
	player.seek(0.0, true)
	player.pause()
	var candidate := Motion.new()
	add_child(candidate)
	if not candidate.configure(model):
		last_error = candidate.diagnostics().last_error
		candidate.free()
		return false
	driver = candidate
	return true

func update_command(command: Dictionary, flight_active: bool, delta: float) -> bool:
	updates += 1
	active = flight_active
	if not is_instance_valid(driver):
		last_error = "Motion study is not configured"
		return false
	if not is_finite(delta) or delta < 0.0:
		driver.reset_neutral()
		active = false
		last_error = "Invalid presentation time"
		return false
	# A slow visual frame may complete the bounded settle, never feed additional
	# time into physics. Inactive frames cannot retain a stale semantic command.
	var axes: Variant = command.get("thrust_axes") if flight_active else PackedFloat64Array([0, 0, 0, 0, 0, 0, 0])
	if not driver.update_thrust_axes(axes, flight_active, minf(delta, Motion.MAX_DELTA)):
		last_error = driver.diagnostics().last_error
		active = false
		return false
	last_error = ""
	return true

func reset_neutral() -> void:
	active = false
	if is_instance_valid(driver):
		driver.reset_neutral()

func diagnostics() -> Dictionary:
	return {"active": active, "updates": updates, "last_error": last_error,
		"motion": driver.diagnostics() if is_instance_valid(driver) else {}}
