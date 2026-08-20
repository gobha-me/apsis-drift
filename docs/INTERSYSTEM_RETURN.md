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

## Origin Station flight and approach

Origin Station version 2 carries a host planet and deterministic circular-orbit
recipe. At every authoritative tick, the application composes the host-planet
ephemeris with that planet-relative orbit. The home jump resolves the future
station position, arrives 40 km along its positive-X corridor, and matches its
velocity. The station is therefore neither the system barycenter nor a static
launch-era waypoint.

One common station-flight state records the authoritative tick, origin system
and station identities, station-relative position and velocity, attitude
basis, flight mode, and held controls. Launch initializes it five kilometres
along the positive-X corridor with matched station velocity; the home jump
initializes it forty kilometres along the same corridor. Absolute render pose
is derived from the same tick-resolved station ephemeris.
Assisted flight is capped at 1,000 m/s with 250 m/s² acceleration. Kitty and
ANSI consume the same station-relative distance, signed closing speed, braking
cue, and code-authored center marker.

Docking physics is deliberately minimal. Distance at or below 5,000 metres and
relative speed at or below 25 m/s produces `ENTER DOCK`; Enter is still
required to dock. A craft at an obsolete station position or above the speed
limit is refused. Docking atomically removes the flight state. Before outbound
commitment it returns the accepted mission to the station without progress;
after the objective it advances the mission from `objective_complete` to
`returned`. The station board then exposes a separate `TURN IN CONTRACT`
action. Repeated or out-of-order docking and turn-in commands are rejected
without changing state.

## Persistence

Save format 16 admits one `origin_station_flight` state during live
`origin_system_flight`, frozen `outbound_jump_spooling`, or
`origin_system_return`. Target-system flight remains present during a
cancelable return spool and is absent after commitment. Objective-complete,
returned, and turned-in states retain the immutable target discovery and
exactly one collected world delta. Formats 1 through 13 are rejected before
state decoding rather than being upgraded with synthesized return state.

## Acceptance replay

The fixed seed-42 replay departs from the opposite longitude at tick 600,
cancels and resumes the return spool, commits at tick 1020, arrives in the
origin system at tick 1260, and docks at tick 5929. It saves and reloads at
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
