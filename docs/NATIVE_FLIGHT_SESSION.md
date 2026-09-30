# Saved native flight session

2026-09-30, #330. `NativeFreedomFlightSession` owns a mutable C++ flight
document, its qualified physical system and generated rotation recipe. It opens
only an explicitly selected [format18 flight save](FREEDOM_FLIGHT_SAVE.md);
docked/career selections and mismatched home identity refuse activation.
Godot still needs its live consumer and the physical station transitions. This
session alone is not the playable handoff specified in #245.

## One clock and one force owner

Presentation submits positive/negative translation and rotation fractions.
Each finite fraction must be in [0,1]. Saved assistance and an optional saved
hold request govern allocation; input does not carry a second assistance flag.
Selection changes do not move the ship or advance its clock.

Every accepted advance is exactly one 120 Hz simulation tick. First the pure
`evaluate_orbit_hold_correction` query validates and allocates the existing
bounded hold law. Then its actual channels enter
`advance_atmospheric_flight`, which evaluates gravity, propulsion and air at
all four coupled RK4 stages. This route also applies to vacuum flight. Choosing
the gravity-only integrator from frame-start altitude would lose air forces
when an active hold tick crosses inward through the atmosphere boundary.

Disabled, Advanced, manual-priority and unavailable hold states retain ordinary
flight through the same kernel. Air does not disappear because hold is paused.
Requested hold force is diagnostic; the applied channels and their propulsive
impulses contain the correction once. Future fuel accounting must consume the
actual gross channels, not bill the diagnostic again.

Integration, post-step observation and save qualification operate on a candidate.
Only a complete persistable candidate replaces the entire document, advancing
rigid and origin-history clocks together without inventing discoveries. Invalid
input, an exhausted clock or refused observation leaves the session unchanged.
The observation query is pure; it reports the selected atmospheric model and
shared orbital telemetry, not a forecast presented as the last applied hold
status. Step results report the allocation that was actually used.

World contexts are temporary views of the session's own physical system. Moving
or copying a session does not retain a pointer into the old owner.

## Explicit persistence

`save_as` requires a bounded absolute path and uses the existing atomic C++
format18 writer. It neither changes the selected source identity nor autosaves
simulation. Reopening that destination explicitly selects its new source path.
All recipe/model/history/state selections survive reopening; derived observations
are recomputed by the same owners.

## Qualification

`native-flight-session-contract` checks malformed activation, all twelve invalid
actuator channels, steps, clock limits, hold selection, paths and atomic refusal
before trajectories. It verifies pure repeated observation and moved ownership.
Vacuum hold and disabled atmospheric traces preserve exact provider behavior.
Inward and outward boundary-crossing fixtures require air impulse during the
crossing tick and exact parity with direct plan-plus-atmospheric integration.

Seeds 0, 42 and maximum u64 run uninterrupted/reopened 600-tick traces through
assistance changes, hold disabling/reengagement and manual translation/rotation.
Actual Save As/Continue files preserve state bits, clocks, histories, model
selections, requested/applied propulsion and observations; original source bytes
remain unchanged. Older provider checksums remain independent.
