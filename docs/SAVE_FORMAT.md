# Save Format and Compatibility

Apsis Drift save format 11 is a deterministic JSON document. It keeps the
generated-world recipe separate from mutable player state and excludes terrain
tiles, render state, terminal capabilities, user preferences, caches, and other
reproducible presentation data.

## Compatibility policy

| Application version | Player-save promise |
| --- | --- |
| `< v0.8.0` | Alpha. A release may reject an earlier format, but rejection must be explicit, transactional, and non-destructive. |
| `v0.8.0` through `< v1.0.0` | Every later beta must load every valid save created at or after `v0.8.0`. |
| `v1.0.0` through `< v2.0.0` | Every later 1.x release must load valid 1.x saves and the supported v0.8+ beta lineage. |

Format 11 is the intentional orbiting-home alpha reset. Formats 1 through 10
are no longer supported player saves. A loader recognizes them from the root
application and format fields, reports `unsupported_alpha_format_version`, and
returns before decoding authoritative recipe or state. It never overwrites,
renames, deletes, or partially migrates the source file.

The beta compatibility floor is deliberately not reserved in advance. The
`v0.8.0` release must record its actual current format as the minimum supported
beta format after skippable onboarding, open exploration, and ordinary
save/load UI are ready for durable careers. Generator recipes remain
independently versioned; after the beta line, an incompatible recipe requires
continued support, migration, or an explicit major-version policy.

## Profile files and transactional loading

Interactive runs may select a validated input profile with `--load PATH`, an
explicit clean-exit destination with `--save PATH`, or a deterministic new
profile with `--new-game-seed N`. Loading and new-game selection are mutually
exclusive. A different load and save path is an explicit save-as; a load path
is never overwritten unless it is also named as the save path.

The loader reads at most the format byte limit and decodes into a temporary
document. Application state changes only after schema, generator, identity, and
state validation all succeed. Missing files, malformed JSON, unsupported alpha
formats, unknown newer formats, incompatible generator versions, and corrupt
authoritative state remain distinct errors.

Saving validates and encodes the complete document before opening the
destination directory. It writes a private temporary file beside the target,
synchronizes it, atomically replaces the target, and synchronizes the directory.
Failures before replacement preserve the previous file and remove the
temporary file. Directory-synchronization failure is reported as a durability
error after replacement.

## Document shape and provenance

Every format-11 document has these required top-level fields:

- `application` is exactly `apsis-drift`;
- `application_version` is the non-empty, printable ASCII version of the build
  that most recently wrote the file, bounded to 64 bytes;
- `format_version` is the unsigned JSON integer `11`;
- `recipe` and `state` are required objects.

Writer provenance is diagnostic metadata. It does not enter `SaveDocument`,
simulation, generation, replay, or gameplay checksums. The encoder always emits
the current build version. A reader of an unknown newer format may use this root
field to identify the writing build without attempting to decode its state.

The recipe records the universe seed, origin-system and active-planet ordinals,
all required generator versions, and the expected regenerated station and
planet IDs. `state.career_kind` selects one current projection:

- `legacy_signal_run` is the bounded local Signal Run scenario used by its
  acceptance path;
- `intersystem_contract` records the current early-game contract identities,
  rule profile, universe tick, mission and travel phases, committed arrival,
  system or planetary flight, origin return, discoveries, and sparse world
  deltas.

Format 11 requires the complete current representation. It does not synthesize
missing arrivals, flight state, rule profiles, alignment, thermal history, or
world deltas. Later orbiting-home and Guided/Skip work may advance the alpha
format again rather than overloading format 11 with fields that do not yet have
authoritative gameplay semantics.

## Encodings and bounds

Unsigned 64-bit seeds, ordinals, and simulation ticks are canonical decimal
strings. Stable IDs use their type prefix followed by exactly sixteen lowercase
hexadecimal digits. Enums use lowercase snake-case names.

Authoritative binary64 values are finite decimal strings encoded with enough
digits to reproduce the exact value. JSON numeric literals are not accepted for
these fields. Duplicate object keys are rejected. Unknown fields in an otherwise
supported object are ignored and discarded on canonical rewrite; unknown enum
values are rejected.

The decoder accepts at most 1 MiB, 4,096 discoveries, 16,384 world-delta
entries, 128 bytes per object key, and 64 bytes of application-version
provenance. These are safety bounds, not target sizes.

## Deterministic validation

The validator regenerates station, system, planet, mission, and signal
identities from the saved seed and exact generator versions before admitting
mutable state. Identity mismatches and invalid/non-finite flight states are
errors, never reasons to reroll or reposition the player.

Contract phases must carry exactly the authoritative craft representation and
immutable arrival data their phase requires. Planetary state must satisfy its
flight regime, clearance, thermal, and journal invariants. Signal Run targets,
discoveries, and deltas must belong to the regenerated bounded catalog and have
monotonic, non-future ticks.

Historical format-1-through-10 acceptance reports remain project evidence, not
player-save compatibility fixtures. Their former migration code and golden save
documents were removed at the format-11 reset.
