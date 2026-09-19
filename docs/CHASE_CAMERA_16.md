# Independent chase-camera attitude

2026-09-19; #269, in response to pilot feedback that exterior movement felt
like the ship was attached to the camera by a stick.

Previously both camera offset and up direction used the current ship basis
directly. Every ship rotation instantly moved the camera around a rigid boom.
The native chase view now separates its attitude from the craft: a normalized
quaternion follows the target orientation at an exponential rate of 4/s.
Constant target response is independent of render cadence; dynamic-target
sampling still depends on the frames presented.

Translation remains immediate. The camera offset is added to the current ship
position, not to a lagged position that could trail kilometres behind at orbital
speeds. L3/manual orbit is applied after attitude following. Existing zoom,
terrain-clearance correction, cockpit eye, FOV and flight controls are unchanged.
The look-at target offset and up vector use the same smoothed basis, avoiding
a second instantaneous roll coupling. Entering chase view and practice
relocation reset the follower so stale orientation is not inherited.

The helper rejects invalid basis/nonfinite or negative delta without poisoning
stored orientation. Tests exercise abrupt yaw, convergence, 30/60/120 Hz,
zero/large/invalid deltas, shortest-arc wrapping, half turns, full roll, entry
reset, immediate 8 km/s translation, orbit offsets and zoom. These are
presentation tests, not changes to authoritative C++ flight.

Pilot acceptance is still needed: the response time is a starting point, not
a declaration that camera feel is finished. No prediction, velocity-aligned
camera, aim reticle or new control preset is introduced in this fix.
