extends Node3D
## One C++ spherical source proxy. No Godot generation or collision owner.
const MinorState = preload("res://scripts/native/native_minor_state.gd")
var nodes: Dictionary = {}
var error := ""
var owner_id := ""
var system_id := ""
var radius := 0.0

func stage(bridge: Variant, pending: Dictionary, state: Dictionary) -> bool:
	if not nodes.is_empty() or not MinorState.valid(state): return false
	var packet: Dictionary = bridge.get_freedom_minor_geometry(pending.get("candidate_id", ""))
	if not valid_packet(packet, state):
		error = "The C++ minor-body inspection geometry is unavailable"
		return false
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = packet.vertices
	arrays[Mesh.ARRAY_NORMAL] = packet.normals
	arrays[Mesh.ARRAY_INDEX] = packet.indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := StandardMaterial3D.new()
	material.albedo_color = state.minor_tint
	material.roughness = 0.96
	var body := MeshInstance3D.new()
	body.mesh = mesh
	body.material_override = material
	body.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	body.layers = 1
	add_child(body)
	nodes[0] = body
	owner_id = state.body_id
	system_id = state.system_id
	radius = state.body_radius
	return apply_state(state)

static func valid_packet(packet: Dictionary, state: Dictionary) -> bool:
	if packet.size() != 14 or packet.get("version") != 1 or packet.get("body_kind") != "minor": return false
	for key in ["body_version", "body_id", "system_id", "craft_id", "frame_id", "frame_version", "tick", "body_radius"]:
		if packet.get(key) != state.get(key): return false
	if packet.get("source_checksum") != state.checksum: return false
	var vertices: Variant = packet.get("vertices")
	var normals: Variant = packet.get("normals")
	var indices: Variant = packet.get("indices")
	if not vertices is PackedVector3Array or vertices.is_empty() or vertices.size() > 2145 or not normals is PackedVector3Array or normals.size() != vertices.size(): return false
	if not indices is PackedInt32Array or indices.is_empty() or indices.size() > 11904 or indices.size() % 3 != 0: return false
	for i in vertices.size():
		if not vertices[i].is_finite() or absf(vertices[i].length() - state.body_radius) > 1.0 or not normals[i].is_finite() or absf(normals[i].length_squared() - 1.0) > 0.0001: return false
	for index in indices:
		if index < 0 or index >= vertices.size(): return false
	return true

func apply_state(state: Dictionary) -> bool:
	if not MinorState.valid(state) or state.body_id != owner_id or state.system_id != system_id or state.body_radius != radius:
		error = "The minor-body geometry owner changed"
		return false
	nodes[0].transform = Transform3D(state.minor_fixed_basis, state.minor_center)
	return true

func tick(_delta: float, _camera_position: Vector3, _paused: bool) -> void:
	pass
