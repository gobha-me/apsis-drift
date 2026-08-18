# Complete Intersystem Contract Acceptance

The v0.4.13 acceptance scenario composes the first-contract systems into one
fixed seed-42 headless trace. It begins with a fresh save docked at the Origin
Station and ends docked with the mission turned in, one discovery, and exactly
one collected world delta.

The scenario uses the application-owned mission state, Assisted jump,
analytic local-system ephemerides, sub-light flight, orbit insertion,
Planetfall collection, planetary ascent, orbital departure, return jump, and
Origin Station rendezvous implementations. It does not introduce an alternate
mission state machine or terminal protocol.

## Ordered path

The canonical trace:

1. accepts and launches the bounded mission from the stable Origin Station;
2. commits the three-second outbound spool and completes the two-second jump;
3. renders the generated star and all six moving planets at two ephemeris
   times, then flies the Assisted arrival to orbit-insertion readiness;
4. selects the bound objective entry, collects it through the existing
   Planetfall journal, and ascends back to orbit;
5. departs into target-system flight, commits the home jump, and approaches
   the explicit Origin Station waypoint;
6. docks and performs a separate, idempotent mission turn-in.

The same scenario also runs the entry-anywhere recovery branch. A ninety-degree
early entry descends through the atmosphere, reverses the descent, and reaches
orbit again with authoritative checksum `7537708600294715479`. That branch
proves the poor entry is recoverable without changing the canonical completion
trace or collected delta.

## Save/resume matrix

The uninterrupted trace records these format-7 checkpoints:

| Checkpoint | Tick |
| --- | ---: |
| Docked, mission offered | 0 |
| Outbound transit committed | 360 |
| Target-system flight | 600 |
| Planet-side objective complete | 9,467 |
| Origin Station return | 30,472 |
| Returned and docked | 31,535 |

Each checkpoint is encoded, decoded, and continued in an independent replay.
Every resumed replay reaches final authoritative save checksum
`9496404445183332939`, mission phase `turned_in`, one discovery, and one
collected world delta. Rendering, timing, terminal capabilities, and cache
contents are excluded from that checksum.

## Run and capture

Run both information-complete semantic presentation paths:

```bash
./build/apsis-drift --intersystem-contract-acceptance \
  --driver kitty --profile remote --report contract-kitty.json \
  --snapshot contract-kitty.ppm
./build/apsis-drift --intersystem-contract-acceptance \
  --driver ansi --profile remote --report contract-ansi.json \
  --snapshot contract-ansi.ppm
```

The reports compare authoritative checkpoints and framebuffer checksums across
Kitty and ANSI. Their `simulation_ms` and `application_render_ms` values are
diagnostic and machine-dependent. `terminal_proxy` is explicitly marked as an
external live-capture measurement: use `--capture-seconds` in a real terminal
session to measure PTY, proxy, decoder, and display throughput, and do not mix
that result with renderer throughput. The publication capture and its explicit
RDP limitation are recorded in
[`docs/performance/2026-08-18`](performance/2026-08-18/README.md).

Invalid dimensions, pixel budgets and buffers, non-finite flight state, wrong
stable identities, illegal transitions, and malformed saves are exercised by
the unit and CLI rejection tests before the paired visual snapshots.
