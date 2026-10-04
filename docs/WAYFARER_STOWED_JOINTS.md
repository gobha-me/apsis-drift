# Static stowed restraint joints

The empty First Flight craft is intended to start with its complete harness
already parked. [Issue #372](https://github.com/gobha-me/apsis-drift/issues/372)
qualifies that static assembly. It does not qualify harness movement, occupied
closure, strength or release operation; those remain in
[issue #362](https://github.com/gobha-me/apsis-drift/issues/362).

The source study retains the straps, buckle, release, seat and unrelated cabin
obstacles. Five added connectors join retained endpoints to declared finite
attachment surfaces. A successful export does not admit a gameplay asset or
prove that the pilot can reach and occupy the seat.

## Declared attachments

Each connector has a root footprint on its retained strap and a fixed
footprint on the retained support. These ten footprints touch eight distinct
source objects. The seat-pan shell supplies three separate footprints.

| Connector | Root partner | Fixed partner |
| --- | --- | --- |
| Port shoulder | Port shoulder strap | Port shoulder anchor |
| Starboard shoulder | Starboard shoulder strap | Starboard shoulder anchor |
| Port lap | Port lap strap | Seat-pan shell, port footprint |
| Starboard lap | Starboard lap strap | Seat-pan shell, starboard footprint |
| Crotch | Anti-submarining strap | Seat-pan shell, centre footprint |

The declared footprints have 26 cap triangles and 46 perimeter-edge
occurrences. Internal triangulation diagonals are part of the footprint
interior; they are not its perimeter. Copied, potentially warped caps preserve
the original indices and float32 bits. The two partial shoulder-anchor bands
retain their actual emitted boundaries and original finite source partitions.
They must not inherit permission over an entire anchor face.

## Contact boundaries

A positive-area opposed cap proves a particular surface attachment. Adjacent
source facets can also touch its closed perimeter, even when they are not
the cap's original owner facets. Such contact needs explicit source-backed
boundary evidence and a selected contact rule. A shared coordinate or a
component name alone does not grant permission.

Boundary observation must account for every selected owner and connector
facet. It records genuine indexed edge incidence, endpoint-only incidence,
original parent facets and exact endpoint bits. Corner-only contact is
distinct from contact along an edge. Coincident geometry with different source
indices must not silently substitute for the declared attachment. Unmatched
partial edges remain visible.

Each selected rule permits a named finite edge or point only inside
its original attachment footprint and on its proven indexed parents. It
does not permit a third corner, an off-footprint segment, surface area,
interior overlap or an entire component. Interior crossings or unsupported
contacts require a geometry correction or a refusal. All source surfaces
remain pilot obstacles pending separate actor-contact qualification.

## Current evidence and remaining gates

The current study has complete structural accounting for 1,746 source
objects: 8,730 connector/source partitions, 1,092 numerical obligations and
27,481,730 source-face occurrences. Structural accounting is separate from
numerical clearance.

The earlier two-rule numerical pass certifies its first ten source-triangle
checks and stops at a finite line contact on the port shoulder strap's declared root
attachment. Eleven calls completed, none unfinished, and 1,081 remain
unstarted. Both previously selected shoulder-anchor edge contacts pass in
that prefix. This is a contact-policy refusal, not proof of penetration.

The complete perimeter observation now covers all ten existing attachments,
including their original edge and corner incidence. It completed within its
fixed work allowance and passed an independent recorded audit. The stopping
strap contact follows genuine indexed attachment correspondence; observing
that correspondence does not itself grant contact permission.

[The selected finite-boundary policy](https://github.com/gobha-me/apsis-drift/issues/372#issuecomment-5975823720)
retains both prior anchor rules and adds 36 finite edge domains and 29 copied
corner domains. Each names its actual indexed support and complete parent
facets, clipped to the original finite attachment triangle. Unmatched partial
edges and unsupported partial corners remain unselected.

Original triangle ownership is recorded per cap: 18 copied caps each belong
to one source face, while the eight partial anchor caps retain their two-face
source partitions. The full attachment's owner-face union cannot substitute
for an individual triangle's owner. The compact ledger preserves all
54 existing rows and adds 65 references. Its independent byte audit reproduces
the original ledger exactly after removing those additions. Input validation
resolves each reference to exactly one original cap row and binds its full
executable geometry and provenance.

The updated assembly inspection completed under the selected policy with all
119 records and complete source-face accounting, within the existing work
limit. [Independent review of its recorded structure passed](https://github.com/gobha-me/apsis-drift/issues/372#issuecomment-5976258379).

The numerical run under this policy certified its first 159 source-triangle
checks. The next call, the port lap connector against face 99 of the pan edge
welt, exhausted the remaining work allowance. All 167 entered methods
completed; 932 source-triangle calls remain unstarted.
[Independent recorded review accepted this closed refusal](https://github.com/gobha-me/apsis-drift/issues/372#issuecomment-5976555427).
It establishes neither penetration nor complete clearance. The dispatcher
records the final over-entitlement counter separately from admitted prior
work; complete final consumption remains unknown.

About 94% of that completed prefix's work was point classification against
connector faces. [PR #400](https://github.com/gobha-me/apsis-drift/pull/400)
adds exact finite-face and forward-ray bounds while preserving the original
predicates, ordered evidence, ambiguity refusals and work allowance. The
original winding code and imported certificates remain unchanged; new behavior
requires its own reviewed source binding. A closed job is never replayed.

A fresh, separately registered attempt with those bounds certified 455 checks
and exhausted the remaining allowance on check 456, against the starboard lap
connector and face 103 of the starboard pan edge welt. All 463 entered methods
completed; 636 source checks remain unstarted.
[Independent recorded review accepted this closed refusal](https://github.com/gobha-me/apsis-drift/issues/372#issuecomment-5977457025).
This is an operation-limit refusal, not penetration or full clearance. Complete
final work consumption remains unknown after the over-entitlement counter.

The completed prefix still spent about 81% of its admitted work on
classification. Its 4,537 queries repeatedly rebuilt and scanned the same face
bounds. [Issue #401](https://github.com/gobha-me/apsis-drift/issues/401) adds a
bounded prepared hierarchy for reuse within one source-triangle evaluation.
Its source consumer and new source binding require separate review before a
fresh numerical attempt. Preparation, traversal and original face ordering
consume the same fixed allowance; original predicates, source geometry and
finite-contact policy remain bound. Synthetic improvements cannot predict
complete ship-batch clearance.

Complete numerical clearance, source/material preservation, package admission
and runtime movement remain separate checks. Existing work and buffer limits
remain unchanged.

## Exact classification bounds

[`wayfarer_fixed_winding.py`](../tools/wayfarer_fixed_winding.py) builds finite
triangle bounds within each query. Closed point bounds prune boundary tests;
closed positive-ray slab intervals prune ray tests. Retained faces use the
unchanged exact predicates and preserve ordered boundary, hit and ambiguity
records. Box equality and positive singleton intersections remain candidates.
No whole-mesh box decision substitutes for winding membership.

The helper consumes a handle from the supplied original winding module. Its
source, predicate functions, typed callable defaults and live work methods are
checked separately; an original handle certificate does not authenticate this
new algorithm. New helper source reads have allocation limits and stable
regular-file FD/name checks. Legacy material verification retains its original
source-read behavior.

The portable tests use invented shapes and byte-exact source references under
[`test/fixtures/wayfarer_fixed_winding`](../test/fixtures/wayfarer_fixed_winding).
Run these checks with Python's standard library:

```sh
python3 test/wayfarer_fixed_winding_differential_test.py
python3 test/wayfarer_fixed_winding_source_reads_test.py
python3 test/wayfarer_fixed_winding_callable_semantics_test.py
python3 test/wayfarer_prepared_winding_test.py
```

For the invented 432-face cube, interior, exterior and boundary controls use
about 24–37% of the reference classifier's charged work. This measures the
defined mathematical operations, excluding source hashing, compilation and
handle verification; it is neither a wall-clock benchmark nor a prediction
that the actual ship batch will fit. Bound construction and rational slab
arithmetic consume the existing allowance. Exhaustion retains partial coverage
and the original over-limit counter; it never grants complete clearance.

### Reusing exact face bounds

`prepare_classifier(winding, material, operation_limit=remaining)` returns
construction status, charged work and a prepared handle. A failed construction
returns no handle. `classify_prepared_point(winding, material, prepared, point,
operation_limit=remaining)` preserves the original classification evidence and
adds traversal accounting. The caller must prepare lazily, subtract construction
work from the same source-triangle allowance, pass the decreasing remainder to
each query and discard the handle when that evaluation ends. These functions
do not enforce the caller's evaluation lifetime or qualify the source consumer.

The deterministic hierarchy stores exact bounds as original vertex references.
Stable median splits use exact coordinate keys; leaves contain at most eight
faces. Closed node and face boxes only filter candidates. Boundary testing
finishes before ray testing, and retained candidates execute the original
predicates in original face order. Equality, positive singleton intersections
and ambiguous rays retain their previous handling. Construction, sorting,
partitioning, traversal and narrow predicates all consume mathematical work;
exhaustion preserves an incomplete result and the actual over-limit counter.

The existing input limits remain 256 vertices and 512 faces. The index has at
most 1,023 nodes and a conservative encoded bound of 29,778 bytes, below its
32 KiB ceiling. It binds the supplied material and winding instances. Integer
field validation, encoding, hashing and existing material/source authentication
are separately bounded identity overhead outside mathematical work. They do
not rebuild geometric bounds or classify points. For the invented 432-face
cube, each index identity pass visits 4,167 integer fields and 8,691 encoded
bytes. Frozen handles follow the existing cooperative module contract.

The invented ten-query comparisons below include construction once, then all
queries, under the same operation counter. They compare complete mathematical
evidence with the original winding classifier and compare cost with the
unchanged flat-bounds helper preserved in the test references.

| Invented workload | Flat operations | Prepared operations | Reduction |
| --- | ---: | ---: | ---: |
| Tetrahedron, mixed boundary/noncontact | 2,649 | 1,933 | 27.0% |
| 432-face cube, mixed boundary/noncontact | 171,648 | 82,315 | 52.0% |
| Same cube, shuffled face order | 171,648 | 86,519 | 49.6% |
| 432-face cube, ten noncontact queries | 191,072 | 83,486 | 56.3% |

A single query can cost more after preparation. These synthetic operation
counts exclude the disclosed identity overhead, are not elapsed-time claims
and do not establish that the actual source batch fits. The original flat API
remains available. The prepared tests also cover legal large rational values,
the maximum vertex/face dimensions, wrong or changed handles, exact decreasing
allowances and partial construction or traversal.

## Portable observer

[`tools/wayfarer_stowed_joint_observation.py`](../tools/wayfarer_stowed_joint_observation.py)
provides the pure standard-library function
`observe_attachments(registration_bytes, independently_expected_registration_sha256, buffers)`.
The registration binds the immutable capture and five original connector
buffers, their complete source signatures and payload digests, and the exact
ten attachment descriptors. The expected registration hash comes from an
independent recorded selection. The function performs no file I/O.

It authenticates bounded input bytes before decoding, rejects malformed or
nonfinite state, and preserves original binary32 values and index order.
Its report includes complete source and connector index rosters, edge and
corner incidence, unmatched partial boundaries and a bounded work record.
Full coordinate buffers are not included. Input parsing has byte, depth and
entry limits before allocation; the complete report must fit one MiB.
An exhausted limit refuses without dropping incidence or granting permission.
The observer's work counter is separate from numerical clearance allowances.

Run its portable fixtures without Blender, Godot or an authoring asset:

```sh
python3 test/wayfarer_stowed_joint_observation_test.py
```

First Flight still requires physical station-to-craft-to-seat travel,
supported seating, authoritative save/state binding, departure, applied
thruster exhaust and the existing same-world atmosphere/terrain/home flight.
No harness study authorizes a teleport, a smaller pilot or a late equipment
swap. See the owner acceptance in
[issue #245](https://github.com/gobha-me/apsis-drift/issues/245),
[the corrected-rest foundation](WAYFARER_CORRECTED_REST.md) and
[Godot ownership](GODOT_ADOPTION.md).
