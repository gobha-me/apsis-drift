# Landing gear articulation asset study

2026-09-19, study revision 4. Separate Blender geometry proposal, not installed flight geometry,
landing integration, an animation rig, or engineering certification.

## Review artifacts

Run from the repository root:

```sh
blender -b --factory-startup -t 8 --python tools/review_landing_gear.py -- --width 1600 --samples 24
```

Outputs remain ignored under `build-godot/gear-study/`. Each PNG has a JSON
sidecar; `provenance.json` records source identity, license, recipe references,
camera poses and limitations. PNG text/EXIF metadata is stripped without
re-encoding pixels, so Blender's private source path is not published.
Separate `.blend` files retain the three poses.
No canonical blend or GLB is overwritten. Rendering uses headless CPU Cycles,
not the running Godot game.

Add `--checkpoint` for the bounded three-image review: deployed ship plus
deployed/stowed cutaways. The manifest lists only the current invocation's
images. Treat any older files absent from it as superseded study drafts, not
revision-four evidence. The full invocation above also renders compression and
intact-cassette views.

- `gear-deployed-ship.png`: whole-ship underside and three deployed assemblies.
- `gear-stowed-ship.png`: same camera, closed doors and retained machinery.
- `gear-deployed-mechanism.png`: intact starboard cassette and extended leg.
- `gear-stowed-mechanism.png`: intact closed cassette.
- `gear-deployed-cutaway.png` / `gear-stowed-cutaway.png`: same starboard
  mechanism with the study bay cheeks, end walls, roof, doors and door hinges
  hidden deliberately. Exact hidden-object names are in the sidecars.
- `gear-compressed-mechanism.png` / `gear-compressed-cutaway.png`: illustrative
  300 mm vertical compression, not a computed suspension load response.

The source is the original BSD-3-Clause `assets/visual/hero-ship.blend`, with
construction helpers from `tools/build_hero_assets.py`. New geometry is original
code-authored work under the same project license. No downloaded models,
external image generation, or new propulsion assets were used.

## Geometry proposal

The source master contains an excluded, static deployed-gear concept and short
closed flight doors. The study hides those collections only in its copy and
adds load-spreader rails, attached clevis brackets, trunnion pins, oleo housings,
polished pistons, gland/wiper rings, replaceable shoes, captive bolts, deployment
actuators, sideways-folding pinned drag links and paired hinged bay doors.

The leg swings about its trunnion without shrinking for stowage; the pad remains
level at its axle. The deployment actuator has a fixed 800 mm housing with a
telescoping rod, not a housing resized between poses. Equal-length drag links
have explicit geometric endpoints, spherical bearings at the hull and piston
collar, and a pinned folding knee. The piston collar follows the rod and has a
solid lateral attachment lug. The compression pose
changes vertical pad clearance by 300 mm; inclined-piston travel is not falsely
identified with that vertical distance.

Nominal shoe contact centres remain those of immutable starter frame 1/version 1:

| Support | Body `(right, up, back)`, metres | Blender `(right, forward, up)`, metres |
| --- | --- | --- |
| Fore | `(0, -1.328, -6.700)` | `(0, 6.700, -1.328)` |
| Port aft | `(-3.500, -1.328, 2.700)` | `(-3.500, -2.700, -1.328)` |
| Starboard aft | `(3.500, -1.328, 2.700)` | `(3.500, -2.700, -1.328)` |

Shoe outlines are 750 × 1100 mm. Beveled edges and sole ribs are visual details,
not a claim that every point of the rectangular support envelope bears load.
Authoritative dimensions remain in `src/craft_frame.cpp` and
[CRAFT_FRAME.md](CRAFT_FRAME.md); the script refuses changed pad coordinates.

## Inspection findings and remaining risks

The original short closed-door assemblies did not establish storage volume for
a believable full-length mechanism. This proposal therefore uses explicitly
longer under-belly cassettes, rather than silently moving the physical pads or
scaling the gear away. This changes the visible underside and requires review
before canonical adoption. The closed-door lowest point is approximately
-0.548 m in Blender Z, within the descriptor's -0.600 m stowed lower bound.

Initial renders exposed excessive door occlusion; the open pose was changed to
166 degrees. A conservative packaging check also caught the fore linkage above
the proposed roof; its **mount**, not its deployed contact point, was lowered.
Independent review caught a variable-length actuator housing, a changing-axis
root joint without a spherical bearing, a missing rod-side lug and coplanar
sole faces. These were corrected; the shoe body was raised 24 mm so the sole
ribs are proud while their tips retain the authoritative contact plane.
The final static review also found the fore collar entering its housing at
full compression, and 12–19 mm of casing/rear-wall interference. The fore
collar now mounts 150 mm from the shoe pivot (aft: 250 mm), and both drag-link
endpoints sit 220 mm laterally from the leg axis. Construction guards check
the complete bearing sphere against the end-gland torus and root housing,
including a 20 mm review margin, rather than checking collar centres alone.
The three inspected poses retain at least 46.5 mm root-bearing/housing and
69.0 mm piston-bearing/end-gland clearance in these analytic checks.
A 340 × 320 mm trunnion
opening replaces the conflicting central part of each rear bulkhead, without
moving the cassette closer to the existing engine. These corrections address
the identified static contradictions, not every possible intersection.
The current script reports moving-part bounding boxes against the proposed
stowed bay. A successful box check is only a packaging diagnostic.

Still unqualified:

- True mesh intersections, swept clearances, actuator/link crossings and a
  continuous deployment timeline, including door-first sequencing.
- Source pressure-skin clearance near the fore cassette, actual internal hull
  structure, and clearance to the existing aft engines/lift housings.
- Structural loads, buckling, drag-link lock/downlock, hydraulic routing,
  asymmetric compression, durability and failure modes.
- Rig/export/LOD rules, cockpit gear indicators, gameplay state ownership,
  contact forces and controller action integration.

Do not infer any of those from a clean render. The next asset step is a pinned
hierarchy with continuous swept-volume inspection, then the approved geometry
can be exported into a separate native inspection before flight integration.

## Pilot asset task references

High-quality male/female pilots are a separate task; this study does not call
the existing synthetic fit mannequins finished characters. Preserve the shared
flight-cell contract in `experiments/godot-freedom/cockpit-layout.json`,
`tools/flight_cell.py`, `tools/build_flight_cell.py` and
`assets/visual/cockpit-fit-report.json`.

Existing synthetic seated fixtures cover 1.60, 1.78 and 1.96 m stature with
adjustable seat/pedal positions. The nominal cabin-local Blender eye is
`(0, -0.195, 1.2854)`; cabin-to-ship translation is `(0, 3.2, 0.35)`.
Grip anchors are `(-0.35, 0.12, 0.77)` and `(0.35, 0.12, 0.77)` in cabin-local
Blender coordinates. These are fit fixtures, not certified population coverage;
do not move the existing eye, seat, windows or controls to accommodate a model.
Use the existing body-axis conversion `(x, y, z) -> (x, z, -y)` for exports.
