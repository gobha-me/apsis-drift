# Complete boarding material source01

These immutable numerical inputs extend the retained Wayfarer contact data for
[the initial material assessment](../../../docs/ORIGIN_BOARDING_INITIAL_MATERIAL01.md).
They contain the full 1,746-object original roster, six complete original
geometry packets, 165 posed construction cross-checks, and thirteen stowed
replacement packets. The effective roster has 1,751 objects after eight exact
removals.

`completion.json` describes the little-endian binary64 vertices, signed
micrometre vertices and ordered triangle indices in `geometry.bin`. The
preparation tool checks their exact SHA-256 identities and emits one immutable
C++ translation unit in the build directory. Building does not require Blender
or the authoring master.

Source provenance and existing source/derivative licenses are linked in
`provenance.json`. Captured geometry and envelopes alone do not authorize
material exclusion, boarding, save changes or First Flight. Runtime assessment
must prove the registered finite material relations and retain any refusal.
