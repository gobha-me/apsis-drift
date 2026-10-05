extends Node
## BSD-3-Clause. Specific seated MPFB proof rig, not a general animation engine.
## Presentation only: resolved semantic input, no camera/input polling or C++ state.
## Caller establishes the seated neutral pose and stops AnimationPlayer first.
## Torso/shoulder ancestors must remain fixed in skeleton space. Moving the
## complete rigid presentation root is supported; independent torso animation
## needs a new neutral/target policy and is deliberately rejected here.

const TRAVEL := 8.0 * PI / 180.0
const SETTLE_SECONDS := 0.15
const MAX_DELTA := 0.25
const CHAIN := ["upperarm01", "upperarm02", "lowerarm01", "lowerarm02", "wrist"]
const KEYS := ["pitch", "roll", "yaw", "heave"]
var _skeleton: Skeleton3D
var _arms := {}
var _animations: Array[AnimationPlayer] = []
var _ancestor_poses := {}
var _configured := false
var _version := 0
var _blend := Vector4.ZERO
var _rejected := 0
var _last_error := ""
var _max_target_error := 0.0

static func _finite(v: Vector3) -> bool:
	return is_finite(v.x) and is_finite(v.y) and is_finite(v.z)

static func _rigid(t: Transform3D) -> bool:
	if not _finite(t.origin) or not _finite(t.basis.x) or not _finite(t.basis.y) or not _finite(t.basis.z):
		return false
	var b := t.basis
	return absf(b.x.length_squared()-1.0) < 0.0001 and absf(b.y.length_squared()-1.0) < 0.0001 and absf(b.z.length_squared()-1.0) < 0.0001 and absf(b.x.dot(b.y)) < 0.0001 and absf(b.x.dot(b.z)) < 0.0001 and absf(b.y.dot(b.z)) < 0.0001 and b.determinant() > 0.9998

static func solve_elbow(shoulder: Vector3, target: Vector3, upper: float, lower: float, bend: Vector3) -> Dictionary:
	# Circle intersection of two spheres; the authored neutral bend side chooses
	# one elbow. No clamping an unreachable target or stretching a bone.
	if not _finite(shoulder) or not _finite(target) or not _finite(bend) or not is_finite(upper) or not is_finite(lower):
		return {}
	if upper < 0.05 or lower < 0.05 or upper > 1.0 or lower > 1.0:
		return {}
	var offset := target-shoulder
	var distance := offset.length()
	if distance <= absf(upper-lower)+0.00001 or distance >= upper+lower-0.00001:
		return {}
	var axis := offset/distance
	var pole := bend-axis*bend.dot(axis)
	if pole.length_squared() < 0.00000001:
		return {}
	var along := (upper*upper-lower*lower+distance*distance)/(2.0*distance)
	var height_squared := upper*upper-along*along
	if height_squared <= 0.0 or not is_finite(height_squared):
		return {}
	return {"elbow": shoulder+axis*along+pole.normalized()*sqrt(height_squared)}

static func _collect(node: Node, skeletons: Array, pivots: Dictionary, animations: Array) -> void:
	if node is Skeleton3D:
		skeletons.append(node)
	if node is AnimationPlayer:
		animations.append(node)
	for side in ["L", "R"]:
		if node is Node3D and str(node.name) == "PROVISIONAL control pivot " + side:
			pivots[side].append(node)
	for child in node.get_children():
		_collect(child, skeletons, pivots, animations)

func _fail(reason: String, restore := true) -> bool:
	_rejected += 1
	_last_error = reason
	if restore:
		reset_neutral()
	return false

func configure(scene_root: Node3D) -> bool:
	if _configured:
		return _fail("Already configured", false)
	if not is_instance_valid(scene_root) or not scene_root.is_inside_tree():
		return _fail("Scene must be inside its tree", false)
	var skeletons := []
	var pivots := {"L": [], "R": []}
	var animations := []
	_collect(scene_root, skeletons, pivots, animations)
	if skeletons.size() != 1 or pivots.L.size() != 1 or pivots.R.size() != 1:
		return _fail("Missing or ambiguous seated rig/control pivots", false)
	for animation in animations:
		if animation.is_playing():
			return _fail("Stop competing animation before configuring neutral", false)
	var skeleton: Skeleton3D = skeletons[0]
	if not _rigid(skeleton.global_transform):
		return _fail("Scaled or malformed skeleton transform", false)
	var arms := {}
	var ancestor_poses := {}
	for side in ["L", "R"]:
		var pivot: Node3D = pivots[side][0]
		if not _rigid(pivot.global_transform) or not _rigid(pivot.transform):
			return _fail("Scaled or malformed control transform", false)
		var ids: Array[int] = []
		var neutral: Array[Transform3D] = []
		var locals: Array[Transform3D] = []
		for prefix in CHAIN:
			var id := skeleton.find_bone(prefix + "." + side)
			if id < 0 or (not ids.is_empty() and skeleton.get_bone_parent(id) != ids[-1]):
				return _fail("Missing or incompatible arm chain", false)
			var pose := skeleton.get_bone_global_pose(id)
			if not _rigid(pose):
				return _fail("Scaled or malformed bone pose", false)
			ids.append(id)
			neutral.append(pose)
			locals.append(skeleton.get_bone_pose(id))
		var shoulder := neutral[0].origin
		var ancestor := skeleton.get_bone_parent(ids[0])
		while ancestor >= 0:
			ancestor_poses[ancestor] = skeleton.get_bone_pose(ancestor)
			ancestor = skeleton.get_bone_parent(ancestor)
		var elbow := neutral[2].origin
		var wrist := neutral[4].origin
		var upper := shoulder.distance_to(elbow)
		var lower := elbow.distance_to(wrist)
		var axis := (wrist-shoulder).normalized()
		var bend := elbow-shoulder-axis*(elbow-shoulder).dot(axis)
		if solve_elbow(shoulder, wrist, upper, lower, bend).is_empty():
			return _fail("Degenerate or unreachable neutral arm", false)
		arms[side] = {"pivot": pivot, "pivot_local": pivot.transform, "ids": ids,
			"neutral": neutral, "locals": locals, "upper": upper, "lower": lower,
			"bend": bend, "wrist_from_pivot": pivot.global_transform.affine_inverse()*skeleton.global_transform*neutral[4]}
	_skeleton = skeleton
	_version = skeleton.get_version()
	_arms = arms
	_ancestor_poses = ancestor_poses
	for animation in animations:
		_animations.append(animation)
	_configured = true
	_last_error = ""
	return true

func reset_neutral() -> void:
	_blend = Vector4.ZERO
	if not _configured:
		return
	var restore_bones := is_instance_valid(_skeleton) and _skeleton.get_version() == _version
	for side in ["L", "R"]:
		var arm: Dictionary = _arms[side]
		if is_instance_valid(arm.pivot):
			arm.pivot.transform = arm.pivot_local
		if restore_bones:
			for i in CHAIN.size():
				_skeleton.set_bone_pose(arm.ids[i], arm.locals[i])
	_max_target_error = 0.0

func update_thrust_axes(axes: Variant, active: bool, delta: float) -> bool:
	# Existing resolved input contract: main, retro, pitch, yaw, roll, strafe,
	# heave. Validate even the deferred finger/engine axes; never poll a device.
	if not axes is PackedFloat64Array or axes.size() != 7:
		return _fail("Expected exactly seven resolved Float64 thrust axes")
	for i in 7:
		var value := float(axes[i])
		if not is_finite(value) or value > 1.0 or value < (0.0 if i < 2 else -1.0):
			return _fail("Resolved thrust axis outside finite bounds")
	return update_controls({"pitch": axes[2], "roll": axes[4], "yaw": axes[3], "heave": axes[6]}, active, delta)

func update_controls(demands: Dictionary, active: bool, delta: float) -> bool:
	if not _configured or not is_instance_valid(_skeleton) or not _skeleton.is_inside_tree() or _skeleton.get_version() != _version:
		return _fail("Unconfigured, removed or changed rig")
	if not is_finite(delta) or delta < 0.0 or delta > MAX_DELTA:
		return _fail("Presentation delta outside 0..0.25 seconds")
	for ancestor in _ancestor_poses:
		if _skeleton.get_bone_pose(ancestor) != _ancestor_poses[ancestor]:
			return _fail("Torso/shoulder ancestor moved from cached neutral")
	for animation in _animations:
		if is_instance_valid(animation) and animation.is_playing():
			return _fail("Competing skeletal animation")
	# Validate even at neutral/inactive: an absent control must not be reported
	# as a usable configured rig simply because no arm solve is required.
	if not _rigid(_skeleton.global_transform):
		return _fail("Scaled or malformed skeleton hierarchy")
	for side in ["L", "R"]:
		if not is_instance_valid(_arms[side].pivot):
			return _fail("Removed control hierarchy")
		var pivot: Node3D = _arms[side].pivot
		if not pivot.is_inside_tree():
			return _fail("Removed control hierarchy")
		var parent := pivot.get_parent_node_3d()
		if not is_instance_valid(parent) or not _rigid(parent.global_transform):
			return _fail("Removed or scaled control hierarchy")
	var target := Vector4.ZERO
	if active:
		for i in KEYS.size():
			var key: String = KEYS[i]
			if not demands.has(key) or typeof(demands[key]) not in [TYPE_INT, TYPE_FLOAT]:
				return _fail("Missing or nonnumeric semantic demand")
			var value := float(demands[key])
			if not is_finite(value) or absf(value) > 1.0:
				return _fail("Semantic demand outside finite -1..1")
			target[i] = value
	var blend := _blend
	for i in 4:
		blend[i] = move_toward(blend[i], target[i], delta/SETTLE_SECONDS)
	if blend == Vector4.ZERO:
		reset_neutral()
		return true
	var candidates := {}
	for side in ["L", "R"]:
		var arm: Dictionary = _arms[side]
		var parent: Node3D = arm.pivot.get_parent_node_3d()
		# +pitch/+heave pull aft, +roll/+yaw tilt right in pilot-local axes.
		# No claim of final physical control travel or flight-to-animation policy.
		var fore_aft := blend.x if side == "L" else blend.w
		var lateral := blend.y if side == "L" else blend.z
		var rotation := Basis(Vector3.RIGHT, fore_aft*TRAVEL)*Basis(Vector3.BACK, -lateral*TRAVEL)
		var pivot_local: Transform3D = arm.pivot_local
		pivot_local.basis = pivot_local.basis*rotation
		var desired: Transform3D = _skeleton.global_transform.affine_inverse()*parent.global_transform*pivot_local*arm.wrist_from_pivot
		var neutral: Array[Transform3D] = arm.neutral
		var solution := solve_elbow(neutral[0].origin, desired.origin, arm.upper, arm.lower, arm.bend)
		if solution.is_empty() or not _rigid(desired):
			return _fail("Grip target unreachable; no limb stretch applied")
		var elbow: Vector3 = solution.elbow
		var upper_rotation := Basis(Quaternion((neutral[2].origin-neutral[0].origin).normalized(), (elbow-neutral[0].origin).normalized()))
		var lower_rotation := Basis(Quaternion((neutral[4].origin-neutral[2].origin).normalized(), (desired.origin-elbow).normalized()))
		var poses: Array[Transform3D] = []
		for i in 4:
			var origin := neutral[0].origin if i < 2 else neutral[2].origin
			var destination := neutral[0].origin if i < 2 else elbow
			var turn := upper_rotation if i < 2 else lower_rotation
			poses.append(Transform3D(turn*neutral[i].basis, destination+turn*(neutral[i].origin-origin)))
		poses.append(desired)
		candidates[side] = {"pivot": pivot_local, "poses": poses}
	# Commit both sides only after every target is valid. Fingers inherit wrist;
	# shoulder parent, spine, pelvis, legs, eyes and all rest transforms untouched.
	_max_target_error = 0.0
	for side in ["L", "R"]:
		var arm: Dictionary = _arms[side]
		var candidate: Dictionary = candidates[side]
		arm.pivot.transform = candidate.pivot
		for i in CHAIN.size():
			_skeleton.set_bone_global_pose(arm.ids[i], candidate.poses[i])
		_max_target_error = maxf(_max_target_error, _skeleton.get_bone_global_pose(arm.ids[4]).origin.distance_to(candidate.poses[4].origin))
	_blend = blend
	_last_error = ""
	return true

func diagnostics() -> Dictionary:
	return {"configured": _configured, "blend": _blend, "rejected": _rejected,
		"last_error": _last_error, "max_wrist_target_error_m": _max_target_error,
		"travel_degrees": 8.0, "scope": "seated presentation only; no mesh collision or gameplay qualification"}
