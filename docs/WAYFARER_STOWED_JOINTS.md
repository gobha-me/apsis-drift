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
Numerical clearance, source/material preservation, package admission and runtime
movement remain separate checks. Existing work and buffer limits remain
unchanged.

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
