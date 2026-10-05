# Finite boarding boot support

Registered before fitting for [#420](https://github.com/gobha-me/apsis-drift/issues/420).
This is a purpose-specific contact/load provider over the selected native
starting assembly, not all-body world clearance or a movement/seat/actor action.

## Source selection

Reuse the 18 admitted upper-floor faces and nine genuine paired partitions from
`OriginCabinCorridorGeometry`. The added original transition top is exactly
original/group0 triangles 62119 and 62120, object 124, source name
`CABIN | cockpit transition step`. An existing effective-contact lookup of the
admitted range [62015,62125) identified only these two upward horizontal faces;
no master, capture, asset or old material proof was replayed. Their plane is
Y=-.16 m and their quad corners are:

```
(+.498841, -.16, -.988)
(-.498841, -.16, -.988)
(-.500881, -.16, -.612)
(+.500881, -.16, -.612)
```

These decimal coordinates denote the exact existing stored binary64 source
values, checked against effective lookup at construction. Side/bevel/bottom
faces remain obstacles; the provider grants them no boot support. Original
keys never become evaluated face IDs or replacement keys.

## One exact stance placement

Evaluate body02/self04 unchanged in LOCAL coordinates. For the selected anchor
boot box center C, half-height h and eligible source plane Y, retain:

```
Ty = exact(Y) - exact(C.y) + exact(h.y)
placed_point = exact(local_point) + (0, Ty, 0)
```

Each exact term is the dyadic value of its stored finite binary64 scalar.
The actual anchor box bottom lies exactly on the source plane by construction.
Every part, joint, mass point and COM receives this same uniform translation. It
never changes dimensions, stretches a link, snaps a boot alone or expands the
floor. The separately rounded diagnostic sole origin may differ from the box
bottom; report that discrepancy rather than use it as contact permission.
A rendered/reporting translation can round once and supplies no authority.

An actually loaded second boot must satisfy its selected plane under the same
Ty. Exact sum signs distinguish contact, gap and below-plane placement; no
point-query 2 µm diagnostic tolerance enters this predicate. Future world
predicates must carry this same placement and cannot consume rounded centers.

## Finite load witnesses

Use actual source quadrilaterals partitioned by their two triangles and the
canonical flat boot frame, with nominal 0.12×0.28 m sole. A pressure disk must
lie in both the real eligible source partition and the sole. Shared diagonals
are internal; a convex hull across separate tile gaps grants no permission.
This bounded first cut may refuse a disk requiring a union of multiple
partitions, even if a later union provider could certify it.

The explicitly stronger initial profile is radius 20 mm with 10 mm disk-edge
margin, plus 10 mm projected-load margin. It does not revise the historical 75%
upper walking policy. Complete edge-distance bounds must prove containment;
unresolved boundary arithmetic refuses without tolerance or geometric growth.

A finite port load-fraction witness w lies in [0,1], with exact complementary
starboard fraction 1-w. Let foot centers be C0/C1 and the canonical projected COM
be M. Define B=w*C0+(1-w)*C1 and pressure centers Pi=Ci+(M-B) using the exact
scalar expression, rather than rounded points as authority. Then
w*P0+(1-w)*P1=M identically. Each positive fraction still needs genuine source
contact and its own finite disk/margins. Zero fraction supplies no load.
This is a bounded vertical-reaction witness, not a general force/IK solver.
The exact complementary weights also carry any identical contained disk at
both pressure centers to a disk at M inside the combined positive-foot support
hull. Its proven radius supplies the separate 10 mm projected-load margin;
unloaded feet contribute no support to that argument.

## Frozen first controls

Use the predeclared mirrored folded-arm ±10-degree body02/self04 checkpoints,
with the lifted opposite hip/knee 30 and port fractions 1/0 respectively. Also
use the neutral folded-arm two-foot stance with fraction .5. Set local root Z
first to −.35 (last upper tile), then −.8 (transition mid-tread); local root X 0
and the earlier registered hip Y construction stay unchanged. No dimensions,
angles or source geometry will be tuned after a refusal. Exact stance placement
provides the loaded plane registration; body/self records remain bit-identical.

Source contact/load, self result and precise refusals are separate. World,
sweep, route, actor and seat qualification remain false. First Flight and
#361/#352 remain open.

## Runtime interface

`make_origin_boarding_boot_support(NativeCraftBinding)` requires the selected
parked starting assembly and its immutable effective contact. The copyable
provider owns the contact handle and exposes the nine upper partitions followed
by the transition partition at index 9. Moved-from and unknown legacy bindings
refuse. Source-name views survive sharing and movement of a live handle.

`assess_origin_boarding_boot_support(provider, request)` accepts only a body02
pose, an eligible anchor side/partition and the port fraction. It evaluates
self04 once and retains all local records. The stance records its anchor IDs
and exact three-term translation. Each boot reports exact relations to all ten
source planes; a below-plane boot refuses when its actual finite footprint may
overlap that source, including an unloaded boot. A boot below an upper plane
without footprint overlap does not turn the lower transition into a collision.
`placement_nonpenetrating` covers only these eligible boot/top-plane checks.

Source-attributed clipped polygons and areas are display evidence, including
zero-area boundary observations and unloaded contacts. They grant no load.
Pressure admission uses complete outward bounds over the actual stored boot
axes and source edges, without assuming rounded sine/cosine columns are exactly
unit length. The sole-origin discrepancy retains its own exact three terms and
sign alongside the rounded report. A successful checkpoint combines this finite
load result with self04; all-body obstacle coverage and clearance still follow.

Build the contract alongside the core:

```sh
cmake --build build --target apsis-drift-boarding-boot-support-tests
ctest --test-dir build -R '^boarding-boot-support-contract$' --output-on-failure
```

## Validation

All six predeclared source-backed controls pass under GCC and Clang: the two
mirrored folded-arm single-foot stances and the neutral dual-foot stance, each
on the last upper tile and the transition tread. The local body/self snapshots
remain bit-identical. The contract performs 10,525 checks using independent
finite integer-dyadic pressure/plane oracles and complete clipping/cofactor
geometry, including real gaps, zero-area contact, source-margin boundaries,
tiny lifted-foot gaps, wrong anchors, nonfinite input and handle lifetime.
The full native suites pass 50/50 for both compilers.

The first build caught a mixed-type declaration, corrected without numerical
changes. The first pressure oracle exceeded its bounded 64-bit precision when
combining tiny COM coordinates with full-size boot coordinates; its replacement
accumulates the fixture dyadics exactly. Initial failures remain recorded. No
frozen posture, body dimension, source face or contact tolerance changed.
