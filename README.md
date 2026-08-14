# Apsis Drift

[![CI](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE.md)

A deterministic, procedurally generated spaceflight experiment rendered inside
a terminal.

Apsis Drift currently renders a 640x480 voxel-space landscape into a TermForge
`PixelSurface`. Kitty-capable terminals receive the full pixel image, while
TermForge selects progressively simpler ANSI and cell-based presentations when
the native image path is unavailable.

This repository began as a feasibility spike. The renderer is fast enough for
interactive use, including a measured 22 FPS through a Guacamole/RDP session;
the next milestone is a cockpit-framed planet flyover and a small playable
orbit-to-surface loop. The longer-term direction is documented in
[docs/CONCEPT.md](docs/CONCEPT.md).

## Build

Requirements:

- CMake 3.28 or newer
- A C++23 compiler (GCC 13+ or Clang 19+ recommended)
- Git when TermForge must be fetched

Apsis Drift first looks for an installed TermForge package, then for a sibling
checkout at `../termforge`. If neither exists, CMake fetches the exact TermForge
revision associated with v0.28.0.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To use a checkout in a different location:

```bash
cmake -S . -B build \
  -DAPSIS_DRIFT_TERMFORGE_SOURCE_DIR=/path/to/termforge
```

## Run

```bash
./build/apsis-drift
```

Interactive controls:

- Arrow keys or W/S: forward/back
- Left/Right or A/D: turn
- Q/E: strafe
- R/F: change flight clearance
- Space: toggle autopilot
- Escape: quit

If the native image area is blank, compare the explicitly selected paths:

```bash
./build/apsis-drift --driver kitty
./build/apsis-drift --driver ansi
./build/apsis-drift --driver fallback
```

After exit, the application prints the selected display tier and capabilities.
Driver forcing skips the startup probe and is diagnostic; it cannot make an
unsupported terminal implement a graphics protocol.

## Measure and capture

Run the deterministic headless benchmark:

```bash
./build/apsis-drift --benchmark 180
./build/apsis-drift --benchmark 1 --snapshot landscape.ppm
```

The benchmark measures CPU-side rendering and TermForge submission. It does not
measure terminal, PTY, proxy, or display performance.

For an end-to-end Kitty measurement on the real terminal path:

```bash
./build/apsis-drift --capture-seconds 60 --report landscape-capture.json
```

Capture mode forwards every frame to the terminal, so its achieved cadence
includes the PTY/proxy/terminal path.

## Feasibility checkpoint

On the 2026-08-14 shared development environment, 180 continuously changing
frames produced these Release results:

| Compiler | Renderer avg/p95 | Complete frame avg/p95 | Headless throughput |
| --- | ---: | ---: | ---: |
| GCC 14.2 | 2.866 / 3.148 ms | 4.104 / 4.508 ms | 381.24 MiB/s |
| Clang 20.1 | 2.163 / 2.373 ms | 3.127 / 3.459 ms | 500.50 MiB/s |

The raw Kitty path emits approximately 1,603.6 KiB per changing 640x480 frame.
The renderer has ample CPU-side headroom for 30 FPS; live capture remains the
meaningful measurement for a particular terminal and transport path.

Headless benchmark runs temporarily use `/dev/null` for stdin. This works
around [TermForge issue #256](https://github.com/gobha-me/termforge/issues/256),
where `test_run_frames()` can block on a cooked terminal's stdin.

## Project boundaries

- Apsis Drift owns terrain and world generation, flight, simulation, cockpit,
  saves, and game-specific rendering.
- TermForge owns terminal protocols, input, capability detection, degradation,
  presentation, and general frame instrumentation.
- RasterForge enters only when reusable raster asset decoding, fitting,
  resizing, or compositing becomes a demonstrated need.

The goal is to build a small game that reveals useful abstractions, not to
begin by inventing a generic 3D engine.

## License

Code is available under the [BSD 3-Clause License](LICENSE.md). Generated or
third-party media assets may carry their own provenance and license metadata.
