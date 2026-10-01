# Native presentation decision — Godot

Accepted by the project owner, 2026-09-18, after the cockpit, live C++ flight,
asset and spherical-streaming studies. Godot is the selected native presentation
engine for Freedom. Do not start a competing SDL/OpenGL/Vulkan engine effort.

This chooses the development direction; it does not claim that every Freedom
feature, target platform, performance budget or production workflow is proved.

## Ownership

- **C++ application:** universe and terrain generation, stable identities,
  versioned seeds, authoritative existing flight/simulation, save compatibility,
  deterministic music/audio policy, headless tests and benchmarks.
- **Godot:** native 3D presentation, lighting/materials, asset scenes and visual
  animation, camera/UI, and native input translated into C++ commands. The
  bridge consumes the existing world; there must not be a second Godot universe.
- **TermForge:** existing terminal protocols, capabilities, input and degradation.
  Terminal presentation remains recoverably preserved and secondary; it does
  not gate native features.
- **New physical systems:** specify ownership and synchronization explicitly
  before integrating landing, walking, docking or an engine physics service.
  Engine adoption alone does not authorize replacing deterministic C++ state.

No production save/generator version changes follow from this decision. Existing
MIDI and procedural audio remain assets, not discarded prototypes. The preview
film uses an offline render of the current C++ music system; it does not by
itself demonstrate live Godot audio integration.

## Supported station walking ownership

The bounded Origin hub-to-D1 slice uses application-owned kinematic locomotion.
C++ holds the station-relative actor pose/velocity/heading, contact/support
checks, attached craft and shared 120-Hz tick. An immutable contact derivative
of the actual station geometry is compiled into the core. Conservative swept
standing-box/triangle tests and source floor probes decide accepted movement;
Godot does not run a competing CharacterBody3D gameplay controller or own an
actor clock. Native input requests C++ steps; the first-person camera consumes
same-tick projected actor/station/craft results.

Format20 explicitly composes actor state with the existing physical docking
save. Formats16–19 retain their contracts. Fresh New Game selects this supported
station state; historical station saves are not given an inferred actor pose.
This kinematic ordinary-interior slice establishes no AG acceleration/failure,
EVA, ladder, seat, dynamic-object collision or planet-surface walking model.
See [saved station walking](SAVED_STATION_WALK.md) for bounds and qualification.

## Build boundary

`apsis-drift::core` holds authoritative world, flight and save code used by the
native bridge and headless contracts. It does not include or link TermForge.
`apsis-drift::lib` adds the retained terminal presentation, input, audio and
legacy raster path. To build and test the native/headless targets without
finding or fetching TermForge:

```sh
cmake -S . -B build-core -DAPSIS_DRIFT_TERMINAL=OFF \
  -DAPSIS_DRIFT_RTAUDIO=OFF -DAPSIS_DRIFT_GODOT_SPIKE=ON
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The default build still includes the terminal application and its benchmarks.

## What remains to qualify

Long-duration circumnavigation and streaming under flight load, LOD transitions,
collision/landing/walking/reboarding, orbital handoff, station interactions,
save/load and revisits, persistent planet-dependent weather, controller/SteamOS
support, quality tiers, installation/cache budgets, and 4K/HDR performance.
The existing numerical portability finding remains open.

The scripted film is a reviewable rendering artifact. Smooth offline frame
capture is not evidence of real-time frame rate or an end-to-end playable game.

Evidence: [study 03](GODOT_STUDY_03.md), [study 04](GODOT_STUDY_04.md),
and the [preview production notes](FREEDOM_PREVIEW_01.md).
