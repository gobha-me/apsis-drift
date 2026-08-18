# Save Format and Compatibility

Apsis Drift save format version 8 is a deterministic JSON document. It keeps
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

The application persists only on a clean exit. The complete legacy Signal Run
owns projection of its gameplay changes; the first intersystem mission board
projects its authoritative contract state independently.

## Document shape

Every document has three required top-level fields:

- `application` is exactly `apsis-drift`;
- `format_version` is the unsigned JSON integer `8` for newly written saves;
- `recipe` and `state` are required objects.

The recipe records the universe seed, origin-system and active-planet
ordinals, the seed, planet, terrain-tile, origin-station, surface-signal,
local-sun, local-system, analytic-ephemeris, intersystem-contract, and
intersystem-jump, system-flight, and origin-return versions, plus the expected
regenerated station and planet IDs.

`state.career_kind` selects one of two explicit projections:

- `legacy_signal_run` retains the version 1/2 local planetary state;
- `intersystem_contract` records the first-contract identities, universe tick,
  mission/travel phases, current system/planet, committed destination, and
  phase-start tick. Version 4 additionally records the immutable Assisted
  arrival solution bound at jump commitment: destination/reference identities,
  arrival tick, and finite system-space position and velocity. Version 5 adds
  the mutable system-flight tick, identities, position, velocity, attitude,
  controls, flight mode, and bounded time scale. Version 6 admits the matching
  target-planet flight state and one collected delta for the contract's bound
  surface objective. Version 7 adds the origin-station approach tick,
  identities, inertial pose, controls, and mode. Version 8 records the
  authoritative `assisted` or `pilot` rule profile selected before launch.

Legacy mutable state records:

- docked or in-flight location;
- the First Signal Run status and bound target signal;
- either `null` flight state while docked or the complete authoritative
  planetary flight state while in flight;
- unique discovered signal records;
- an ordered sparse world-delta journal.

The checked-in [`save-v2-golden.json`](../test/data/save-v2-golden.json) and
[`save-v1-golden.json`](../test/data/save-v1-golden.json) remain legacy
migration fixtures. Newly encoded documents are canonical version 8.

## Encodings

Unsigned 64-bit seeds, ordinals, and simulation ticks are canonical decimal
strings. Zero is `"0"`; leading zeroes and signs are invalid. This avoids loss
in JSON consumers whose numeric representation cannot exactly hold every
64-bit integer.

Stable IDs retain their canonical forms: `system-`, `star-`, `mission-`,
`station-`, `planet-`, or `signal-` followed by exactly sixteen lowercase
hexadecimal digits. Enums use lowercase snake-case names.

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

All fields for a selected supported format are required. Unknown fields in
otherwise supported JSON objects are ignored and discarded when the document
is rewritten; this lets
new optional diagnostics travel through older readers without changing game
state. Duplicate object keys are rejected rather than resolved by ordering.
Unknown enum values and delta kinds are rejected because silently dropping
their semantics could resurrect or duplicate generated content.

Formats 1 through 8 and the generator versions compiled into the current build
are supported. Version 1 is decoded with local-sun generator version 1;
formats 1 and 2 rewrite as version 8 on the next explicit save. They remain
`legacy_signal_run` careers and are never assigned the intersystem contract.
Released version 3 intersystem careers preserve every recorded phase and tick;
their absent arrival solution remains absent rather than synthesizing progress.
Released version 4 target arrivals initialize system flight from their immutable
arrival solution; no destination, tick, or mission progress is rerolled.
Released version 5 system flight remains exact. For a version 3–5 contract that
already recorded objective completion, migration materializes the matching
collected delta at its saved universe tick; it does not advance a mission.
Released version 6 Planetfall state remains exact. A released origin arrival
with an immutable return solution materializes the matching station-approach
state without changing its tick, mission phase, or world delta.
Formats 1 through 7 have no rule-profile field and migrate to `assisted`; their
version-1 intersystem contract recipe is normalized to contract version 2.
Other format versions fail as unsupported and other generator versions fail as
incompatible. Older builds reject version 8 before reading fields, so they
cannot silently discard the new mission state.

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

## Intersystem staging

Version 3 reserved the high-level travel-state envelope. Version 4 binds and
persists the Assisted FTL arrival solution. Version 5 persists mutable
target-system craft flight and permits exactly one matching system or target-
planet flight representation for its travel phase. Version 6 makes the
target-planet projection playable: it retains planetary flight and requires
objective-complete, returned, and turned-in contracts to carry exactly one
collected delta for the immutable target, at or before the universe tick.
Pre-completion intersystem state cannot carry a world delta. Camera state,
terminal capabilities, render profiles, caches, and presentation progress
remain excluded.

Version 7 preserves a frozen target-system craft state during cancelable
return spooling, removes it at jump commitment, and admits exactly one matching
`origin_return` state after origin arrival. Docked, returned, and turned-in
states contain no active craft representation. The origin-return state must
match the contract tick, origin system, and stable station identity and must
contain only finite bounded inertial values.

Version 8 adds the rule profile to the high-level intersystem contract. It may
change only while docked with an offered or accepted mission and is locked by
launch. Thermal load, alignment samples, and arrival-quality fields remain
deferred to the systems that first make them authoritative; presentation-only
profile labels and guidance are not serialized.
