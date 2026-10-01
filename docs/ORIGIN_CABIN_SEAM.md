# First cabin seam qualification

Issue [#355](https://github.com/gobha-me/apsis-drift/issues/355) owns one
craft-local contact provider between the first two cabin walking tiles. It
builds on the [boarding support catalog](ORIGIN_BOARDING_SUPPORT.md) and
the existing C++ operating-motion recipe. The physical route remains
[#352](https://github.com/gobha-me/apsis-drift/issues/352); this provider adds
no actor action, save phase or ordinary New Game transition.

## Declared technical proxy

The engine recipe retains the ordinary standing reservation: 0.64 m square,
1.93 m high, beginning 45 mm above the foot datum. Two finite soles are
0.12 m wide and 0.28 m long, centered laterally at +/-0.14 m. Each boot
reservation extends 0.10 m above its sole. Qualification requires at least
75% real contact area per sole and a 10 mm projected-load margin.

These are fixed engine design assumptions for a pack-absent proxy. They do
not measure the pixel characters, certify equipment fit or provide articulated
limb reach. The existing station standing tolerances stay unchanged.

## Exact source surfaces

The full operating-02 contact remains selected by SHA256
`109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a`.
Exact object attribution comes from the admitted support catalog.

| Walking tile | Group-local flat-top triangles |
| --- | --- |
| `CABIN | lift-out walking tile 00-1` | `craft_fixed` 85129, 85130 |
| `CABIN | lift-out walking tile 01-1` | `craft_fixed` 85537, 85538 |

Their flat tops lie at craft Y=-0.10 m. The planar support gap between them
is 34 mm, from Z=3.6305 to 3.6645 m. Their whole-object bounds have a smaller
16 mm gap because the bevels extend beyond the planar tops. Neither distance
replaces the source triangles or fills the recessed pressure floor.

The declared crossing has foot center X=0, Y=-0.10 m and
Z=3.84 to 3.45 m, plus its reverse. Hardware remains fixed at
`OperatingProgress{1, 1, 1, 0}`. Motion comes from the existing C++ evaluator;
source-rest corrections are already baked and are not applied again.

## Proof boundary

Finite sole support uses the actual clipped top faces and support hull.
Whole-segment support requires a bound over the complete translation;
sampled poses are diagnostic evidence. Collision qualification uses complete
conservative swept body and boot reservations against the admitted craft
geometry. Boundary touch and interior penetration are separate results.
Shared bevel edges remain geometry rather than collision exceptions.

The four selected triangles are checked against their exact two rectangular
partitions during admission. Sole overlap is affine between the complete set
of sole-edge/rectangle-edge events. The certificate checks these events and
the endpoints in both directions. The minimum supported area is 123/140
(about 87.86%) of each sole, with a minimum projected-load margin of 106 mm.
These bounds exceed the declared 75% and 10 mm requirements.
Reported extrema use the provider's binary64 arithmetic and documented area
comparison precision. They are not claims of exact real-number computation.

For fixed axes and linear translation, the endpoint-union body and boot boxes
conservatively contain every intervening pose. Triangle/box tests use the
separating-axis theorem, classify boundary and interior separately, and skip
zero axes. A boundary result permits no penetration tolerance. The crossing
reports six original floor or bevel triangles per boot and no body contact.

Static geometry is cropped. The original extractor retains full evaluated
moving-group triangles, subject to its documented quantization treatment.
Completeness follows the independently bound source policy for each group;
the static crop cannot be imposed on every moving group.

This is craft-local evidence. It establishes no attached-station clearance,
mount, ladder reach, cockpit step, occupied seating or player transition.
The separate lower-floor evidence package is available for later admission;
it does not alter the original contact or this qualification.

## Provider and checks

`decode_origin_cabin_seam_contact` returns an immutable geometry handle after
bounded schema, coordinate, roster and triangle validation. CMake independently
pins the approved contact bytes and extraction policy; replacing geometry and
recalculating a receipt cannot admit a different source. The approved source
is represented by bounded literal chunks rather than a second JSON artifact.

`assess_origin_cabin_proxy_support` reports actual clipped polygons and their
source triangle keys for a finite fixed-proxy foot pose. The seam wrapper and
`certify_origin_cabin_seam_translation` restrict certification to the declared
centerline and range. `assess_origin_cabin_proxy_clearance` permits finite
endpoint controls while retaining the same proxy, hardware and coverage rules.

The contract includes insufficient real contact area, a missing floor,
recessed floor datum, one-millimetre penetration, neighboring hardware,
incomplete static coverage, malformed dimensions, non-finite coordinates,
oversized buffers and source substitution. A source-backed control has clear
endpoints at craft X=-0.7 m, Y=-0.10 m, Z=0 and 2 m, while the full translation
is obstructed by the actual lower latch. This checks that endpoint-only
collision tests cannot authorize movement through hardware.
