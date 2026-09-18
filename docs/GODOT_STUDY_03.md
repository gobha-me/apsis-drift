# Study 03 — seated cabin, structural attachments and experimental relief

2026-09-18. A response to direct cockpit/ship review, not a production engine
selection or a claim of finished AAA art. All PNGs below are real 3840×2160
native viewport captures, with adjacent measurement JSONs.

Follow-up: [study 04](GODOT_STUDY_04.md) adds opt-in spherical streaming. Fixed
patch limitations below describe study 03 and remain applicable without it.

## Review and run

- [Pilot-facing starboard controls](media/freedom-cabin-right-v3.png)
- [Port controls](media/freedom-cabin-left-v3.png)
- [Closed footwell and deck](media/freedom-cabin-floor-v3.png)
- [Aft cabin closure](media/freedom-cabin-aft-v3.png)
- [Live C++ cockpit](media/freedom-cabin-flight-v3.png)
- [Closed landing bays and ventral lift hardware](media/freedom-ship-belly-v3.png)
- [Experimental terrain relief](media/freedom-terrain-relief-v3.png)

```sh
# Original terrain remains the default. New cabin geometry applies to both.
tools/run_godot_study.sh --live=true --pilot=true

# Explicitly opt into the separate C++ relief experiment.
tools/run_godot_study.sh --live=true --pilot=true --relief=true

# Seated asset inspection, including reproducible side/floor/rear views.
tools/run_godot_study.sh --view=3 --look=right
tools/run_godot_study.sh --relief=true --view=4
```

Restart an already-open study to load the rebuilt assets. RMB now turns the
head in cockpit views, with pitch limited to approximately −70°/+60° and yaw
to ±140°. The camera stays at the seated eye point; WASD cannot fly it through
the cabin floor. External views retain free inspection movement. `--look`
accepts `front`, `left`, `right`, `floor`, `aft`, and `underside` (ship view).

## Cabin and ship

Side panel assemblies rotate together: enclosure, legends, switch sockets,
levers and indicators. The vertical cards face inward; the console banks also
tilt upward. The builder asserts their orientation toward the pilot.

Canopy bows meet sill feet, outriggers and the forward coaming. The roof has
crossmembers and a centre spine; overhead equipment has suspension members.
The main instrument body has deck pedestals, the glare shield has supports,
the side sticks sit on console cantilevers, and the pedals have a deck-mounted
cassette and hinge shaft. Seat slides, handrail standoffs, cable trays, wiring
glands and lamp brackets replace disconnected-looking elements.

The floor has solid backing plus removable non-slip panels; aft pressure wall,
hatch, lockers, ventilation and roof closure remain visible when looking back.
This is visual structure, not a stress analysis, airtightness simulation, or
collision mesh. Interior/exterior packaging and a walkable hull still need a
shared module/attachment-coordinate design; these remain separate study assets.

The flight ship exports closed gear bay doors, not deployed landing shoes. The
deployed concept remains editable in an explicitly excluded archive collection
in the Blender master. Four ventral lift units now have nozzle throats, guide
vanes, heat shields, load spreaders and ties into the hull/wing structure.
They communicate the intended VTOL mechanism; thrust allocation, exhaust,
landing detection and animated gear deployment are **not implemented**.

Masters and four exchange tiers were rebuilt. Old Cycles review PNGs are
retained as revision-two history, not relabeled as current evidence. No
generated texture downloads were added; geometry/shaders and captures retain
BSD-3-Clause provenance.

## Why the terrain changed

The old native patch had 129×129 samples across 64 km: 500 m spacing, plain
vertex colors and broad version-one relief. The original terminal landscape
and the later spherical terrain generator are different systems; increasing
viewport resolution cannot restore absent landforms.

The new default material adds mineral variation, slope-dependent exposed rock,
strata, filtered normal detail and distance haze without moving vertices. It
uses the [Godot spatial-shader interface](https://docs.godotengine.org/en/4.7/tutorials/shaders/shader_reference/spatial_shader.html).

`--relief=true` additionally selects an explicit experiment recipe:

- C++ `relief.hpp`, recipe `experimental_relief_version: 1`; omitted/zero means
  the exact original path. Unknown versions are rejected.
- Independent derived terrain-detail seed, namespaced integer hashing, smooth
  sphere sampling, warped ridges and four geometric detail bands. It introduces
  no Godot-generated hills and consumes no mutable simulation random stream.
- The exporter and live flight sampler call the **same C++ relief function**.
  Godot displays those exported vertices and existing C++ flight poses.
- 513×513 samples over 32 km: 62.5 m spacing, 263,169 vertices and 524,288
  triangles. Heights remain metres, not renderer-only exaggeration.
- The 19,022,602-byte JSON is a disposable generated inspection cache, not a
  shipped planet asset. The procedural recipe is small and the cache can be
  regenerated. SHA-256:
  `bc0f872367386c722aa6e1500b99ef9c177cc83c3a42e70a5a528b38776413f6`.

This is **not** terrain-generator v2 or save-compatible terrain. Production
tiles, save schemas, existing seeds and baseline snapshots are unchanged. A
production adoption requires a deliberate generator/version compatibility
decision. The existing portable-math question also remains unresolved.

The mesh still approximates a continuous surface; 62.5 m triangles are not a
walking/collision solution. The flight's existing 16 m clearance clamp remains
an experiment safeguard, not landing physics. No streamed world, erosion model,
rocks/vegetation placement, ground contact, weather or HDR validation is implied.
This is a stronger landform study, not the final AAA surface.

## Verification

- GCC and Clang build the exporter, tests and live extension; snapshot contract
  tests pass on both. The complete detailed exports compare byte-for-byte.
- Regenerated baseline snapshot compares byte-for-byte with the pre-change
  snapshot. No golden checksums were updated.
- Both actual extensions pass 301 checkpoint comparisons plus 30/60/144 FPS
  replay schedules for baseline and relief. Final relief replay checksum is
  `13752674877022478875`; baseline remains `6185323095807381229`.
- Consumer validation passes 28 malformed fixtures, both head-look limits and
  non-finite mouse motion. C++ tests reject invalid relief coordinates/versions
  and check bounds, seed sensitivity, repetition and opt-out identity.
- Blender mounting-envelope checks cover 22 support families, a detached
  negative control, and 12 floor rays. These are geometry regressions, not a
  proof that every model element is structurally engineered.
- All 12 GLBs pass finite-value, buffer, index and LOD-budget checks. Native
  multi-angle captures verify the actual exported near meshes.
- Native synthetic input smoke verifies thrust, turn, pause, focus handling,
  cockpit switching and RMB head motion without leaving the ship-relative seat.
- The existing broader numerical-compatibility failures documented in
  [study 02](GODOT_STUDY_02.md) are not fixed by this work.

```sh
blender -b --factory-startup --python-exit-code 1 \
  --python tools/verify_cabin_structure.py
python tools/verify_hero_assets.py
ctest --test-dir build -R '^godot-snapshot-contract$' --output-on-failure
```
