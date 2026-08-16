# Save Format and Compatibility

Apsis Drift save format version 1 is a deterministic JSON document. It keeps
the generated-world recipe separate from mutable player state and does not
serialize terrain tiles, render state, terminal capabilities, preferences, or
other reproducible presentation data.

Issue #18 defines the document and validation contract only. Atomic files,
profile paths, CLI load/save options, and live-state commit behavior belong to
#19. Sparse-journal application and compaction belong to #20.

## Document shape

Every document has three required top-level fields:

- `application` is exactly `apsis-drift`;
- `format_version` is the unsigned JSON integer `1`;
- `recipe` and `state` are required objects.

The recipe records the universe seed, origin-system and active-planet
ordinals, the seed, planet, terrain-tile, origin-station, and surface-signal
generator versions, plus the expected regenerated station and planet IDs.
Version 1 supports origin-system ordinal zero and any unsigned active-planet
ordinal.

The mutable state records:

- docked or in-flight location;
- the First Signal Run status and bound target signal;
- either `null` flight state while docked or the complete authoritative
  planetary flight state while in flight;
- unique discovered signal records;
- an ordered sparse world-delta journal.

The checked-in [`save-v1-golden.json`](../test/data/save-v1-golden.json) is the
canonical representative document. It is byte-for-byte reproduced by the
version 1 encoder.

## Encodings

Unsigned 64-bit seeds, ordinals, and simulation ticks are canonical decimal
strings. Zero is `"0"`; leading zeroes and signs are invalid. This avoids loss
in JSON consumers whose numeric representation cannot exactly hold every
64-bit integer.

Stable IDs retain their existing canonical forms: `station-`, `planet-`, or
`signal-` followed by exactly sixteen lowercase hexadecimal digits. Enums use
the lowercase snake-case names emitted in the golden fixture.

Authoritative floating-point state is encoded as a finite decimal string with
enough significant digits to reproduce the exact binary64 value. The encoder
uses `std::to_chars` with `max_digits10`; the decoder uses `std::from_chars`
and rejects non-finite, overflowing, or partially parsed values. JSON numeric
literals are not accepted for these fields.

World-delta object keys are non-empty lowercase ASCII identifiers of at most
128 bytes using letters, digits, `-`, `_`, `.`, `:`, or `/`. Version 1 defines
the journal envelope and the `discovered`, `collected`, `completed`, and
`removed` state names. #20 remains responsible for stable generated-object key
construction, duplicate resolution, application, and compaction.

## Validation and compatibility

All version 1 fields are required. Unknown fields in otherwise supported JSON
objects are ignored and discarded when the document is rewritten; this lets
new optional diagnostics travel through older readers without changing game
state. Duplicate object keys are rejected rather than resolved by ordering.
Unknown enum values and delta kinds are rejected because silently dropping
their semantics could resurrect or duplicate generated content.

Only format version 1 and the generator versions compiled into the current
build are supported. Other format versions fail as unsupported; other
generator versions fail as incompatible. There are no legacy Apsis Drift save
files and therefore no migrations in version 1.

The validator deterministically regenerates the origin station and active
planet from the recipe. A stored identity mismatch is corrupt or incompatible
data, never a reason to silently move the player. In-flight state must use that
planet, finite valid coordinates, a known regime and mode, at least the minimum
flight clearance, and a consistent optional regime transition. Docked state
must not contain active flight state; in-flight state must contain it and
cannot retain a merely offered objective.

The decoder accepts at most 1 MiB, 4,096 discoveries, 16,384 journal entries,
and 128 bytes per object key. These are format bounds, not a target size;
normal early saves should remain in the kilobyte range.
