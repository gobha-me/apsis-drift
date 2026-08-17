# Save Format and Compatibility

Apsis Drift save format version 2 is a deterministic JSON document. It keeps
the generated-world recipe separate from mutable player state and does not
serialize terrain tiles, render state, terminal capabilities, preferences, or
other reproducible presentation data.

Issue #18 defines the document and validation contract only. Atomic files,
profile paths, CLI load/save options, and live-state commit behavior belong to
#19. Sparse-journal application and compaction belong to #20.

## Profile files

Interactive runs may select a validated input profile with `--load PATH`, an
explicit clean-exit destination with `--save PATH`, or a deterministic new
profile with `--new-game-seed N`. Loading and new-game selection are mutually
exclusive. A different load and save path is an explicit save-as; a load path
is never overwritten unless it is also named as the save path.

The loader reads at most the format byte limit, decodes and validates into a
temporary document, and returns it for application-owned commit only after all
compatibility and regenerated-identity checks succeed. Failed loads therefore
cannot partially mutate live state.

Saving validates and encodes the complete document before opening the
destination directory. It writes a private temporary file beside the target,
synchronizes the file, atomically replaces the target, and synchronizes the
directory. Failures before replacement preserve the previous valid file and
remove the temporary file. A directory-synchronization failure is reported as
a durability error after replacement rather than pretending the old file was
retained.

The current v0.4 flyover resolves this profile plumbing and persists only on a
clean exit. The complete Signal Run in #24 owns projection of gameplay changes
back into the mutable profile fields.

## Document shape

Every document has three required top-level fields:

- `application` is exactly `apsis-drift`;
- `format_version` is the unsigned JSON integer `2` for newly written saves;
- `recipe` and `state` are required objects.

The recipe records the universe seed, origin-system and active-planet
ordinals, the seed, planet, terrain-tile, origin-station, surface-signal, and
local-sun generator versions, plus the expected regenerated station and planet
IDs. Version 2 supports origin-system ordinal zero and any unsigned
active-planet ordinal.

The mutable state records:

- docked or in-flight location;
- the First Signal Run status and bound target signal;
- either `null` flight state while docked or the complete authoritative
  planetary flight state while in flight;
- unique discovered signal records;
- an ordered sparse world-delta journal.

The checked-in [`save-v2-golden.json`](../test/data/save-v2-golden.json) is the
canonical representative document and is reproduced byte-for-byte by the
encoder. [`save-v1-golden.json`](../test/data/save-v1-golden.json) remains the
legacy migration fixture.

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
`removed` state names. The active version 1 application recognizes canonical
surface-signal keys and applies their compact current state after deterministic
regeneration. Stable key construction, duplicate resolution, terminal-state
behavior, and unknown-object policy are specified in the
[Sparse Generated-World Journal](WORLD_DELTAS.md) contract.

## Validation and compatibility

All version 1 fields are required. Unknown fields in otherwise supported JSON
objects are ignored and discarded when the document is rewritten; this lets
new optional diagnostics travel through older readers without changing game
state. Duplicate object keys are rejected rather than resolved by ordering.
Unknown enum values and delta kinds are rejected because silently dropping
their semantics could resurrect or duplicate generated content.

Formats 1 and 2 and the generator versions compiled into the current build are
supported. Version 1 is decoded with local-sun generator version 1 and rewrites
as version 2 on the next explicit save. Other format versions fail as
unsupported; other generator versions fail as incompatible. Older builds
reject version 2 before reading fields, so they cannot silently discard the
new compatibility version.

Local-sun geometry is regenerated from the active planet's independent
celestial stream and the saved authoritative flight tick. The direction is not
serialized as mutable state: save/reload reproduces it from the recorded
generator version, planet identity, and tick.

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

## Planned intersystem format

The current contract-only intersystem work does not change this format or its
golden fixtures. The first implementation that persists system travel will use
format version 3 for stable system/mission identities, one authoritative
universe tick, the active location representation, and committed-jump state.
Formats 1 and 2 will migrate as legacy origin-system Signal Runs without
teleporting, retargeting, or synthesizing progress. The complete projection and
presentation exclusions are specified in the
[Deterministic Intersystem Mission and Travel Contract](INTERSYSTEM_CONTRACT.md).
