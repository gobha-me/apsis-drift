# Home-Planet Signal Run

The v0.4.32 Signal Run is Guided onboarding contract one. It composes the
deterministic Origin Station, origin-system flight, tutorial-safe home planet,
planetary regimes, scanner, collection journal, moving-station rendezvous, and
atomic save loader into one application-owned objective. Terminal protocols
and degradation remain TermForge-owned.

## Contract binding and progression

The contract is regenerated from independent deterministic streams. For one
universe seed it binds exactly one stable contract ID, the existing Origin
Station, the tutorial-safe home planet, and surface-signal ordinal zero. It
does not reroll terrain, reorder the signal catalog, or consume mutable
discovery state.

The authoritative local objective progresses only through:

```text
offered -> active -> completed -> returned -> turned_in
```

Acceptance occurs while docked. Launch creates a real station-relative craft.
Moving outside the 5 km docking envelope and pressing Enter hands the craft's
physical system-space position to home Planetfall. Collection changes the
objective to `completed`; ascent returns to orbital flight, then the physical
planetary departure initializes a station-relative approach to the station's
current analytic waypoint. Docking changes the objective to `returned`, and an
explicit station turn-in changes it to `turned_in` and advances Guided
onboarding atomically to contract two.

Redocking before Planetfall leaves the accepted objective active. Enter at or
inside the inclusive 5 km boundary redocks when relative speed is at most
25 m/s. If the craft is inside that boundary but moving faster, Enter refuses
the transition and tells the player to brake or depart; it never silently
starts Planetfall. Enter starts home Planetfall only after an active contract
has moved outside the docking envelope. Every outcome remains visible as
station or cockpit feedback, so an immediate post-launch Enter cannot resemble
a process exit.

The Origin Station instrument always reports actual camera-relative bearing,
elevation, range, closing speed, and AHEAD/BEHIND location. When the projected
station is outside the framebuffer or behind the camera, a stable cyan edge
cue points toward it. The action line states exactly one current Enter result:
`ENTER DOCK`, `BRAKE/GO`, `ENTER FALL`, or `APPROACH`.

The flight check may be completed, ignored, or repeated without falsely
advancing mission state. Flying away costs time but never moves or rerolls the
bound target.

## Contextual flight check

The station briefing presents every required concept before launch: A/D
attitude, Q/E translation, R/F vertical translation, W thrust, released-input
coast, S braking, Enter targeting/Planetfall, and the later Enter redock. The
cockpit then advances contextual prompts only after observing those real input
commands. It never disables unrelated controls or gates mission progress on
the prompts.

Observed prompt history is presentation-only. It is excluded from save
projection, simulation checksums, generation, and mission state, so a reload
may repeat useful guidance without changing the career. The same semantic
COMMS and instrument text is drawn into the application framebuffer before
Kitty or ANSI presentation.

## Planetfall and return rules

Assisted and Pilot share the same physical route and immutable target.
Planetfall begins in a bounded orbital state derived from the station craft;
the player can take an early or opposite-side entry and continue around the
home planet. The acceptance guidance holds orbital altitude until the target
corridor is near, then crosses orbital, atmospheric, and terrain regimes.

Pilot retains the implemented thermal load and forced-abort rules. Its starter
guidance reacts to rising load by braking and climbing until the craft cools,
then resumes entry. The contract remains completable without suppressing or
rewriting Pilot consequences. Assisted reports the same thermal information
without enforcing an abort.

After collection, R ascends to the orbital regime. Enter creates a physical
station-return state from the planetary departure pose and velocity. The home
contract uses a bounded high-speed station-flight envelope for the long local
transfer; the established intersystem station-flight envelope is unchanged.
Autopilot continuously resolves the moving station and brakes to the inclusive
5 km / 25 m/s docking boundary.

## v0.4.32 acceptance matrix

Seed `42` binds:

- station `station-ce51e866ec4e032d`;
- contract `contract-b9e5a14a1d979f3a`;
- home signal `signal-71d4c959dcd64423`.

The schema-6 renderer-neutral report runs the complete Assisted route for
seeds `42`, `12648430`, and `1`, then completes seed `42` under Pilot thermal
rules. All generated tutorial homes are temperate by contract. The report
records nonzero thermal, planetary, return-flight, and framebuffer evidence.

The canonical run verifies exact save/encode/decode/hydrate equivalence at
eight ordered boundaries:

1. accepted while docked;
2. station flight;
3. home orbit;
4. atmospheric flight;
5. terrain flight;
6. objective complete;
7. ascent back to orbit;
8. moving-station rendezvous.

It also writes and reloads a private same-directory checkpoint at planetary
tick 600, compares the authoritative flight and framebuffer checksums, and
removes the private file. Invalid viewports and missing report destinations
fail before allocation.

Run both retained render profiles with:

```bash
./build/apsis-drift --signal-run-acceptance \
  --profile local --report signal-run-local.json
./build/apsis-drift --signal-run-acceptance \
  --profile remote --report signal-run-remote.json
```

`--snapshot PATH` retains the canonical final pre-departure planetary frame
for visual inspection. Rendering cadence, render profile, prompt history, and
framebuffer checksums never enter authoritative simulation.

## v0.4.37 departure acceptance

The driver-backed departure trace uses the shipped application input path. It
accepts and launches contract one, presses Enter immediately to prove an
explicit redock while the process remains alive, relaunches, thrusts beyond
5 km, and presses Enter again to prove Planetfall begins. The report retains
the interaction events, application framebuffer checksum, authoritative flight
checksums, and encoder byte evidence. CTest compares repeated runs across both
Kitty and ANSI and the local and remote render profiles.

```bash
./build/apsis-drift --guided-departure-acceptance \
  --driver kitty --profile local --report departure-kitty.json
./build/apsis-drift --guided-departure-acceptance \
  --driver ansi --profile remote --report departure-ansi.json
```

An explicit `--driver kitty|ansi` and `--report PATH` are required. The mode
does not accept save, seed, viewport, workload, or keyboard overrides.
