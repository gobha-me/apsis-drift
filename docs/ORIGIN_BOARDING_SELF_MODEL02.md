# Origin boarding self Model02

Issue #368 registers a separate static self-checkpoint model. Policy01 world
reservations, dimensions, masses, frames, soles, validated freedoms and old
self diagnostics remain unchanged. This checkpoint does not test ship geometry,
foot support, a motion interval, actor behavior or saves.

## Authoritative input and output

`evaluate_origin_boarding_self_model02` accepts only `BoardingBodyPose` and
calls `evaluate_origin_boarding_body` once. Its `canonical` field retains the
result, including all 15 world reservations, joint positions, eye, integer
mass weights, center of mass and Model01 diagnostic records. The new result
separately identifies self-model version 2, its 15 self solids, 14 fixed
connected regions and all 105 unordered self pairs in canonical enum order.
The 14 regions also follow lexicographic pair order; each connected index
names the same first/second pair. Caller-provided shapes, regions or proof
flags cannot grant permission.

The recipe is complete; arbitrary-pose certificate coverage is incomplete.
A valid pose can produce unresolved pair records and refuse. Every accepted
pair needs a complete certificate; there is no permissive default. The
self-checkpoint flag is true only when all 91 nonadjacent pairs have certified
absence of common positive interior and all 14 connected pairs have either
such absence or complete ownership containment. Contact alone is not positive
clearance. Old Model01 self conflicts are retained as old diagnostics and do
not decide the new self result.

## Actual stored geometry

Only trunk and helmet self boxes become ellipsoids. Centers, stored frame
columns and half sizes remain identical; the half sizes become semi-axes.
For the column matrix F, center c and semi-axes h, the closed ellipsoid is
`c + F diag(h) u`, `dot(u,u) <= 1`. The actual stored columns define the
affine image. Membership requires their inverse; using their transpose,
recomposing ideal trigonometric rotations, snapping small components or
normalizing columns changes the assessed geometry and is forbidden. Support
uses the actual columns directly. Singular, unsupported or numerically
unresolved frames refuse. All other self solids remain the existing stored
boxes or Euclidean finite capsules.

The new geometry does not replace policy01 source-clearance reservations,
a boot sole, pan skin or a support shape. Mass points and integer weights are
unchanged synthetic game data, not recomputed volume densities. Model01's
private transpose-based depth routines are not authority for new affine
ellipsoid or box membership.

## Frozen finite connected regions

The entire actual common interior and its limiting closure must belong to
the named fixed region. A strict common-interior witness can prove refusal;
a sampled collection of points cannot prove complete ownership. Nonadjacent
pairs receive no ownership region.

| Junction | Closed region | Frozen binary64 extent |
| --- | --- | --- |
| Waist | Trunk ellipsoid clipped below its hip-relative trunk-frame Y plane | `0x1.bb0cd605d7512p-3` m |
| Neck | Helmet ellipsoid clipped below the same coordinate plane | `0x1.3ced916872b02p-1` m |
| Each hip | Hip-to-knee prefix capsule, thigh radius | `0x1.bb0cd605d7512p-3` m axis |
| Each knee | Sphere at knee | `0x1.18f69ad39e925p-2` m radius |
| Each ankle | Ankle-to-knee prefix capsule, shin radius | `0x1.7529d1ebf8034p-3` m axis |
| Each shoulder | Sphere at shoulder | `0x1.56872b020c49cp-2` m radius |
| Each elbow | Sphere at elbow | `0x1.bab11b9fbb660p-3` m radius |
| Each wrist | Wrist-to-elbow prefix capsule, forearm radius | `0x1.12c49dd0cc1e9p-4` m axis |

Both cap coordinates are `inverse(Rt)(x-P)`, using the actual stored trunk
frame and hip. The frozen planes are used directly; a pose-dependent rounded
center-plus-extent plane is not substituted. The waist extent derives from
`hypot(.12,.18)`, the pelvis sagittal diagonal. The neck extent is the frozen
`.3495 + .2695` recipe. Hip uses the same sagittal diagonal. Ankle derives
from the boot's furthest ankle-relative corner. Wrist derives from the hand
half-size diagonal. Knee and elbow derive from the supported sagittal
half-ray angular bounds. Shoulder is the explicitly registered synthetic
`.2695 + .065` junction envelope; its distal down-arm endpoint is outside.
These are registered semantic regions, not shapes selected after a ship fit.

Each prefix is constructed from the actual joints, in this separately rounded
binary64 order with contraction disabled:

1. Subtract `toward-joint`, once per component.
2. Compute one three-argument `std::hypot` norm and one reciprocal `1/n`.
3. Multiply each difference by that reciprocal, then by the frozen length.
4. Add each resulting step to the actual joint.

The resulting stored endpoints define the region. Do not substitute direct
component division, correct its endpoint, assume an exact nominal length or
infer exact collinearity from the construction. Zero components are valid;
zero/nonfinite norm and nonfinite intermediates refuse.

## Complete proof families

General support-plane separation encloses the full convex shapes. Candidate
normals are deterministic geometric directions, including world axes, center
displacement and capsule endpoint directions. Failure to find a certifiable
plane is unresolved. No absence of witnesses implies separation.

Connected certificates are purpose-specific complete bounds:

- Waist and neck bound the full partner by the frozen cap plane. Actual
  cofactor arithmetic preserves stored-coordinate residuals. The identity
  standing neck margin is exactly `2^-54` m; rounded addition alone erases it.
- Hip requires exact forward collinearity of the actual prefix and original
  thigh axis, and bounds the complete pelvis projection by the actual prefix.
- Knee and elbow bound the complete common intersection of the two joint-
  origin half-ray capsules by the stored sphere. The opposite-ray endpoint-
  ball specialization avoids unstable angle subtraction. Angular bounds use
  the actual stored endpoints, not nominal input angles.
- Ankle can certify that the complete boot lies behind the shin endpoint
  plane. Its common intersection then belongs to the shared endpoint ball,
  included by the finite prefix regardless of prefix collinearity.
- Shoulder covers the complete upper-arm root ball, axis cylinder and distal
  ball. Outside the finite sphere, the cylinder's minimum axial parameter
  and directional support must exclude it from the actual trunk ellipsoid;
  the entire distal ball must also be excluded.
- Wrist covers the complete identity-frame hand with a vertical actual
  prefix in three Y bands: the middle belongs to the wrist ball, the lower
  band to the finite prefix cylinder, and the upper band lies behind the
  original forearm endpoint plane. Tiny original-axis slope is retained.
  This family does not assume forearm/prefix collinearity. Other frames or
  directions require a complete certificate or refuse as unsupported.

The evaluator reports the applicable certificate or unresolved reason. It
never compares the pose to a table of seven known inputs to grant acceptance.

## Numerical refusal boundary

Geometry inputs are the exact real values represented by stored binary64
numbers. Bounded outward intervals enclose arithmetic, roots and inverses;
compensated binary64 signs preserve exact linear/quadratic boundary cases.
`nextafter` widens arithmetic bounds only. No geometric epsilon, tolerance
repair, normalized-frame assumption, clamped trigonometric dot product or
wider production scalar authorizes acceptance. Unsupported products,
conditioning, fixed-capacity overflow or unresolved roots refuse.

Strict negative witnesses must be certified inside both actual solids; a
connected negative must also be strictly outside its fixed ownership region.
Witness membership of an ellipsoid is dimensionless and is not reported as
metres of penetration. An unrepresentable or numerically unresolved witness
cannot become clearance.

## Registered qualification controls

The sole registered positive acceptance control has hip `(0,1.04763,0)`, zero
yaw/pelvis/relative torso/leg flex, both shoulders45deg/elbows135deg, no
unsupported freedoms, suit on, pack detached and flat boots. It was selected
before ship queries. Its independent source-free prototype proved 91
nonadjacent separating planes and 14 complete connected ownership bounds.
Production must certify its own actual predicates.

All five frozen145deg forearm/trunk controls remain strict negatives, as do
the two shoulder pairs of the registered down-arm distal control. Independent
tests check canonical bit identity, all105 records, frozen constants and
prefix endpoints on both compilers, cap equality/adjacent values, complete
curved partitions, actual affine membership, malformed/nonfinite geometry
and bounded arithmetic refusal. Additional private applicability controls
exercise geometric predicates without searching for new admitted actor poses.

A successful self checkpoint grants no world clearance, sole support,
continuous route, restraint opening or actor action. Subsequent lower-transfer
and boarding work must prove those separately in the existing authoritative
C++ world. No new assets, economy or missions gate this contract.

## Verification

The 2026-10-02 integrated GCC and Clang checks each pass 5,983 assertions,
using each compiler's own complete core library. The registered standing
control has 91 nonadjacent separation certificates and 14 complete connected
ownership certificates. All six registered negative controls refuse. The
independent tests use separate inverse-frame/support/containment arguments;
the exact neck golden uses integer dyadic arithmetic. Their wider test-only
geometric oracle requires at least 64 mantissa bits; production uses bounded
binary64 arithmetic. This does not establish cross-host library-math replay.

After configuring the project, run this contract with:

```sh
cmake --build build --target apsis-drift-boarding-self-model02-tests
ctest --test-dir build -R '^boarding-self-model02-contract$' --output-on-failure
```

Publication also runs the complete GCC/Clang core regressions, Godot native
contracts and pinned format/static analysis. No ship query or actor admission
is part of this test target.
