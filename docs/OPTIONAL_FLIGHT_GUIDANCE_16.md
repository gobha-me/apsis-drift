# Optional flight guidance study 16

2026-09-19; first bounded increment of #268, related to #238, #50 and #171.

## Approved interaction

Guidance is off at startup. D-pad Up opens a live-flight plan menu; G is the
keyboard shortcut. Choose Orbit, Atmospheric return, Escape, or Clear flight
plan. D-pad navigates, A/Cross selects, B/Circle closes. Keyboard navigation
uses Tab / Shift-Tab and Enter; Escape closes. Start closes the active selector
or opens the normal pause menu when the selector is closed.

Selecting a plan never changes assist, throttle, attitude, velocity, fuel or
simulation time. Clearing a plan removes presentation only. The menu does not
pause flight: continue flying while choosing. A future assisted maneuver will
require a separate explicit execution action; none is implemented here.

Existing version-4 bindings are preserved without a profile migration. A
custom flight binding on any D-pad direction disables the D-pad opener; a
custom G binding disables that shortcut. Mapped menu keys/directions remain
flight controls, not double-bound shortcuts. The pause menu provides a mouse /
controller-accessible Flight guidance entry as fallback; this explicitly
resumes flight before opening the selector. Focus loss and controller safety
pauses close it. Normal pause still requires neutral controls on resumption.

## Read-only native prediction

The C++ provider consumes the same planet descriptor and lab state as flight.
It projects a coast under central gravity, ignoring ongoing thrust, assist,
drag, terrain relief, body rotation and other bodies. It is not a second flight
simulation, a save format or a maneuver executor. No mutable generator streams
are used. Polling occurs only while selected, at at most five updates/second.

Requests are bounded to 1–1,800 seconds and 2–257 samples; the native display
requests 900 seconds and 129 samples. RK4 substeps are at most two seconds.
The forecast ends at the atmosphere threshold, reference sphere, requested
time horizon or numerical envelope. Segment clipping is conservative and can
end slightly early at a grazing crossing; it is not terrain collision detection.
An in-atmosphere start gets no fictional vacuum descent path.

The graph uses an instantaneous radial/tangential plane. A white dot is the
current ship position, green is the predicted coast, and dashed blue is a
geometric reference. The reference is distinctly **not** a future prediction
of the current ship. Near-radial motion has no determined orbital plane; the
reference uses projected ship heading or a deterministic fallback, explicitly
identified in the result. The graph can rescale as the state changes.

- Orbit reference: a circle at the current altitude or 20 km beyond the
  atmosphere boundary, whichever is higher. Ideal tangential speed alone is
  insufficient while substantial radial motion remains.
- Return reference: a zero-radial-speed ellipse starting at the current radius
  as its apoapsis, with periapsis at the air boundary (reference sphere for an
  airless body). This is not a solved burn from the actual current velocity,
  atmospheric corridor or safe landing promise.
- Escape reference: the local zero-energy tangential threshold. Already having
  escape energy does not imply a safe outward path: an inward trajectory can
  still intersect the body. Watch the green coast and termination warning.

Ideal instantaneous delta velocity is exposed only when the target position
matches the current position and the craft is outside atmosphere; the UI does
not present it as an executable burn. It shows reference altitude/speed and
current sideways/vertical speed. Burn timing, pointing gates, thrust-limited
integration and atmospheric corridors remain future work.

## Display and verification boundaries

Selected guidance replaces only the cockpit NAV area. The flight-vector and
propulsion screens retain their normal functions. Chase view gets an optional
compact graph. Selecting Clear restores the original uncluttered displays.
The numerical provider, bridge read-only behavior and synthetic menu routing
have dedicated tests; synthetic input is not physical-controller qualification.
GPU inspection and a pilot usability pass are separate acceptance gates.

Local verification: full GCC and Clang builds, eight focused C++ contracts per
compiler, the native guidance read-only contract, existing input/rotational
coast contracts, selector contracts and a headless main/bridge integration
fixture passed. The latter stubs graphics bootstrap; it is not GPU evidence.
Separate 1920×1080 native GPU captures inspected the orbit cockpit/chase views,
return reference and menu. That review prompted a larger, cockpit-specific
instrument layout. The user-controlled running build was not replaced.

Reproduce bridge tests with the installed Godot binary and a prepared snapshot:

```sh
godot --headless --path experiments/godot-freedom \
  --script res://guidance_test.gd -- /absolute/path/to/snapshot.json
godot --headless --path experiments/godot-freedom \
  --script res://flight_plan_menu_test.gd
godot --headless --path experiments/godot-freedom \
  --script res://guidance_integration_test.gd -- \
  --snapshot=/absolute/path/to/snapshot.json
```

`guidance_visual_review.gd` uses the usual native study arguments plus
`--review-dir=/absolute/path/to/an/existing/output-directory`. It produces PNGs
with adjacent original-project/BSD-3-Clause provenance. Practice relocation is
explicitly marked; these images do not demonstrate an achieved orbit. No new
binary assets are committed. Controller feel and sofa-distance readability
remain for the pilot to judge.

No new raster assets, changes to existing GLBs, physics tuning, controller
defaults or saved universe generation accompany this guidance implementation.

## Related approved landing design (not implemented here)

The same landing action will request a bounded assisted landing when assist is
on, or deploy gear only when assist is off. Turning assist on alone will not
deploy gear. Keep gear state separate from landing-maneuver state; pilot
intervention cancels the maneuver without an automatic surprise retraction.
Thrust availability and later fuel limits apply. Unsuitable ground requires an
explicit refusal/abort. The pure touchdown envelope from #265 does not yet
remove the experimental floor guard or implement physical landing.
