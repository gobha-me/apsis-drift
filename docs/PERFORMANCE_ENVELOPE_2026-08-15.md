# Flight Deck performance envelope — 2026-08-15

This measurement pass establishes conservative viewport guidance for the v0.2
Flight Deck. It compares the same continuously changing deterministic workload
on a direct Kitty path and through a Kitty terminal inside an RDP desktop. The
results describe these two environments; they are not universal hardware or
network limits.

## Decisions

| Use | Profile | Cadence | Reason |
| --- | --- | ---: | --- |
| RDP/remote default | `remote` (320x240) | 30 FPS | Sustained 30.29 FPS with 8.12 ms p95 complete-frame work. |
| Direct local default | `local` (640x480) | 30 FPS | Sustained 30.73 FPS with 11.54 ms p95 complete-frame work. |
| Direct cinematic | `cinematic` (1024x768) | 30 FPS | Sustained 30.72 FPS with 24.88 ms p95 complete-frame work. |

`balanced` and `local` remain useful manual choices through RDP, but neither is
a conservative 30 FPS default on the measured path. They achieved 20.44 and
21.24 FPS respectively, with p95 complete-frame work above 100 ms.
`cinematic` is not suitable for this RDP path: it achieved 6.42 FPS.

Using stable 30 FPS presentation as the playability threshold, 320x240 is the
highest reliably playable resolution measured through RDP. The next named tier,
512x320, is already below that threshold. Direct Kitty did not reach its ceiling:
the highest tested resolution, 1024x768, still sustained 30 FPS.

## Live terminal results

Each row is one 60-second capture using seed `12648430`. Capture mode used the
ordinary TermForge terminal output path and the application's fixed 33 ms frame
interval. `Payload rate` is the application-to-PTY Kitty payload rate reported
by Apsis Drift; it is not the compressed RDP network bitrate.

| Path | Profile | Viewport | Achieved FPS | Renderer avg/p95 | Complete frame avg/p95 | KiB/frame | Payload rate |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Direct Kitty | `remote` | 320x240 | 30.70 | 2.10 / 2.62 ms | 3.65 / 4.46 ms | 401.8 | 12.05 MiB/s |
| Direct Kitty | `balanced` | 512x320 | 30.72 | 3.00 / 3.60 ms | 6.09 / 7.06 ms | 857.0 | 25.71 MiB/s |
| Direct Kitty | `local` | 640x480 | 30.73 | 3.95 / 4.75 ms | 10.41 / 11.54 ms | 1606.8 | 48.21 MiB/s |
| Direct Kitty | `cinematic` | 1024x768 | 30.72 | 6.38 / 7.38 ms | 23.37 / 24.88 ms | 4113.1 | 123.39 MiB/s |
| Kitty over RDP | `remote` | 320x240 | 30.29 | 2.82 / 4.15 ms | 5.91 / 8.12 ms | 401.8 | 11.88 MiB/s |
| Kitty over RDP | `balanced` | 512x320 | 20.44 | 6.78 / 12.57 ms | 37.55 / 114.41 ms | 857.0 | 17.11 MiB/s |
| Kitty over RDP | `local` | 640x480 | 21.24 | 6.25 / 11.35 ms | 46.64 / 102.56 ms | 1606.8 | 33.32 MiB/s |
| Kitty over RDP | `cinematic` | 1024x768 | 6.42 | 12.82 / 31.35 ms | 155.66 / 376.33 ms | 4113.2 | 25.80 MiB/s |

The RDP `local` result is slightly faster than `balanced` despite carrying more
payload. That non-monotonic result, together with the large p95 tails, is why
these observations should select a conservative tier rather than define a
simple bandwidth ceiling.

### Stage interpretation

- **Renderer:** `Renderer avg/p95` measures Apsis Drift's CPU-side landscape
  rendering directly. The headless baseline isolates this stage from terminal
  presentation.
- **Transport:** bytes per frame and payload rate measure uncompressed Kitty
  data written by the application to the PTY. The RDP proxy's compressed
  network bitrate was not instrumented, so these values are workload size, not
  a network-throughput measurement.
- **Decode:** Kitty protocol parsing and image decode were not independently
  instrumented. Their cost can only contribute to the live-path behavior and
  backpressure observed outside the renderer timing.
- **Display:** compositor, RDP client, and physical display latency were not
  independently instrumented. Achieved FPS and complete-frame work describe
  the aggregate application-visible path, not end-to-end photon latency.

Kitty 0.32.2 on the RDP host repeatedly reported that a pending-mode stop was
issued after the pending operation had already timed out. All four captures
still completed and produced reports. This is terminal/presentation-path
evidence, not renderer time, and is tracked in
[TermForge issue #269](https://github.com/gobha-me/termforge/issues/269).

## Headless baseline

The headless sweep rendered 180 frames per profile and evaluated 24, 30, and
60 FPS budgets. It does not include a terminal, PTY, RDP proxy, decoder, or
display. The matching checksums across compilers confirm that both sweeps ran
the same deterministic workload.

| Compiler | Profile | Throughput | Renderer p95 | Complete-frame p95 |
| --- | --- | ---: | ---: | ---: |
| GCC 16.2 | `remote` | 497.06 FPS | 1.78 ms | 2.14 ms |
| GCC 16.2 | `balanced` | 326.04 FPS | 2.68 ms | 3.32 ms |
| GCC 16.2 | `local` | 218.65 FPS | 3.83 ms | 4.95 ms |
| GCC 16.2 | `cinematic` | 117.13 FPS | 6.31 ms | 9.02 ms |
| Clang 22.1 | `remote` | 498.34 FPS | 2.13 ms | 2.59 ms |
| Clang 22.1 | `balanced` | 335.90 FPS | 2.75 ms | 3.34 ms |
| Clang 22.1 | `local` | 226.34 FPS | 3.89 ms | 4.94 ms |
| Clang 22.1 | `cinematic` | 125.82 FPS | 6.01 ms | 8.47 ms |

Even the headless cinematic profile stays below 10 ms p95 complete-frame work.
The much larger RDP tails therefore belong to the live presentation path, not
the CPU-side renderer ceiling.

## Environments and method

Direct Kitty used Apsis Drift commit `07bf95cb4b2f07bcd90f03d2f85cce19cd6a6daf`
on CachyOS Linux 7.1.8, an AMD Ryzen 7 5800X, Radeon RX 6900 XT, Kitty 0.48.2,
and a 120x40 terminal. The Release capture binary used GCC 16.2.1. The headless
baseline also used Clang 22.1.8.

The remote host used Debian Linux 6.12.74, Kitty 0.32.2, GCC 14.2.0, and a
118x45 terminal. It was displayed in a fullscreen 2560x1440 KRDC 26.04.3 RDP
session. The remote CPU model and compressed RDP network bitrate were not
recorded, so no conclusion depends on either.

The live command for each profile was:

```bash
./build/apsis-drift --capture-seconds 60 --profile PROFILE \
  --seed 12648430 --report REPORT.json
```

The paired baseline command was:

```bash
./build/apsis-drift --sweep 180 \
  --sweep-viewports remote,balanced,local,cinematic \
  --sweep-fps 24,30,60 --seed 12648430 --report REPORT.json
```

Raw reports and environment records are retained in
[`docs/performance/2026-08-15`](performance/2026-08-15/README.md).
