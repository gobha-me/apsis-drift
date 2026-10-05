extends SceneTree
const Shell = preload("res://scripts/native/native_start_shell.gd")
class LateDamagedShell extends Shell:
	var damage := ""
	var staged_reference: WeakRef
	func stage_view(owner: Variant, assets: String, pending: Dictionary) -> Control:
		var candidate := super.stage_view(owner, assets, pending)
		if candidate != null and not damage.is_empty():
			staged_reference = weakref(candidate)
			if damage == "last_root":
				candidate.staged_model.replacement_nodes[13].transform = Transform3D.IDENTITY
			else:
				candidate.staged_model.atlas_material.roughness = 0.123456
		return candidate
var failures := 0
func check(value: bool, message: String) -> void:
	if not value:
		push_error(message)
		failures += 1
func _initialize() -> void:
	call_deferred("run")
func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3: quit(1); return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not ClassDB.class_exists("FreedomBridge"):
		check(false, "Native C++ bridge unavailable")
		quit(1)
		return
	var bridge: Variant = ClassDB.instantiate("FreedomBridge")
	var shell := LateDamagedShell.new()
	shell.presentation_only = true
	root.add_child(shell)
	check(shell.select_start(bridge, {"mode": "continue", "value": args[1], "assets": args[0]}), "Selected journey could not stage through the ready-view seam")
	var old: Dictionary = bridge.get_freedom_walk_state()
	var old_flight: Dictionary = bridge.get_freedom_flight_state()
	var options := {"mode": "new_game", "value": "42", "assets": args[0] + "-missing"}
	check(not shell.select_start(bridge, options), "Missing assets committed pending starter")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight and bridge.get_pending_freedom_start().is_empty(), "Failed model staging changed active session")
	check(not shell.select_start(bridge, {"mode": "continue", "value": args[2], "assets": args[0]}), "Corrupt save staged")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight, "Save refusal changed active session")
	check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "assets": args[0], "validate_only": true}), "Source-only validation refused")
	check(bridge.get_freedom_walk_state() == old and bridge.get_freedom_flight_state() == old_flight, "Source-only validation committed")
	check(shell.select_start(bridge, {"mode": "new_game", "value": "42", "assets": args[0]}), "Ready model/session transaction refused: " + shell.error)
	check(shell.current_view != null and shell.current_view.activated and shell.current_view.staged_model.valid_installed(), "Committed view was not complete")
	var current: Dictionary = bridge.get_freedom_walk_state()
	check(current.universe_seed == "42" and current.tick == "0" and bridge.get_pending_freedom_start().is_empty(), "Presentation changed new actor clock or left pending")
	var view: Control = shell.current_view
	check(not shell.select_start(bridge, options), "Second missing-assets stage accepted")
	check(shell.current_view == view and bridge.get_freedom_walk_state() == current, "Refused replacement lost previous complete model/session")
	for damage in ["last_root", "atlas"]:
		shell.damage = damage
		check(not shell.select_start(bridge, {"mode": "new_game", "value": "43", "assets": args[0]}), "Late staged " + damage + " committed")
		check(shell.current_view == view and bridge.get_freedom_walk_state() == current and bridge.get_pending_freedom_start().is_empty(), "Late refusal changed current view/session or retained pending")
		check(shell.staged_reference.get_ref() == null, "Refused detached candidate leaked")
	shell.free()
	print("Native start staging checks: %d failures" % failures)
	quit(0 if failures == 0 else 1)
