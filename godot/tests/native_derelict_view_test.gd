extends RefCounted
## Called by the actual native integration case with an earned C++ contact.
## Malformed/camera-distance copies test presentation only, never bridge input.
const Derelict = preload("res://scripts/native/native_derelict_view.gd")
const Motion = preload("res://scripts/native/derelict_motion.gd")

static func packet_boundaries(packet: Dictionary, check: Callable) -> void:
	check.call(Derelict.valid_packet(packet), "Actual C++ derelict packet refused")
	for field in packet:
		var missing := packet.duplicate(true)
		missing.erase(field)
		check.call(not Derelict.valid_packet(missing), "Derelict packet accepted missing " + str(field))
	var extra := packet.duplicate(true)
	extra["private_orbit"] = Vector3.ZERO
	check.call(not Derelict.valid_packet(extra), "Derelict packet accepted a private extra field")
	var singular := packet.duplicate(true)
	singular.basis = Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO)
	check.call(not Derelict.valid_packet(singular), "Singular derelict basis admitted")
	for patch in [{"version": 1.0}, {"known": 1}, {"note": "x".repeat(513)}, {"history": "x".repeat(513)}, {"position": Vector3(NAN, 0.0, 0.0)}, {"position": Vector3(0.0, INF, 0.0)}, {"position": Vector3(2.1e15, 0.0, 0.0)}, {"basis": Basis(Vector3(2.0, 0.0, 0.0), Vector3.UP, Vector3.BACK)}, {"basis": Basis(Vector3.LEFT, Vector3.UP, Vector3.BACK)}, {"distance_metres": NAN}, {"distance_metres": -0.1}, {"relative_speed_metres_per_second": INF}, {"relative_speed_metres_per_second": -0.1}]:
		var bad := packet.duplicate(true)
		bad.merge(patch, true)
		check.call(not Derelict.valid_packet(bad), "Malformed derelict packet admitted: " + str(patch))
	var hidden := packet.duplicate(true)
	hidden.merge({"known": false, "local": false, "inspected": false, "can_inspect": false, "id": "", "position": Vector3.ZERO, "basis": Basis.IDENTITY, "distance_metres": 0.0, "relative_speed_metres_per_second": 0.0, "history": ""}, true)
	check.call(Derelict.valid_packet(hidden) and Derelict.text(hidden).is_empty(), "Unknown derelict leaked a HUD contact")
	for patch in [{"id": "1"}, {"history": "Private history"}, {"position": Vector3.ONE}, {"basis": Basis(Vector3.UP, 0.1)}, {"distance_metres": 1.0}, {"relative_speed_metres_per_second": 1.0}]:
		var leak := hidden.duplicate(true)
		leak.merge(patch, true)
		check.call(not Derelict.valid_packet(leak), "Unknown derelict leaked identity, pose or history")
	var ready := packet.duplicate(true)
	ready.merge({"selected": true, "known": true, "local": true, "id": "1", "position": Vector3(0.0, 0.0, -100.0), "basis": Basis.IDENTITY, "distance_metres": 100.0, "relative_speed_metres_per_second": 20.0, "can_inspect": true}, true)
	check.call(Derelict.valid_packet(ready), "Inclusive inspection distance/speed boundary refused")
	for patch in [{"id": "0"}, {"id": "01"}, {"id": "18446744073709551616"}, {"selected": false}, {"distance_metres": 99.9}, {"distance_metres": 500.1}, {"relative_speed_metres_per_second": 20.1}]:
		var wrong := ready.duplicate(true)
		wrong.merge(patch, true)
		check.call(not Derelict.valid_packet(wrong), "Derelict allowed an invalid actionable contact")

static func run(parent: Node, assets: String, binding: Dictionary, contact: Dictionary, check: Callable) -> void:
	packet_boundaries(contact, check)
	if not Derelict.valid_packet(contact) or not contact.known or not contact.local:
		check.call(false, "Derelict rendering needs the actual earned local C++ contact")
		return
	var view := Derelict.new()
	parent.add_child(view)
	if not view.initialize(assets, binding, contact.id):
		check.call(false, view.error)
		view.free()
		return
	# Only these read-only copies change camera-distance presentation. They do
	# not represent travelled positions or claim a flown approach.
	var local := contact.duplicate(true)
	local.merge({"position": Vector3(0.0, 0.0, -250.0), "distance_metres": 250.0}, true)
	check.call(view.present(local, false, 500.0, 2000.0) and view.visible and view.transform == Transform3D(local.basis, local.position), "Local derelict lost the C++-supplied pose")
	var frozen := view.transform
	check.call(view.present(local, true, 500.0, 2000.0) and view.transform == frozen, "Pause invented derelict motion")
	var geometries: Array[Node] = view.find_children("*", "GeometryInstance3D", true, false)
	check.call(not geometries.is_empty(), "Admitted derelict exterior has no geometry")
	for geometry: GeometryInstance3D in geometries: check.call(geometry.layers == 2, "Nearby derelict used the distant layer")
	local.merge({"can_inspect": false, "position": Vector3(0.0, 0.0, -1000.0), "distance_metres": 1000.0}, true)
	check.call(view.present(local, false, 500.0, 2000.0) and view.visible, "Distant-layer contact disappeared")
	for geometry: GeometryInstance3D in geometries: check.call(geometry.layers == 1, "Distant derelict used the near layer")
	check.call(view.present(local, true, 500.0, 800.0) and not view.visible, "Beyond-far hull remained visible")
	local.merge({"local": false, "position": Vector3.ZERO, "basis": Basis.IDENTITY, "distance_metres": 0.0, "relative_speed_metres_per_second": 0.0}, true)
	check.call(view.present(local, true, 500.0, 2000.0) and not view.visible, "Remote contact manufactured a local hull")
	check.call(not view.present(contact, false, NAN, 2000.0) and not view.visible, "Invalid render range retained a visible hull")
	check.call(not view.present(contact, false, 500.0, INF) and not view.visible, "Infinite far clip admitted")
	view.free()


static func motion_boundaries(state: Dictionary, check: Callable) -> void:
	var packet: Dictionary = state.derelict_motion
	check.call(Motion.valid_packet(packet) and Motion.valid_state(state) and packet.available, "Actual earned local C++ motion packet refused")
	check.call("Closing" in Motion.text(state) and "Sideways" in Motion.text(state) and "m/s" in Motion.text(state), "Current C++ motion lost sign/unit labels")
	for field in packet:
		var missing := state.duplicate(true)
		missing.derelict_motion.erase(field)
		check.call(not Motion.valid_state(missing) and Motion.text(missing).is_empty(), "Motion accepted missing " + str(field))
	for patch in [{"version": 1.0}, {"available": 1}, {"tick": "01"}, {"tick": "18446744073709551616"}, {"note": ""}, {"note": "x".repeat(1025)}, {"note": "x" + String.chr(0)}, {"closing_metres_per_second": 1}, {"closing_metres_per_second": NAN}, {"closing_metres_per_second": INF}, {"closing_metres_per_second": -4.1e9}, {"transverse_metres_per_second": -0.1}, {"transverse_metres_per_second": NAN}, {"transverse_metres_per_second": INF}, {"transverse_metres_per_second": 4.1e9}]:
		var bad := state.duplicate(true)
		bad.derelict_motion.merge(patch, true)
		check.call(not Motion.valid_state(bad) and Motion.hud_text(bad).is_empty(), "Malformed motion admitted: " + str(patch))
	var extra := state.duplicate(true)
	extra.derelict_motion["future_position"] = Vector3.ONE
	check.call(not Motion.valid_state(extra), "Motion admitted a private extra field")
	var stale := state.duplicate(true)
	stale.derelict_motion.tick = str(int(state.tick) + 1)
	check.call(not Motion.valid_state(stale) and Motion.text(stale).is_empty(), "Motion accepted a different simulation tick")
	var coincident := state.duplicate(true)
	coincident.derelict.distance_metres = 0.0
	check.call(not Motion.valid_state(coincident), "Motion invented a line of sight at zero separation")
	var hidden := state.duplicate(true)
	hidden.derelict_motion.merge({"available": false, "closing_metres_per_second": 0.0, "transverse_metres_per_second": 0.0}, true)
	check.call(Motion.valid_state(hidden) and Motion.hud_text(hidden).is_empty(), "Unavailable motion retained a live cue")
	for field in ["closing_metres_per_second", "transverse_metres_per_second"]:
		var leak := hidden.duplicate(true)
		leak.derelict_motion[field] = 1.0
		check.call(not Motion.valid_state(leak), "Unavailable motion leaked a nonzero rate")
	# Text-only copies exercise labels; they are not travelled physical states.
	for closing in [2.5, -2.5, 0.0]:
		var copy := state.duplicate(true)
		copy.derelict_motion.merge({"closing_metres_per_second": closing, "transverse_metres_per_second": 1.5}, true)
		var expected := "Closing %+.1f m/s · Sideways 1.5 m/s" % closing
		check.call(Motion.text(copy) == expected, "Closing sign or sideways label changed")
	var almost_zero := state.duplicate(true)
	almost_zero.derelict_motion.closing_metres_per_second = -0.01
	check.call("Closing +0.0 m/s" in Motion.text(almost_zero), "Subprecision closing displayed negative zero")
