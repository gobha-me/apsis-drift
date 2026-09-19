# Asset lighting/material review 14

2026-09-19. A bounded art-direction comparison using the existing native ship
and fitted cockpit, performed alongside flight/control work. This review does
**not** install a new gameplay lighting style, alter source GLBs or change
geometry, camera anchors, field of view, flight state, seeds or controls.

## Candidates and recommendation

Three profiles use the same paused authoritative C++ state, native Godot
renderer, FILMIC tonemapping, sun direction and camera poses:

| Profile | Intent |
| --- | --- |
| Baseline | Existing space lighting and exported material settings |
| Utility | More neutral cabin light, reduced mirror-like painted materials |
| Cinematic | Restrained warm practicals, cooler fill, matte paint with distinct exposed metal |

Prefer the **cinematic** candidate for further in-game qualification. It makes
the cabin trim read more like attached equipment instead of isolated glowing
screens. The exterior changes are deliberately subtle: no added greebles,
changed silhouette, excessive bloom, damage textures or copied assets. The
existing glazing is preserved; the review does not replace it with a screen.

Painted hull and dark fittings become less metallic and rougher; exposed steel
retains a distinct metallic response. Emissive instrument artwork and live
panels are unchanged. The warm roof light is anchored at the existing practical
strip midpoint. Additional very low-energy local light is an **art-review
bounce proxy**, not a newly modeled lamp or physically validated GI solution.
Before adoption, qualify it against actual source-fixture emission and shadow
behavior, and avoid turning it into unexplained light leaking through the hull.

## Camera/readability finding

The zero-angle pilot view uses the existing medium eye anchor and 75-degree
FOV. Its lower panel sections extend below the screen edge. Main values remain
visible, but all panel footnotes are not simultaneously visible. The additional
11.5-degree downward review view shows the whole panel group. These are two
explicitly different poses, not before/after camera fixes. No seat, eye, canopy
or instrument placement was moved to make the pictures look better.

The control-loop camera correction must be evaluated separately: toggling
interior/exterior and releasing head-look should restore the same agreed
neutral pose. Whether that neutral pose should eventually include a small
downward gaze is a human playtest/design decision, not an asset-lighting fix.

## Evidence and limits

`experiments/godot-freedom/asset_look_review.gd` produces nine native PNGs:
three profiles at straight-ahead cockpit, lowered-gaze cockpit and exterior
poses. It also produces three lossless side-by-side comparisons, always
**baseline left / cinematic right**, JSON sidecars and a review receipt.

The final local review completed at 3840×2160 per frame, with 7680×2160
comparison boards. Both authors and the independent visual reviewer inspected
the actual native outputs. The initial pass was 1920×1080 per frame; a second
pass refined local mounting-surface light and tightened exterior framing.
Script parsing and `git diff --check` passed. This is not a real-time
performance, HDR, physical controller or continuous-flight qualification.

Terrain geometry is deliberately hidden to isolate the asset. The analytic
planet/sky background remains, so its lavender silhouette in these pictures
is **not** evidence of terrain rendering quality. Instrument values are from a
paused 650 km survey relocation, not an achieved orbit.

The outputs remain in ignored `build-godot/asset-look-14/pass2/`; no large new
binary assets are added to the repository. Each output is an original native
render of project-owned assets, BSD-3-Clause, with local provenance metadata.
No external image generation, stock textures or third-party source assets were
used. No production asset manifest was modified by this review.

## Reproduce

Prepare the normal streamed snapshot, extension and assets. Create a new empty
output directory, then run the project Godot executable with:

```sh
--path experiments/godot-freedom --audio-driver Dummy \
--script res://asset_look_review.gd -- \
--snapshot="$(pwd)/build-godot/snapshot-42-stream-true.json" \
--assets="$(pwd)/assets/visual" \
--stream=true --flight-model=thrust --pilot=true --start-paused=true \
--render-size=3840x2160 --review-dir=/path/to/new-empty-directory
```

Run this independently; it does not stop another running game. Do not copy the
review-only profile into gameplay without checking daytime surface, night,
space, head-look extremes, dynamic display contrast and lower quality tiers.

## Follow-up: matched environment qualification

`asset_environment_review.gd` extends the isolated review with actual streamed
terrain visible. It compares baseline and candidate at identical poses and
identical environmental light/exposure within each pair: surface daylight at
4 km reference altitude, a night-light fixture at that same location, and a
250 km orbital-height survey. These are paused inspections, not flight proof.
The night fixture suppresses the primary light and attenuates sky/ambient to
test readability; it does **not** implement planetary rotation, solar occlusion
or a physical day/night atmospheric model.

The candidate now uses material response instead of global grading:

| Existing material / ownership | Metallic | Roughness |
| --- | --- | --- |
| Ivory coated hull | 0.05 | 0.68 |
| Painted surfaces | 0.10 | 0.60 |
| Exposed steel | 0.90 | 0.36 |
| Shared dark exterior material | 0.08 | 0.88 |
| Shared exterior panel/nozzle material | 0.65 | 0.48 |
| Dark cabin equipment/panels | 0.08 | 0.72 |

Base colors, markings, glazing, emission artwork, vertex data and all fit
dimensions are unchanged. Runtime GLBs batch components by material, so they
do not preserve every source part's semantic name. Consequently the shared
dark exterior group includes multiple purposes, not just thermal protection;
this is **not** a per-component thermal-material assignment. Future precise
machinery/heat-shield differentiation needs semantic export groups, not guesses
that every black mesh is a heat shield.

The completed final set is in ignored
`build-godot/asset-look-14/environments-final/`: twelve matched 3840×2160 PNGs,
eight additional neighboring camera-pose images, per-image provenance JSON and
`environment-review.json`. The surface preview is
`cinematic-surface-day-exterior.png` with its same-name JSON sidecar. Reproduce
using the command above with `--script res://asset_environment_review.gd` and
a new empty output directory.

Observed limits: surface/orbit coatings read less like chrome while exposed
metal remains distinct; instrument text stays readable in the lowered review
pose. Night exterior detail is mostly silhouette, and the terrain's existing
distance haze becomes conspicuously luminous and flat. A useful daytime
material pass must not be mistaken for finished night presentation. Eight
nearby camera poses show angle-dependent canopy highlights; they are not a
continuous-flight clip or comprehensive temporal/shimmer qualification.

A preliminary environment run was rejected when focus loss reopened its pause
menu midway through capture. The review helper now hides its own menu layer and
suppresses that inspection-only menu state while streaming settles. Production
focus/pause behavior is unchanged. The final full capture run completed
successfully without that contamination.

Independent review recommends the coated-hull and matte-cabin material changes
for integration consideration. Keep the four additional shadowed practical /
bounce lights experimental: their benefit is modest and their cost is not yet
measured. The night view also exposes blue light on an opaque exterior shoulder
and dorsal docking ring, apparently from the existing unshadowed cabin lights
(also present in baseline). Do not hide that potential light leak with extra
fill. The sampled camera poses showed coherent highlight travel without gross
popping, but a proper moving-camera temporal test remains necessary.

## Integrated subset: material response only

`asset_materials.gd` applies the reviewed coating response to ivory/paint,
retains a distinct exposed-steel response, and makes cabin black/panel surfaces
matte. The normal study now loads this subset by default. Use
`--asset-materials=baseline` for exact exported-material inspection; unknown
profile names are rejected. Review scripts explicitly start from baseline so
the new default does not contaminate their historical comparisons.

This integration deliberately excludes all four extra review lights and the
broad exterior black/nozzle overrides. No geometry, GLBs, textures, emission,
glazing, display artwork, camera anchors or simulation parameters change.
Only approved opaque, non-emissive, lit `StandardMaterial3D` surfaces receive
per-instance duplicates with new metallic/roughness values. Original shared
materials and mesh data remain untouched; applying the helper twice does not
allocate another override. Station assets and fit mannequins are not restyled.

`asset_materials_test.gd` covers rejected inputs, empty meshes, non-finite
response, protected transparency/emission/UI, exact baseline behavior,
shared-resource isolation and real near-tier ship/cockpit imports. The native
assets have seven changed surfaces out of 26 inspected; vertex/index buffers
remain byte-identical. No new lights, shader programs or render passes are
introduced, but performance and material-instance memory costs are unmeasured.

For an installed-default preview, run the environment review with
`--installed-materials=tuned` (or `baseline`) and a new empty output directory.
This path leaves actual gameplay light values intact and asserts seven or zero
overrides respectively plus the unchanged two cabin lights. It renders a
surface cockpit/exterior pair with per-image provenance and current C++ state.
