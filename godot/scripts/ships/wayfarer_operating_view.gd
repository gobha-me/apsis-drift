extends Node3D
## Read-only operating-hardware inspection. This does not board or seat a player.
const SOURCE_HASH := "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677"
const STATION_HASH := "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80"
# Approved external policy from tools/operating_asset_identity.py. A receipt
# matching edited files is insufficient: these exact qualified bytes are pinned.
const SELECTED_SHA256 := {
	"wayfarer-operating-02.glb": "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c",
	"wayfarer-operating-02.json": "db7b0a2de2516adf9925abe74133a6c523bd334062884a1bfc403448c2b1e72e",
	"station-d1-clearance-01.glb": "a02d4b2a14f4459f6b5109768fa102aa214e611d3aa3faf28e573cd557e3a29e",
	"contact.json": "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a",
	"station-closure.json": "662287856666bc9f3bffdd6ccda3fce2a2c2b55fd393998f4cf65597c3ffeada",
	"qualification.json": "06d38bbc05b085c3ad73fe1b09cd5728bc94034e9d3d49748ea0c6e9cdc7cd46"
}
var specification: Dictionary = {}
var moving_nodes: Dictionary = {}
var error := ""
var selected_pose := ""

static func exact_fields(value: Variant, fields: Array) -> bool:
	if not value is Dictionary or value.size() != fields.size():
		return false
	for field in fields:
		if not value.has(field):
			return false
	return true

static func integral(value: Variant, minimum: int, maximum: int) -> bool:
	# Actual glTF extras use floats. Never cast before finite/range qualification.
	return (value is int or value is float) and is_finite(float(value)) and float(value) >= minimum and float(value) <= maximum and floor(float(value)) == float(value)

static func finite_vector(value: Variant, limit: float = 100.0) -> bool:
	if not value is Array or value.size() != 3:
		return false
	for coordinate in value:
		if not (coordinate is int or coordinate is float) or not is_finite(float(coordinate)) or absf(float(coordinate)) > limit:
			return false
	return true

static func vector(value: Array) -> Vector3:
	return Vector3(value[0], value[1], value[2])

static func pose(value: Array) -> Transform3D:
	return Transform3D(Basis(vector(value[0]), vector(value[1]), vector(value[2])), vector(value[3]))

static func valid_transform(value: Variant) -> bool:
	if not value is Array or value.size() != 4:
		return false
	for column in value:
		if not finite_vector(column):
			return false
	var transform := pose(value)
	return absf(transform.basis.determinant() - 1.0) < 0.001 and transform.basis.is_equal_approx(transform.basis.orthonormalized())

static func sanitized_name(value: String) -> String:
	return value.replace(".", "_")

static func unique_nodes(root: Node) -> Dictionary:
	var result := {}
	var queue: Array[Node] = [root]
	while not queue.is_empty():
		var node: Node = queue.pop_back()
		if result.has(str(node.name)):
			return {}
		result[str(node.name)] = node
		for child in node.get_children():
			queue.append(child)
	return result

static func valid_mesh_buffers(model: Node) -> bool:
	var queue: Array[Node] = [model]
	var vertex_total := 0
	var triangle_total := 0
	while not queue.is_empty():
		var node: Node = queue.pop_back()
		for child in node.get_children():
			queue.append(child)
		if not node is MeshInstance3D:
			continue
		var mesh: Mesh = node.mesh
		if mesh == null or mesh.get_surface_count() < 1 or mesh.get_surface_count() > 512:
			return false
		for surface in mesh.get_surface_count():
			if mesh.surface_get_primitive_type(surface) != Mesh.PRIMITIVE_TRIANGLES:
				return false
			var counts := surface_counts(mesh.surface_get_arrays(surface))
			if counts.x < 0:
				return false
			vertex_total += counts.x
			triangle_total += counts.y
	return vertex_total > 0 and vertex_total <= 8000000 and triangle_total > 0 and triangle_total <= 4000000

static func surface_counts(arrays: Variant) -> Vector2i:
	var invalid := Vector2i(-1, -1)
	if not arrays is Array or arrays.size() != Mesh.ARRAY_MAX or not arrays[Mesh.ARRAY_VERTEX] is PackedVector3Array:
		return invalid
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	if vertices.is_empty() or vertices.size() > 4000000:
		return invalid
	for vertex in vertices:
		if not vertex.is_finite() or vertex.length() > 100.0:
			return invalid
	var indices: Variant = arrays[Mesh.ARRAY_INDEX]
	if indices != null and not indices is PackedInt32Array:
		return invalid
	var triangles := 0
	if indices != null and not indices.is_empty():
		if indices.size() % 3 != 0:
			return invalid
		triangles = int(indices.size() / 3)
		for index in indices:
			if index < 0 or index >= vertices.size():
				return invalid
	else:
		if vertices.size() % 3 != 0:
			return invalid
		triangles = int(vertices.size() / 3)
	for attribute in [Mesh.ARRAY_NORMAL, Mesh.ARRAY_TEX_UV]:
		var values: Variant = arrays[attribute]
		if values == null:
			continue
		if (attribute == Mesh.ARRAY_NORMAL and not values is PackedVector3Array) or (attribute == Mesh.ARRAY_TEX_UV and not values is PackedVector2Array):
			return invalid
		if not values.is_empty():
			if values.size() != vertices.size():
				return invalid
			for value in values:
				if not value.is_finite():
					return invalid
	return Vector2i(vertices.size(), triangles)

const GROUP_NODES := {
	"roof_port": "WFOpRoofPort", "roof_starboard": "WFOpRoofStarboard",
	"inner_port_outer": "WFOpInnerPortOuter", "inner_port_inner": "WFOpInnerPortInner",
	"inner_starboard_outer": "WFOpInnerStarboardOuter", "inner_starboard_inner": "WFOpInnerStarboardInner",
	"ladder_base": "WFOpLadderBase", "ladder_upper": "WFOpLadderUpper",
	"seat_carriage": "WFOpSeatCarriage", "seat_swivel": "WFOpSeatSwivel", "seat_lift": "WFOpSeatLift",
	"seat_entry_arm": "WFOpSeatEntryArm", "seat_lock": "WFOpSeatLock"
}
const GROUP_RIGS := {
	"roof_port": "AFT01 | dock hatch hinge -1", "roof_starboard": "AFT01 | dock hatch hinge 1",
	"inner_port_outer": "AFT01 | inner sliding leaf rig -1 0", "inner_port_inner": "AFT01 | inner sliding leaf rig -1 1",
	"inner_starboard_outer": "AFT01 | inner sliding leaf rig 1 0", "inner_starboard_inner": "AFT01 | inner sliding leaf rig 1 1",
	"ladder_base": "AFT01 | swing-away dock ladder", "ladder_upper": "AFT01 | telescoping ladder upper",
	"seat_carriage": "RIG | fore-aft carriage", "seat_swivel": "RIG | 90 degree boarding swivel",
	"seat_lift": "RIG | seat height adjustment", "seat_entry_arm": "RIG | left entry arm", "seat_lock": "RIG | positive swivel lock"
}
const POSES := ["rest", "roof_open", "transfer_deployed", "inner_open", "seat_boarding", "transfer_cabin"]
const CHANNELS := ["roof_transfer", "inner_door", "seat_boarding"]

static func digest(value: Variant) -> bool:
	if not value is String or value.length() != 64:
		return false
	for byte in value.to_utf8_buffer():
		if not (byte >= 48 and byte <= 57) and not (byte >= 97 and byte <= 102):
			return false
	return true

static func valid_group_poses(value: Variant) -> bool:
	if not exact_fields(value, GROUP_NODES.keys()):
		return false
	for group in value:
		if not valid_transform(value[group]):
			return false
	return true

static func valid_spec(value: Variant) -> bool:
	if not exact_fields(value, ["schema_version", "id", "units", "axes", "source", "exporter", "model", "groups", "channels", "poses", "anchors", "roster", "gear_preview", "gear_preview_samples", "screens", "seat_adjustment_metres", "licenses", "limits", "derivative_corrections"]) or not integral(value.schema_version, 1, 1) or value.id != "wayfarer-operating-02" or value.units != "metres" or value.axes != "Godot +Y up, -Z forward":
		return false
	if not exact_fields(value.source, ["path", "sha256"]) or value.source.sha256 != SOURCE_HASH or value.source.path != "assets/visual/hopper-craft-09.blend":
		return false
	if not exact_fields(value.exporter, ["path", "sha256", "base_sha256", "classification_sha256", "pose_helper_sha256", "validation_helper_sha256", "binary_audit_sha256"]) or value.exporter.path != "tools/export_wayfarer_operating.py" or not digest(value.exporter.sha256) or not digest(value.exporter.classification_sha256) or not digest(value.exporter.pose_helper_sha256) or not digest(value.exporter.validation_helper_sha256) or not digest(value.exporter.binary_audit_sha256) or value.exporter.base_sha256 != "762499e37fe13a33bf297fc58801e711590cd9d56058a6462b3467ff6b7102fe":
		return false
	if not exact_fields(value.model, ["file", "sha256"]) or value.model.file != "wayfarer-operating-02.glb" or not digest(value.model.sha256):
		return false
	if not value.groups is Array or value.groups.size() != GROUP_NODES.size():
		return false
	var groups := {}
	var objects := {}
	for group in value.groups:
		if not exact_fields(group, ["id", "runtime_node", "source_rig", "source_parent", "source_rest_transform", "runtime_rest_transform", "source_objects", "contact_role"]) or not GROUP_NODES.has(group.id) or groups.has(group.id) or group.runtime_node != GROUP_NODES[group.id] or group.source_rig != GROUP_RIGS[group.id]:
			return false
		if not (group.source_parent == null or group.source_parent is String) or not valid_transform(group.source_rest_transform) or not valid_transform(group.runtime_rest_transform) or not pose(group.runtime_rest_transform).is_equal_approx(Transform3D.IDENTITY):
			return false
		if not group.source_objects is Array or group.source_objects.is_empty() or group.source_objects.size() > 2000 or group.contact_role != ("roof_assembly" if group.id.begins_with("roof_") else "inner_pressure_leaf" if group.id.begins_with("inner_") else "dock_ladder" if group.id.begins_with("ladder_") else group.id):
			return false
		for source_object in group.source_objects:
			if not source_object is String or source_object.is_empty() or objects.has(source_object):
				return false
			objects[source_object] = group.id
		groups[group.id] = true
	if not exact_fields(value.poses, POSES):
		return false
	for name in POSES:
		if not valid_group_poses(value.poses[name]):
			return false
	if not value.channels is Array or value.channels.size() != CHANNELS.size():
		return false
	var channels := {}
	for channel in value.channels:
		if not exact_fields(channel, ["id", "samples"]) or not CHANNELS.has(channel.id) or channels.has(channel.id) or not channel.samples is Array or channel.samples.size() != 21:
			return false
		channels[channel.id] = true
		for index in 21:
			var sample: Variant = channel.samples[index]
			if not exact_fields(sample, ["progress", "transforms"]) or not (sample.progress is int or sample.progress is float) or not is_finite(float(sample.progress)) or absf(float(sample.progress) - index / 20.0) > 0.0000001 or not valid_group_poses(sample.transforms):
				return false
	if not valid_support_metadata(value) or not valid_corrections(value.derivative_corrections):
		return false
	if not exact_fields(value.anchors, ["pilot_eye", "boarding_eye", "roof_collar"]):
		return false
	for anchor in value.anchors.values():
		if not finite_vector(anchor, 15.0):
			return false
	return vector(value.anchors.pilot_eye).is_equal_approx(Vector3(0, 1.365, -2.49)) and vector(value.anchors.roof_collar).is_equal_approx(Vector3(0, 2.907, 5.2))

static func metadata_matches(actual: Variant, expected: Variant) -> bool:
	if expected is Dictionary:
		if not exact_fields(actual, expected.keys()):
			return false
		for key in expected:
			if not metadata_matches(actual[key], expected[key]):
				return false
		return true
	if (expected is int or expected is float) and integral(expected, -1000000, 1000000):
		return integral(actual, -1000000, 1000000) and float(actual) == float(expected)
	if expected is float:
		return (actual is float or actual is int) and is_finite(float(actual)) and absf(float(actual) - expected) < 0.000001
	return typeof(actual) == typeof(expected) and actual == expected

func bind_operating_model(model: Node3D, spec: Dictionary) -> bool:
	if not valid_spec(spec) or not model.transform.is_equal_approx(Transform3D.IDENTITY) or not valid_mesh_buffers(model):
		error = "Operating manifest or mesh buffers refused"
		return false
	var nodes := unique_nodes(model)
	if nodes.is_empty():
		error = "Duplicate or ambiguous runtime node name"
		return false
	var mesh_roster := {}
	for item in spec.roster.included:
		mesh_roster[item.runtime_node] = true
	var mesh_count := 0
	for node in nodes.values():
		if node is MeshInstance3D:
			mesh_count += 1
			if not mesh_roster.has(str(node.name)):
				error = "Unexpected imported mesh outside the source roster"
				return false
	if mesh_count != mesh_roster.size():
		error = "Source roster mesh is absent from the imported model"
		return false
	var candidate := {}
	for group in spec.groups:
		var node: Variant = nodes.get(group.runtime_node)
		if not node is MeshInstance3D or not node.transform.is_equal_approx(pose(group.runtime_rest_transform)):
			error = "Operating group missing, wrong type or changed rest transform"
			return false
		if node.get_parent() != model:
			error = "World-delta groups must remain flat siblings"
			return false
		if not metadata_matches(node.get_meta("extras", {}), {"operating_group": group.id, "source_rig": group.source_rig, "source_sha256": SOURCE_HASH}):
			error = "Operating group source metadata changed"
			return false
		candidate[group.id] = node
	for node in nodes.values():
		var extras: Variant = node.get_meta("extras", {})
		if extras is Dictionary and extras.has("operating_group") and not candidate.values().has(node):
			error = "Unlisted operating identity"
			return false
	specification = spec.duplicate(true)
	moving_nodes = candidate
	error = ""
	return true

func apply_poses(transforms: Variant) -> bool:
	if not valid_group_poses(transforms) or moving_nodes.size() != GROUP_NODES.size():
		return false
	var candidate := {}
	for id in GROUP_NODES:
		if not is_instance_valid(moving_nodes[id]):
			return false
		candidate[id] = pose(transforms[id])
	# World-baked sibling meshes already contain source rest geometry. Their
	# complete world delta is applied once, never multiplied by a parent delta.
	for id in candidate:
		moving_nodes[id].transform = candidate[id]
	return true

func set_pose(name: String) -> bool:
	if not specification.get("poses", {}).has(name):
		return false
	if not apply_poses(specification.poses[name]):
		return false
	selected_pose = name
	return true

func set_channel(name: String, progress: float) -> bool:
	if not is_finite(progress) or progress < 0 or progress > 1:
		return false
	for channel in specification.get("channels", []):
		if channel.id != name:
			continue
		# Only the exact source-qualified samples are admitted. Interpolating
		# baked world deltas drifts hinge pivots; continuous mechanism motion
		# requires a separately qualified reconstruction of the source curves.
		for sample in channel.samples:
			if progress == sample.progress:
				if not apply_poses(sample.transforms):
					return false
				selected_pose = name + ":" + str(progress)
				return true
		return false
	return false

static func columns(transform: Transform3D) -> Array:
	return [[transform.basis.x.x, transform.basis.x.y, transform.basis.x.z], [transform.basis.y.x, transform.basis.y.y, transform.basis.y.z], [transform.basis.z.x, transform.basis.z.y, transform.basis.z.z], [transform.origin.x, transform.origin.y, transform.origin.z]]

const StationPresentation = preload("res://scripts/native/native_station_view.gd")
const MANIFEST := "wayfarer-operating-02.json"
const CLOSURE_SOURCE_HASH := "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4"
var station: Node3D
var ship: Node3D
var closure_spec: Dictionary = {}
var closure_nodes: Dictionary = {}
var closure_progress := 0.0
var owner_state: Dictionary = {}

static func metadata_subset(actual: Variant, expected: Dictionary) -> bool:
	if not actual is Dictionary:
		return false
	for key in expected:
		if not actual.has(key) or not metadata_matches(actual[key], expected[key]):
			return false
	return true

static func valid_closure(value: Variant) -> bool:
	if not exact_fields(value, ["schema_version", "source_sha256", "closure_source_sha256", "model_sha256", "coordinate_contract", "timeline_frames", "attached_closed_progress", "bindings", "corrections", "correction_coordinate_contract"]) or not integral(value.schema_version, 1, 1) or value.source_sha256 != "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6" or value.closure_source_sha256 != CLOSURE_SOURCE_HASH or value.model_sha256 != STATION_HASH:
		return false
	if value.coordinate_contract != "Godot metre node-local axes; Blender(x,y,z)->(x,z,-y)" or not value.timeline_frames is Array or value.timeline_frames.size() != 2 or not integral(value.timeline_frames[0], 110, 110) or not integral(value.timeline_frames[1], 300, 300) or not (value.attached_closed_progress is float or value.attached_closed_progress is int) or not is_finite(float(value.attached_closed_progress)) or absf(value.attached_closed_progress - 160.0 / 190.0) > 0.000000001 or not value.bindings is Array or value.bindings.size() != 17:
		return false
	if value.correction_coordinate_contract != "Correction records: original Blender node-local metres; replacement GLB: identity Godot node-local metres (x,z,-y)" or not valid_station_corrections(value.corrections):
		return false
	var ids := {}
	var names := {}
	var sources := {}
	for index in 17:
		var binding: Variant = value.bindings[index]
		if not exact_fields(binding, ["id", "source_object", "runtime_node", "runtime_parent", "expected_extras", "rest_transform", "knots"]) or binding.id != "station_d1_%02d" % index or ids.has(binding.id) or not binding.source_object is String or binding.source_object.is_empty() or sources.has(binding.source_object) or not binding.runtime_node is String or binding.runtime_node != sanitized_name(binding.source_object) or names.has(binding.runtime_node) or not binding.runtime_parent is String or binding.runtime_parent.is_empty():
			return false
		if not binding.expected_extras is Dictionary or binding.expected_extras.is_empty() or not valid_transform(binding.rest_transform) or not binding.knots is Array or binding.knots.size() < 2 or binding.knots.size() > 64:
			return false
		var last := -1.0
		for knot in binding.knots:
			if not exact_fields(knot, ["progress", "transform"]) or not (knot.progress is int or knot.progress is float) or not is_finite(float(knot.progress)) or knot.progress < 0 or knot.progress > 1 or knot.progress <= last or not valid_transform(knot.transform):
				return false
			last = knot.progress
		if binding.knots[0].progress != 0 or last != 1 or not pose(binding.knots[0].transform).is_equal_approx(pose(binding.rest_transform)):
			return false
		ids[binding.id] = true
		names[binding.runtime_node] = true
		sources[binding.source_object] = true
	return true

func bind_station_model(model: Node3D, spec: Dictionary) -> bool:
	if not valid_closure(spec):
		error = "D1 closure specification refused"
		return false
	var nodes := unique_nodes(model)
	if nodes.is_empty():
		error = "Station runtime names are ambiguous"
		return false
	var candidate := {}
	for binding in spec.bindings:
		var node: Variant = nodes.get(binding.runtime_node)
		if not node is Node3D or str(node.get_parent().name) != binding.runtime_parent or not node.transform.is_equal_approx(pose(binding.rest_transform)) or not metadata_subset(node.get_meta("extras", {}), binding.expected_extras):
			error = "D1 node, parent, metadata or open rest transform changed"
			return false
		candidate[binding.id] = node
	closure_spec = spec.duplicate(true)
	closure_nodes = candidate
	error = ""
	return true

func set_station_progress(progress: float, inspect_release: bool = false) -> bool:
	if not is_finite(progress) or progress < 0 or progress > 1 or closure_nodes.size() != 17 or (not inspect_release and progress > closure_spec.attached_closed_progress):
		return false
	var candidate := {}
	for binding in closure_spec.bindings:
		if not is_instance_valid(closure_nodes[binding.id]):
			return false
		for index in binding.knots.size() - 1:
			var low: Dictionary = binding.knots[index]
			var high: Dictionary = binding.knots[index + 1]
			if progress >= low.progress and progress <= high.progress:
				var weight: float = (progress - low.progress) / (high.progress - low.progress)
				candidate[binding.id] = pose(low.transform).interpolate_with(pose(high.transform), weight)
				break
	if candidate.size() != 17:
		return false
	for id in candidate:
		closure_nodes[id].transform = candidate[id]
	closure_progress = progress
	return true

static func bounded_json(path: String) -> Variant:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() == 0 or file.get_length() > 8 * 1024 * 1024:
		return null
	var text := file.get_as_text()
	var opened: Array[int] = []
	var quoted := false
	var escaped := false
	for byte in text.to_utf8_buffer():
		if quoted:
			if escaped:
				escaped = false
			elif byte == 92:
				escaped = true
			elif byte == 34:
				quoted = false
		elif byte == 34:
			quoted = true
		elif byte == 123 or byte == 91:
			if opened.size() == 64:
				return null
			opened.append(byte)
		elif byte == 125 or byte == 93:
			if opened.is_empty() or opened.pop_back() != (123 if byte == 125 else 91):
				return null
	if quoted or not opened.is_empty():
		return null
	return JSON.parse_string(text)

static func valid_glb_container(path: String) -> bool:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() < 20 or file.get_length() > 96 * 1024 * 1024 or file.get_32() != 0x46546c67 or file.get_32() != 2 or file.get_32() != file.get_length():
		return false
	var chunks := 0
	while file.get_position() < file.get_length():
		if file.get_length() - file.get_position() < 8:
			return false
		var length := file.get_32()
		var kind := file.get_32()
		if length == 0 or length % 4 != 0 or length > file.get_length() - file.get_position() or kind != (0x4e4f534a if chunks == 0 else 0x004e4942) or chunks >= 2:
			return false
		file.seek(file.get_position() + length)
		chunks += 1
	return chunks == 2 and file.get_position() == file.get_length()

func initialize(owner: Variant, starter_assets: String, operating_assets: String) -> bool:
	if not starter_assets.is_absolute_path() or not operating_assets.is_absolute_path() or not moving_nodes.is_empty():
		error = "Operating inspection requires absolute prepared directories and an unused consumer"
		return false
	if not valid_prepared(operating_assets):
		error = "Prepared operating receipt or file hashes refused"
		return false
	var spec: Variant = bounded_json(operating_assets.path_join(MANIFEST))
	var closure: Variant = bounded_json(operating_assets.path_join("station-closure.json"))
	if not valid_spec(spec) or not valid_closure(closure):
		error = "Source-bound operating or station closure contract refused"
		return false
	var path := operating_assets.path_join(spec.model.file)
	if FileAccess.get_sha256(path) != spec.model.sha256 or not valid_glb_container(path):
		error = "Operating model changed, truncated or has invalid chunk boundaries"
		return false
	owner_state = owner.get_freedom_flight_state()
	if not valid_owner_state(owner_state):
		error = "Inspection requires the existing C++ attached D1 world"
		return false
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		error = "Verified operating model could not be imported"
		return false
	var model := document.generate_scene(state) as Node3D
	if model == null or not bind_operating_model(model, spec):
		if model != null:
			model.free()
		return false
	var candidate_station := StationPresentation.new()
	if not candidate_station.initialize(owner_state, owner.get_freedom_station_geometry(), starter_assets, false):
		error = candidate_station.error
		candidate_station.free()
		model.free()
		moving_nodes.clear()
		return false
	# The normal station adapter adds port-label/ring debug helpers. They occlude
	# the actual shaft hardware at close inspection range. Remove only those
	# generated helpers here; all imported source nodes and C++ ports remain.
	for marker in candidate_station.port_markers:
		marker.free()
	candidate_station.port_markers.clear()
	if not apply_station_correction(candidate_station.get_child(0), operating_assets, closure.corrections) or not bind_station_model(candidate_station.get_child(0), closure):
		candidate_station.free()
		model.free()
		moving_nodes.clear()
		return false
	station = candidate_station
	add_child(station)
	station.transform = Transform3D(owner_state.station_basis, owner_state.station_position)
	ship = Node3D.new()
	ship.name = "OperatingWayfarerInspection"
	add_child(ship)
	ship.add_child(model)
	ship.transform = Transform3D(owner_state.body_basis, Vector3.ZERO)
	var glass: Node = model.find_child("HopperGlass", true, false)
	if glass is MeshInstance3D:
		var material := StandardMaterial3D.new()
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.albedo_color = Color(0.75, 0.90, 0.96, 0.055)
		material.roughness = 0.08
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		glass.material_override = material
		glass.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	return set_pose("rest")

static func valid_prepared(directory: String) -> bool:
	var receipt: Variant = bounded_json(directory.path_join("prepared.json"))
	if not exact_fields(receipt, ["schema", "package_sha256", "sources", "files"]) or receipt.schema != "apsis.wayfarer-operating-assets/1" or not digest(receipt.package_sha256) or not exact_fields(receipt.sources, ["wayfarer", "station", "station_closure"]) or receipt.sources.wayfarer != SOURCE_HASH or receipt.sources.station != "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6" or receipt.sources.station_closure != CLOSURE_SOURCE_HASH:
		return false
	var files := ["wayfarer-operating-02.glb", "wayfarer-operating-02.json", "contact.json", "station-closure.json", "qualification.json", "provenance.json", "station-d1-clearance-01.glb"]
	if not exact_fields(receipt.files, files) or not approved_identities(receipt.files):
		return false
	for name in files:
		if not digest(receipt.files[name]) or FileAccess.get_sha256(directory.path_join(name)) != receipt.files[name]:
			return false
	return true

static func approved_identities(files: Variant) -> bool:
	if not files is Dictionary:
		return false
	for name in SELECTED_SHA256:
		if files.get(name) != SELECTED_SHA256[name]:
			return false
	return true

static func valid_support_metadata(value: Dictionary) -> bool:
	if not exact_fields(value.roster, ["included", "excluded"]) or not value.roster.included is Array or value.roster.included.size() != 1746 or not value.roster.excluded is Array or value.roster.excluded.size() > 4000:
		return false
	var seen := {}
	var membership := {}
	for item in value.roster.included:
		if not exact_fields(item, ["source_object", "source_type", "source_parent", "runtime_node", "motion_group"]) or not item.source_object is String or item.source_object.is_empty() or seen.has(item.source_object) or not ["MESH", "CURVE", "FONT"].has(item.source_type) or not (item.source_parent == null or item.source_parent is String) or not item.runtime_node is String or item.runtime_node.is_empty():
			return false
		if item.motion_group != null and (not GROUP_NODES.has(item.motion_group) or item.runtime_node != GROUP_NODES[item.motion_group]):
			return false
		seen[item.source_object] = true
		membership[item.source_object] = item.motion_group
	for item in value.roster.excluded:
		if not exact_fields(item, ["source_object", "reason"]) or not item.source_object is String or item.source_object.is_empty() or seen.has(item.source_object) or not item.reason is String or item.reason.is_empty():
			return false
		seen[item.source_object] = true
	for group in value.groups:
		for source_object in group.source_objects:
			if not membership.has(source_object) or membership[source_object] != group.id:
				return false
	if not integral(value.gear_preview_samples, 21, 21) or not value.gear_preview is Dictionary or value.gear_preview.size() != 30:
		return false
	for name in value.gear_preview:
		if not name is String or not name.begins_with("HopperGear") or not value.gear_preview[name] is Array or value.gear_preview[name].size() != 21:
			return false
		for transform in value.gear_preview[name]:
			if not transform is Array or transform.size() != 4:
				return false
			for column in transform:
				if not finite_vector(column):
					return false
			var basis := pose(transform).basis
			if basis.determinant() <= 0.1 or basis.determinant() >= 10 or basis.x.length() >= 10 or basis.y.length() >= 10 or basis.z.length() >= 10:
				return false
	if not value.screens is Array or value.screens.size() != 3:
		return false
	for index in 3:
		var screen: Variant = value.screens[index]
		if not exact_fields(screen, ["page", "role", "transform", "size"]) or not integral(screen.page, index, index) or screen.role != ["NAV", "FLIGHT", "SYSTEMS"][index] or not valid_transform(screen.transform) or not screen["size"] is Array or screen["size"].size() != 2:
			return false
		for dimension in screen["size"]:
			if not (dimension is int or dimension is float) or not is_finite(float(dimension)) or dimension < 0.1 or dimension > 1.0:
				return false
	return exact_fields(value.seat_adjustment_metres, ["up", "forward"]) and value.seat_adjustment_metres.up == 0.18 and value.seat_adjustment_metres.forward == 0.08 and value.licenses == ["LicenseRef-Apsis-Hopper-Meshy-Output", "BSD-3-Clause"] and value.limits is Array and not value.limits.is_empty()

static func valid_owner_state(value: Variant) -> bool:
	if not value is Dictionary or value.get("mode") != "freedom_flight" or not value.get("attached") is bool or not value.attached or not value.get("target_port") is int or value.target_port != 1 or not value.get("station_position") is Vector3 or not value.station_position.is_finite() or value.station_position.length() > 1000.0:
		return false
	for name in ["body_basis", "station_basis"]:
		if not value.get(name) is Basis or not value[name].is_finite() or absf(value[name].determinant() - 1.0) > 0.00001 or not value[name].is_equal_approx(value[name].orthonormalized()):
			return false
	for name in ["station_id", "tick", "universe_seed"]:
		if not canonical_uint64(value.get(name)):
			return false
	return value.station_id != "0"

static func canonical_uint64(value: Variant) -> bool:
	# Avoid signed integer conversion and float rounding at the uint64 boundary.
	if not value is String or value.is_empty() or value.length() > 20 or (value.length() > 1 and value.begins_with("0")):
		return false
	for byte in value.to_utf8_buffer():
		if byte < 48 or byte > 57:
			return false
	return value.length() < 20 or value <= "18446744073709551615"

static func valid_corrections(value: Variant) -> bool:
	if not exact_fields(value, ["schema_version", "axes", "operations"]) or not integral(value.schema_version, 1, 1) or value.axes != "Blender source local" or not value.operations is Array or value.operations.size() != 4:
		return false
	var expected := {
		"roof_hinge_link_clearance": {"count": 4, "parameters": {"inward_metres": 0.025}},
		"inner_door_stroke": {"count": 4, "parameters": {"authored_fraction": 0.55}},
		"seat_lock_withdrawal": {"count": 1, "parameters": {"withdrawal_metres": 0.070, "withdraw_begin": 0.50, "withdraw_end": 0.60, "turn_begin": 0.60, "turn_end": 0.95, "restore_begin": 0.95, "restore_end": 1.0}},
		"seat_lock_bolt_clearance": {"count": 1, "parameters": {"pitch_circle_radius_metres": 0.24, "angle_radians": PI / 24.0}}
	}
	var ids := {}
	for operation in value.operations:
		if not exact_fields(operation, ["id", "reason", "source_objects", "parameters"]) or not expected.has(operation.id) or ids.has(operation.id) or not operation.reason is String or operation.reason.is_empty() or not operation.source_objects is Array or operation.source_objects.size() != expected[operation.id].count or not exact_fields(operation.parameters, expected[operation.id].parameters.keys()):
			return false
		for key in operation.parameters:
			var parameter: Variant = operation.parameters[key]
			if not (parameter is float or parameter is int) or not is_finite(float(parameter)) or absf(float(parameter) - expected[operation.id].parameters[key]) > 0.0000001:
				return false
		var sources := {}
		for object in operation.source_objects:
			if not exact_fields(object, ["source_object", "source_parent", "original_rest_local_transform", "operating_rest_local_transform", "local_translation_metres"]) or not object.source_object is String or object.source_object.is_empty() or sources.has(object.source_object) or not (object.source_parent == null or object.source_parent is String) or not valid_transform(object.original_rest_local_transform) or not valid_transform(object.operating_rest_local_transform) or not finite_vector(object.local_translation_metres, 1.0):
				return false
			if not pose(object.operating_rest_local_transform).basis.is_equal_approx(pose(object.original_rest_local_transform).basis):
				return false
			if not (pose(object.operating_rest_local_transform).origin - pose(object.original_rest_local_transform).origin).is_equal_approx(vector(object.local_translation_metres)):
				return false
			sources[object.source_object] = true
		ids[operation.id] = true
	return true

static func valid_station_corrections(value: Variant) -> bool:
	if not value is Array or value.size() != 3:
		return false
	var expected := {
		"pipe_route": ["DK09 | equalization pipe.001", "D1_CLEARANCE_PIPE", "pipe_route"],
		"deck_service_bore": ["DK09 | continuous deck with open well.001", "D1_CLEARANCE_DECK", "service_bore"],
		"flange_service_bore": ["DK09 | belly shaft mounting flange.001", "D1_CLEARANCE_FLANGE", "service_bore"]
	}
	var ids := {}
	var model_hash := ""
	for item in value:
		if not exact_fields(item, ["id", "filename", "model_sha256", "source_object", "runtime_node", "runtime_parent", "expected_extras", "rest_transform", "replacement_node", "original_geometry_sha256", "replacement_geometry_sha256", "surface_count", "kind", "original_points_metres", "replacement_points_metres", "tube_radius_metres", "bore_centre_metres", "bore_radius_metres", "bore_depth_interval_metres"]) or not expected.has(item.id) or ids.has(item.id) or item.filename != "station-d1-clearance-01.glb" or not digest(item.model_sha256) or not digest(item.original_geometry_sha256) or not digest(item.replacement_geometry_sha256):
			return false
		var extras: Dictionary = {"dock_id": "D1", "dock_part": "ring"} if item.id == "flange_service_bore" else {"dock_id": "D1"}
		if item.source_object != expected[item.id][0] or item.runtime_node != sanitized_name(item.source_object) or item.runtime_parent != "KIT09 | dock D1" or item.replacement_node != expected[item.id][1] or item.kind != expected[item.id][2] or not metadata_matches(item.expected_extras, extras) or not valid_transform(item.rest_transform) or not pose(item.rest_transform).is_equal_approx(Transform3D.IDENTITY) or not integral(item.surface_count, 1, 1):
			return false
		if not model_hash.is_empty() and model_hash != item.model_sha256:
			return false
		model_hash = item.model_sha256
		if not item.original_points_metres is Array or not item.replacement_points_metres is Array or not finite_vector(item.bore_centre_metres) or not (item.tube_radius_metres is int or item.tube_radius_metres is float) or not is_finite(float(item.tube_radius_metres)) or not (item.bore_radius_metres is int or item.bore_radius_metres is float) or not is_finite(float(item.bore_radius_metres)) or not item.bore_depth_interval_metres is Array:
			return false
		if item.kind == "pipe_route":
			if item.original_points_metres.size() != 4 or item.replacement_points_metres.size() != 7 or absf(item.tube_radius_metres - 0.018) > 0.000000001 or item.bore_radius_metres != 0 or not item.bore_depth_interval_metres.is_empty() or vector(item.bore_centre_metres) != Vector3.ZERO:
				return false
			for point in item.original_points_metres + item.replacement_points_metres:
				if not finite_vector(point):
					return false
			if not vector(item.original_points_metres[0]).is_equal_approx(vector(item.replacement_points_metres[0])) or not vector(item.original_points_metres[-1]).is_equal_approx(vector(item.replacement_points_metres[-1])):
				return false
		else:
			if not item.original_points_metres.is_empty() or not item.replacement_points_metres.is_empty() or item.tube_radius_metres != 0 or not vector(item.bore_centre_metres).is_equal_approx(Vector3(0.5,3.23,0)) or absf(item.bore_radius_metres - 0.020) > 0.000000001 or item.bore_depth_interval_metres.size() != 2:
				return false
			var depth: Array = [-0.014,0.0] if item.id == "deck_service_bore" else [-0.30,-0.14]
			for index in 2:
				var bound: Variant = item.bore_depth_interval_metres[index]
				if not (bound is int or bound is float) or not is_finite(float(bound)) or absf(float(bound) - depth[index]) > 0.000000001:
					return false
		ids[item.id] = true
	return true

func apply_station_correction(model: Node3D, directory: String, corrections: Array) -> bool:
	if not valid_station_corrections(corrections):
		return false
	var path: String = directory.path_join(corrections[0].filename)
	if FileAccess.get_sha256(path) != corrections[0].model_sha256 or not valid_glb_container(path):
		return false
	var existing_nodes := unique_nodes(model)
	if existing_nodes.is_empty():
		return false
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return false
	var replacement := document.generate_scene(state) as Node3D
	if replacement == null:
		return false
	var replacement_nodes := unique_nodes(replacement)
	var mesh_count := 0
	for node in replacement_nodes.values():
		if node is MeshInstance3D:
			mesh_count += 1
	if mesh_count != 3 or not replacement.transform.is_equal_approx(Transform3D.IDENTITY) or not valid_mesh_buffers(replacement):
		replacement.free()
		return false
	var candidates := []
	for item in corrections:
		var original: Variant = existing_nodes.get(item.runtime_node)
		var replacement_mesh: Variant = replacement_nodes.get(item.replacement_node)
		if not original is MeshInstance3D or str(original.get_parent().name) != item.runtime_parent or not original.transform.is_equal_approx(pose(item.rest_transform)) or not metadata_subset(original.get_meta("extras", {}), item.expected_extras) or original.mesh == null or original.mesh.get_surface_count() != 1 or not replacement_mesh is MeshInstance3D or replacement_mesh.get_parent() != replacement or not replacement_mesh.transform.is_equal_approx(Transform3D.IDENTITY) or replacement_mesh.mesh.get_surface_count() != 1:
			replacement.free()
			return false
		var corrected: ArrayMesh = replacement_mesh.mesh.duplicate()
		corrected.surface_set_material(0, original.mesh.surface_get_material(0))
		candidates.append({"node": original, "mesh": corrected})
	# All three candidates qualify before any existing mesh changes. Imported
	# render vertices may split at UV/normal seams; source geometry digests are
	# provenance. Hashes and exact node bindings qualify these replacements.
	for candidate in candidates:
		candidate.node.mesh = candidate.mesh
	replacement.free()
	return true
