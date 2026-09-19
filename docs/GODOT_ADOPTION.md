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
