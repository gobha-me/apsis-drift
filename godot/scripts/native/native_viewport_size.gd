extends RefCounted
## Keep 3D targets within the physical output and the existing logical budget.
## UI remains in logical canvas coordinates; both flight depth passes match.

static func target(logical: Vector2, density: Vector2) -> Vector2i:
	if not logical.is_finite() or not density.is_finite() or logical.x < 1.0 or logical.y < 1.0 or logical.x > 2147483000.0 or logical.y > 2147483000.0 or density.x <= 0.0 or density.y <= 0.0:
		return Vector2i.ZERO
	# Clamp before multiplying/converting, including very large finite densities.
	var pixels := logical * Vector2(minf(1.0, density.x), minf(1.0, density.y))
	return Vector2i(maxi(1, roundi(pixels.x)), maxi(1, roundi(pixels.y)))


static func fit(container: SubViewportContainer, logical: Vector2, density: Vector2) -> bool:
	var pixels := target(logical, density)
	if pixels == Vector2i.ZERO or not is_instance_valid(container): return false
	container.set_anchors_preset(Control.PRESET_TOP_LEFT)
	container.position = Vector2.ZERO
	container.size = Vector2(pixels)
	container.scale = logical / Vector2(pixels)
	return true
