# Save Format and Compatibility

Apsis Drift save format 16 is a deterministic JSON document. It keeps the
generated-world recipe separate from mutable player state and excludes terrain
tiles, render state, terminal capabilities, user preferences, caches, and other
reproducible presentation data.

## Compatibility policy

| Application version | Player-save promise |
| --- | --- |
| `< v0.8.0` | Alpha. A release may reject an earlier format, but rejection must be explicit, transactional, and non-destructive. |
| `v0.8.0` through `< v1.0.0` | Every later beta must load every valid save created at or after `v0.8.0`. |
| `v1.0.0` through `< v2.0.0` | Every later 1.x release must load valid 1.x saves and the supported v0.8+ beta lineage. |

Format 16 is the intentional origin-system-contract alpha reset. Formats 1
through 15 are no longer supported player saves. A loader recognizes them from the root
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

Every format-16 document has these required top-level fields:

- `application` is exactly `apsis-drift`;
- `application_version` is the non-empty, printable ASCII version of the build
  that most recently wrote the file, bounded to 64 bytes;
- `format_version` is the unsigned JSON integer `16`;
- `recipe` and `state` are required objects.

Writer provenance is diagnostic metadata. It does not enter `SaveDocument`,
simulation, generation, replay, or gameplay checksums. The encoder always emits
the current build version. A reader of an unknown newer format may use this root
field to identify the writing build without attempting to decode its state.

The recipe records the universe seed; origin-system, tutorial-home, and
active-planet ordinals; all required generator versions; the regenerated home,
station, host, and active-planet IDs; and the station's integer circular-orbit
radius, period, epoch phase, inclination, and ascending node.
The versioned `origin_system_contract` generator stream is independent from
the home Signal Run and first intersystem-contract streams.
`state.career_kind` selects one current projection:

- `legacy_signal_run` is the bounded local Signal Run scenario used by its
  acceptance path;
- `intersystem_contract` records the current early-game contract identities,
  rule profile, universe tick, mission and travel phases, committed arrival,
  system or planetary flight, origin station flight, discoveries, and sparse
  world deltas.

Format 16 requires the complete current representation. It does not synthesize
missing arrivals, flight state, rule profiles, alignment, thermal history, or
world deltas. One `origin_station_flight` state stores craft position and
velocity in the current station-relative frame during outbound free flight,
its frozen jump spool, and the return approach. System-space pose and guidance
are regenerated from the state's validated tick; live flight matches
`universe_tick`, while an outbound spool retains the phase-start tick until
cancellation retimes it to the current contract tick.

Every state projection also carries the authoritative bounded onboarding
object:

```json
"onboarding": {
  "state": "guided",
  "chapter": "contract_one"
}
```

`state` is `guided`, `skipped`, or `completed`. Guided requires exactly one
chapter from `contract_one`, `contract_two`, or `contract_three`; skipped and
completed require JSON `null`. The starting home station, home planet, and
basic origin-system chart are derived access shared by every state. Contract
three additionally exposes the first-jump solution, while skipped and
completed expose the post-onboarding navigation baseline. These access facts
do not change the generated recipe, consume random streams, or synthesize
discoveries, visits, mission completions, rewards, or world deltas.

Every projection also carries `location` and a complete `first_objective`
object. Its deterministic `contract_id` and `target_signal_id` must match the
regenerated home binding even while offered; the mutable status is one of
`offered`, `active`, `completed`, `returned`, or `turned_in`. Guided contract
one retains a dormant authored intersystem contract solely as the shared
career clock and rule profile. Guided advances to contract two only after the
home objective is docked and explicitly turned in.

Guided advances to contract three only after the origin-system contract is
turned in. Contract-three universe-view focus and pending selection are
presentation state and do not enter the save; the existing committed jump
destination remains authoritative once spool commitment occurs. Explicit
station turn-in changes Guided onboarding to completed.

Guided contract two additionally carries a nullable
`origin_system_contract`, plus `origin_system_discoveries` and
`origin_system_world_deltas`. The immutable binding regenerates the distinct
destination planet and bound signal from the origin-system recipe. The mutable
phase records offered, accepted, station departure, outbound transfer, target
planet, objective completion, return transfer, station rendezvous, returned,
or turned in. Exactly one craft representation is admitted for each in-flight
phase. Until turn-in, the original `discoveries` and `world_deltas` remain the
completed home-contract history and the origin-system arrays describe the
target body. Turn-in moves both bounded histories into origin-system career
knowledge in home-then-target order and clears the later intersystem mission's
history vectors.

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

Historical format-1-through-15 acceptance reports remain project evidence, not
player-save compatibility fixtures. Their former migration code and golden save
documents are not supported by the format-16 loader. Publication tests submit
every historical format number from 1 through 15 to the file loader and verify
that each rejection leaves the source bytes unchanged.

## Local catalog boundary

Format 16 remains an explicit-path player-save format and does not yet carry a
local-catalog header. The version-1
[Menu and Local Profile Contract](MENU_AND_PROFILE_CONTRACT.md) defines the
future bounded metadata projection, header-derived ordering, transactional
New/Continue/Load/Save behavior, and CLI precedence. Catalog metadata will be
non-authoritative and excluded from simulation checksums; duplicated header
values must agree exactly with the fully validated recipe and state before a
profile can replace the live session.

Format 16 also does not carry an active direct-interstellar cruise. The
version-1 [universe-navigation contract](UNIVERSE_NAVIGATION.md) defines the
future bounded projection and proves it remains below 1 KiB, but this research
change does not reserve a new format number or synthesize navigation state in
existing saves.
