# Fixed source endpoint vertical load01

Registered 2026-10-05 for [#441](https://github.com/gobha-me/apsis-drift/issues/441),
before implementation or any load evaluation. The source-only method and compact
record layout received independent review. No load outcome is assigned here.
Refines the preserved fixed-endpoint plan and independent source review.

## Fixed scope and owned prerequisites

Deliver one static normalized vertical-reaction certificate for the unchanged
SourceEndpoint01/Self01 body. Public input is only the immutable
OriginBoardingBootSupport provider. Own Self01 once, including its one unchanged
endpoint, Sites02, source handle, all105 pair results and14 original regions.
Require complete supported bindings/coverage, self qualification, original15
mass model and plane/link/joint/body identities before new load arithmetic.
No alternate body, rootY/Z, pose, source, mass, force fraction, normal, pressure
choice, expression tree or supplied accepted flag enters the public assessment.

Child fields remain unchanged. In particular Sites02's +.040Z/zero pressure
offsets remain geometric-only records. Load pressures are newly derived records
and never replace those offsets. Failed child assessment starts zero new quad,
pressure or edge checks, retains the child diagnostic and records prerequisite
refusal. No retry, body reassembly, independent endpoint/self query, search,
old BodyPose conversion or pressure fitting.

## Shared reaction-point algebra

Use the SAME actual full15 weighted COM M and actual boot centers C0/C1 in XZ.
Define exact-real B=(C0+C1)/2, delta=M-B and Pi=Ci+delta. Each upward normalized
reaction is Ri=(0,1/2,0); downward gravity is (0,-1,0) through actual M. The
two stored .5 fractions are exact halves of one unit, both strictly positive.
This is a proxy static gravity/reaction model, not an installed actor force,
articulated torque/strength, acceleration or friction certificate.

Authenticate the original mass IDs, midpoint rules, integer weights, upright
frames and shared X identities: central780 at rootX; two30 arm groups at
rootX +/- .20265; two180 leg groups at rootX +/- .14. Exact cancellation yields
M.X=B.X=stored(.16); delta.X=0. Pressure X retains .16 +/- .14 as the SAME
expression, never independent .02/.30 or cancellation of widened bounds.
The COM-X domain is this fixed compiled body only; no claim for tilted frames,
asymmetric masses, different root expressions or caller-selected bodies.

B.Z is (stored(-.5)+stored(-.8))/2, not stored(-.65). Outward delta.Z and Pi.Z
bounds enclose the owned full15-mass COM expression, not a rounded midpoint or
new compact mass model. Both pressures use that ONE M/B/delta definition; an
interval is a bound, not an independently selectable pressure realization.

Pressure Y is each actual source/sole plane. Sum Ri+(0,-1,0)=0. Moment about M
is (-sum(Pi.Z-M.Z)/2,0,sum(Pi.X-M.X)/2)=0 by the shared XZ identity. Unequal
pressure heights multiply zero horizontal reaction components and cancel from
this moment. Do not assert that the full3D mean of pressure points equals COM.
Identity flags attest this fixed algebra, never a rounded near-zero residual.

## Real horizontal upward source and finite contact

Use the unchanged provider's10 quads and actual source triangle pairs/keys.
Bind upper plane to stored -100000.0*1e-6 and transition to stored
-160000.0*1e-6; all perimeter/triangle coordinates must be finiteabs<=8.
All four perimeter Y values and all six triangle-corner Y values must equal
the declared stored plane exactly. Reject tilted/noncoplanar support rather
than dropping Y and admitting its projection.

For ordered perimeter q0..q3, require each finite edge's squared length lower
bound>0, and strictly positive clockwise signed side for BOTH non-edge vertices
on every edge. These eight signs prove a nondegenerate strictly convex upward
ordered XZ quad; uncertain signs deny validity. With horizontal coordinates,
clockwise XZ winding means geometric +Y, not a shading-normal assertion.

Actual source guard additionally verifies each retained triangle has three
distinct corners from that exact perimeter, upward geometric winding, and the
two triangles cover the quad on one opposite-corner diagonal: incidence counts
1,1,2,2 and the shared vertices are opposite in the perimeter. Each triangle's
clockwise signed side must be strictly positive with supported bounds. Keep
the original two source keys and objects; never infer upward support only from
unit_normal_current or four equal Y values. No provider mutation/recapture.

Both identity soles retain half(.06,.05,.14) and the child's SAME centers.
Canonical Y plane+.05-rootY, ONE common rootY and halfY .05 give each actual
sole plane exactly. Check the original sole frames/center terms and complete
ten-top footprint/below-higher-plane child evidence. No boot-only snap, floor
epsilon or convex joining of source gaps. Validate real horizontal/upward/convex
quads and all arithmetic before using the edge-distance recipe.

For each loaded Pi, require all four sole edges and all four edges of at least
one same-plane source quad to certify signedSide>0 and

    lower(side^2 - (exact(.020)+exact(.010))^2 * edgeLength^2) >= 0.

Require supported finite bounds, positive edge length and exact disk-radius
plus edge-margin sum. Preserve definite failure versus uncertain margin. Scan
the fixed source roster in order for both feet; first contained matching quad
selects evidence, but coverage/arithmetic checks continue through the allowed
prefix. Missing support or partial coverage never supplies a load certificate.
All10 quads must pass the real source guard, including those not selected.

Two radius20mm admissible disks with exact half weights imply a radius20mm disk
about M in projected force-resultant support by the Minkowski-combination
identity. Therefore certify required projected load margin10mm without a
rounded hull. Store required disk radius .020, disk-edge margin .010 and required
projected margin .010 separately; no combined40mm requirement or floor/path
continuity claim. Report .020 as the certified projected resultant-disk radius
only after both actual finite disk proofs pass.

## Frozen public API and bounded records

New header: `include/apsis_drift/origin_boarding_source_endpoint_load.hpp`.
Additive end section: `src/origin_boarding_boot_support.cpp`; old kernels and
evaluation order/results remain unchanged.

    kBoardingSourceEndpointLoadVersion = 1
    assess_origin_boarding_source_endpoint_load(provider)
      -> expected<BoardingSourceEndpointLoadDiagnostic,string>

Diagnostic owns unchanged `BoardingSourceEndpointSelfDiagnostic self` plus
`BoardingSourceEndpointLoadPayload load`. New payload only, excluding child,
must be<=4096 bytes; static assertions precede first evaluation. Freeze records:

- QuadMathEvidence: ONE minimum-of-four edge-squared bound and ONE
  minimum-of-eight signed-side bound; checked_edges/checked_side_signs counters (four perimeter edges/eight perimeter
  convexity signs; triangle winding is separately mandatory)
  and supported/horizontal/upward/convex/nondegenerate/valid flags. All eight
  signs are actually checked, but their individual bounds are not retained.
  Ten source guard records.
- CandidateSummary: enum unexamined/noncoplanar/contained/margin_refused/
  invalid_support/numerical_unresolved; supported flag and
  min-side/min-squared-gap bounds.
  Ten summaries per pressure. A compact status never substitutes for real edges.
- PressureEvidence: plane, pressure Vec lower/upper bounds, four sole edges,
  four selected-source edges, optional source partition index, original two
  source keys, prefix count and supported/coplanar/sole/source/complete flags.
- Payload: B.Z/delta.Z bounds; fixed reaction fractions; two PressureEvidence;
  ten QuadMathEvidence; counters for checked quads, pressure candidates and
  edges; symmetry/positive-reaction/barycenter/force/moment identity flags;
  finite placement/contact, projected-margin/load/complete flags; first refusal.

The selected source indices/keys are bound into the owned provider; no copied
geometry/catalog/name ownership or dynamically sized output is needed. Missing
selected evidence stays optional. Detailed scratch source edges are retained
only for the selected quad, while every checked candidate keeps its summary.
Conditions: self_prerequisite, invalid_binding, unsupported_arithmetic,
invalid_support_quad, pressure_capacity, sole_disk_margin, source_disk_margin,
no_matching_support, numerical_unresolved. First refusal includes optional
site/source indices; child failures preserve their separate genuine first error.
Algebra identities may survive a disk failure; force/load qualification cannot.

Concrete native binary64 record accounting from the current definitions:
RigidVector3 is three doubles (24 bytes); LowerCockpitTriangleKey contains its
byte buffer enum and two uint32 fields (12 bytes); FootSiteScalarBounds has
two doubles plus supported flag (24 bytes); FootSiteEdgeEvidence has three
such bounds and a flag (80 bytes). The proposed QuadMathEvidence therefore
has two scalar bounds, two size_t counters and six flags:72 bytes. A candidate
summary with two scalar bounds, enum and supported/quad-valid flags is56 bytes.
PressureEvidence has plane8 +pressure-bounds48 +sole-edges320 +source-edges320
+optional-index16 +two-keys24 +ten-summaries560 +prefix8 +six-flags/padding8:
1312 bytes. Two pressures2624 +ten-quad-guards720 =3344 bytes.

Freeze Payload's remaining fields to version/padding8, B.Z/delta.Z48, reaction
fractions16, disk-radius/edge-margin terms16, required-load-margin8,
certified-resultant-radius8, three counters24, twelve flags/padding16 and one
optional refusal48. These add192: proposed total3536 bytes, leaving560 under
4096. This is source-derived native layout accounting, not a compiler-size
observation. Assert ACTUAL sizeof each record and payload cap before running;
an unusual ABI must not silently enlarge the registered output. Do not store
eight side records per source, twenty detailed edge arrays, duplicated source
names/geometry or a second child.

Fixed new scratch<=2048 bytes excludes independently owned child/output. Its
explicit conservative sum is: fifteen XZ Points (two centers, COM/B/delta,
two pressures, four sole and four source corners)480; one four-edge array320;
two single edge records160 covering local return/copy; quad guard72 +candidate
summary56; twenty-four Interval temporaries384; sixteen size_t slots128 covering
triangle incidence/loops/counters; ten runtime arithmetic-probe doubles80;
one by-value numeric-control perimeter of four Vec96; multiply's four-product
array32. Total1808 leaves240 bytes for remaining explicitly declared flags and
scalar temporaries. Process sites/quads sequentially. Write four edges into the
supplied scratch/output array by calling existing site_edge individually; do
not create a second four-edge return array. Assert actual explicit scratch
structure PLUS nested helper allowance<=2048. No old256-term Expansion,
uncontrolled sqrt, generic math engine or hidden geometry table.

Reuse the supported Sites02 Point/Interval squared-side kernel, guarding finite
state before flattening. Require IEEEbinary64 RN, fegetround nearest, actual
unit tie/three-quarter-ULP addition probes and preserved subnormal input/output.
No new predicate relies on old unguarded linear_sign or old edge_distance sqrt.

## Frozen private seams and prefix semantics

    detail::boarding_source_endpoint_load_bounded(provider,
      max_source_partitions=10,max_body_records=1,
      max_self_pairs=105,max_self_axes=14,max_pressure_partitions=10)
      -> expected<BoardingSourceEndpointLoadDiagnostic,string>

Only reduce budgets. Delegate the first four unchanged to Self01 exactly once.
Pressure cap0..10 bounds the new source-quad prefix, followed by both pressure
candidate prefixes within it. Each visited site/source pair increments the
pressure-candidate count, including wrong-plane or invalid quads. Global edge
checks count only actual disk-margin site_edge predicates; quad geometry has
its separate four-edge/eight-sign counters. Skip disk predicates for wrong-plane
or invalid quads, while retaining every candidate in the permitted prefix. Cap0 yields no new quad/pressure/edge attempts;
caps<10 deny new coverage/complete/load even if a contained prefix is found.
No rootY/Z, body, fraction, force, pressure offset, tilt or geometry controls.

    detail::boarding_source_endpoint_load_pressure_math(provider,site,
      source_partition,array<BoardingPlantedLegScalarBounds,2> pressure_xz)
      -> expected<BoardingSourceEndpointLoadPressureMathEvidence,string>
    detail::boarding_source_endpoint_load_quad_math(array<RigidVector3,4>,plane)
      -> expected<BoardingSourceEndpointLoadQuadMathEvidence,string>

These are arithmetic-only tests. Pressure seam selects fixed site0/1 and one
real source partition0..9, validates its genuine quad/plane, and checks the two
sets of four edges for a finite ordered XZ bound within[-8,8]. It does not own
or query self/body or grant reaction equilibrium. Quad seam accepts only finite
coordinates/plane within[-8,8] and validates horizontal/upward/nondegenerate
convex geometry; it creates no provider. Neither result type can contain a
true body/self/force/load/world/route/actor flag. Malformed domains/API provider
refuse; supported reversed/tilted/degenerate inputs return invalid geometry;
unsupported environment grants no new geometric positive. No supplied normals.

## Independent acceptance and denied scope

First GCC/Clang public observations precede outcome assertions. Independently
reconstruct the unfactored knees/full15 COM and exact paired-X mass cancellation;
corroborate pressure bounds and equal-reaction moment identities with unequal
source heights; verify selected actual keys, all four source/sole edges and
real finite margins. Snapshot unchanged child endpoint/self/Sites02 results,
all105 decisions and their original counters/regions.

Exercise arithmetic-only pressure outside sole/source, finite gap, exact and
nextafter margins, nonfinite/reversed bounds; quad clockwise/reversed, bow-tie,
concave, duplicate/zero edge, tilted/noncoplanar and supported uncertainty;
lowered child and pressure prefixes, cap0, missing/moved provider, copied result
lifetime and fenv/FTZ/DAZ. No pressure/pose/source fitting after refusal.
Publication requires full native GCC/Clang contracts, pinned format20/tidy20 and
preserved earlier snapshots. No fresh asset/master/capture/archive replay.

Qualification concerns only static normalized vertical reactions of this fixed
proxy. World/crop/sweep/volume/material, articulated strength, dynamics, continuous
motion/acquisition/swing/transfer/route, actor/seat/save/FirstFlight remainfalse.
A later fixed endpoint world query may own load→self→endpoint once and use
ORIGINAL full box/capsule world reservations, never Self01 ellipsoid shrinkage.
