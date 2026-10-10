# Saved native flight session

2026-09-30, #330. `NativeFreedomFlightSession` owns a mutable C++ flight
document, its qualified physical system and generated rotation recipe. It opens
only an explicitly selected [physical flight save](FREEDOM_FLIGHT_SAVE.md) or
supported versioned journey wrapper;
docked/career selections and mismatched home identity refuse activation.
The [saved native consumer](SAVED_NATIVE_FLIGHT.md) uses this owner for ordinary
flight and physical station/surface transitions; presentation does not advance
a second body or universe.

## One clock and one force owner

Presentation submits positive/negative translation and rotation fractions.
Each finite fraction must be in [0,1]. Saved assistance and an optional saved
hold request govern allocation; input does not carry a second assistance flag.
Selection changes do not move the ship or advance its clock.

`current_orbit_hold_target()` is a pure availability query for the explicit
player selector. It requires a stable bound trajectory above the actual air
boundary and no conflicting pilot, attachment, surface, travel or loss state.
The target radius comes from the qualified gravity query; its signed plane comes
from normalized C++ specific angular momentum with canonical zero components.
`hold_current_orbit()` validates that target through the existing provider and
commits the saved request without changing pose, time, resources or history.
`set_hold({})` clears it. This application selection does not change the pure
controller's target contract or create an automatic orbit-insertion mode.

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
impulses contain the correction once. The selected resource ledger bills actual
gross firing once; insufficient fuel falls back to passive motion without
discarding the request or pretending that a dry thruster fired.

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
writer for the selected flight/journey format. It neither changes the selected source identity nor autosaves
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
