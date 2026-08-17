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

## Canonical acceptance

Seed `42` produces station `station-ce51e866ec4e032d` and target
`signal-71d4c959dcd64423`. Its initial three-dimensional range is 87,889.861
metres. The v0.4.1 deterministic guidance replay launches at tick 0, reloads
an in-flight checkpoint at tick 600, enters atmosphere at tick 3725, enters
terrain flight at tick 15233, reaches the signal at tick 15294, completes
collection at tick 15713, and returns to the orbital rendezvous at tick 38890.
The checkpoint and reload both have flight checksum
`14947176626171235385`; the return flight checksum is
`11922358221174102146`.

### First-launch pacing

The pacing target is an atmospheric handoff within 35 seconds and target
arrival within 150 seconds, without moving the generated rendezvous or signal.
The deterministic probe measures 90% cruise acceleration and counter-thrust
braking through zero from the same launch state.

| Seed-42 measurement | v0.4.0 | v0.4.1 |
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

Run either supported presentation matrix:

```bash
./build/apsis-drift --signal-run-acceptance \
  --driver kitty --profile local --report signal-run-kitty.json
./build/apsis-drift --signal-run-acceptance \
  --driver ansi --profile remote --report signal-run-ansi.json
```

The mode writes and reloads a private same-directory checkpoint during the
first orbital leg, verifies the complete semantic document and flight
checksum, removes the private checkpoint, returns to the station, and emits a
versioned report. `--snapshot PATH` retains
the final pre-docking planetary frame for visual inspection. Rendering cadence,
driver choice, and framebuffer checksum never enter authoritative simulation.
