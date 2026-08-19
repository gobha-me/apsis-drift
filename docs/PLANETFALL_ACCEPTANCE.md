# Planetfall Acceptance Path

The v0.3 Planetfall acceptance scenario connects the versioned planet
descriptor, coordinate and LOD contract, deterministic terrain tiles,
planetary flight regimes, and planetary presentation renderer in one
application-owned replay. It does not use terminal input, wall-clock time, or
presentation cadence as simulation inputs.

## Canonical path

The scenario identifier is `v0.3-planetfall`. Planet seed `42` generates the
dense-atmosphere volcanic planet Carayx. The craft begins at latitude `0.625`
radians, longitude `-pi/4`, altitude `226000` metres, heading `0.35` radians,
and the deterministic LOD-12 terrain sample beneath that position. The fixed
120 Hz command schedule is:

| Tick | Command |
| ---: | --- |
| 0 | press forward |
| 0 | press right turn |
| 0 | press fall |
| 20379 | release fall |

Every simulation step samples the generated surface at the craft subpoint
before advancing `PlanetaryFlightState`. With the v0.4.2 atmosphere-normalized
envelope, the replay crosses the atmosphere at tick `4080`, enters the first
mixed terrain frame at tick `15555`, releases descent at tick `20379`, and ends
at tick `30089` in stable low-level forward flight. Its authoritative final
flight checksum is `15251675909814434464`.

The report retains one checkpoint for each presentation stage:

| Stage | Tick | Flight regime |
| --- | ---: | --- |
| `orbital` | 0 | orbital |
| `atmospheric` | 4080 | atmospheric |
| `terrain-blend` | 15555 | atmospheric |
| `local-terrain` | 30089 | terrain flight |

Each checkpoint records its geodetic state, clearance, flight checksum,
framebuffer checksum, terrain anchor, tile counts, and 60-frame timing sample.
Every presentation checkpoint derives its terminator, sky response, and local
terrain illumination from the same versioned planet/tick local-sun geometry.
State and generated identities are deterministic acceptance values. Pixel
hashes are deterministic visual diagnostics for the same implementation and
host math behavior. Timings describe only the application renderer and are
never compared as simulation state.

## Run and verify

Run either named profile from a Release build:

```bash
./build/apsis-drift --planetfall-acceptance \
  --profile remote --report planetfall-remote.json
./build/apsis-drift --planetfall-acceptance \
  --profile local --report planetfall-local.json \
  --snapshot planetfall-local.ppm
```

The mode fixes its seed and requires a report path. It rejects a seed override,
terminal driver or keyboard selection, and every other benchmark, capture, or
acceptance mode. A custom validated viewport may replace the named profile for
diagnostics.

The CTest acceptance matrix runs remote and local twice and compares the
scenario, planet, ordered stage ticks, flight checksums, framebuffer checksums,
and terrain anchors while excluding timing fields. The normal GCC and Clang
matrices also retain the existing invalid-dimension, non-finite-state,
framebuffer-boundary, capability-refusal, Flight Deck, and headless benchmark
coverage.

The v0.4.10 handoff regression matrix additionally renders the 2,500, 2,250,
2,000, 1,000, and near-ground clearance checkpoints with level and downward
cameras. It covers 320x240, 640x480, 1024x320, and 320x1024 viewports. A
deliberately range-bounded local pass that reaches no terrain must retain the
spherical framebuffer, proving that coverage rather than blend weight alone
controls when the planet may disappear.

## Recorded envelope

On the 2026-08-16 reference host, both GCC and Clang keep every remote 320 by
240 and local 640 by 480 stage below the 33.33 ms 30 FPS application-renderer
budget. These dated reports preserve the pre-v0.4.1 flight checksum and stage
ticks as historical performance evidence. The v0.3.1 optimization reduces the local terrain-blend checkpoint
from 85.48/91.77 ms p95 under GCC/Clang to 24.87/23.09 ms while retaining the
then-authoritative final flight checksum `15600629779145530762`. The complete
before/after compiler and profile measurements are retained with the
[dated performance evidence](performance/2026-08-16/README.md).

The v0.4.10 coverage-aware handoff was also compared directly with v0.4.9 on
the same 2026-08-18 host using GCC 14 Release builds and pinned TermForge
v0.42.0. Application-renderer p95 values were:

| Profile | Stage | v0.4.9 | v0.4.10 |
| --- | --- | ---: | ---: |
| `remote` | Terrain blend | 23.55 ms | 23.28 ms |
| `remote` | Local terrain | 11.46 ms | 12.06 ms |
| `local` | Terrain blend | 46.39 ms | 47.64 ms |
| `local` | Local terrain | 23.74 ms | 25.92 ms |

The local mixed stage already exceeded 33.33 ms on this host before the fix;
the coverage path preserves that explicit miss rather than allowing a
sky-only handoff. These measurements exclude terminal, proxy, decoder,
compositor, display, and network throughput.
