# Alternate head-look and six-axis flight direction

Historical layout 2. Superseded by [thrust flight lab / layout 4](THRUST_FLIGHT_LAB_09.md):
analog main/retro triggers, direct roll, stick-click head-look and opt-in six-DOF.

2026-09-18. Owner direction: ship control takes priority over continuous
head-look; hold LT/L2 and use the right stick to look, then release to snap to
the ship's forward centerline. Eventual flight must expose all six degrees of
freedom within the vessel's atmospheric / space capabilities.

## Implemented input increment

Layout 2 uses LT/L2 as a remappable **hold-to-look modifier**, not reverse drive.
Holding it redirects the right stick from lateral/vertical translation to
head-look. Release snaps pitch/yaw of the head to zero relative to the ship;
the ship itself does not rotate. Translation waits for the stick to recenter
before rearming, preventing a sudden strafe when leaving head-look. Heading and
forward/reverse drive remain available while looking. Trigger hysteresis avoids
repeated snapping near the activation threshold.

Keyboard equivalent: Alt + arrows; existing RMB mouse inspection remains.
The translation actions are suppressed in look context, including their keyboard
equivalents. Holding the modifier is a context change, not an additional ship
command. Pause/focus/disconnect safety remains in force.

**Current model limitation:** C++ still has four controllable axes: heading and
three translations. In this interim layout left stick X changes heading, left
stick Y drives forward/reverse, and right stick translates sideways/up/down.
Pitch and roll are not implemented or faked with camera motion. This is not yet
six-DOF flight. The left-stick Y assignment is transitional, not a decision that
pitch should be unavailable in the final layout.

The new profile is `user://freedom-controls-v2.json`; the old v1 profile is left
untouched. Layout 2 starts with new defaults rather than loading incompatible
trigger and stick bindings. Both keyboard and pad bindings remain editable.
The controller-06 screenshot is historical and shows the earlier mapping.

Tests cover modifier gating, no simultaneous look/translation, hysteresis,
single release recenter, stick-neutral rearming, settings round-trip, and native
camera snap without ship strafe. Hardware feel remains a playtest concern.

## Next physics increment — not implemented by the input patch

- C++ owns complete orientation and angular velocity, body-relative force and
  torque demands, limits and fixed-step integration. Godot follows that pose.
- Preserve existing digital replay/save behavior; introduce any changed saved
  state and simulation semantics explicitly, with compatibility/version tests.
- The six axes are pitch, yaw, roll, forward/back, lateral and vertical motion.
  Two ordinary thumbsticks supply four analog channels; bumpers/triggers or an
  explicit control layer must provide the remaining pair. Final assignments
  need controller playtesting, not an assumption that two sticks provide six
  independent analog channels.
- In space, translational and angular inertia and ship thruster authority drive
  response. Assistance is an explicit control policy, not hidden camera banking.
- In atmosphere, the same command meanings feed density/relative-airflow,
  gravity, drag and vessel-specific lift/control authority. Atmospheric limits
  should change forces and achievable motion continuously, not swap bindings
  or categorically prohibit every roll. Include thermal/environmental limits.
- Test finite/bounded input and state, attitude normalization, torque/force
  symmetry, frame cadence, atmosphere transitions, and old-path compatibility
  before calling the result six-DOF or revisiting collision/landing qualification.

No economy, mining or progression prerequisite is introduced by these controls.
