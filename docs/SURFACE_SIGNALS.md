# Deterministic Surface Signal Contract

Surface signals are immutable generated-world recipes. Version 1 defines a
six-entry ordered approach-target catalog without storing discovery, mission,
or collection state in the generator.

## Identity and independent streams

Signal ordinal `0` through `5` derives its identity directly from the planet:

```text
planet seed
`-- encounter domain, signal ordinal      (signal ID)
    |-- encounter domain, ordinal 1       (placement)
    `-- encounter domain, ordinal 2       (attributes)
```

The signal ID is the first derived 64-bit seed. Its canonical textual form is
`signal-` followed by 16 lowercase hexadecimal digits. Retrying placement does
not consume the attribute stream, and adding signal behavior must not consume
terrain, planet-descriptor, or unrelated encounter streams.

The catalog order is ordinal order and is compatibility data. The six ordinals
map to positive x, negative x, positive y, negative y, positive z, and negative
z cube faces respectively. One signal per face distributes the initial mission
set around the planet and keeps the anchors widely separated without a mutable
global placement pass.

## Bounded placement

Each signal receives at most 64 candidates from its placement stream. A
candidate is the center of a LOD-12 terrain tile selected from the central half
of its assigned cube face. Avoiding face edges keeps each local safety probe in
one canonical tile and establishes a minimum angular separation of more than
30 degrees between signals on different faces.

The generator samples the candidate tile at the 3x3 grid formed by sample
coordinates 16, 32, and 48. A candidate is rejected when the maximum local
elevation minus the minimum exceeds 750 metres. The accepted surface elevation
is the center sample; the approach altitude is 1,000 metres above the highest
of the nine samples. If an ordinal exhausts all 64 candidates, the complete
catalog fails with `placement_exhausted`; partial catalogs are never returned.
An accepted signal records the zero-based candidate attempt for compatibility
diagnostics.

Terrain cache contents and prior access order may change generation cost but
not signal identity or output.

## Attributes and mutable state boundary

The attribute stream selects one of `survey`, `recovery`, or `anomaly`, a
signal strength from 4,000 through 10,000 basis points, and a reward of one
through three discovery points. These are minimal objective metadata rather
than an economy or settlement reward system.

Discovery, selection, scan progress, completion, collection, and removal are
mutable state. They belong to the later mission and sparse-journal systems and
must refer back to the generated signal ID rather than modifying this catalog.

Changing the derivation hierarchy, face order, catalog size, tile LOD,
candidate range, retry bound, terrain probe, acceptance threshold, approach
clearance, attribute mapping, or ranges requires a new surface-signal generator
version. Save data that depends on this recipe must record that version.
