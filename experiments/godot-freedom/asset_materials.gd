extends RefCounted
## Reviewed native response tuning; no geometry, textures or shared-resource edits.
## BSD-3-Clause. Baseline is a load-time bypass, not an in-place undo operation.
const REVISION := 1
const MARKER := "apsis_material_response_revision"

static func response(kind: String, cabin: bool) -> Vector2:
	match kind:
		"ivory": return Vector2(0.05, 0.68)
		"paint": return Vector2(0.10, 0.60)
		"steel": return Vector2(0.90, 0.36)
		"black", "panel":
			if cabin:
				return Vector2(0.08, 0.72)
	return Vector2(-1, -1)

static func apply(root: Node, cabin: bool, mode: String = "tuned") -> Dictionary:
	if root == null or mode not in ["baseline", "tuned"]:
		return {"ok": false, "error": "Invalid asset material root or profile", "changed_surfaces": 0}
	var result := {"ok": true, "revision": REVISION, "mode": mode, "changed_surfaces": 0}
	if mode == "tuned":
		visit(root, cabin, result)
	return result

static func visit(node: Node, cabin: bool, result: Dictionary) -> void:
	# A whole-instance override owns its appearance (for example a live UI).
	if node is MeshInstance3D and node.mesh != null and node.material_override == null:
		for index in node.mesh.get_surface_count():
			var source: Material = node.get_active_material(index)
			if not source is StandardMaterial3D:
				continue
			if source.emission_enabled or source.transparency != BaseMaterial3D.TRANSPARENCY_DISABLED or source.shading_mode == BaseMaterial3D.SHADING_MODE_UNSHADED:
				continue
			if not is_finite(source.metallic) or not is_finite(source.roughness):
				continue
			var values := response(source.resource_name.to_lower(), cabin)
			if values.x < 0 or source.get_meta(MARKER, 0) == REVISION:
				continue
			var tuned: StandardMaterial3D = source.duplicate()
			tuned.metallic = values.x
			tuned.roughness = values.y
			tuned.set_meta(MARKER, REVISION)
			node.set_surface_override_material(index, tuned)
			result.changed_surfaces += 1
	for child in node.get_children():
		visit(child, cabin, result)
