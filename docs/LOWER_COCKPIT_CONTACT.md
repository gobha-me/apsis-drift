# Lower cockpit contact geometry

Issue [#357](https://github.com/gobha-me/apsis-drift/issues/357) adds the
source triangles missing below the original static cockpit crop. This supplies
collision evidence for the [boarding route](https://github.com/gobha-me/apsis-drift/issues/352).
It does not yet let a player climb the steps or occupy the pilot seat.

The additive package preserves the delivered `wayfarer-floor-evidence-01`
source files, extraction tools and five inherited license documents. Its halo
contains 4,598 integer-micrometre vertices and 8,100 triangles attributed to
75 static craft source objects. The approved halo SHA256 is
`31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42`.
Original operating, motion and support packages remain unchanged.

The canonical package is `assets/native/lower-cockpit-contact-01`. Its runtime
halo preserves the delivered bytes; `contact-policy.json` separately binds the
source, original runtime dependencies and finite query coverage. CMake pins
both approved files independently. Package verification also checks the closed
source and license roster before an atomic preparation into a new directory.

For a separate verified copy, use a fresh output directory:

```sh
python3 tools/prepare_lower_cockpit_contact.py \
  --package assets/native/lower-cockpit-contact-01 \
  --output build-native/lower-cockpit-contact-01
```

Preparation refuses an existing destination. It neither loads a model into
Godot nor changes player state.

## Coordinates and coverage

The halo is already in corrected craft-rest coordinates, with Blender axes
mapped to `(x,z,-y)`. Its recorded source matrices are provenance, including
legitimate scale; applying those matrices again would move the contact away
from the visible source. Static halo geometry receives no moving-seat delta.
The original moving obstacles retain their C++-evaluated poses at the fixed
hardware selection used by the [first cabin seam](ORIGIN_CABIN_SEAM.md).

Static coverage is the union of two boxes, in metres:

| Source | X | Y | Z |
| --- | --- | --- | --- |
| Original static crop | [-1.05, 1.05] | [-0.20, 2.97] | [-3.50, 6.35] |
| Added lower crop | [-1.05, 1.05] | [-0.85, -0.20] | [-3.50, -0.50] |

A reservation crossing Y=-0.20 must fit the appropriate box on each side.
A lower reservation extending to Z=-0.40 is incomplete even if its upper
part is covered. Retained triangles extend beyond the query boxes because the
extractor preserves complete faces; their extrema do not expand coverage.
All original and added craft obstacles participate in contact queries.
Attached station geometry and mechanism motion sweeps remain separate work.

## Attribution and proof limits

Original group-local triangle IDs, evaluated source-object face IDs and new
halo-global triangle IDs have different meanings. Contact results preserve
their buffer identity and object attribution instead of appending faces into
the original groups. For example, intermediate-step source faces 182 and 183
are halo triangles 662 and 663. The previously admitted pressure-tub range
`craft_fixed[111783,111835)` is independent of its 18 added halo faces.

The contact diagnostic reuses the already declared body and boot reservations
at the actual intermediate step. It reports coverage, boundary contact and
interior penetration against source geometry. It grants no standing or stair
permission. The first-seam support-area threshold is not a global stair rule;
articulated transfers require their own qualification.

At foot center X=0, Y=-230000 micrometres, Z=-1.265 m, the combined geometry
covers the unchanged technical body and boots. Halo faces 662/663 report
boundary contact with the boots, while neighboring geometry obstructs the
body reservation. This is a valid contact result, not a qualified standing
pose. Lowering the foot by one representable value reports top-face interior
contact; lowering it by one millimetre remains a penetrating refusal.

The source checker was reproduced on a disposable copy because it writes its
check report. Earlier independent intake matched 21 ordered omitted faces
against source extraction; it did not independently replay all 8,100 faces.
Approved-byte checks preserve this reviewed evidence boundary rather than
claiming a broader source replay. Admission and query tests cover malformed
buffers, substituted identities, ownership, range/index boundaries, incomplete
coverage and real penetration. Existing walking, seam and save contracts
remain publication gates.

## C++ consumer

`decode_origin_lower_cockpit_contact` takes the halo and policy bytes plus an
already admitted immutable `OriginCabinSeamGeometry`. The resulting immutable
handle retains the original geometry and added source attribution. A moved-from
handle refuses queries.

`assess_lower_cockpit_reservations` evaluates finite endpoint-union body and
boot boxes against both geometry buffers at the declared fixed hardware pose.
Coverage and interior clearance are distinct results. Complete coverage is
required before interpreting a clear result; a boundary contact permits no
penetration tolerance. This conservative swept box can refuse a movement that
would need a different posture, rather than claiming articulated motion.

The shared triangle/box calculation projects relative to the box's lower
corner. Center-and-half-extent recentering rounded the exact intermediate-floor
touch into penetration; the lower-corner formulation preserves that contact
without an epsilon. Tests distinguish the exact plane, adjacent representable
heights and a one-millimetre intrusion, while retaining the original seam proof.

`lookup_lower_cockpit_face` preserves the buffer, object and original group
identity. Added faces also retain their evaluated source-face index; the
original catalog does not contain that index, so original-face queries report
it as absent. `assess_lower_cockpit_surface_point` reports a real face normal,
barycentric coordinates and signed plane distance. Its numerical point-on-face
comparison is diagnostic evidence and cannot waive an interior collision.
