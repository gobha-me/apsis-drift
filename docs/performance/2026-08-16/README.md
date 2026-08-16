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
