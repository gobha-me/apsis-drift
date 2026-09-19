# Seated pilot presentation studies

**Build 20 human review: placeholder only, not accepted character art.**
The pilots read as cartoonish; the female variant is not sufficiently distinct.
Passing cabin-fit and visibility tests does not meet the cinematic visual bar.
See [the revised art brief](PILOT_ART_DIRECTION.md) before further mesh work.

These are original procedural Blender meshes, not finished character art. The
male and female adult studies share one pressure-suit design and the existing
1.78 m synthetic cockpit fit fixture. Modest torso/hip proportions differ; this
is not an anthropometric population model or an ergonomic certification.

Generate the editable masters, GLBs and review PNGs:

```sh
blender -b --factory-startup --python tools/build_seated_pilots.py -- --samples 24 --size 1000
```

Outputs stay in ignored `build-godot/pilot-study/`; the script does not replace
canonical cockpit or ship assets. Each variant has a `.glb`, editable `.blend`,
front/side/eye-down PNGs and a provenance sidecar. The sidecars identify original
BSD-3-Clause authorship, source/dependency/layout hashes and artifact hashes.
PNG textual/EXIF metadata is removed without changing pixel/color chunks.

## Integration contract

Meshes are authored in metres, Blender +Y forward/+Z up, at the cabin datum.
GLB export converts to Godot +Y up/-Z forward. Mount the GLB at identity beneath
the existing cabin; do not apply the cabin-to-ship offset twice. The gameplay
eye, grip and lower-body joint anchors come from `tools/flight_cell.py` and
`experiments/godot-freedom/cockpit-layout.json`; no camera relocation or whole
body scaling is required. The helmet/neck is positioned behind the eye anchor
so the eye is toward the visor, rather than at the centre of the skull.

Exact exported grouping nodes:

- `PilotBody`: garment, hands, boots, collar, restraint and chest hardware.
- `PilotHead`: helmet shell, optical visor, communications pods and helmet lock.

Hide `PilotHead` and its descendants in first person; keep `PilotBody` visible.
Restore the head for exterior inspection. The editable source retains separate
parts; GLB geometry is evaluated and batched by material within each visibility
group, so every stitch/fastener does not become a separate mesh draw call.
These static material batches are **not** an animation skeleton or production
LOD solution.

The canonical near cockpit is one merged mesh: its original orange restraint
cannot be hidden by source-object name at runtime. Generate an optional
occupied-cabin companion instead:

```sh
blender -b --factory-startup --python tools/build_occupied_cabin.py
```

This loads `assets/visual/hero-cockpit.blend`, removes exactly the two named
`SeatMoving harness` source curves and their buckle in memory, and runs the
unchanged existing LOD exporter into `build-godot/pilot-study/`. Use
`hero-cockpit-occupied-near.glb` with the pilot; retain the original cockpit for
an empty seat. The master and canonical GLBs are not changed. A separate
`occupied-cabin-provenance.json` identifies the master hash, removed objects,
export recipe and generated artifacts. Source type/tier/name checks refuse a
changed master rather than deleting a broader set of parts.

The new pilot shoulder webbing starts at the shared seat-back hardpoints and
closes at a body-owned latch. Existing elbows deliberately sit above/outside the armrests;
the study does not claim elbow support. Boot soles use the existing pedal tread
datum, including its small authored contact/compression overlap.

## Review limits

Studio images include the authored couch plus review grip/pedal geometry, not
the full cabin. Pedal carriage/track are omitted from that context; this is not
a claim that unsupported pedal plates are acceptable ship construction. Review
the GLBs in the real cockpit before accepting sightlines, seat interaction or
head-look clipping. The eye-down studio view uses the fixed design eye but a
review lens, not a certification of the native camera's FOV/near plane.

The suit has shaped garment sections, articulated fingers, boot soles, pressure
neck layers, opaque curved visor, seams and hardware. It remains a stylized fit
study: no face beneath the visor, skinned animation, cloth simulation, material
baking, cinematic character finish, production LODs, collisions, or full range
of body types are supplied. No external art, scans or generated bitmap assets
are used. The useful next review is the actual seated pilot in the cockpit, not
a claim that character production is complete.
