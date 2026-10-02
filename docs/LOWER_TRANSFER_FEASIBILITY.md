# Lower transfer feasibility checkpoint

The lower transfer in [#361](https://github.com/gobha-me/apsis-drift/issues/361)
is **not qualified**. The 2026-10-02 investigation uses the dimensions registered
in [policy 01](LOWER_TRANSFER_POLICY.md), without changing the body, joint limits,
seat, seal, shell or other source geometry after a failed fit.

The registered policy predates the fits at commit
`daa3696f6361b93e79e765958f72303e3378b8a9`; its document SHA-256 is
`e20db178c7126460ffc16e06b9fdcfc921b65b328956292e27608523bdaee002`.
It fixes dimensions and intended contact rules. Remaining forward-frame,
connected-part overlap, finite skin and contact-side definitions still need
registration before admitting an articulated trajectory.

## What the admitted seat can do

The existing source and operating recipe move the whole seat and its harness.
They contain no independent pilot-restraint opening state. The original
`craft_seat_lift` group (index 11) contains the following strict obstacles; ranges
are group-local, half-open triangle ranges from the admitted support catalog.

| Source object | Object ID | Triangle range |
| --- | ---: | --- |
| Anti-submarining strap | 846 | [108, 128) |
| Buckle release | 853 | [2720, 2828) |
| Five-point buckle | 886 | [13940, 14048) |
| Harness shoulder anchor | 887 | [14048, 14156) |
| Harness shoulder anchor.001 | 888 | [14156, 14264) |
| Lap restraint | 891 | [14480, 14500) |
| Lap restraint.001 | 892 | [14500, 14520) |
| Shoulder restraint | 905 | [20336, 20372) |
| Shoulder restraint.001 | 906 | [20372, 20408) |

Read-only Blender inspection of the pinned Craft09 master found these meshes
directly parented to the seat-height rig, without their own actions, drivers,
shape keys or constraints. The historical producer builds closed ribbons
converging on the buckle. The unrelated cabin wall-seat stowage clips supply
neither a pilot-harness stow position nor an opening action.

## Bounded fit results

The existing immutable C++ decoders exported 407,287 posed original/halo
triangles at `OperatingProgress{1,1,1,0}`. Those already posed triangles were not
transformed again. The actual seat pads establish boarding forward -X, width -Z.

For hip `(-.10,.83,-1.70)`, zero pelvis/trunk lean, original group 11 triangle
110 (`Anti-submarining strap`) has a clipped witness
`(-.232666694,.733333332,-1.712000038)`, 23.33mm inside the nearest pelvis core
plane. Triangle 20336 (`Shoulder restraint`) has a clipped witness
`(.038333326,1.256431924,-1.547218358)`, 41.67mm inside the nearest trunk plane.
These are actual triangle/box intersections, rather than broadphase refusals.
Neither face is an eligible seat-contact skin face.

A finite endpoint lattice tested 1,365 poses: hip X from -.55 to +.05m in 50mm
steps, hip Z from -1.85 to -1.55m in 50mm steps, pelvis lean from -35 to +35
degrees in 5-degree steps, at the source-derived 90-degree boarding yaw.
Of these, 1,219 had finite selected-pan skin contact; every such candidate had
a strict pelvis-core obstruction. No clear seated endpoint was admitted.
Even the least-obstructed front-edge perch crossed eight anti-submarining
strap faces. This bounded search does not prove that every possible pose fails.
Skin area is candidate geometry, not admitted friction, load or body support.

The fixed-length exploratory chain is world-surface clear at the last tile and
transition checkpoints. A single sideways boot at sole `(-.20,-.23,-1.32)`
is clear and has genuine intermediate tread contact: an 86.84mm-long rectangle
containing the declared 20mm pressure disk. A corresponding two-link leg can
reach the high-seat candidate, but its thigh crosses the actual pan shell by
74.44mm radially. Thus a supported boot and sufficient segment length do not
establish a supported body route. No combined load, self-collision, timing or
continuous sweep qualification follows from these checkpoints.

## Next implementation boundary

[#362](https://github.com/gobha-me/apsis-drift/issues/362) investigates an explicit
unbuckled/stowed restraint derivative using the retained meshes and anchors.
All straps and surrounding hardware must remain represented in presentation
and collision in their declared states. The current master and admitted
packages stay recoverable and unchanged.

After that capability exists, repeat the frozen fit checks and separately
resolve supported pan acquisition around the shell. Opening the harness alone
proves neither that the remaining seat fits, that the closed harness fits a
subsequently occupied actor, nor that the whole transfer works. A failed source
study must remain a failed study. Do not publish a route provider from these
refused candidates or silently resize the body until it passes.

## Restraint source studies

The first bounded opening investigation retained all 1,746 source objects and
5,503,130 evaluated triangles. Its 28 rigid-pivot candidates and 588 sampled
poses preserved ribbon topology, materials and terminal centers. A final
shoulder lift/splay and negative crotch release cleared the central exploratory
body at its floating hip position, but that position had no admitted pan load.
Strict source intersections remained; sampled clearance would not prove the
whole curve, attachment strength or occupied closure.

One concrete obstruction is the existing right lap strap crossing the seat
service manifold, including in the original closed geometry. A separately
declared temporary +80mm source-local X manifold correction cleared the housing
against original closed neighbors. It failed the final 60-degree lap stow: an
actual triangle-intersection segment measured 1.805mm, with strictly positive
barycentric coordinates in both faces. The preceding 20 samples lacked that
pair; this did not qualify the endpoint or a continuous sweep. No conditional
body search followed that refusal.

The source manifold is a closed layout-proposal housing with no modeled ports.
The retained service-loop endpoint was already disconnected in the inspected
boarding pose. No functional service continuity was inferred or claimed. The
temporary correction was not saved or admitted. Further declared study
candidates remain separate from the original geometry and its strict contact
policy.

[The closed-rest exporter](WAYFARER_RESTRAINT_EXPORT.md) preserves the original
seat render union while separating its seven restraint components. It supplies
source attribution for later motion work; it includes no housing correction,
opening curve or route qualification.

The subsequent fixed 33-pose study found five source-surface-clear pan
snapshots with finite selected skin polygons and friction-eligible normals.
Independent self review refused all five: each forearm axis runs 313.820mm
inside the trunk, with its 55mm-radius midpoint ball entirely contained. The
same proxy also has shoulder-cap/helmet and hip-cap/trunk conflicts at its
canonical reference, independent of limb flexion. These model defects must be corrected before drawing physical fit
conclusions.

The [C++ checkpoint diagnostic](ORIGIN_BOARDING_BODY.md) registers the unchanged
world reservations and these negative controls before renewed source fitting.
Positive self geometry and finite connected ownership require a separate
versioned registration. A clear world-surface query or positive pan margin
cannot override a failed self check.

[#366](https://github.com/gobha-me/apsis-drift/issues/366) separately corrects the
restraint foundation: the original closed shoulder ribbons already cross the
back pad and each other outside the buckle. Retaining that starting geometry
makes a continuously clear opening impossible under the strict contract.
Any corrected rest path must disclose changed geometry and lengths, preserve
original recovery, and qualify actual finite attachments before another curve.
No runtime replacement or supported seating follows from that source work.

The [corrected closed-rest foundation](WAYFARER_CORRECTED_REST.md) records the
registered edits, separate rest/posed comparisons and finite exported
attachment/coupler proofs. The unchanged closed-rest exporter remains a
historical identity reference. Neither derivative supplies an opening curve or
qualifies the lower transfer.

[Self Model02](ORIGIN_BOARDING_SELF_MODEL02.md), registered in
[#368](https://github.com/gobha-me/apsis-drift/issues/368), keeps the same world
reservations and introduces rounded self trunk/helmet solids plus fixed finite
connected regions. Every static checkpoint requires all 105 pair certificates.
The five historical forearm/trunk negatives remain refusals; a source-free
standing control does not qualify the lower transfer or a boarding action.

[Self Recipe03](ORIGIN_BOARDING_SELF_MODEL03.md), registered in
[#371](https://github.com/gobha-me/apsis-drift/issues/371), replaces only the
neck and hip/ankle/wrist ownership definitions. Its seven registered source-free
controls qualify all 105 self pairs; the six original conflicts still refuse.
The folded-arm controls have no new source or seat-support qualification.
[#372](https://github.com/gobha-me/apsis-drift/issues/372) separately investigates
retained stowed starting restraints; it does not supply the lower transfer.

## Evidence identities

| Input | SHA-256 |
| --- | --- |
| Craft09 master | `87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677` |
| Operating motion recipe | `afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298` |
| Boarding support catalog | `58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3` |
| Original contact source | `109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a` |
| Lower contact geometry | `31407a19a36d45eb43f318d83f4bc362c837e7e2bf4b92c332f1444f4fcb7c42` |
| Lower contact policy | `da32508e5b8b062ba622576d8c3ce23828b2f119235542ece1cc66ffa73d7020` |

Ignored local evidence is under `build-native/lower-transfer-probe/` and
`build-native/restraint-audit/`, with opening studies under
`build-native/restraint-study/` and `build-native/restraint-manifold-study/`:
source export/probe scripts, exact candidate records, clipped witnesses,
receipts and artifact identities. These are bounded
diagnostics, not a production route package. No actor action, saved phase or
First Flight completion is claimed; #361, #352, #291 and #245 remain open.
