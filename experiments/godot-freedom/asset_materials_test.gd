extends SceneTree
const Materials = preload("res://asset_materials.gd")
var failures := 0
var checked_surfaces := 0
var material_keepalive: Array[Material] = []

func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)

func instance(material: Material) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	node.mesh = BoxMesh.new()
	node.mesh.material = material
	return node

func material(kind: String) -> StandardMaterial3D:
	var source := StandardMaterial3D.new()
	source.resource_name = kind
	source.metallic = 0.4
	source.roughness = 0.3
	source.albedo_color = Color(0.14, 0.24, 0.34)
	return source

func collect(node: Node, slots: Array) -> void:
	if node is MeshInstance3D and node.mesh != null:
		for index in node.mesh.get_surface_count():
			var source: Material = node.get_active_material(index)
			if source is StandardMaterial3D:
				slots.append({"node": node, "index": index, "source": source, "mesh": node.mesh, "metal": source.metallic, "rough": source.roughness, "color": source.albedo_color, "emission": source.emission, "transparent": source.transparency, "normal": source.normal_texture, "geometry": var_to_bytes(node.mesh.surface_get_arrays(index))})
	for child in node.get_children():
		collect(child, slots)

func verify_asset(path: String, cabin: bool) -> void:
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	check(document.append_from_file(path, state) == OK, "Asset import failed")
	var asset := document.generate_scene(state)
	if asset == null:
		check(false, "Asset produced no scene")
		return
	var slots: Array = []
	collect(asset, slots)
	var result: Dictionary = Materials.apply(asset, cabin)
	check(result.ok and result.changed_surfaces > 0, "Native asset received no response tuning")
	for slot in slots:
		var source: StandardMaterial3D = slot.source
		var current: Material = slot.node.get_active_material(slot.index)
		# Keep override RIDs alive through immediate node destruction; Godot's
		# dummy renderer queries them while freeing MeshInstance3D resources.
		material_keepalive.append(current)
		check(source.metallic == slot.metal and source.roughness == slot.rough, "Shared source resource mutated")
		check(slot.node.mesh == slot.mesh and var_to_bytes(slot.mesh.surface_get_arrays(slot.index)) == slot.geometry, "Mesh or vertex/index buffers changed")
		check(current.albedo_color == slot.color and current.emission == slot.emission and current.transparency == slot.transparent and current.normal_texture == slot.normal, "Protected material fields changed")
		var expected: Vector2 = Materials.response(source.resource_name.to_lower(), cabin)
		var protected: bool = source.emission_enabled or source.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED or source.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED
		if expected.x < 0 or protected:
			check(current == source, "Unapproved native material overridden")
		else:
			check(current != source and is_equal_approx(current.metallic, expected.x) and is_equal_approx(current.roughness, expected.y), "Tuned native material mismatch")
		checked_surfaces += 1
	check(Materials.apply(asset, cabin).changed_surfaces == 0, "Repeated application replaced materials again")
	print("Material response inspected %s: %d changes / %d surfaces" % [path.get_file(), result.changed_surfaces, slots.size()])
	asset.free()

func _initialize() -> void:
	check(not Materials.apply(null, false).ok, "Null asset accepted")
	var source := material("ivory")
	var first := instance(source)
	var untouched := MeshInstance3D.new()
	untouched.mesh = first.mesh
	check(not Materials.apply(first, false, "unknown").ok and first.get_surface_override_material(0) == null, "Invalid profile mutated instance")
	check(Materials.apply(first, false, "baseline").changed_surfaces == 0 and first.get_surface_override_material(0) == null, "Baseline is not exact")
	check(Materials.apply(first, false).changed_surfaces == 1, "Coating did not tune")
	check(first.get_active_material(0) != source and untouched.get_active_material(0) == source, "Shared mesh material was mutated")
	check(is_equal_approx(source.metallic, 0.4) and is_equal_approx(source.roughness, 0.3), "Original values changed")
	check(first.get_active_material(0).albedo_color == source.albedo_color, "Base color changed")
	material_keepalive.append(first.get_active_material(0))
	first.free()
	untouched.free()
	for protected_kind in ["glass", "emissive", "unshaded", "unknown", "global-override"]:
		var protected := material("ivory")
		if protected_kind == "glass": protected.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		if protected_kind == "emissive": protected.emission_enabled = true
		if protected_kind == "unshaded": protected.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		if protected_kind == "unknown": protected.resource_name = "optical canopy"
		var node := instance(protected)
		if protected_kind == "global-override": node.material_override = protected
		check(Materials.apply(node, true).changed_surfaces == 0 and node.get_active_material(0) == protected, "Protected fixture changed: " + protected_kind)
		node.free()
	var empty := MeshInstance3D.new()
	check(Materials.apply(empty, false).changed_surfaces == 0, "Null mesh not safely skipped")
	empty.mesh = ArrayMesh.new()
	check(Materials.apply(empty, false).changed_surfaces == 0, "Zero-surface mesh not safely skipped")
	empty.free()
	for kind in ["black", "panel"]:
		var node := instance(material(kind))
		check(Materials.apply(node, false).changed_surfaces == 0, "Broad shared exterior material changed")
		check(Materials.apply(node, true).changed_surfaces == 1, "Cabin equipment not tuned")
		node.free()
	var nonfinite := material("ivory")
	nonfinite.roughness = NAN
	var invalid_node := instance(nonfinite)
	check(Materials.apply(invalid_node, false).changed_surfaces == 0, "Non-finite source response was overridden")
	invalid_node.free()
	var assets := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--assets="): assets = arg.trim_prefix("--assets=")
	if not assets.is_empty():
		verify_asset(assets.path_join("hero-ship-near.glb"), false)
		verify_asset(assets.path_join("hero-cockpit-near.glb"), true)
	print("Asset material contracts: %d failures; %d native surfaces checked" % [failures, checked_surfaces])
	quit(0 if failures == 0 else 1)
