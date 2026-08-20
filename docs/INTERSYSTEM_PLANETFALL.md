# Entry-anywhere Intersystem Planetfall

The v0.4.11 target-planet path composes the existing planetary flight,
procedural terrain, surface-signal scanner, navigation, collection, and sparse
world-delta systems into one application-owned state. It does not introduce a
generic engine or move simulation into TermForge.

## Entry and guidance

System-flight orbit insertion preserves the craft's arrival side, heading,
velocity, and authoritative tick. Once inserted, descent is legal from every
finite longitude: the target bearing and scanner cue are guidance only, never
an orbital alignment gate. The cockpit explicitly labels them `ALIGNMENT
GUIDANCE ONLY` and `ENTRY ANYWHERE` while the craft is orbital.

Each tick samples the real procedural terrain below the current geodetic
subpoint before advancing planetary flight. Flight, scanner navigation,
collection dwell, and journal mutation are staged on a copy and commit
together. Invalid planet identity, target identity, non-finite flight state,
terrain lookup, navigation, collection, or journal state rejects the entire
tick.

The mission target is the stable surface-signal ID bound by the accepted
contract. Tab input cannot retarget this path. Reaching the target uses the
existing acquisition and scan dwell; completion emits exactly one `collected`
delta and advances the contract at the same resulting universe tick.

## Persistence

Save format 15 retains the authoritative target-planet flight and the compact
world-delta journal. An active objective carries no delta. Objective-complete,
returned, and turned-in contracts require exactly one collected delta whose key
matches the immutable mission target and whose tick is not in the future.
Terrain tiles, scanner formatting, caches, camera state, and render state are
regenerated on load.

Hydration regenerates the target planet and signal catalog, reapplies the
journal, restores the scanner's fixed selection, and derives navigation from
the saved craft position. A terminal delta restores collection completion
without duplicating the delta.

## Acceptance

The deterministic acceptance trace covers descent from the target side, a
ninety-degree early entry with a save/resume checkpoint, and the antipode. It
also proves an atmospheric abort can climb back to orbit, completes collection
at the bound target, renders the final terrain frame, and replays the entire
trace twice before producing a report.

Run the application-owned simulation and framebuffer path:

```sh
./build/apsis-drift --intersystem-planetfall-acceptance \
  --profile remote --report intersystem-planetfall-application-framebuffer.json \
  --snapshot intersystem-planetfall-application-framebuffer.ppm
```

The schema 3 report identifies `evidence_scope: application_framebuffer` and
separates authoritative flight and abort checksums from the final framebuffer
checksum. The acceptance path is a semantic regression trace, not
a terminal/proxy throughput measurement; the benchmark and live capture paths
remain unchanged.
