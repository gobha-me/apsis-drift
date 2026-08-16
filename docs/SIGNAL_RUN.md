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
- the cockpit reports target bearing, distance, strength, and scan progress;
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
`signal-71d4c959dcd64423`. The deterministic guidance replay launches at tick
0, enters atmosphere at tick 12168, enters terrain flight at tick 23675,
reaches the signal at tick 24303, completes collection at tick 24722, and
returns to the orbital rendezvous at tick 53171. The in-flight checkpoint and
its atomic reload both have flight checksum `9296960089338770158`.

Run either supported presentation matrix:

```bash
./build/apsis-drift --signal-run-acceptance \
  --driver kitty --profile local --report signal-run-kitty.json
./build/apsis-drift --signal-run-acceptance \
  --driver ansi --profile remote --report signal-run-ansi.json
```

The mode writes and reloads a private same-directory checkpoint, verifies the
complete semantic document and flight checksum, removes the private checkpoint,
returns to the station, and emits a versioned report. `--snapshot PATH` retains
the final pre-docking planetary frame for visual inspection. Rendering cadence,
driver choice, and framebuffer checksum never enter authoritative simulation.
