extends SceneTree
const Chase = preload("res://chase_camera.gd")
var failures := 0

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func difference(a: Basis, b: Basis) -> float:
	return maxf((a.x - b.x).length(), maxf((a.y - b.y).length(), (a.z - b.z).length()))

func cadence(hz: int, target: Basis) -> Basis:
	var chase := Chase.new()
	chase.reset(Basis.IDENTITY)
	var result := Basis.IDENTITY
	for _frame in range(hz):
		result = chase.follow(target, 1.0 / hz)
	return result

func _initialize() -> void:
	var chase := Chase.new()
	var yaw := Basis(Vector3.UP, PI * 0.5)
	check(chase.reset(Basis.IDENTITY), "Identity camera reset rejected")
	var initial: Basis = chase.follow(yaw, 1.0 / 60.0)
	check(difference(initial, Basis.IDENTITY) > 0.01 and difference(initial, yaw) > 0.5,
		"Yaw step rigidly dragged chase camera with ship or did not start following")
	var converged := initial
	for _frame in range(240):
		converged = chase.follow(yaw, 1.0 / 60.0)
	check(difference(converged, yaw) < 0.0001, "Chase attitude did not converge")
	var a := cadence(30, yaw)
	var b := cadence(60, yaw)
	var c := cadence(120, yaw)
	check(difference(a, b) < 0.0001 and difference(b, c) < 0.0001,
		"Constant-target chase response depends materially on render cadence")
	var before: Basis = chase.follow(yaw, 0.0)
	check(difference(chase.follow(Basis.IDENTITY, 0.0), before) == 0.0,
		"Zero delta changed initialized camera attitude")
	for delta: float in [-1.0, NAN, INF, -INF]:
		var rejected: Basis = chase.follow(Basis.IDENTITY, delta)
		check(not chase.accepted_last_update and difference(rejected, before) == 0.0,
			"Invalid delta modified camera orientation")
	var invalid := Basis.IDENTITY
	invalid.x.x = NAN
	var infinite := Basis.IDENTITY
	infinite.y.z = INF
	var skew := Basis.IDENTITY
	skew.y.x = 0.2
	for bad: Basis in [invalid, infinite, Basis(Vector3.ZERO, Vector3.ZERO, Vector3.ZERO), Basis.IDENTITY.scaled(Vector3(2, 1, 1)), Basis.IDENTITY.scaled(Vector3(-1, 1, 1)), skew]:
		check(not chase.reset(bad), "Malformed camera basis reset accepted")
		var held: Basis = chase.follow(bad, 0.1)
		check(not chase.accepted_last_update and difference(held, before) == 0.0,
			"Malformed camera basis poisoned a valid orientation")
	# Camera-mode re-entry must seed the current ship attitude, not old chase.
	var reentry := Basis(Vector3.FORWARD, 1.2)
	check(chase.reset(reentry) and difference(chase.follow(yaw, 0.0), reentry) < 0.000001,
		"Chase re-entry retained a stale view")
	var fresh := Chase.new()
	check(difference(fresh.follow(reentry, 0.0), reentry) < 0.000001,
		"First valid follow did not seed a fresh view")
	# The equivalent +179/-179 representations are only two degrees apart.
	chase.reset(Basis(Vector3.UP, deg_to_rad(179.0)))
	var wrapped := chase.follow(Basis(Vector3.UP, deg_to_rad(-179.0)), 0.1)
	check(difference(wrapped, Basis(Vector3.UP, PI)) < 0.04,
		"Shortest-path quaternion follow swung around the long arc")
	for axis: Vector3 in [Vector3.UP, Vector3.RIGHT, Vector3.BACK]:
		chase.reset(Basis.IDENTITY)
		var half_turn := Basis(axis, PI)
		var previous := Basis.IDENTITY
		for _frame in range(120):
			var current: Basis = chase.follow(half_turn, 1.0 / 60.0)
			check(current.is_finite() and absf(current.determinant() - 1.0) < 0.00001 and difference(previous, current) < 0.22,
				"180-degree chase produced a jump, reflection or nonfinite attitude")
			previous = current
		check(difference(previous, half_turn) < 0.002, "Half-turn follow failed to converge")
	chase.reset(Basis.IDENTITY)
	var previous := Basis.IDENTITY
	for frame in range(1, 721):
		var roll := Basis(Vector3.BACK, TAU * frame / 720.0)
		var current: Basis = chase.follow(roll, 1.0 / 120.0)
		check(current.is_finite() and difference(previous, current) < 0.03,
			"Continuous full roll introduced a quaternion-sign discontinuity")
		previous = current
	check(difference(chase.follow(Basis.IDENTITY, 1e300), Basis.IDENTITY) < 0.000001,
		"Large finite delta failed to settle safely")
	# Position is intentionally outside the helper: immediate translation keeps
	# even an 8km/s craft at the same orbit-offset distance, without spring lag.
	var offset := Basis.from_euler(Vector3(-0.3, 0.7, 0)) * Vector3(0, 5, 25)
	var ship_position := Vector3(100, 20, 30)
	var attitude: Basis = chase.follow(yaw, 1.0 / 60.0)
	var camera_position := ship_position + attitude * offset
	var next_ship_position := ship_position + Vector3(8000.0 / 60.0, 0, 0)
	var next_camera_position := next_ship_position + attitude * offset
	check((next_camera_position - camera_position - (next_ship_position - ship_position)).length() < 0.0001,
		"High-speed translation acquired position lag")
	check(absf((next_camera_position - next_ship_position).length() - offset.length()) < 0.0001,
		"Orbit offset or zoom distance was altered by attitude helper")
	print("Chase camera presentation contracts: %d failures" % failures)
	quit(0 if failures == 0 else 1)
