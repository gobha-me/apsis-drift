# One physical flight cell — fit review 05

Follow-up: [control clearance 07](COCKPIT_CONTROLS_07.md) lowers the FLIGHT/JUMP
pods to clear the displays, with the same eye, grip and exterior geometry.
The images below retain the original revision-five control placement.

2026-09-18. The current candidate uses **actual glazing** with a common interior
and exterior envelope. Camera-fed viewports remain an alternative, not something
secretly mixed into this model. This is a geometry/ergonomics study, not a finished
character, walkable ship or human-factors certification.

## Native 4K review

- [Assembled exterior](media/freedom-fit-exterior-v5.png) and
  [hull cutaway](media/freedom-fit-hull-cutaway-v5.png)
- [Side-on seated fit](media/freedom-fit-side-v5.png)
- Suited mannequins: [small](media/freedom-fit-small-v5.png),
  [medium](media/freedom-fit-medium-v5.png), [tall](media/freedom-fit-tall-v5.png)
- Pilot-eye views: [small](media/freedom-fit-eye-small-v5.png),
  [medium](media/freedom-fit-eye-medium-v5.png), [tall](media/freedom-fit-eye-tall-v5.png)
- [Whole shuttle](media/freedom-fit-shuttle-v5.png)
- [Cockpit with procedural terrain outside](media/freedom-fit-terrain-v5.png)
- [Capture and pose receipt](media/freedom-fit-v5.json)

These are actual 3840×2160 Godot captures. Cutaways deliberately remove pressure
skins, frames/glazing and the near-side secondary console to expose the fit.
Those are not holes in the normal asset. Instruments remain static artwork.
The additional terrain-backed frame uses the film rig's elevated stationary
survey, not a flight recording; [its capture receipt](media/freedom-fit-terrain-v5.json)
is separate from the ten-image fit review.

## Shared physical layout

Previously the pilot eye was roughly 1.3 m above the seat cushion, and exterior
window patches lay over an opaque hull. The two views were not physically
consistent. The new source of truth is
`experiments/godot-freedom/cockpit-layout.json`, consumed by Blender authoring
through `tools/flight_cell.py` and by Godot.

Cabin-to-ship translation is `(0, 3.2, 0.35)` in Blender +Y-forward/+Z-up metres.
There is no rotation or scale correction. Five shared glazing panels replace
the opaque flight-cell interval. The nose profile, forward RCS location and
affected markings were adjusted to match. Both native exterior and pilot views
assemble the same cockpit inside the ship, hiding its duplicate outer skin.
The camera uses that mount plus the shared eye anchor. The film rig also installs
the interior in exterior views; the previously delivered movie is unchanged.

After inspecting the first render, immediate switches moved from an obstructing
center card to positions beside the stick/throttle. This leaves the flight
display unobstructed. Graphics and lettering now scale with their smaller
display cases; shrinking only bezels had caused overlap. The dashboard remains
a closed equipment bay below the eye line. The primary controls are near the
armrests, secondary controls farther out, and service/isolation panels on aft
side cabinets require unstrapping and moving. Access walking is not implemented.

The old empty HUD combiner remains absent but preserved historically. A future
HUD or targeting display still needs an explicit eye-box/optical/UI design.

## Adjustable fit fixtures

These are synthetic design poses, **not population percentiles or coverage**.
Height alone does not cover torso/leg proportions, body shape, mobility, suit
bulk or preferred posture. More fixtures and playtesting are needed. Limb
segments are not stretched to reach controls.

| Fixture stature | Cushion top above floor | Eye above floor | Fore/aft relative to medium |
| --- | --- | --- | --- |
| 1.60 m | 0.471 m | 1.249 m | 45 mm forward |
| 1.78 m | 0.430 m | 1.285 m | reference |
| 1.96 m | 0.389 m | 1.321 m | 45 mm aft |

The seat has explicit carriage/height geometry and pose parameters. Pedals adjust
separately (about 50 mm total across these fixtures). These are tested positions,
not approved mechanical travel limits. The normal camera uses the medium pose;
there is no seat-adjustment UI or occupant animation yet. Mannequins are optional
inspection GLBs, excluded from normal cockpit tiers, and hidden in the master.

## Verification and limits

- Pure authoring checks reject invalid/non-finite stature and anchors, check
  primary reach with extension margin, and identify service controls as beyond
  restrained seated reach.
- Blender checks five common windows, 18 interior/exterior sightline rays across
  three poses, helmet/roof clearance, sampled leg clearance against hard
  equipment, floor/deck coverage and a mismatch negative control. This is not
  comprehensive mesh-collision, stress or ergonomics verification.
  Three extra forward-footwell rays check the standalone module's front liner;
  the exterior nose must not merely hide an open interior.
- The native review checks exported hero GLB window vertices agree within
  0.02 mm after mounting, rather than only comparing source data.
- All asset tiers and fixture GLBs pass finite-value, buffer/index and budget
  checks. Hero/near preserve the authored cell; distant reductions are visual
  approximations, not seated-interaction geometry.
- Godot rejects malformed eye/mount vectors and verifies the assembled medium
  eye at `(0, 1.6354, -3.005)` in ship-local Godot coordinates. Snapshot,
  bounded head-look and film tests pass, as do GCC/Clang snapshot/streaming tests.
  Existing broader numerical-portability failures remain open.

This does not implement walking/egress, hatch traversal, interactive instruments,
window damage, optical distortion or certified load paths. Forward sightline
checks do not prove every landing view or head pose. Whole-ship habitation and
access routing remain work. Earlier masters/exports were backed up; the original
film and all earlier review images remain historical evidence.

## Reproduce

```sh
python tools/flight_cell.py
blender -b --factory-startup --python-exit-code 1 --python tools/build_hero_assets.py -- cockpit --no-render --export
blender -b --factory-startup --python-exit-code 1 --python tools/build_hero_assets.py -- ship --no-render --export
blender -b --factory-startup --python-exit-code 1 --python tools/verify_cabin_structure.py
python tools/verify_hero_assets.py
```

Prepare the normal study snapshot and a new empty output directory. Run Godot
with `res://cockpit_fit_review.gd`, normal `--snapshot`/`--assets` paths,
`--render-size=3840x2160` and `--fit-output=/path/to/new-empty-directory`.
The review outputs ten PNGs and a pose receipt. The regular study launcher uses
the updated assembled cockpit for live inspection.

New code-authored geometry, fixtures and captures are BSD-3-Clause; see
`assets/provenance.json`. No new external assets were used.
