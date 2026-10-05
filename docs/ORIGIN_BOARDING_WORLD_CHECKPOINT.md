# Supported boarding poses against the selected craft

Registration for #422, 2026-10-05, before implementation or world queries.

This registration follows issue #422 and the existing source-only plan.
Publication depends on #420 merge. No master, capture or previous proof replay
is part of this work.
The six #420 poses are frozen inputs; this plan makes no assertion about their
whole-body clearance.

## Public seam and result

Own the future `origin_boarding_world_checkpoint.hpp/.cpp`. Keep one query:

```cpp
assess_origin_boarding_world_checkpoint(
    const OriginBoardingBootSupport&, const BoardingBootSupportRequest&)
    -> std::expected<BoardingWorldCheckpointDiagnostic, std::string>;
```

Evaluate boot/self once and retain the complete `BoardingBootSupportDiagnostic`
as `boot_support`. Read exactly its 15 `local_self.canonical.parts`, not the
connected self04 regions. Copy neither geometry nor catalogs. All reservations
use the returned exact three-term uniform Y placement; its reporting value,
reporting COM and contact clipping polygons never become predicate inputs.

Proposed diagnostic fields:

* `boot_support` and 15 coverage records keyed by `BoardingBodyPartId`;
* `effective_triangle_count`, `expected_pairs`, `examined_pairs` and
  `certified_pairs`, all checked `uint64_t` counters;
* `coverage_complete`, `comparisons_complete`, `nonpenetrating` and
  `checkpoint_supported` separately;
* one optional `first_refusal` with part ID, reason, optional full
  `LowerCockpitTriangleKey`, object ID, owned source-object string, optional
  evaluated source face, axes examined and unsupported-axis count.

Reasons distinguish failed boot/self prerequisites, uncovered crop,
unsupported reservation arithmetic and absence of a separation certificate.
The last two mean unresolved; they do not claim a collision witness. Invalid
requests/moved providers remain API errors. A geometrically refused diagnostic
retains its honest prefix and prerequisite results. A passing checkpoint means
self/load, complete crop coverage and every effective triangle/part comparison
passed. Sweep, route, actor and seat qualification remain false.

Do not retain an unbounded certificate vector for every pair. Summary counts
and the first refusal suffice. Copy strings retained beyond visitation so a
diagnostic does not borrow a destroyed provider. No alternate contact class or
caller-editable diagnostic input is required.

## Root-owned private contact seam

The proposed `src/origin_lower_cockpit_contact_internal.hpp` is sufficient:

* `LowerCockpitEffectiveTriangle` borrows the existing immutable
  `CabinContactObstacle` and carries key/object/source-object/source-face;
* `visit_effective_lower_cockpit_contact` reports total, visited and complete,
  with callback true to continue and false to stop;
* `covers_lower_cockpit_bounds` validates a finite ordered bounding box and
  uses unchanged `union_covers` semantics.

Root factors historical reservation traversal through this same visitor.
Order stays original minus the complete 456-face mask, then halo, then
replacement. Legacy contact remains supported by historical queries; the
world query receives only the selected immutable boot provider. There is no
second geometry universe or query of original provenance as effective world.

World traversal is canonical part order, then the visitor's source order.
Observe the effective count without guessing it from catalog declarations;
check `15 * count` for overflow. Each attempted triangle pair increments
examined; a successful support inequality increments certified. An early stop
cannot set comparisons complete. Failed prerequisites/coverage perform no
triangle comparisons. The visitor's total must be available even when a
callback stops, so the report retains a meaningful denominator.

## Numerical certificate recipe

Reuse private `boarding_self_model02_support` on each actual box/capsule
converted to the corresponding `BoardingSelfSolid`. Its box support uses the
stored affine columns and half-sizes; its capsule support uses actual endpoints
and radius with an outward norm. Minimum support is negative maximum support
on the opposite axis. No ideal unit-frame assumption is introduced.

Add the translation projection as the exact expression
`a.y*t0 + a.y*t1 + a.y*t2`. Products and summation use outward intervals with
finite/domain refusal. Never multiply a rounded Ty. Triangle projections also
use outward bounds of all three original point dot products. A certificate is
`triangle_max <= reservation_min` or
`reservation_max <= triangle_min`. This excludes entry into the open solid
interior; equality may be contact. Interval overlap alone is unresolved.

Use one finite, source-independent axis recipe, without adaptive fitting:

* box: world XYZ (3), box-column face cofactors (3), triangle face normal (1),
  triangle edges crossed with box columns (9): at most 16 candidates;
* capsule: world XYZ (3), triangle face normal (1), capsule segment (1),
  segment crossed with triangle edges (3), endpoint-to-closest-triangle
  proposals (2), triangle-vertex-to-closest-segment proposals (3), and bounded
  segment/triangle-edge closest-pair proposals (3): at most 16 candidates.

Closest-feature calculations only propose stored axes. Their approximate
feature location, distance or region selection provides no clearance evidence.
Any resulting nonzero finite axis is acceptable only after its full support
inequality succeeds. Scale proposals by their maximum absolute component into
the existing support coefficient domain; the scaled stored vector itself is
the axis, without any claim of unit length. Skip zero/nonfinite/unsupported
proposals, retain an unsupported count, and refuse if none certifies. A
zero-length capsule keeps sphere semantics. No iteration or nearest-point
distance oracle is added.

Explicitly handle exact world-unit-Y contact for a flat stored box. Verify
its actual columns have the required Y projection before using
`center_y-half_y+t0+t1+t2-source_y`. The existing finite-term sign helper proves
this six-term difference, including zero, against every triangle vertex plane.
There is no floor penetration epsilon. Unsupported signs refuse. General
rounded dot/cross products must never be fed into the exact sum helper while
claiming their original products are exact. The explicit flat branch can
decide equality that conservative generic support intervals cannot.

## Closed crop coverage

Compute outward placed bounds using the same support and translation functions
on the three world axes. Pass only a certified containing AABB to Root's
unchanged closed-union predicate. Keep the original/lower endpoint constants
and lower-front notch unchanged. A bound spanning the Y join can conservatively
refuse even if a tighter shape-union test could succeed.

For an exactly proven endpoint contact, an outward interval may protrude by an
ULP. A finite exact sign can prove the actual extremum lies on/inside the fixed
endpoint; only then may that bound be tightened to the endpoint. This changes
the conservative enclosure after proof, not the body placement or crop.
Without that proof refuse. A rounded reporting box cannot grant coverage.

## Meaningful independent controls and limits

Keep the frozen six #420 requests unchanged and report actual source positives
or source-attributed refusals. Tests should separately cover canonical/self/Ty
identity, exact and adjacent sole contact, stored-column last-bit cases,
box/capsule triangle intrusion, unsupported/zero axes, true separation,
early-prefix counters, removed-original/halo/replacement namespaces, crop
endpoints/join/notch and shared/moved lifetime. A blocking triangle must not
clear merely because a broad bound or another triangle clears. Root retains
historical corridor/reservation regressions through its visitor refactor.

Implementation introduces only the private traversal/coverage seam and this
static predicate. No body dimensions/angles/source transforms are adjusted.
No source master/capture/previous numerical job is consulted. No static result
grants a sweep, gait, seated contact, actor mutation, saved phase, occupied
hardware motion or First Flight completion. GCC/Clang, pinned format20/tidy20
and complete native regression tests follow implementation, not this plan.

## Frozen inputs and limits

The six requests are exactly the #420 neutral and mirrored folded-arm stances
at rootZ -0.35 on upper partition 8 and rootZ -0.8 on transition partition 9.
All body dimensions, joint angles, fractions, source partitions, transforms and
strict obstacle policies remain unchanged. No refusal authorizes fitting.
The result certifies static source-triangle surface clearance under the existing
strict policy; it does not classify source solid-volume containment or grant
material, sweep, route, actor or seat authority. Existing source/material
acceptance remains closed.

## Separate lateral obstruction controls

Registered 2026-10-05 before querying these controls. The six initial requests
remain unchanged. To exercise source-attributed refusal independently, also
observe the neutral folded-arm dual upper request at rootZ -0.35 and rootX
+0.30, -0.30, +0.45 and -0.45 metres. Its anchor remains port/partition8 and
fraction .5. Separately observe the mirrored original single-foot upper requests
with rootX +0.58 for the port-loaded request and -0.58 for the starboard-loaded
request. All other pose, stance and source inputs remain unchanged.

These are a fixed set of lateral observations, not a search or a declaration
that each must collide. The existing floor partitions are wider than the feet;
a side translation can preserve pressure support while bringing the larger
trunk, helmet or limbs near an actual obstacle. Report the actual prerequisite,
coverage or source-surface outcome without tuning these offsets, body sizes,
joint angles, source geometry or certificate axes. A source comparison refusal
continues to mean no separation certificate, without claiming collision proof.
