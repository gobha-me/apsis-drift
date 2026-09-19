# Godot study 01 — evidence and continuation

2026-09-18. **Continue evaluation; no adopt/reject decision yet.**
This is a bounded, read-only frontend experiment for #244, not a port.

Follow-up: [study 02](GODOT_STUDY_02.md) adds optional live C++ flight. The results
below remain the record of the original snapshot-only experiment.

## What actually ran

Godot 4.7.2 stable, native Linux Vulkan Forward+, 4x MSAA, real GPU rendering.
The official standalone download's SHA-512 matched the release checksum.
The binary is isolated under ignored build output, not installed system-wide.
No Venice service, external generated image or replacement audio was needed.

The existing C++ world supplied 16,641 vertices / 32,768 terrain triangles from
nine version-1 tiles across a 64 km patch. Actual sampled elevations range from
-1,408 to 2,403 metres. The origin is at 1,533 metres, latitude 0.25 and longitude
0.4 radians. The flight recording starts 60 metres above that surface.
This proves this generator is callable and renderable, not that the reported
terminal visual defect is fixed or that fine-scale surface detail exists.

Saved, inspected images:

- [Ship over C++ terrain, recorded flight](media/freedom-native-ship.png), 3840×2160.
- [Station inspection](media/freedom-native-station.png), 3840×2160.
- [Cockpit inspection](media/freedom-native-cockpit.png), 3840×2160.
- [True-scale terrain overview](media/freedom-native-terrain.png), 1920×1080.

These are direct viewport captures, not generated concept art. Adjacent JSON
files record actual image dimensions, renderer, load time, frame interval
percentiles, draw statistics and Godot memory estimates. They are **smoke-test
measurements**, not a game FPS claim: 120 frames after warmup, windowed, VSync
and compositor scheduling included, some runs overlapping CPU checks. No GPU
timer, sustained frame-tail/streaming test, process peak RSS/VRAM, HDR display
test, packaged runtime or target-device run was performed.

Visual inspection caught and corrected the bridge's reversed triangle winding,
sRGB vertex-color handling and a misleading oversized-window capture request.
The final fixed-size viewport checks actual PNG dimensions. Terrain still
lacks small-scale relief/material detail. GLBs use their existing fallback
materials: the detailed Blender shading has not become game-ready baked maps.
These are useful integration proofs, **not movie-quality finished assets**.

## Determinism and test results

- GCC and Clang build the opt-in exporter and pass its boundary, identity,
  coordinate, authoritative terrain and fixed-tick replay checks.
- The full default snapshot is byte-identical between those compilers:
  `cb20044b1ba1694860cfab8a491a0efe8e101c4b232f75201026023a7ab7cd9e`
  (SHA-256; interchange schema 1).
- Godot's headless consumer accepts that fixture and rejects 22 malformed
  fixtures, including non-finite vertices, dimension and buffer errors, numeric
  64-bit identity and broken replay tick ordering. No GPU render is implied by
  this headless check.
- Existing MIDI score tests pass under both compilers. No audio code changed.
- Broader CTest runs on both compilers: **91 passed, seven failed, one deliberately
  interrupted** (the long onboarding acceptance run). CTest lists the interrupted
  run as an eighth failure; this is not a fully passing regression suite.

The seven completed failures are `apsis-drift-tests`,
`intersystem-planetfall-acceptance-matrix`,
`intersystem-contract-acceptance-matrix`,
`origin-system-contract-acceptance-matrix`, `planetfall-acceptance-matrix`,
`signal-collection-acceptance-matrix` and `signal-run-acceptance-matrix`.
The main suite reports nine failed assertions, including golden planetary replay
and render-cadence checks. **Do not update goldens to make them pass blindly.**

The intersystem-planetfall checksum failure was reproduced with the Godot option
disabled. No existing simulation, renderer or test source changed in this study.
That rules out this optional frontend target as its cause, but does not diagnose
the underlying regression or prove every other failure has the same cause.
Local full logs and the disabled-option reproduction are retained under
`build-godot/`; they are not public artifacts because raw logs contain local paths.

Follow-up: the [numerical compatibility investigation](NUMERICAL_COMPATIBILITY_FINDING.md)
isolated `hypot` implementation differences and clarified the replay test's
diagnostics without changing its reference checksum. The no-test-source-change
statement above describes the initial study, before that follow-up.

## Size accounting, not a game-size promise

| Item | Actual bytes | Meaning |
| --- | ---: | --- |
| Godot standalone binary | 146,414,384 | Editor-capable binary, **not** a measured exported game runtime |
| Download archive | 77,860,424 | Development download; excluded from content |
| C++ snapshot JSON | 1,245,652 | Disposable debug interchange, regenerable from seed |
| Ship near GLB | 8,869,468 | Reused existing asset |
| Cockpit near GLB | 9,324,172 | Reused existing asset |
| Station near GLB | 10,513,440 | Reused existing asset |

Source masters, preview PNGs, this project's import/shader cache and C++ build
dependencies are development artifacts, not a proposed shipping install. A
release export and measured cache growth are still required before comparing
engine overhead or claiming a small distributable. Godot's video-memory monitor
does not include all driver allocations; each inspection scene currently also
keeps the terrain and ship allocated even when hidden.

## Next work, in order

1. Investigate the existing deterministic regression failures before treating
   simulation preservation as proven across the whole game.
2. Replace the snapshot boundary with a narrow live C++ adapter: fixed-tick
   commands, authoritative state, local origin shifts and bounded tile requests.
   Keep Godot presentation-only until an explicit ownership decision.
3. Exercise tile streaming/cancellation and moving origins, then real collision,
   suitable arbitrary landing, suited walking, reboarding and ascent. Imported
   geometry and a free camera do not count as those mechanics.
4. Carry MIDI synthesis/audio scheduling across that boundary; preserve the
   small authored score assets before deciding whether new audio is needed.
5. Measure packaged content/runtime/cache, long frame tails, Linux controls and
   SteamOS target behavior, SDR/HDR and 1080p/4K. Only then complete #244's
   adopt/adapt/reject decision and decompose the next implementation slice.

Run instructions: [native study](../experiments/godot-freedom/README.md).
