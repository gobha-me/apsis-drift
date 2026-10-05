# Origin boarding self Recipe03

[#371](https://github.com/gobha-me/apsis-drift/issues/371) registers a new static
self recipe before further lower-transfer fitting. It retains every policy01
world reservation and the rounded self solids of
[Model02](ORIGIN_BOARDING_SELF_MODEL02.md). The canonical body is reconstructed
once from validated pose input. Its dimensions, frames, joints, soles, integer
masses, eye, center of mass and old diagnostic records remain authoritative.

Model02 stays available with its original meaning. Its separately rounded
prefix endpoints need not be exactly collinear with an original limb, and
its frozen hip-relative neck plane leaves real ownership slivers in historical
controls 12/26. Those findings motivated an explicit version change; arithmetic
uncertainty cannot forgive actual material outside a registered region.

## Fixed finite regions

The waist, knee, elbow and shoulder regions retain their existing recipes and
constants. The neck region is the helmet ellipsoid clipped by
`inverse(actual trunk frame)(x - actual trunk center).Y <= stored trunk halfY`.
This is the actual trunk-top plane. No rounded world-coordinate top or ideal
rotation substitutes for it. The upper helmet remains excluded.

Each hip, ankle and wrist now owns a finite part of its original Euclidean
capsule K. With actual stored endpoints A/B and unchanged finite length L,
the region is

```text
K intersect { (B-A) dot (x-A) <= L * sqrt((B-A) dot (B-A)) }
```

Differences in this definition are exact real differences of stored binary64
values. Hip points from hip toward knee; ankle from ankle toward knee; wrist
from wrist toward elbow. The radius must be at most L, and L must be strictly
shorter than the original axis. This retains the root ball and excludes distal
material. The front is a flat cut; it does not inherit the old short capsule's
distal hemisphere. No new rounded endpoint, nominal unit axis or complete
partner exemption defines the region.

The entire common interior and its limiting closure must belong to the named
region. A complete bound on the partner box's axial support is sufficient:
common material already belongs to K. Failure of that bound does not prove a
collision. A strict conflict needs a separately verified common-interior
witness outside the region; otherwise an unresolved pair refuses.

## Evidence and arithmetic

The result owns version3 region, certificate and pair records. It enumerates
all 14 connected regions and all 105 unordered pairs in lexicographic part order.
Nonadjacent pairs require a full separation certificate. Connected pairs need
complete ownership or certified absence of common positive interior. Every
connected index names the same pair. The public entry accepts only
`BoardingBodyPose`; fixture geometry and proof flags cannot grant gameplay
permission.

Existing private actual-solid membership/support and unchanged pair families
are reused. Shoulder coverage adds both signs of the represented cross product
of actual trunk columns 0/1. This is a stored certificate direction, rather than
an asserted exact inverse row. The proof covers the complete root ball,
outside-sphere cylinder and distal ball with actual-axis support bounds.

General axial predicates use outward binary64 enclosures of the unnormalized
gap `L*|d| - d dot (C-A) - sum(hj*abs(d dot Fj))`. These bounds have units of
square metres. No corner sampling proves whole ownership. A bounded exact-norm
specialization can resolve contact only when endpoint differences, component
squares, sequential square sums and the candidate root square are all checked
exact. Every nonzero product is guarded before multiplication/FMA, including
scaled residuals. Purpose-specific raw term limits are 26 for a point and 98 for
a box, within checked capacity 256. Absolute value applies to each entire exact
column projection, not its individual terms.

Root brackets require verified squared-residual signs with at most four
directed corrections. Unsupported arithmetic, conditioning or capacity refuses.
Arithmetic enclosure does not authorize a geometric tolerance, ignored tiny
component, normalized frame or wider production scalar. Arbitrary-pose
certificate coverage remains incomplete.

## Qualification controls and integration boundary

Thirteen public source-free controls were registered before evaluation. The
existing standing control, its yaw 90° variant and five historical hip/leg/torso
configurations with shoulders 45° / elbows 135° each qualify all 105 pairs. The
five original 145-degree forearm/trunk controls and the down-arm distal control
retain strict refusals. Extra unresolved records in negative controls remain
visible. The seven qualified controls are explicit regression requirements.

Independent tests verify actual inverse-frame geometry, original-axis contact
and adjacent representable intrusion, retained root/flat-front semantics,
complete box support including cancellation, malformed pair/region binding,
guarded norm refusal and complete shoulder partitions. An adversarial box has
all eight corners outside a capsule while its actual common interior escapes
the axial cut; it must never receive an ownership certificate from samples.
Test-only wider geometric oracles require at least 64 mantissa bits and check
deep margins; dyadic contact fixtures use separate exact arithmetic. Same-host
dual-compiler agreement does not establish cross-host library-math replay.

Local GCC and Clang qualification each passed 17,599 focused checks, all 43
CTests and all 43 native Godot contracts. The public control output was
byte-identical across compilers. Pinned format20 and full tidy20 passed. Twelve
Python suites retain their passing evidence on 168 byte-identical inputs.
These headless gates do not establish GPU performance or playable boarding.

After configuring the project, run:

```sh
cmake --build build --target apsis-drift-boarding-self-model03-tests
ctest --test-dir build -R '^boarding-self-model03-contract$' --output-on-failure
```

This is a static self checkpoint. Ship-source clearance, seat/sole load support,
continuous transfer, occupied hardware movement, actor phases and saves remain
separate. [The lower transfer](LOWER_TRANSFER_FEASIBILITY.md) and #352/#291/#245
remain open. The retained stowed-harness child #372 may supply a starting
equipment state; it does not prove sitting or boarding. No source fitting or
runtime asset admission belongs to this contract.

[Recipe04](ORIGIN_BOARDING_SELF_MODEL04.md) separately binds these unchanged
finite regions and predicates to body policy02. This Recipe03 entry point stays
on policy01 and does not silently certify lateral poses.
