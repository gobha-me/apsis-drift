# Apsis Drift

[![CI](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE.md)

A deterministic, procedurally generated spaceflight experiment rendered inside
a terminal.

Apsis Drift renders a voxel-space landscape into a TermForge `PixelSurface`,
defaulting to 640x480. Kitty-capable terminals receive the full pixel image;
truecolor ANSI terminals receive TermForge's half-block presentation. The game
refuses startup when neither supported presentation is available.

This repository began as a feasibility spike. The renderer is fast enough for
interactive use: the measured direct-Kitty path holds 30 FPS through the
cinematic profile, while the measured RDP path holds 30 FPS at the remote
profile. The dated results and path-specific recommendations are documented in
[the Flight Deck performance envelope](docs/PERFORMANCE_ENVELOPE_2026-08-15.md).
The longer-term direction is documented in [docs/CONCEPT.md](docs/CONCEPT.md).

## Build

Requirements:

- CMake 3.28 or newer
- A C++23 compiler (GCC 13+ or Clang 19+ recommended)
- Git when TermForge must be fetched

Apsis Drift first looks for a TermForge v0.32.0-or-newer package, then for a
compatible sibling checkout at `../termforge`. Older siblings are ignored. If
neither exists, CMake fetches the tagged TermForge v0.32.0 release, which
includes structured event sources and semantic press/repeat/release input
capabilities.

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

Named viewport profiles make the logical render resolution explicit:

| Profile | Viewport |
| --- | ---: |
| `remote` | 320x240 |
| `balanced` | 512x320 |
| `local` (default) | 640x480 |
| `cinematic` | 1024x768 |

The measured conservative defaults are `remote` at 30 FPS through RDP,
`local` at 30 FPS for direct Kitty, and `cinematic` at 30 FPS only as a
direct-path quality mode. The measured RDP playability ceiling is 320x240; the
same workstation's exploratory direct-path probe held a clean 30 FPS through
1280x960. These are starting points rather than universal limits; see the
[dated measurement report](docs/PERFORMANCE_ENVELOPE_2026-08-15.md).

Select a profile or provide a validated custom viewport. The explicit viewport
wins when both options are present:

```bash
./build/apsis-drift --profile cinematic
./build/apsis-drift --viewport 800x600
./build/apsis-drift --profile remote --viewport 640x360
```

Custom viewports are limited to 4096 pixels on either axis and 4,194,304 total
pixels so malformed or runaway dimensions fail before framebuffer allocation.

The cockpit requires an 80x24 terminal. It uses compact instrument rails at
that size and switches to wider bays at 120x32. Smaller terminals withdraw the
pixel viewport and show the required and current dimensions instead of drawing
overlapping or clipped regions.

Interactive controls:

- Startup/pause menus: Up/Down or Tab/Shift-Tab selects an action;
  Enter, Space, or left click activates it
- Arrow keys or W/S: forward/back
- Left/Right or A/D: turn
- Q/E: strafe
- R/F: change flight clearance
- Space: toggle autopilot
- Left-button hold in the exterior viewport: forward/back and turn
- Right-button hold in the exterior viewport: strafe and change flight clearance
- Middle-button click in the exterior viewport: toggle autopilot
- Escape during flight: pause
- Escape while paused: resume

The game opens on a title screen with explicit Start Flight and Exit actions.
Escape never exits active flight or the title screen. While paused, fixed-step
simulation does not advance, accumulated host time is discarded, and held
keyboard or mouse controls are reconciled before the first resumed tick. The
menu keeps textual focus markers and cell-native action labels on both Kitty
and supported truecolor ANSI paths. Ctrl-C remains the terminal interrupt path.

The title uses an original code-authored bitmap alphabet and palette with
integer scaling; it does not load an encoded font or image asset. Its exact
glyph coverage, origin, and license are recorded in
[docs/ASSET_PROVENANCE.md](docs/ASSET_PROVENANCE.md).

Mouse flight divides each viewport axis into thirds. The center third is a
dead zone; the outer thirds select the corresponding direction, and diagonal
holds combine both axes. Release and press again in another zone to change the
mouse direction. Keyboard and mouse holds are tracked independently,
so releasing one does not cancel the other. Opposing directions cancel to a
neutral axis. Button release, leaving the exterior viewport, resize, or input
loss neutralizes mouse-owned controls without clearing keyboard holds. A
terminal without suitable mouse reporting therefore remains fully playable by
keyboard.

Input is converted to tick-addressed simulation commands. Apsis Drift requires
semantic keyboard press, repeat, and release events, supplied by TermForge
through the enhanced terminal keyboard protocol or an explicitly configured
structured event source. High-frequency pointer changes are coalesced to
bounded per-tick intent. A mouse autopilot toggle is applied before manual
commands at the same tick, so simultaneous keyboard or held mouse input returns
to manual flight.
Any manual-control press disengages autopilot. Apsis Drift does not scan or open
raw input devices; terminal protocols, device permission, focus, layout, and
degradation policy remain outside the game.

The cockpit's navigation rail reports heading in normalized degrees, altitude
and requested terrain clearance in world units, and horizontal speed. The
flight rail reports manual or autopilot mode. Clearance at or below 24 units
raises a textual `LOW CLR` warning; invalid numeric telemetry is shown with
fixed-width dashes and a `TELEM ERR` warning so neither condition relies on
color alone.

Flight simulation advances at a fixed 120 Hz independently of rendering. Host
stalls contribute at most 125 ms of catch-up work per frame; excess elapsed
time is discarded instead of creating an unbounded simulation backlog.

Diagnostic capability forcing can exercise the supported paths and startup
refusals independently:

```bash
./build/apsis-drift --driver kitty
./build/apsis-drift --driver ansi
./build/apsis-drift --driver ansi --keyboard press-only
./build/apsis-drift --driver fallback
```

An explicit driver defaults to enhanced input; `--keyboard press-only` isolates
the missing-repeat/release refusal. The fallback choice exists only to verify
missing-truecolor refusal. Unsupported combinations exit nonzero before the
title menu or alternate-screen entry. After exit, the application prints the
selected display tier and effective input capabilities. Forcing skips the
startup probe and is diagnostic; it cannot make an unsupported terminal
implement a protocol.

## Measure and capture

Run the deterministic headless benchmark:

```bash
./build/apsis-drift --benchmark 180
./build/apsis-drift --benchmark 1 --snapshot landscape.ppm
./build/apsis-drift --benchmark 1 --profile cinematic
```

The benchmark measures CPU-side rendering and TermForge submission. It does not
measure terminal, PTY, proxy, or display performance. Its frame clock is
synthetic, so simulation follows the same fixed-step path without sleeping or
mixing wall-clock jitter into deterministic framebuffer checksums.

Run a repeatable resolution and target-cadence sweep and retain its JSON report:

```bash
./build/apsis-drift --sweep 180 --report landscape-sweep.json
./build/apsis-drift --sweep 180 \
  --sweep-viewports remote,640x360,local,cinematic \
  --sweep-fps 24,30,60 \
  --seed 12648430 \
  --report landscape-sweep.json
```

The default sweep covers the remote, balanced, and local profiles at 30 and 60
FPS. Each viewport is measured once at full headless throughput; target FPS
values calculate deadline headroom and required wire throughput without
sleeping or changing the simulated workload. The versioned JSON records the
explicit seed and frame count. Repeated runs can compare ordered profile
identities and checksums while treating timing values as machine-dependent.

For an end-to-end Kitty measurement on the real terminal path:

```bash
./build/apsis-drift --capture-seconds 60 --report landscape-capture.json
```

Capture mode forwards every frame to the terminal, so its achieved cadence
includes the PTY/proxy/terminal path. It bypasses the title menu and exits after
the requested duration.

## Measured Flight Deck envelopes

The 2026-08-15 live matrix measured every named profile for 60 seconds:

| Path | Remote | Balanced | Local | Cinematic |
| --- | ---: | ---: | ---: | ---: |
| Direct Kitty | 30.70 FPS | 30.72 FPS | 30.73 FPS | 30.72 FPS |
| Kitty over RDP | 30.29 FPS | 20.44 FPS | 21.24 FPS | 6.42 FPS |

The paired GCC and Clang headless sweeps kept cinematic complete-frame p95
below 10 ms, separating CPU-side renderer capacity from the much larger RDP
presentation tails. The full methodology, timing distributions, environment
details, limitations, and raw JSON are in the
[dated performance envelope](docs/PERFORMANCE_ENVELOPE_2026-08-15.md).

Headless benchmark runs temporarily use `/dev/null` for stdin. This works
around [TermForge issue #256](https://github.com/gobha-me/termforge/issues/256),
where `test_run_frames()` can block on a cooked terminal's stdin.

## Project boundaries

- Apsis Drift owns terrain and world generation, flight, simulation, cockpit,
  saves, and game-specific rendering.
- TermForge owns terminal protocols, structured event sources, input capability
  detection, degradation, presentation, and general frame instrumentation.
- RasterForge enters only when reusable raster asset decoding, fitting,
  resizing, or compositing becomes a demonstrated need.

The goal is to build a small game that reveals useful abstractions, not to
begin by inventing a generic 3D engine.

## Roadmap

Development is organized around playable milestone outcomes. Implementation
issues are deliberately sized for one focused session and include explicit
acceptance criteria, verification, and dependencies. Epic issues track the
milestones; they are not implementation sessions themselves.

- [v0.2 — Flight Deck](https://github.com/gobha-me/apsis-drift/issues/33)
- [v0.3 — Planetfall](https://github.com/gobha-me/apsis-drift/issues/34)
- [v0.4 — Signal Run](https://github.com/gobha-me/apsis-drift/issues/35)
- [v0.5 — First Light](https://github.com/gobha-me/apsis-drift/issues/36)

New work should use the session-sized issue form. If implementation uncovers
additional work, record it as a follow-up issue instead of silently expanding
the active session.

## License

Code is available under the [BSD 3-Clause License](LICENSE.md). Generated or
third-party media assets may carry their own provenance and license metadata.
