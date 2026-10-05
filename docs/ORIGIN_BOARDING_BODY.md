# Bounded canonical boarding checkpoint evaluator

Implement a purpose-specific diagnostic for #361 with **policy01 world reservations unchanged**. It builds the 15 named boxes/capsules, fixed surrogate mass points, joint-limit evidence and self-intersection evidence. It can faithfully refuse the five frozen controls before any further ship query. A diagnostic result is not a physical route or saved actor action.

## Canonical coordinates and exact replay conventions

Use hip origin P, +Y up, forward -Z, width +X, column vectors and right-handed rotations in radians. Define `Rx(a)(0,-L,0)=(0,-L*cos(a),-L*sin(a))`; root yaw is `Ry(y)`. No implicit angle clamping, IK or alternative multiplication order.

Pose input fields use explicit degrees; rotation formulas convert internally to
radians. Root yaw is bounded to [-180,180] degrees. Both relative torso lean
and its resulting absolute lean must remain within [-35,35] degrees. Each hip
coordinate is bounded to ±8 metres for finite local arithmetic; this numeric
workspace is not source-crop coverage or physical contact authority.

- `R0=Ry(yaw)`, `Rp=R0*Rx(pelvisLean)`, `Rt=Rp*Rx(torsoRelativeLean)`. Both frames originate at P for policy01 replay. The frozen files' global torso lean is `pelvisLean+torsoRelativeLean`; do not apply pelvis lean twice.
- Pelvis center=P, half-size(.24,.12,.18), orientation Rp. Trunk center=`P+Rt*(0,.3495,0)`, half-size(.26,.2695,.18), orientation Rt.
- Helmet center=`P+Rt*(0,.70237,0)`, half-size(.16,.18,.18), orientation Rt. Eye=`P+Rt*(0,.65237,0)`. Zero neck rotation is the only registered replay state; nonzero neck angles refuse until their pivot/frame is registered. In the straight pose, hip height 1.04763 gives sole 0, eye 1.70 and crown 1.93 m.
- Side sign s is -1 port, +1 starboard. Hip joint=`P+Rp*(s*.14,0,0)`. `Rthigh=Rp*Rx(hipRelativeFlex)`. Knee=`hipJoint+Rthigh*(0,-.47285,0)`. `Rshin=Rthigh*Rx(-kneeFlex)`. Ankle=`knee+Rshin*(0,-.47478,0)`. Capsules retain radii .105/.075 including full endpoint balls.
- The frozen replay's boots are flat: `Rboot=R0`, center=`ankle+R0*(0,-.05,0)`, sole origin=`ankle+R0*(0,-.10,0)`, half-size(.06,.05,.14). Required relative ankle pitch is `-(pelvisLean+hipRelativeFlex-kneeFlex)`; enforce its ±30° bound. This equality must be explicit, not an independent floating boot placement.
- Shoulder=`P+Rt*(s*.20265,.579,0)`. `Ru=Rt*Rx(shoulderFlex)`, elbow=`shoulder+Ru*(0,-.35898,0)`. `Rf=Ru*Rx(elbowFlex)`, wrist=`elbow+Rf*(0,-.386,0)`. Upper/forearm radii stay .065/.055. Unlike the knee sign, positive elbow flex turns the down axis toward -Z; 145° reproduces the frozen chain.
- For exact policy01 diagnostic replay, hand center=wrist and orientation=Rt, half-size(.04,.05,.02). This is the existing synthetic hand reservation, not an invented forearm-following wrist. Independent wrist freedom, nonzero neck, abduction, axial rotation, roll and twist need explicitly registered frames before use; a supplied unregistered freedom refuses even if inside a historical limit.

The five inputs have yaw 90°, pelvis lean -10°/-5°, relative torso lean 10°/20°/15°, relative hip flex 100°/95°, knee 90°, shoulder flex 0°, elbow 145° and flat ankles. Keep their original hip positions. Do not claim they are new legal poses. All declared flex/lean limits still apply; malformed/nonfinite state fails before geometry. Suit-on/pack-detached must be an explicit input prerequisite.

## Self evidence and refusal

Connected pairs are pelvis/trunk, pelvis/thigh, thigh/shin, shin/boot, trunk/upperarm, upperarm/forearm, forearm/hand and trunk/helmet through the unresolved neck. Their positive overlap is **UnregisteredConnectedOverlap** until an independently declared bounded joint/seam neighborhood exists. No pair-wide exemptions. Every other pair is **NonadjacentInteriorOverlap** under policy01, including forearm/trunk, upperarm/helmet and thigh/trunk. Boundary equality does not imply positive interior; no body epsilon or world contact waiver.

Use finite capsule/box segment distance, capsule/capsule segment distance and box/box SAT with actual oriented shapes. Emit both parts, classification, witness, positive depth and policy/version. Do not stop at a broad AABB or hide known intersections behind another incomplete-definition error. Report DefinitionIncomplete and SelfConflict independently; no unresolved model may qualify.

Mass points are trunk/pelvis/head centers, thigh/shin midpoints, boot centers, and upper/forearm midpoints/hand centers. Integer weights with denominator 1200 are trunk 540, pelvis 144, head 96; each thigh 120, shin 48, boot 12; each upperarm 15, forearm 10, hand 5. They sum exactly 1200; divide once for COM. This preserves the registered surrogate, not a material-density model.

## Required controls and separate correction

Canonical zero-neck shoulder endpoint balls intersect the helmet by 22.350 mm, independent of arm flex. Canonical zero-torso hip endpoint balls intersect the trunk by 25.000 mm, independent of hip/knee flex. The five 145° forearms additionally cross the trunk over 313.820 mm of axis, with their 55 mm-radius midpoint ball fully contained. These are deterministic negative controls; changing elbows cannot cure the shoulder endpoint conflict.

Keep ordinary standing reservation .64m square and every policy01 world shape, source core, crop, namespace and contact comparison unchanged. Report existing finite support geometry/mass margins separately if later consuming qualified C++ contacts; self refusal cannot be promoted by positive pan evidence. No new world fits are needed for this child.

Policy01 cannot treat these conservative overlapping world reservations as mutually exclusive physical self solids and simultaneously accept its canonical zero-angle reference. A separately versioned Model02 must explicitly register **self geometry and finite proximal joint ownership**, while preserving the full policy01 world reservations for every source query. Its exact affected definitions are shoulder/helmet and hip/trunk proximal ownership, connected neck/waist/hip/knee/shoulder/elbow/wrist/ankle neighborhoods, and hand/neck frame freedom. Distal forearm/trunk intersections remain strict; no old failed pose becomes legal through an ad hoc mask. Select legal arm articulation before any renewed ship fit. This review does not choose smaller self dimensions, new postures or a positive Model02; independently justified self definitions must be registered and tested before admission. The diagnostic evaluator can proceed now with the intrinsic conflicts honestly unresolved/refused.

## Scope and registration

This contract was registered before the C++ diagnostic implementation under
[#365](https://github.com/gobha-me/apsis-drift/issues/365), after the independent
source-free model review. It preserves [policy01](LOWER_TRANSFER_POLICY.md)
and its world-clearance dimensions. It does not grant a supported body,
source-volume containment, contact crop authority, occupied seat, continuous
motion, load transfer or actor action. Existing simulation/save/native behavior
and all admitted source packages remain unchanged.

The local frozen contract identity is
`4c265f1b32740e3945f5e91da2121f060369106fdc55f7746f796df372dd2abe`.
The model-review evidence, including intrinsic finite witnesses, is retained
under `build-native/lower-transfer-model-review/`. The previous five static
source-clear snapshots are negative self controls. Source constraint repairs
remain separately tracked in
[#366](https://github.com/gobha-me/apsis-drift/issues/366); correcting the ship
alone cannot correct this proxy's intrinsic self-definition conflict.

## C++ diagnostic interface

`evaluate_origin_boarding_body(BoardingBodyPose)` returns an expected
`BoardingBodyDiagnostic` or a `BoardingBodyError`. Input angles have explicit
`*_degrees` field names. The two side records are port, then starboard. Input
validation precedes geometry; values are never clamped into a valid pose.

A successful evaluation includes all 15 part reservations, named joints,
flat-sole origins, required ankle pitches, eye, center of mass and all 105
unique unordered part pairs. Each pair distinguishes nonadjacent parts from
connected parts with unregistered finite neighborhoods. Its intersection is:

- `separated_or_contact`: the strict narrow test reports no positive interior.
  This combines separation and boundary contact; it does not report a positive
  clearance distance or admit a route.
- `interior_with_witness`: a finite common point has strictly positive depth
  in both actual shapes. Both depths accompany the point.
- `interior_unresolved`: the strict test indicates interior, but binary64
  arithmetic did not produce a verified common interior point. This remains
  a refusal, including overlaps too thin to contain a representable witness.
- `invalid_geometry`: private geometry controls refuse malformed shapes or
  frames. The public evaluator converts this condition to `numerical_failure`.

`self_conflict` and `unresolved_intersection` summarize their corresponding
pair findings. `definition_incomplete=true`, `model_qualified=false` and
`route_qualified=false` are compile-time invariants for this policy version.
A returned diagnostic therefore cannot become an accepted actor through a
caller changing a qualification flag.

The private narrow helpers bound coordinates to ±16 metres and box half
dimensions/capsule radii to [1e-6,4] metres. Degenerate capsule axes represent
spheres; nonzero axes with underflowed squared length refuse. These are
numerical workspace limits, separate from public body dimensions and source
coverage. Proper orthonormal frames have a conditioning allowance only for
rounded rotation matrices; it never enters contact, distance, SAT or witness
comparisons. There is no collision tolerance.

Capsule narrow comparisons preserve binary64 subtraction/addition and explicit
FMA product residuals in a bounded expansion before comparing squared distance
with squared radii. Box SAT similarly preserves center projection, basis
projection, projected half-size scaling and summed extents before its sign
test. The checked fixed expansion has 96 slots for at most 84 SAT terms.
Rounded square-root or extent-sum equality cannot turn a resolved strict
intrusion into clearance. Unsupported products or nonzero cross-axis
underflow refuse; rounded placement
without a verified witness remains `interior_unresolved`. This arithmetic is
part of the checkpoint diagnostic, without continuous-sweep authority.

The `boarding-body-contract` CTest target independently checks dimensions,
integer mass points, rotation order, knee/elbow signs, frozen negative poses,
actual common-interior witnesses, malformed inputs and exact/neighboring
binary64 contact cases. Its wider arithmetic is independent test evidence,
not production geometry authority. Existing world queries, actor actions and
saves do not consume this diagnostic.

The separate [lateral policy02 evaluator](ORIGIN_BOARDING_LATERAL_BODY02.md)
adds a bounded, explicitly versioned hip-abduction frame for load-shift
diagnostics. This policy01 evaluator and Recipe03 keep their existing contracts;
new lateral poses require later self/source/support qualification.
