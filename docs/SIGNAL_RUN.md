# Signal Run Acceptance Path

The v0.4 Signal Run composes the deterministic origin station, planetary
flight, generated surface signals, scanner, collection state machine, sparse
world journal, and atomic save loader into one application-owned objective.
Terminal protocols and degradation remain TermForge-owned.

## Playable loop

A fresh profile starts docked at its generated Origin Station. Select
`ACCEPT BRIEFING`, then `LAUNCH`. The briefing binds the first compatibility-
ordered signal and records it as the sole initial discovery. In flight:

- steer with arrows or W/A/S/D, strafe with Q/E, and descend/ascend with F/R;
- the cockpit reports target bearing, distance, signed closing speed, arrival
  estimate, braking cue, strength, and scan progress;
- orbital flight coasts when thrust is released; use opposing W/S or R/F
  thrust to brake before the target;
- remain within 1,000 metres through acquisition and scan completion;
- after collection, follow the textual Origin Station distance cue, ascend to
  the orbital regime, and press Enter after `RENDEZVOUS` appears;
- exit cleanly to write the projected state when `--save PATH` was supplied.

Loading an in-flight profile restores the exact craft, objective target,
discovery list, and compact world-delta journal before terminal startup. A
restored collected objective is terminal: it cannot emit the collected delta
again. A returned profile is docked with no active flight state while retaining
the completed objective, discovery, and collected delta.

The rendezvous is an explicit planet-relative waypoint derived from the
independent Origin Station identity and the active first target. It is neither
the system barycenter nor a procedural parent of planet, terrain, or signal
generation.

## v0.4.3 acceptance matrix

Seed `42` produces station `station-ce51e866ec4e032d` and target
`signal-71d4c959dcd64423`. Its initial three-dimensional range is 87,889.861
metres. The canonical deterministic guidance replay launches at tick 0, reloads
an in-flight checkpoint at tick 600, enters atmosphere at tick 3725, enters
terrain flight at tick 15233, reaches the signal at tick 15294, completes
collection at tick 15713, and returns to the orbital rendezvous at tick 38890.
The checkpoint and reload both have flight checksum
`14947176626171235385`; the return flight checksum is
`11922358221174102146`.

The v0.4.3 matrix repeats the full launch, collection, return, and docking path
across airless, temperate, and dense atmosphere classes. Atmospheric descent
must enter terrain flight within 120 seconds, stored flight states must retain
at least 16 metres of terrain clearance, and each approach must produce a
nonzero atmospheric framebuffer checksum.

| Seed | Atmosphere class | Atmosphere tick | Terrain | Target | Complete | Return | Minimum clearance |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 42 | airless | 3725 | 15233 | 15294 | 15713 | 38890 | 1972.975030 m |
| 12648430 | temperate | 3736 | 15722 | 15983 | 16402 | 40571 | 1723.154200 m |
| 1 | dense | 4086 | 16013 | 16615 | 17034 | 34545 | 1974.678256 m |

A separate seed-`12648430` safety probe holds forward and fall controls for
120,000 fixed ticks (1,000 seconds). It must remain valid while crossing rising
terrain, maintain the exact 16 metre contact floor, and produce authoritative
flight checksum `18312514460843648054`.

### First-launch pacing

The pacing target is an atmospheric handoff within 35 seconds and target
arrival within 150 seconds, without moving the generated rendezvous or signal.
The deterministic probe measures 90% cruise acceleration and counter-thrust
braking through zero from the same launch state.

| Seed-42 measurement | v0.4.0 | v0.4.1–v0.4.3 |
| --- | ---: | ---: |
| First authoritative motion | tick 1 | tick 1 |
| 90% orbital acceleration | 4.50 s | 3.60 s |
| Counter-thrust braking | 4.50 s | 3.61 s |
| Atmospheric handoff | 101.40 s | 31.04 s |
| Target reached | 202.53 s | 127.45 s |
| Collection complete | 206.02 s | 130.94 s |
| Orbital return | 443.09 s | 324.08 s |

The tuned orbital envelope is 4,000 m/s horizontal, 2,000 m/s vertical, and
1,000 m/s² acceleration/braking. The replay peaks at 4,472.136 m/s total
speed. Textual thrust, coast, closing/opening, ETA, and `BRAKE NOW` cues are
available in both presentations; deterministic velocity streaks and the
thrust marker provide immediate pixel-space motion feedback without entering
simulation state.

Run the renderer-neutral matrix at both retained render profiles:

```bash
./build/apsis-drift --signal-run-acceptance \
  --profile local --report signal-run-local.json
./build/apsis-drift --signal-run-acceptance \
  --profile remote --report signal-run-remote.json
```

The mode writes and reloads a private same-directory checkpoint during the
canonical seed's first orbital leg, verifies the complete semantic document
and flight checksum, removes the private checkpoint, runs every atmosphere
class plus the long terrain-safety probe, returns each scenario to the station,
and emits a versioned report. The checkpoint also requires identical local-sun
geometry and framebuffer output after reload. Three fixed-seed solar
checkpoints at ticks 66800, 72000, and 77200 verify visible, planet-occluded,
and re-emerged states with identical direction semantics across render profiles.
Schema 5 reports `evidence_scope: application_framebuffer`; this mode does not
construct a Kitty or ANSI encoder.
`--snapshot PATH` retains the canonical final
pre-docking planetary frame for visual inspection. Rendering cadence, render
profile, and framebuffer checksum never enter authoritative simulation.
