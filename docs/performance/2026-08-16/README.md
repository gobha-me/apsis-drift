# Orbital renderer headless measurements — 2026-08-16

These measurements exercise the deterministic moving-camera orbital workload
for 60 frames per viewport. The command was:

```bash
./build/apsis-drift --sweep 60 --sweep-viewports remote,local \
  --sweep-fps 30,60 --seed 42 --workload orbital --report REPORT.json
```

The host ran Linux 6.12.74 on an Intel Core i9-13900H. Both builds were Release
configuration and used the same TermForge checkout. GCC was 14.2.0 and Clang
was 20.1.8.

| Compiler | Profile | Renderer avg/p95 | Complete frame avg/p95 | 30 FPS headroom | 60 FPS headroom |
| --- | --- | ---: | ---: | ---: | ---: |
| GCC | `remote` | 5.43 / 5.66 ms | 5.79 / 6.20 ms | 27.13 ms | 10.47 ms |
| GCC | `local` | 21.57 / 22.47 ms | 22.71 / 23.60 ms | 9.73 ms | -6.93 ms |
| Clang | `remote` | 4.48 / 4.53 ms | 4.80 / 4.87 ms | 28.46 ms | 11.80 ms |
| Clang | `local` | 17.86 / 17.99 ms | 18.91 / 19.07 ms | 14.27 ms | -2.40 ms |

Remote fits both evaluated renderer/call-path budgets on this host. Local fits
30 FPS but not 60 FPS. This is a headless CPU and application-to-driver result,
not a live terminal performance claim. The final framebuffer checksum is
`10688641563149901707` at remote and `2347094954246874526` at local under both
compilers.

Raw reports:

- [`orbital-headless-gcc.json`](orbital-headless-gcc.json)
- [`orbital-headless-clang.json`](orbital-headless-clang.json)

## Planetary presentation handoff

The tile-backed planetary workload cycles orbital, atmospheric,
terrain-blend, and local-terrain frames at one canonical surface location. It
ran 60 frames per viewport with the same host and compiler builds:

```bash
./build/apsis-drift --sweep 60 --sweep-viewports remote,local \
  --sweep-fps 30,60 --seed 42 --workload planetary \
  --report REPORT.json
```

| Compiler | Profile | Renderer avg/p95 | Complete frame avg/p95 | Orbital/local/composite pass avg | 30 FPS headroom |
| --- | --- | ---: | ---: | ---: | ---: |
| GCC | `remote` | 11.54 / 24.25 ms | 11.91 / 24.61 ms | 5.59 / 5.06 / 0.88 ms | 8.72 ms |
| GCC | `local` | 35.51 / 75.41 ms | 36.64 / 76.52 ms | 22.05 / 10.07 / 3.37 ms | -43.19 ms |
| Clang | `remote` | 12.01 / 25.22 ms | 12.36 / 25.56 ms | 5.72 / 5.47 / 0.81 ms | 7.77 ms |
| Clang | `local` | 36.92 / 79.43 ms | 37.96 / 80.49 ms | 22.73 / 10.94 / 3.24 ms | -47.16 ms |

Remote stays inside the 30 FPS renderer and complete-frame budgets. Local's
average modestly misses the budget, while terrain-blend frames intentionally
render both passes and produce a roughly 77–80 ms p95. The report preserves
that miss rather than confusing it with terminal/proxy throughput or weakening
the transition coverage; the integrated replay below isolates the follow-up in
issue #62. Both compilers touched at
most four unique tiles per frame and produced matching final framebuffer checksums:
`4926365054958479375` at remote and `2261776808789085952` at local.

Raw reports:

- [`planetary-headless-gcc.json`](planetary-headless-gcc.json)
- [`planetary-headless-clang.json`](planetary-headless-clang.json)

## Integrated Planetfall acceptance

The canonical seed-42 replay advances authoritative flight against generated
terrain for 119360 ticks, then renders each deterministic checkpoint 60 times.
The same Release host used GCC 14.2.0 and Clang 20.1.8. Values below are total
renderer p95 in milliseconds for the ordered orbital, atmospheric,
terrain-blend, and local-terrain checkpoints:

| Compiler | Profile | Orbital | Atmospheric | Terrain blend | Local terrain |
| --- | --- | ---: | ---: | ---: | ---: |
| GCC | `remote` | 6.96 | 6.88 | 26.93 | 16.06 |
| GCC | `local` | 20.36 | 32.33 | 85.48 | 25.61 |
| Clang | `remote` | 4.69 | 6.38 | 26.00 | 14.87 |
| Clang | `local` | 21.74 | 27.98 | 91.77 | 28.78 |

Remote stays inside the 33.33 ms 30 FPS application-renderer budget at every
stage. Local also fits except for the intentionally explicit mixed frame,
which renders tile-backed orbital and local passes plus the full composite.
That hotspot is tracked in
[issue #62](https://github.com/gobha-me/apsis-drift/issues/62); it does not
change the shared final flight checksum `15600629779145530762` or the ordered
stage identities. These headless timings do not include a terminal, PTY,
proxy, decoder, compositor, display, or network.

Raw reports:

- [`planetfall-acceptance-gcc-remote.json`](planetfall-acceptance-gcc-remote.json)
- [`planetfall-acceptance-gcc-local.json`](planetfall-acceptance-gcc-local.json)
- [`planetfall-acceptance-clang-remote.json`](planetfall-acceptance-clang-remote.json)
- [`planetfall-acceptance-clang-local.json`](planetfall-acceptance-clang-local.json)
