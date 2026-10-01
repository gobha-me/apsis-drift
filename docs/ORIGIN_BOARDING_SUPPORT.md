# Origin boarding support catalog

Issue [#353](https://github.com/gobha-me/apsis-drift/issues/353) adds a
source-bound C++ catalog for exact contact-triangle ownership and local
candidate-surface evidence. It supplies the attribution prerequisite for
[#352](https://github.com/gobha-me/apsis-drift/issues/352), which retains the
physical boarding-route qualification.

## Source and admission

`assets/native/boarding-support-01` preserves the original support, checks,
provenance, extractor, checker and five inherited license documents. Its new
runtime catalog binds those sources to the unchanged operating-02 contact and
the admitted operating-motion-01 recipe. The source sidecar predates that motion
binding; its archived bytes remain unchanged.

| Selected input | SHA256 |
| --- | --- |
| Original support | `2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2` |
| Operating contact | `109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a` |
| Motion recipe | `afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298` |
| Normalized catalog | `58694e671102f27df5f405478af43d00fd05b3aeaef34c612939af6fa548a8d3` |

The 707,366-byte catalog records 32 groups and 1,087 source objects, attributing
all 475,212 contact triangles exactly once. Its 42 candidate patches reference
1,474 faces, including intentional overlap between boot and grasp roles.
Their union contains 1,322 faces and 1,217 group-qualified vertices. Integer
micrometre coordinates and original face winding are retained.

Original source matrices stay in the evidence archive. Two source objects have
legitimate scale; all source corrections are already baked into contact.
Runtime motion uses the existing proper rigid group deltas once.

The producer reconstructs normalized data from independently pinned source
bytes. Preparation verifies the closed file roster, dependencies, provenance
and reconstruction before installing cached verified bytes atomically into a
new destination. Existing destinations, symlinks and nonregular inputs refuse.
The C++ decoder checks bounded structure and geometry before comparison with
the independently pinned catalog compiled into the core.

Reproduce from the preserved source archive:

```sh
python3 tools/package_boarding_support.py \
  --source assets/native/boarding-support-01/sources --repository . \
  --output build-support/package
python3 tools/prepare_boarding_support.py \
  --package build-support/package --repository . \
  --output build-support/prepared
python3 test/boarding_support_package_test.py
```

Use new output directories for each reproduction. Package integrity establishes
copied data and identities; actor motion and engine rendering have their own
acceptance.

## C++ queries

`decode_origin_boarding_support` takes bounded catalog JSON and the qualified
operating-motion recipe. Successful admission returns an immutable shared
handle. Its metadata views live while a sharing handle remains alive;
moved-from handles have empty views and refuse queries.

`lookup_boarding_triangle` accepts a group index and that group's triangle
index. It returns one source-object index and the matching patch indices.
Every source object retains obstacle policy.

`boarding_support_group_transform` evaluates the catalog's admitted motion.
Craft groups use full craft-world rest deltas; moving D1 groups use canonical
station-contact deltas, including their offset. Fixed groups use identity.

`assess_boarding_candidate_surface` accepts a patch, triangle, named body
category, current owner-local point, optional body-side probe and operating
progress. It returns separate evidence for body-category compatibility,
selected-triangle membership, point-on-triangle, rod interval and declared side.
The point is mapped back to group rest once. No caller-supplied transform or
simulation clock enters this query.

Twenty grasp patches have undeclared side evidence. A sided patch without a
probe has missing evidence. The 233 reversed-winding pad faces use the explicit
source-probed side. Rod intervals already exclude 30 mm at each end.

Numerical point tolerances are 2 micrometres for surface evidence, 1 nanometre
for rod endpoints and a signed side-probe projection greater than 1 micrometre.
They establish no permitted body penetration. Matching metadata predicates
supplies no posture, reach, support acquisition, neighboring-body clearance or
collision exemption.

## Physical route still required

The admitted crop is finite. Attribution and selected-face queries cannot
establish that unrepresented neighboring or exterior space is clear.

The actual deployed C++ geometry includes 352 mm and 300 mm rung joints and a
70 mm lateral rung offset. Top acquisition needs a declared crouched/reachable
contact; the source catalog has no above-deck handhold candidate. Cabin tile
seams and the recessed threshold need finite foot/step support qualification.
The lower cockpit approach requires separately source-bound floor/contact
coverage. Occupied seating and initial outward departure remain unqualified.

Ordinary New Game, actor actions, save formats 16–20, standing support policy
and current flight retain their existing behavior. This catalog introduces no
new saved phase or playable boarding transition. First Flight and broader
Freedom stay open until their composed acceptance succeeds.
