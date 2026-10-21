extends RefCounted
const WalkView = preload("res://scripts/native/native_walk_view.gd")
## Shared assertions for the real walking and flight Controls; synthetic times
## exercise layout only, never stand in for measured GPU performance.

static func run(tree: SceneTree, view: Control, owner: Variant, path: String, check: Callable) -> void:
	var paused: bool = view.paused
	var focused: bool = view.focused
	var status: String = view.save_status.text
	var was_processing := view.is_processing()
	view.set_process(false)
	var original_size := tree.root.size
	var flight: Dictionary = owner.get_freedom_flight_state()
	var walk: Dictionary = owner.get_freedom_walk_state()
	check.call(owner.save_freedom_as(path), "Diagnostic baseline Save refused")
	var bytes := FileAccess.get_file_as_bytes(path)
	check.call(not view.frame_diagnostics.enabled and not view.frame_diagnostics.visible, "Fresh view did not default diagnostics off")
	view.pause_controls("Diagnostic control check")
	view.frame_diagnostics_button.pressed.emit()
	check.call(view.frame_diagnostics.enabled and view.paused and not view.frame_diagnostics.visible and view.frame_diagnostics_button.text.ends_with("On"), "Paused diagnostic action did not select without resuming")
	view._process(60.0)
	check.call(view.frame_diagnostics.samples.is_empty() and not view.frame_diagnostics.visible, "Paused view sampled frames or showed diagnostic panel")
	view.focused = false
	view.frame_diagnostics_button.pressed.emit()
	check.call(view.frame_diagnostics.enabled, "Unfocused diagnostic action changed selection")
	view.focused = true
	var monotonic_clock: Callable = view.frame_diagnostics.clock_usec
	var synthetic_usec := [0]
	view.frame_diagnostics.clock_usec = func() -> int:
		synthetic_usec[0] += 16667
		return synthetic_usec[0]
	# Exercise the overlay's actual layout at the two output sizes without
	# advancing the paused world to manufacture performance samples.
	for pixels in [Vector2i(1280, 720), Vector2i(640, 450)]:
		tree.root.size = pixels
		for frame in 3: await tree.process_frame
		var packet: Dictionary = view.state
		var targets: Array = [view.viewport.size] if view is WalkView else [view.far_viewport.size, view.near_viewport.size]
		for sample in 40: view.frame_diagnostics.observe(1.0 / 60.0, packet, targets, true)
		for frame in 3: await tree.process_frame
		var panel: PanelContainer = view.frame_diagnostics
		check.call(panel.visible and panel.label.text.contains("GPU timing not sampled") and panel.label.text.contains("dropped"), "Enabled diagnostics omitted its metric boundary or clock")
		check.call(Rect2(Vector2.ZERO, view.size).encloses(panel.get_rect()), "Diagnostic panel escaped its parent at " + str(pixels))
		check.call(panel.label.size.y >= panel.label.get_minimum_size().y and panel.get_global_rect().encloses(panel.label.get_global_rect()), "Diagnostic text clipped at " + str(pixels))
		check.call(panel.mouse_filter == Control.MOUSE_FILTER_IGNORE and panel.label.mouse_filter == Control.MOUSE_FILTER_IGNORE and panel.focus_mode == Control.FOCUS_NONE, "Diagnostic overlay intercepts gameplay input")
		view.pause_controls("Discard synthetic diagnostic samples")
		check.call(panel.samples.is_empty() and not panel.visible, "Safety pause retained diagnostic epoch")
	view.frame_diagnostics_button.pressed.emit()
	view.frame_diagnostics.clock_usec = monotonic_clock
	check.call(not view.frame_diagnostics.enabled and view.frame_diagnostics_button.text.ends_with("Off"), "Diagnostic action did not disable")
	check.call(owner.get_freedom_walk_state() == walk and owner.get_freedom_flight_state() == flight and owner.save_freedom_as(path) and FileAccess.get_file_as_bytes(path) == bytes, "Diagnostic controls/layout changed complete authoritative Save")
	tree.root.size = original_size
	for frame in 3: await tree.process_frame
	view.focused = focused
	view.save_status.text = status
	if not paused: view.resume_requested()
	view.set_process(was_processing)
