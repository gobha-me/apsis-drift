# Lower transfer policy 01

This is the fixed synthetic game proxy for [#361](https://github.com/gobha-me/apsis-drift/issues/361),
registered before articulated fit attempts. The intended qualification is a
bounded kinematic route; human percentile coverage, material strength and a
general body simulation are outside it. Existing standing walking and save
formats 16–20 retain their
contracts. The provider uses suit-on, pack-detached geometry; later actor
composition must make that pack state explicit.

## Fixed dimensions

The 0.64-metre ordinary standing reservation is the complete broad walking
volume. It is not a solid anatomical trunk extending down to the ankles.
This articulated proxy keeps the 1.93-metre crown, 1.70-metre standing eye,
and existing boots, with separate finite body parts. The upper trunk is
520mm wide and 360mm deep, fixed independently of route fit. Its width includes
the shoulder region; the retained fixture's shoulder-joint span plus upper-arm
radii is about 535mm and remains inside the existing broad walking reservation.
No dimension or joint limit may be tuned after a failed fit without explicitly
revising this version and explaining the independently justified model change.
Character images cannot qualify collision geometry.

| Part | Fixed reservation |
| --- | --- |
| Upper trunk | Oriented box, 0.52m wide × 0.36m deep; from hip+0.08m to shoulder+0.04m |
| Pelvis | Oriented box, 0.48m wide × 0.36m deep × 0.24m high, centered at hip |
| Helmet/head | Oriented box, 0.32m wide × 0.36m deep × 0.36m high; upright center 1.75m above soles |
| Thigh, each | Capsule, joint length 0.47285m, radius 0.105m |
| Shin, each | Capsule, joint length 0.47478m, radius 0.075m |
| Boot, each | Rigid oriented box, 0.12m wide × 0.28m long × 0.10m high; centers X=±0.14m upright |
| Upper arm, each | Capsule, joint length 0.35898m, radius 0.065m |
| Forearm, each | Capsule, joint length 0.386m, radius 0.055m |
| Hand, each | Finite box, 0.08m wide × 0.10m long × 0.04m deep |

Joint lengths reuse the retained synthetic fixture proportions at stature
1.93m: thigh .245H, shin .246H, upper arm .186H, forearm .20H. Canonical ankle,
knee, hip and shoulder heights are .10m, .57478m, 1.04763m and 1.62663m above
soles. Shoulder joints are X=±.20265m; hip/knee/ankle centers are X=±.14m in
the canonical straight pose. Capsules include their end caps. Fixed lengths
never stretch. Straight joints are allowed at stable checkpoints; no generic
inverse-kinematics solver or singularity waiver is needed to evaluate registered
forward joint curves.

Canonical forward is craft-local -Z, up is +Y and width lies along X. The
boarding-seat orientation follows its actual source-selected pan/back geometry;
it cannot be chosen independently to make an endpoint pass.

Joint limits: hip flexion [-20,120] degrees, abduction ±35, axial rotation ±45;
knee flexion [0,135]; ankle pitch ±30 and roll ±15; torso/pelvis lean ±35 and
twist ±45; shoulder flexion [-30,150], abduction [0,100]; elbow [0,145]; neck
pitch/yaw ±30. Fixed relative transforms carry the head and standing eye through
an articulated pose; there is no separately shortened crouched character.
Nonadjacent parts cannot intersect. Connected joint caps and pelvis/trunk seams
may overlap only in registered joint neighborhoods, never through unrelated
body or world geometry. Folded arms must fit the initial broad reservation.

## Contacts and kinematic stability

Every rigid core retains strict source-triangle collision semantics. Boots may
load only through actual finite sole contact with eligible source surfaces.
Floor-plane equality, representable penetration and source/crop membership
retain their existing strict comparisons. A loaded stair sole must contain a
20mm-radius center-of-pressure disk whose center lies at least 10mm inside its
actual contact hull. This is a new explicit stair contact model; it does not
weaken the 75% upper flat-route policy.

Only the source-selected pan patch30 may provide pelvis/thigh seat support.
Back patches29/31, head patch28 and elbow patches26/27 allow their declared
contact categories. There is no eligible lower-cockpit hand grip, tensile load,
head load, pull-up or elbow-only suspension. No shell, welt, seal, housing,
control or whole cushion object receives an exemption.

A compliant contact skin is at most 5mm thick on registered pelvis/thigh
undersides, trunk back and elbow/forearm pad regions. Only their explicitly
selected seat faces may enter that skin. The rigid core remains nonpenetrating;
nonselected geometry remains strict. Skin compression never relaxes sole or
floor collision. Headrest contact is optional positional evidence, with no
load-bearing skin permission inferred for the helmet.

Synthetic mass fractions for a geometric center-of-mass surrogate: trunk45%,
pelvis12%, head8%, thighs10% each, shins4% each, boots1% each, arms/hands2.5%
each. Within each arm share, assign one half to the upper-arm midpoint, one
third to the forearm midpoint and one sixth to the hand center. These sum to
100%; they are fixed game assumptions, not measured physiology.

At every phase, the surrogate load projection must lie 10mm inside the genuine
combined support hull. No zero-area touch or unsupported pause grants support.
For pan contact, a source-backed compressive reaction must admit the vertical
load direction within the fixed synthetic .6 friction cone; geometry whose
normal cannot do so supplies no vertical load support. Vertical reaction forces
permit this projected-hull test even across support heights. This is a bounded
kinematic stability model, not a material-rating, general force solver or
Newtonian articulated-body dynamics claim. Acquire replacement support before
unloading the old support. Seat occupancy requires pan support without floor
load; reverse standing requires foot support before unloading the pan.

## Motion and evidence

Hold `OperatingProgress{1,1,1,0}`. Qualify one complete reversible sequence from
the last upper tile through the actual transition and intermediate supports,
seal/lip clearance and finite pan acquisition, ending at a specific seated
boarding pose. Use the tub ramp only if the supported sequence needs it. Every
original and halo obstacle remains present, with its source namespace. Existing
posed seam triangles cannot be transformed a second time as rest data.

Register analytic root/joint curves and support events. Maximum root speed
.25m/s, swing-sole speed .35m/s, joint angular speed 30 degrees/s and surrogate
root/load acceleration .10m/s² bound this technical controlled transfer. Phase
has no independent simulation clock. Segment durations must satisfy these
limits; normalized samples cannot certify them or certify sweep clearance.
Qualify complete part sweeps, limb/joint reach, support transfer and exact union
coverage; unresolved intervals refuse. No collision forgiveness follows from
speed, skin labels or extra time.

Provider output is geometric evidence, not an actor action. Stable checkpoints
must have an actual load path, and cancellation uses a qualified reverse curve.
Occupied seat mechanism motion consumes the specific final pose in a following
child with independently admitted rest-source triangles; free cabin walking,
new saved actor phases, final art and First Flight completion are outside this
qualification.
