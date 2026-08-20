# Complete Intersystem Contract Acceptance

The v0.4.29 acceptance scenario composes the first-contract systems into one
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

1. accepts and launches the bounded mission, flies away from the moving Origin
   Station, returns under assist, docks, and relaunches;
2. begins the outbound spool from the live craft, saves, cancels back to the
   exact relative pose, respools, and completes the jump;
3. renders the generated star and all six moving planets at two ephemeris
   times, then flies the Assisted arrival to orbit-insertion readiness;
4. selects the bound objective entry, collects it through the existing
   Planetfall journal, and ascends back to orbit;
5. departs into target-system flight, commits the home jump, and approaches
   the explicit Origin Station waypoint;
6. docks and performs a separate, idempotent mission turn-in.

The same scenario also runs the entry-anywhere recovery branch. A ninety-degree
early entry descends through the atmosphere, reverses the descent, and reaches
orbit again with authoritative checksum `15160466842829483543`. That branch
proves the poor entry is recoverable without changing the canonical completion
trace or collected delta.

## Save/resume matrix

The uninterrupted trace records these format-14 checkpoints:

| Checkpoint | Tick |
| --- | ---: |
| Docked, mission offered | 0 |
| Origin-system free flight | 121 |
| Outbound spool with frozen craft | 534 |
| Free flight after canceled spool | 534 |
| Outbound transit committed | 894 |
| Target-system flight | 1,134 |
| Planet-side objective complete | 10,001 |
| Origin Station return | 31,006 |
| Returned and docked | 32,075 |

Each checkpoint is encoded, decoded, and continued in an independent replay.
Every resumed replay reaches final authoritative save checksum
`8587354319391325309`, mission phase `turned_in`, one discovery, and one
collected world delta. Rendering, timing, terminal capabilities, and cache
contents are excluded from that checksum.

## Run and capture

Run the application-owned simulation and framebuffer path once:

```bash
./build/apsis-drift --intersystem-contract-acceptance \
  --profile remote --report contract-application-framebuffer.json \
  --snapshot contract-application-framebuffer.ppm
```

Schema 2 reports `evidence_scope: application_framebuffer`; this mode does not
construct a TermForge encoder. Its `simulation_ms` and
`application_render_ms` values are
diagnostic and machine-dependent. `terminal_proxy` is explicitly marked as an
external live-capture measurement: use `--capture-seconds` in a real terminal
session to measure PTY, proxy, decoder, and display throughput, and do not mix
that result with renderer throughput. Use the headless benchmark or
system-navigation acceptance for actual Kitty/ANSI encoded-byte evidence. The
publication capture and its explicit
RDP limitation are recorded in
[`docs/performance/2026-08-18`](performance/2026-08-18/README.md).

Invalid dimensions, pixel budgets and buffers, non-finite flight state, wrong
stable identities, illegal transitions, and malformed saves are exercised by
the unit and CLI rejection tests before the optional visual snapshot.
