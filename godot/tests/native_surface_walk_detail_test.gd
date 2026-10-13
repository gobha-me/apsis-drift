extends "res://tests/native_surface_walk_test.gd"
## Reuse the complete real save/input/on-foot/reboard consumer with policy2.

func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 3:
		quit(1)
		return
	if not ClassDB.class_exists("FreedomBridge"):
		GDExtensionManager.load_extension("res://bin/freedom.gdextension")
	if not check(ClassDB.class_exists("FreedomBridge"), "Native bridge unavailable"):
		quit(1)
		return
	var owner: Variant = ClassDB.instantiate("FreedomBridge")
	if not check(owner.stage_freedom_continue(args[0].path_join("source.json")), "Selected landed walking source refused"):
		quit(1)
		return
	var candidate: Dictionary = owner.get_pending_freedom_start()
	if not check(candidate.flight_state.get("terrain_policy") == 2, "Walking fixture silently fell back to legacy terrain"):
		owner.discard_pending_freedom_start(candidate.candidate_id)
		quit(1)
		return
	check(owner.discard_pending_freedom_start(candidate.candidate_id), "Walking preflight discard refused")
	owner = null
	await super.run()
