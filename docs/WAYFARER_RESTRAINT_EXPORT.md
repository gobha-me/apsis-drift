# Closed-rest seat and restraint export

[Issue #363](https://github.com/gobha-me/apsis-drift/issues/363) separates the
existing seat render geometry into independently identifiable components. It
supplies a producer prerequisite for [#362](https://github.com/gobha-me/apsis-drift/issues/362);
it does not admit an opening state, runtime package, occupied seat or boarding
route. The admitted operating package and selected authoring master remain
unchanged.

## Reproduction

With Blender and the selected Craft09 master available, run from the repository
root. Set `CRAFT_SOURCE` to that source file and `RESTRAINT_OUTPUT` to a new
directory whose parent already exists, outside the source directory and frozen
reference package:

```sh
blender --background --factory-startup --disable-autoexec --python-exit-code 1 \
  --python tools/export_wayfarer_restraints.py -- \
  --source "$CRAFT_SOURCE" --output-dir "$RESTRAINT_OUTPUT"
```

The default independent reference is `assets/native/wayfarer-operating-02`.
`--old-package` accepts another copy with the same pinned identities. The
producer checks those identities before opening the source. It requires Linux
`renameat2` for atomic installation without replacing a concurrently created
destination. Unsupported installation environments refuse explicitly.

The source identity is
`87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677`;
the frozen full operating GLB identity is
`a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c`.
Source opening uses the existing operating preparation, rest reset, derivative
corrections and qualified base exporter. Only the seat subset is exported.
Neither the source file nor the full-craft package is rewritten.

## Geometry and evidence

The old `WFOpSeatLift` node combines 64 source objects. This export retains 57
objects in `WFRSeatLiftResidual` and separates these seven components:

| Source object | Render node |
| --- | --- |
| Shoulder restraint | `WFRShoulderPort` |
| Shoulder restraint.001 | `WFRShoulderStarboard` |
| Lap restraint | `WFRLapPort` |
| Lap restraint.001 | `WFRLapStarboard` |
| Anti-submarining strap | `WFRCrotch` |
| Five-point buckle | `WFRBuckle` |
| Buckle release | `WFRRelease` |

All eight nodes are unparented scene roots with identity transforms. Geometry is
baked once in metres with Godot +Y up and -Z forward. Fixed shoulder anchors and
the service manifold remain in the residual node at their original positions.
No proposed housing correction or restraint rotation is included.

The exporter compares the complete triangle multiset against the independent
original seat node: positions, winding, normals, UVs and complete material
descriptions must match exactly. Cyclic triangle indexing is equivalent;
reversed winding is not. Existing zero-area render triangles remain represented
and counted; they provide no contact support. Every exported triangle also maps
to its prepared source object and evaluated triangle. Identical occurrences are
assigned in stable source order, with no invented unique contact identity.

Output includes the GLB, face attribution, metadata, checks, provenance,
inherited licenses and a file-hash manifest. Tool hashes identify the exact
producer dependencies. The producer verifies source bytes, original selected
transforms and unrelated geometry before installation. Staging is removed on
failure, and an existing destination is preserved.

## Verification and limits

The portable suite requires Python's standard library, without Blender or the
authoring master:

```sh
python3 test/wayfarer_restraint_export_checks_test.py
```

Independent miniature GLBs and synthetic pinned packages check split-union
identity, winding and attributes, flat-node hierarchy, invalid numeric and
buffer boundaries, changed reference bytes, bounded decompression and atomic
destination races. CI runs this suite alongside the existing package checks.

Source replay with Blender 5.2.2 produced a 785,152-byte GLB with the exact
15,916-triangle original union, including 72 inherited degenerate render faces.
Two final replays produced identical bytes for all 11 output files. The GLB
SHA-256 is
`ca1d35a769bb345b37923fa7dc5edded40e97a8bd06a74b17f10ed8255ca88f3`;
the complete triangle-attribute/material union identity is
`2d5c58a47f6812d911cb5c4f8a78f2a4b691e32c392bab6b4c2a9421a54140ad`.
Successful producer checks establish export
integrity; they do not establish runtime binding, continuous collision-free
motion, functioning service connections, occupied harness fit, load support or
visual approval. Failed opening studies stay separate from this unchanged
closed-rest derivative. Their original source geometry remains strict
collision geometry until a separately qualified replacement is admitted.
