# Hero asset studies / revision 02

Historical render review. Cockpit/ship masters and exchange tiers now include
the [revision-three native review](GODOT_STUDY_03.md); the Cycles PNGs and contact
sheets described here remain revision-two evidence. Station geometry is unchanged.
The cockpit subsequently received the [revision-four dashboard correction](COCKPIT_LAYOUT_04.md).
Current cockpit and ship geometry use the [shared revision-five flight cell and suited fit fixtures](COCKPIT_FIT_05.md).

This pass raises the visual target from the initial primitive studies to
cinematic reference assets that can also supply future native presentation.
The deliverable is real editable geometry and Blender materials, with PNGs
rendered directly from that geometry. The previous Wayfinder files are retained
as revision 01. `hero-design-board.png` is the current review entry point.

## Delivery

All generated files live directly under `assets/visual/`:

- `hero-{cockpit,ship,station}.blend`: editable metre-scale masters, organized
  into named functional collections, with inspection cameras and lighting.
- `hero-{asset}-view-01.png`: 2560×1440 primary render.
- `hero-{asset}-view-02.png`: 2560×1440 instrument, propulsion, or habitat detail.
- `hero-{asset}-{hero,near,mid,far}.glb`: four actual geometry tiers.
- `hero-{asset}-geometry.json`: evaluated source geometry counts and bounds.
- `hero-{asset}-lods.json`: measured exported triangle counts and file sizes.
- `hero-{asset}-lod-{near,mid,far}.png`: 800×450 views rendered after importing
  the actual exchange GLBs back into the master's camera and lighting set.
- `hero-design-board.png`: six complete views in a labeled PNG contact sheet.
- `hero-lod-board.png`: the nine actual GLB round-trip views side by side.

Collections prefixed `STAGE` contain the inspection set, lighting, or the
procedural background planet. They are excluded from exchange assets and
geometry totals. The separate `LOD3` collection contains authored silhouette
geometry and is hidden in the master render. Blender uses +Y forward, +Z up,
and metres; glTF exports convert to glTF's +Y-up convention.

## What changed after render review

1. **Primary forms.** The courier now has a faceted pressure body, swept lifting
   surfaces, canted stabilizers, and layered armor. Open engine bells have
   separate petals, annuli, throats, and feed hardware. The station's habitat,
   core, drydock, freight racks, power systems, and thermal systems are distinct.
2. **Human scale.** The cockpit is approximately 4.06 m wide, with smaller
   guarded controls, captive screws, labeled toggles, three functional-looking
   MFD layouts, side sticks, service wiring, and a fitted acceleration couch.
   Display values are authored visual fixtures, not connected telemetry.
3. **Camera and sightlines.** Initial pilot framing cut off the instruments.
   The revised camera and narrower glare shield expose them; side consoles were
   moved outward after they obscured portions of the outer displays. The second
   cockpit camera is an instrument inspection view.
4. **Surface errors.** Coplanar armor produced visible interference on the
   first ship render. The armor now has a geometric offset from the substrate.
   Rectangular wing overlays were replaced with patches that follow the swept
   leading edge, removing the sawtooth silhouette.
   A later close inspection found that fixed chamfer sizes crossed at the
   narrow nose. Chamfers now scale with section thickness; seven thin-to-thick
   profiles and seven invalid/non-finite input cases were checked.
   The underlying pressure vessel was also inset from the armor to prevent
   its larger ruled faces from breaking through subdivided nose plates.
5. **Lighting and close-ups.** Station illumination was revised to show the
   habitat, trusses, and dock. A separate procedural planet supplies orbital
   context. Engine and habitat close-ups were inspected before final rendering.
6. **Orbital scale.** A small backdrop planet caused an oversized station shadow.
   The stage planet was moved to orbital distances while retaining the camera
   composition. A reversed/floating dock marking was moved onto a gantry face.
7. **Exchange materials.** GLB round-trip renders caught white fallback surfaces
   where procedural node links could not be translated. Export now explicitly
   substitutes the authored material factors in an export-only scene, leaving
   the saved master shader graphs intact.
8. **Silhouette continuity.** The first station mid tier incorrectly discarded
   the solar assembly. Solar wing structure, tanks, and the communications
   crown now survive the mid tier; individual PV cells, capillaries, retaining
   bands, and plumbing are independently assigned removable detail tiers.
   Mid-distance export also removes small bevels before mesh collapse, and
   removes the courier's small engine rings/pipes while retaining the docking
   collar. This spends the reduced budget on structure and reduces armor-sheet
   distortion from collapsing edge fillets.
9. **Far-proxy transforms.** Round-trip comparison exposed stale transforms on
   hidden proxy collections. The exporter now reveals and evaluates the entire
   collection before copying it, preserving engine, console, and solar-wing
   placement in the lowest tier.

## Reduction and native presentation

The hero tier preserves the source geometry. Near and mid tiers first discard
semantic detail collections, then combine and reduce geometry toward ceilings
of 150,000 and 35,000 triangles. Far is a separately authored silhouette with a
3,000-triangle ceiling. Exact counts, rather than estimates, are in the LOD
sidecars. Mesh reduction is a starting point for distance selection, not a
screen-space error guarantee; the appropriate switching distances depend on the
future camera and renderer.

The cockpit's far proxy is a shell/mask reference, not an information-complete
flight UI. The application must render authoritative telemetry and warnings
independently at every supported presentation tier.

Triangle reductions do not guarantee smaller files: combining shared source
meshes can expand instances and vertex splits. The measured byte sizes are
included so a future renderer can choose instancing, batching, compression,
and asset loading independently of the triangle budget.

The GLBs carry base material factors and emissive materials. Blender's
procedural color variation, roughness, and micro-surface bump remain in the
masters; those shader graphs do not transfer automatically to glTF. Matching
the film finish in a runtime renderer requires UV/texture baking and renderer
material support. Animated switches, telemetry textures, collision hulls,
rigging, and a verified internal/external cockpit packaging fit remain separate
production tasks. The masters are intended for art review, not a claim of
finished VFX production certification.

An SDL window can be a third presentation destination without constraining the
asset master. SDL is not itself a 3D shading backend: a native path still needs
the application's scene, camera, rasterization or graphics API, and material
handling. These files supply geometry for that decision. The existing terminal
paths can use baked shell images, reduced geometry, or the far silhouettes.

The repository and issue review found the persistent full-resolution cockpit,
one shared exterior camera, and independently updated panes in issues
[#54](https://github.com/gobha-me/apsis-drift/issues/54) and
[#237](https://github.com/gobha-me/apsis-drift/issues/237). It did not establish
an accepted Apsis SDL presentation implementation. The sibling termforge3d
README currently describes SDL for gamepad input with window/render subsystems
disabled. This asset pass does not change those renderer decisions.

`tools/package_hero_assets.py` assembles a standalone kit with the same relative
source paths, a subset provenance manifest, the repository license, convenient
top-level copies of both PNG boards, and SHA-256 checksums for every file.

## Reproduction and checks

From the repository root with Blender 5.2 and ImageMagick 7:

```sh
blender -b --factory-startup -t 8 --python tools/build_hero_assets.py -- cockpit --final --export
blender -b --factory-startup -t 8 --python tools/build_hero_assets.py -- ship --final --export
blender -b --factory-startup -t 8 --python tools/build_hero_assets.py -- station --final --export
blender -b --factory-startup -t 8 --python tools/render_hero_views.py -- cockpit 2
blender -b --factory-startup -t 8 --python tools/render_hero_views.py -- ship 2
blender -b --factory-startup -t 8 --python tools/render_hero_views.py -- station 2
python3 tools/verify_hero_assets.py
blender -b --factory-startup -t 8 --python tools/review_hero_lods.py -- cockpit
blender -b --factory-startup -t 8 --python tools/review_hero_lods.py -- ship
blender -b --factory-startup -t 8 --python tools/review_hero_lods.py -- station
bash tools/compose_hero_board.sh
./build/apsis-drift-asset-validator --root . assets/provenance.json
```

`--draft` gives a 1200-pixel render for iterative inspection. Geometry
construction has no random sampling. Cycles renders are not promised bitwise
identical across hardware or Blender versions.

Generation checks finite evaluated vertices and polygon index bounds. The
exchange verifier checks GLB headers, chunk sizes, buffer/accessor boundaries,
finite floating-point attributes, triangle indices, measured counts, decreasing
LOD complexity, and the tier budgets. These are asset checks; no C++ runtime
behavior or renderer benchmark is changed by this work.

All geometry and procedural surfaces are repository-authored; no external model
or texture pack was used. Labels use Blender's built-in font and the contact
sheet uses the installed DejaVu font. The original asset content is distributed
under the repository BSD-3-Clause license and recorded in the provenance
manifest.
