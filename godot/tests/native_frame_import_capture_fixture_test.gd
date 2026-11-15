extends SceneTree
## Real exported documents supply the valid half of every negative shape test.
const Capture = preload("res://studies/captures/native_frame_import_capture.gd")
var checks := 0
var failures := 0


func check(value: bool, reason: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error(reason)


func _initialize() -> void:
	call_deferred("run")


func rejected(candidate: Variant, system: Dictionary, canonical: Dictionary, label: String) -> void:
	check(not Capture.valid_fixture_pair(candidate, canonical), "Invalid system shape accepted: " + label)
	check(not Capture.valid_fixture_pair(system, candidate), "Invalid canonical shape accepted: " + label)


func run() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 2:
		check(false, "Expected two real exporter fixture paths")
	else:
		var source: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[0]))
		var before: Variant = JSON.parse_string(FileAccess.get_file_as_string(args[1]))
		if not (source is Dictionary and before is Dictionary):
			check(false, "Authoritative fixture documents unavailable")
		else:
			var system: Dictionary = source
			var canonical: Dictionary = before
			var original_system := system.duplicate(true)
			var original_canonical := canonical.duplicate(true)
			check(Capture.valid_fixture_pair(system, canonical), "Real format35/system and format31/planetary pair refused")
			check(not Capture.valid_fixture_pair(canonical, canonical), "Duplicated real canonical saves accepted")
			check(not Capture.valid_fixture_pair(system, system), "Duplicated real system saves accepted")
			check(not Capture.valid_fixture_pair(canonical, system), "Real fixture roles reversed")
			for value in [null, false, true, 0, 35, 31.0, NAN, INF, "", "system_flight", [], [system], Vector3.ZERO]:
				rejected(value, system, canonical, str(value))
			for index in 2:
				var valid: Dictionary = system if index == 0 else canonical
				var missing := valid.duplicate(true)
				missing.erase("format_version")
				check(not Capture.valid_fixture_pair(missing, canonical) if index == 0 else not Capture.valid_fixture_pair(system, missing), "Missing schema accepted in role " + str(index))
				for version in [null, false, true, 0, -1, 30, 34, 36, "35", "31", {}, [], NAN, INF]:
					var wrong := valid.duplicate(true)
					wrong.format_version = version
					check(not Capture.valid_fixture_pair(wrong, canonical) if index == 0 else not Capture.valid_fixture_pair(system, wrong), "Wrong schema accepted in role %d: %s" % [index, str(version)])
				var other_schema := valid.duplicate(true)
				other_schema.format_version = 31 if index == 0 else 35
				check(not Capture.valid_fixture_pair(other_schema, canonical) if index == 0 else not Capture.valid_fixture_pair(system, other_schema), "Other role schema accepted in role " + str(index))
				missing = valid.duplicate(true)
				missing.erase("state")
				check(not Capture.valid_fixture_pair(missing, canonical) if index == 0 else not Capture.valid_fixture_pair(system, missing), "Missing state accepted in role " + str(index))
				for state in [null, false, 0, "system_flight", [], [{}], {}, {"location": null}, {"location": false}, {"location": 0}, {"location": []}, {"location": {}}, {"location": ""}, {"location": "station_walk"}, {"location": "surface_walk"}]:
					var wrong := valid.duplicate(true)
					wrong.state = state
					check(not Capture.valid_fixture_pair(wrong, canonical) if index == 0 else not Capture.valid_fixture_pair(system, wrong), "Wrong state accepted in role %d: %s" % [index, str(state)])
				var other_location := valid.duplicate(true)
				other_location.state.location = "planetary_flight" if index == 0 else "system_flight"
				check(not Capture.valid_fixture_pair(other_location, canonical) if index == 0 else not Capture.valid_fixture_pair(system, other_location), "Other role location accepted in role " + str(index))
			check(system == original_system and canonical == original_canonical, "Shape helper mutated authoritative documents")
	await process_frame
	print("Native frame import fixture helpers: %d checks, %d failures" % [checks, failures])
	quit(1 if failures else 0)
