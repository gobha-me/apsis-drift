extends "res://scripts/flight/flight_basics.gd"
## One paused snapshot; no queries, policy or simulation ownership.
var reference_text := ""

func open() -> void:
	page = 0
	scroll.scroll_vertical = 0
	refresh()
	show()
	back_button.grab_focus()

func refresh() -> void:
	if not is_instance_valid(heading): return
	heading.text = "SURFACE CONDITIONS"
	_paused.text = "PAUSED / READ-ONLY REFERENCE"
	_hint.text = "Up / down: scroll text\nB / Circle / Esc / Start: back to controls"
	body.text = reference_text
	previous_button.hide()
	next_button.hide()
	back_button.focus_neighbor_left = NodePath(".")
	back_button.focus_neighbor_right = NodePath(".")
	back_button.focus_previous = NodePath(".")
	back_button.focus_next = NodePath(".")
