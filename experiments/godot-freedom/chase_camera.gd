extends RefCounted
## Presentation-only chase attitude. Translation is deliberately NOT stored:
## callers add the CURRENT ship position to the returned rotated camera offset.
## This avoids kilometres of positional lag at high flight speeds. Apply the
## player's orbit offset/zoom after this basis and terrain clearance afterward.

const RESPONSE_PER_SECOND := 4.0
const BASIS_TOLERANCE := 0.0001
var accepted_last_update := false
var _initialized := false
var _orientation := Quaternion.IDENTITY

static func _valid_basis(value: Basis) -> bool:
	if not value.is_finite() or value.determinant() <= 0.0:
		return false
	for axis: Vector3 in [value.x, value.y, value.z]:
		if absf(axis.length_squared() - 1.0) > BASIS_TOLERANCE:
			return false
	return absf(value.x.dot(value.y)) <= BASIS_TOLERANCE and absf(value.x.dot(value.z)) <= BASIS_TOLERANCE and absf(value.y.dot(value.z)) <= BASIS_TOLERANCE

## Call on entering chase mode/resetting a scene so no old view is inherited.
## Invalid input is rejected transactionally, retaining the previous attitude.
func reset(target_basis: Basis) -> bool:
	accepted_last_update = false
	if not _valid_basis(target_basis):
		return false
	_orientation = target_basis.orthonormalized().get_rotation_quaternion().normalized()
	_initialized = true
	accepted_last_update = true
	return true

## Exponential shortest-arc attitude follow; no Euler-angle wrapping or roll
## singularity. Constant targets converge independently of render cadence.
## Delta zero holds an initialized view; first valid input seeds a fresh view.
## Invalid/nonfinite delta or basis cannot poison the stored camera rotation.
func follow(target_basis: Basis, delta: float) -> Basis:
	accepted_last_update = false
	if not is_finite(delta) or delta < 0.0 or not _valid_basis(target_basis):
		return Basis(_orientation)
	if not _initialized:
		reset(target_basis)
		return Basis(_orientation)
	accepted_last_update = true
	if delta == 0.0:
		return Basis(_orientation)
	var target := target_basis.orthonormalized().get_rotation_quaternion().normalized()
	if _orientation.dot(target) < 0.0:
		target = Quaternion(-target.x, -target.y, -target.z, -target.w)
	# Beyond ten seconds the exponential rounds to one; bound its arithmetic
	# explicitly rather than multiply arbitrarily large finite frame durations.
	var weight := 1.0 - exp(-RESPONSE_PER_SECOND * minf(delta, 10.0))
	_orientation = _orientation.slerp(target, weight).normalized()
	return Basis(_orientation)
