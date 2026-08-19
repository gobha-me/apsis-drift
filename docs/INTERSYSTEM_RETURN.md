# Deterministic Intersystem Return

The v0.4.12 return path completes one mission after the entry-anywhere
Planetfall objective. Terrain, planetary flight, system flight, jump transit,
station guidance, mission state, and rendering remain application-owned;
TermForge continues to own terminal input and presentation protocols.

## Return sequence

From any valid surface location, the player may climb back through terrain and
atmospheric flight. In the orbital regime, Enter reverses the planet-fixed
handoff into target-system inertial flight without changing the authoritative
tick, planet, objective, or collected delta. Re-entry remains legal before or
after objective completion.

The home jump is available only after objective completion. J begins a
three-second cancelable spool. The source system-flight state is frozen and
saved while spooling; cancellation restores it at the current universe tick.
Commitment removes that state, binds the existing origin arrival solution, and
finishes after the same deterministic two-second transit as the outbound jump.

## Origin Station approach

The version-1 Origin Station waypoint is
`(40,000, -80,000,000,000, 0)` metres in origin-system inertial space. The
return corridor arrives at `(0, -80,000,000,000, 0)`, leaving an explicit
40 km approach. The station is therefore neither the system barycenter nor the
universe origin.

The approach state records the authoritative tick, origin system and station
identities, position, velocity, attitude basis, flight mode, and held controls.
Assisted flight is capped at 1,000 m/s with 250 m/s² acceleration. Kitty and
ANSI consume the same station-relative distance, signed closing speed, braking
cue, and code-authored center marker.

Docking physics is deliberately minimal. Distance at or below 5,000 metres
produces `ENTER DOCK`; Enter is still required to dock. Docking atomically
removes the flight state and advances the mission from `objective_complete` to
`returned`. The station board then exposes a separate `TURN IN CONTRACT`
action. Repeated or out-of-order docking and turn-in commands are rejected
without changing state.

## Persistence

Save format 11 admits one `origin_return` state only during
`origin_system_return`. Target-system flight remains present during a
cancelable return spool and is absent after commitment. Objective-complete,
returned, and turned-in states retain the immutable target discovery and
exactly one collected world delta. Earlier alpha formats are rejected before
state decoding rather than being upgraded with synthesized return state.

## Acceptance replay

The fixed seed-42 replay departs from the opposite longitude at tick 600,
cancels and resumes the return spool, commits at tick 1020, arrives in the
origin system at tick 1260, and docks at tick 5923. It saves and reloads at
departure, canceled spool, committed transit, origin arrival, mid-approach,
docked, and turned-in boundaries.

Run the application-owned simulation and framebuffer path:

```bash
./build/apsis-drift --intersystem-return-acceptance \
  --profile remote --report return-application-framebuffer.json
```

The schema 2 report identifies `evidence_scope: application_framebuffer` and
does not claim Kitty/ANSI encoding. Invalid dimensions, buffer mismatches, non-finite craft
state, wrong identities, mistimed commands, repeated transitions, and
out-of-order docking are rejected before visual smoke checks.
