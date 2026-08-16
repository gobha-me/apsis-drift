# Apsis Drift

> Project direction snapshot, 2026-08-14.

**Apsis Drift** is a deterministic, procedurally generated spaceflight game
rendered inside a terminal. Its defining view is a spacecraft cockpit framing
a real-time pixel viewport: orbital flight, atmospheric descent, and low-level
flight over generated planetary terrain.

The current landscape experiment has established the central feasibility
claim. TermForge can present a continuously changing 640x480 voxel-space scene
through Kitty, including through the development Guacamole/RDP path. The next
question is therefore not whether a terminal can show the game, but which
small game loop best exercises that capability.

The feasibility spike has been promoted as the `apsis-drift` project. This
document preserves the reasoning behind that decision and guides the first
playable vertical slice.

## Product direction

Build a game before building a generic 3D or game engine. Keep the terrain,
flight, procedural generation, and simulation application-owned. Reusable
facilities should move into TermForge or a future RasterForge only after the
game demonstrates a concrete, repeated need for them.

A useful first vertical slice is framed by the origin station while preserving
the orbit-to-surface flight loop:

1. Begin docked at the deterministic origin station with zero discoveries.
2. Accept the first Signal Run and launch into orbit inside the cockpit.
3. Select and descend toward the generated signal.
4. Fly over the generated terrain and locate the target.
5. Scan, collect, or otherwise interact with it.
6. Climb back to orbit, return to the station, and persist the result.

This is a complete loop without requiring a large authored universe, an asset
pipeline, or a general-purpose engine.

## Deterministic universe

Generated content should be reproducible from stable, hierarchical seeds.
Subsystems must use independent named streams rather than consuming one global
random sequence:

```text
universe seed
`-- system seed
    `-- planet seed
        |-- orbital-properties seed
        |-- terrain seed
        |-- biome seed
        |-- weather seed
        |-- settlement seed
        `-- encounter seed
```

Independent streams prevent an unrelated change, such as adding a random
cockpit light, from changing all subsequently generated planets. Prefer a
specified integer PRNG and stable derivation/hash algorithm. Standard-library
random distributions and unconstrained floating-point behavior should not be
treated as cross-version or cross-platform serialization formats.

The exact version 1 domain identifiers, integer encoding, derivation algorithm,
golden-vector contract, and save compatibility consequences are recorded in
[Seed Derivation Compatibility](SEED_DERIVATION.md).
The deterministic home-system child, zero-discovery docked start, bounded first
objective, return condition, and presentation boundary are recorded in the
[Origin Station and New-Game Contract](ORIGIN_STATION.md).
The stable objective identities, independent encounter streams, bounded
terrain-vetted placement, and immutable signal metadata are recorded in the
[Deterministic Surface Signal Contract](SURFACE_SIGNALS.md).
The versioned fields, units, named substreams, and diagnostic representation
of one generated planet are recorded in the
[Planet Descriptor Compatibility](PLANET_GENERATION.md) contract.
The system, planet-fixed, local tangent, cube-sphere tile, and altitude-driven
LOD conventions are recorded in the
[Coordinate and Terrain LOD Contract](COORDINATE_SYSTEM.md).
The application-owned orbital sphere, camera, lighting, validation, and
headless workload are recorded in the
[Orbital Planet Rendering](ORBITAL_RENDERING.md) contract.
The fixed sample layout, seam-safe integer generation, compatibility rules,
and bounded cache for local planet data are recorded in the
[Deterministic Terrain Tile Compatibility](TERRAIN_TILES.md) contract.
The planet-relative craft state, bounded regime controls, altitude hysteresis,
transition telemetry, and deterministic failure behavior are recorded in the
[Planetary Flight Regime Contract](FLIGHT_REGIMES.md).
The tile-backed orbital/local render seam, descriptor-derived atmosphere,
transition weights, and presentation instrumentation are recorded in the
[Planetary Presentation Handoff](PLANETARY_PRESENTATION.md).
The canonical seed, tick-addressed descent, stage identities, final checksum,
and compiler/profile measurements are recorded in the
[Planetfall Acceptance Path](PLANETFALL_ACCEPTANCE.md).

Generation should be divided by scale:

- Orbital scale: a procedural system model and planet sphere.
- Approach scale: atmosphere, horizon, broad terrain characteristics, and
  progressively selected regions.
- Flight scale: deterministic heightfield tiles, biome detail, landmarks, and
  encounters generated around the craft.

The same planet seed connects all three representations. Level-of-detail
changes affect presentation, not the identity or state of the planet.

## Save model

The generated world should not be serialized. A save contains the recipe plus
the player's changes to its result:

- Save-format and generator versions
- Universe seed and any active subsystem RNG counters/state
- Current system, planet, position, orientation, and velocity
- Ship configuration, fuel, cargo, and damage
- Discoveries, missions, and economy/progression state
- A sparse journal of mutable world deltas, such as collected objects,
  completed encounters, and modified locations

Early saves should plausibly remain in the kilobyte range. A generator version
must be retained with every save so a changed algorithm does not silently
rewrite an existing universe. Migration or legacy-generation support can be
chosen later.

Simulation should use a fixed timestep and deterministic state transitions.
Rendering resolution and frame rate may change without changing flight physics
or generated results.

## Cockpit and presentation

The cockpit is both the visual identity and a useful rendering boundary. The
terminal can have a full-screen cockpit while only the exterior viewport is a
high-frequency pixel surface.

The initial composition should use non-overlapping regions:

- A dynamic Kitty pixel viewport for exterior flight
- A truecolor ANSI half-block viewport when Kitty graphics are unavailable
- TermForge cell UI for the cockpit frame and most instruments
- Small independently updated gauges and displays
- Static or low-frequency decorative regions

This works with TermForge's current composition model and reduces transport
cost. Translucent pixel HUDs, overlapping image layers, and persistent static
pixel decorations should wait for a clean layering facility or be baked into
the dynamic viewport when necessary.

The supported game paths require truecolor presentation and semantic key press,
repeat, and release events. Unsupported presentation or press-only input paths
should refuse startup on the normal screen instead of maintaining a second,
less capable flight experience.

## Resolution and performance envelope

The current raw RGBA Kitty path costs approximately 5.33 transmitted bytes per
pixel after base64 encoding. Its useful first-order bandwidth model is:

```text
bandwidth = width * height * 5.33 * frames_per_second
```

Representative full-frame costs are:

| Dynamic image | FPS | Approximate bandwidth |
| --- | ---: | ---: |
| 320x240 | 60 | 23.4 MiB/s |
| 512x384 | 30 | 30.0 MiB/s |
| 640x360 | 30 | 35.2 MiB/s |
| 640x480 | 30 | 46.9 MiB/s |
| 640x480 | 60 | 93.8 MiB/s |
| 1024x768 | 60 | 240 MiB/s |
| 1280x720 | 60 | 281 MiB/s |
| 1920x1080 | 30 | 316 MiB/s |

The [2026-08-15 Flight Deck measurements](PERFORMANCE_ENVELOPE_2026-08-15.md)
confirmed that this model describes the application-to-PTY payload but not a
complete presentation budget. Direct Kitty sustained the 30 FPS target through
1024x768. The measured RDP path sustained 30 FPS only at 320x240; 512x320 and
640x480 reached approximately 20 and 21 FPS with p95 complete-frame tails above
100 ms, while 1024x768 reached approximately 6 FPS. The paired headless sweeps
kept complete-frame p95 below 10 ms at every profile, so the RDP limit is not
the renderer's CPU ceiling.

The cockpit therefore uses 320x240 as the conservative remote recommendation.
A 512x320 viewport remains a useful explicit quality choice, but its theoretical
25 MiB/s payload is not enough to predict stable 30 FPS through the measured
RDP presentation path.

The initial named resolution profiles are:

- `remote`: 320x240
- `balanced`: 512x320
- `local`: 640x480 and the compatibility default
- `cinematic`: 1024x768

An explicit viewport supports intermediate measurements such as 640x360 and
800x600 without adding a profile name for every useful point. Profile cadence
remains 30 FPS. A later `auto` mode can select a tier from recent presentation
time and missed deadlines, starting from the measured remote and local
recommendations rather than a universal maximum.

The headless sweep command measures each requested viewport once and evaluates
its renderer time, complete-frame work, and wire size against each requested
cadence budget. Its stable JSON identities and deterministic checksums support
run-to-run comparison; its timing values describe the machine that produced
the report and do not substitute for live terminal capture.

There is no single useful maximum resolution. The practical upper bound is the
combination of renderer time, encoding time, PTY/proxy throughput, terminal
decode/presentation time, and the desired cadence. The dated direct-Kitty and
RDP matrix records one reproducible pair of envelopes without treating either
machine as universal.

Potential future TermForge improvements that would move the envelope include
RGB24 and compressed image payloads, partial rectangular image updates, shared
memory on local terminals, persistent/named image layers, and observable frame
timing. These remain terminal infrastructure rather than game-engine features.

## Sound, music, and generated assets

Venice can contribute more than visual concept art. Useful generated material
includes:

- Cockpit concepts, panels, insignia, and palette studies
- Ambient music beds and exploration themes
- Engine, thruster, atmosphere, impact, warning, and UI sounds
- Radio fragments, navigational signals, and encounter stingers

Selected generation outputs should be baked into the project rather than made
a runtime dependency. Retain a small provenance manifest alongside them with
the prompt, model, seed when available, generation date, source file, edits,
and applicable usage/license information. This makes chosen assets reproducible
or replaceable without coupling gameplay to an external service.

Runtime playback is a separate concern. RtAudio or another small callback-based
backend can support a procedural mixer and baked assets later. Development in
a shared Kubernetes/Guacamole environment also needs an explicit no-audio
backend because the process may have no reachable audio device. Audio should
never control simulation timing or deterministic world generation.

## Project boundaries

For the initial game:

- **Apsis Drift owns:** simulation, flight model, camera, terrain and planet
  generation, cockpit behavior, missions, saves, and game-specific rendering.
- **TermForge owns:** event/input handling, structured event sources, terminal
  capabilities, Kitty and truecolor ANSI presentation, cell UI, transport, and
  general frame instrumentation.
- **RasterForge may later own:** reusable raster decoding, scaling, fitting,
  compositing, or asset processing once the game presents repeated use cases.
- **Audio backend owns:** device discovery and sample delivery; the game owns
  mixing policy, procedural sound parameters, and gameplay cues.

Do not begin by extracting a generic scene graph, 3D API, ECS, asset system, or
game engine. Extraction is justified when at least two real game systems need
the same abstraction and its boundary is visible from working code.

## Next discussion

For the first development discussion, decide:

1. The precise first flight loop and player objective.
2. Cockpit layout and viewport geometry for the remote profile.
3. Whether the first planet is a heightfield projection or begins the
   sphere-to-local level-of-detail transition.
4. The fixed simulation state and initial save schema.
5. The resolution/FPS sweep and adaptive-resolution controller.
6. The smallest useful audio experiment and whether to generate its first
   assets with Venice.

Those choices should produce a playable vertical slice before any library
extraction.
