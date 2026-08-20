# Menu and Local Profile Contract

Version 1 defines how Apsis Drift owns local careers and destructive session
transitions before the title and pause menus grow beyond their current two
actions. This is an application policy. TermForge continues to own terminal
input, focus, capability detection, and degradation; it does not discover save
files or decide whether a game session may be replaced.

This contract records the behavior consumed by the later title, pause,
settings, preferences, and Guided/Skip work. It does not implement those
screens, add an autosave system, or change save format 14.

## Storage ownership and bounds

Local profiles live below the platform data directory, not the configuration
directory used by presentation and audio preferences:

```text
${XDG_DATA_HOME}/apsis-drift/profiles
${HOME}/.local/share/apsis-drift/profiles  # when XDG_DATA_HOME is unset
```

An empty or relative `XDG_DATA_HOME` is treated as unset. A missing or relative
`HOME`, an uncreatable directory, or a non-directory at any required component
disables the local catalog with an explicit diagnostic. Explicit CLI paths
remain available. The resolved catalog path is limited to 4,096 bytes before
filesystem access. Apsis Drift creates its owned directories with user-only
access and does not follow directory or profile-file symlinks while
cataloging.

The first catalog supports at most 64 profiles. A canonical filename is
`profile-` followed by exactly sixteen lowercase hexadecimal digits and
`.json`, for example `profile-0000000000000001.json`. The hexadecimal value is
the positive local `ProfileId`; zero is invalid. New allocates the lowest
unused ID while holding the catalog write lock. The ID is local storage
identity, not a universe seed, generated-world identity, or random stream.
Explicit deletion may make an ID reusable; deletion itself is outside the
first menu implementation.

Catalog discovery considers regular canonical files only. Temporary files,
foreign names, subdirectories, sockets, and symlinks are ignored. If more than
64 canonical candidates exist, the catalog reports an overflow and disables
New, Continue, Load, and catalog writes until the directory is brought back
within the bound. It never chooses an arbitrary subset.

## Header-derived catalog

There is no separate catalog index in version 1. Each future catalog-managed
save carries a bounded, non-authoritative root `profile` header alongside the
existing application, writer, format, recipe, and state data. The header is a
projection for listing; it does not enter `SaveDocument`, simulation,
generation, replay, or gameplay checksums.

The conceptual metadata shape is:

```cpp
struct ProfileMetadata {
  ProfileId id;                    // positive uint64
  std::uint64_t save_sequence;     // positive, catalog-wide ordering key
  std::string application_version; // printable ASCII, at most 64 bytes
  std::uint32_t format_version;
  Seed universe_seed;
  IntersystemRuleProfile rule_profile;
  OnboardingState onboarding_state;
  std::optional<OnboardingChapter> onboarding_chapter;
  ProfileLocation location;
  SimulationTick tick;
  std::optional<std::string> saved_at_utc; // display only
};
```

The future JSON projection is exact at this boundary:

```json
"profile": {
  "id": "1",
  "save_sequence": "7",
  "saved_at_utc": "2026-08-20T12:00:00Z",
  "summary": {
    "universe_seed": "42",
    "rule_profile": "assisted",
    "onboarding_state": "guided",
    "onboarding_chapter": "contract_one",
    "location": "docked_at_origin",
    "tick": "0"
  }
}
```

The existing root `application_version` and `format_version` fields supply the
same-named `ProfileMetadata` members; they are not duplicated inside
`profile`. The `profile` object is bounded to 2,048 encoded bytes. Supported
onboarding states are `guided`, `skipped`, and `completed`; chapter is
`contract_one`, `contract_two`, or `contract_three` only while guided, and is
JSON `null` otherwise. The location summary uses exactly the current travel
names: `docked_at_origin`, `origin_system_flight`,
`outbound_jump_spooling`, `outbound_jump_committed`,
`target_system_flight`, `target_planet_flight`, `return_jump_spooling`,
`return_jump_committed`, and `origin_system_return`.

`ProfileId`, `save_sequence`, the seed, and the tick use canonical decimal
strings in JSON. Enums use lowercase snake-case names. `saved_at_utc`, when
present, is a printable UTC RFC 3339 value of at most 32 bytes. It is advisory
display text and never controls ordering, Continue, simulation, or checksums.
The UI derives an initial label such as `CAREER 0001` from the profile ID;
editable career names are deferred. Catalog diagnostics are bounded to 160
printable bytes before display; longer filesystem or decoder details are
truncated without terminal control characters.

The metadata reader consumes no more than the save-document byte limit and
does not construct terrain, mission catalogs, renderers, or a live session. It
checks root structure, types, bounds, canonical encodings, the filename/header
ID match, and supported application/format provenance. The catalog may show a
bounded invalid-entry reason, but an invalid entry is never activatable.

Header values that duplicate authoritative state are hints until full load.
Full validation must prove that seed, rule profile, onboarding state/chapter,
location, and tick exactly match the decoded recipe and state. A mismatch is a
corrupt profile, not a reason to rewrite metadata or trust whichever value is
newer. Format-13 files without a `profile` header remain valid explicit CLI
files but do not silently enter the local catalog; Save As may import them into
a new catalog slot once a later format defines the header.

## Stable ordering and crash behavior

Every successful catalog write embeds a positive, catalog-wide
`save_sequence`. While holding the catalog write lock, the writer reads all
bounded canonical headers, rejects duplicate profile IDs or sequence overflow,
and assigns one greater than the largest valid sequence. Failed or canceled
writes do not consume a visible sequence. Catalog rows sort by descending
sequence and then ascending profile ID. Filesystem enumeration order,
modification time, wall-clock changes, and locale never affect the result.
Rows whose headers cannot supply a valid sequence follow usable rows in
ascending canonical filename order. With no separate index, a file missing at
scan time simply is not a row; a file removed after scanning fails activation,
then disappears on refresh.

The catalog has no cache that can become authoritative or stale. Saving uses
the existing same-directory private temporary file, validation, file sync,
atomic replacement, and directory sync. Save As creates only an unused
canonical destination. The application holds an advisory lock in the profiles
directory across ID/sequence allocation and replacement; inability to obtain
the lock is a failed write, not permission to race.

When a loaded slot is about to be replaced, the writer compares the bounded
source bytes with the fingerprint captured by the successful load or save. A
concurrent change causes a conflict diagnostic and preserves both the live
session and the changed file. The player may reload or choose Save As. A
replacement that succeeded but whose directory sync failed remains a reported
durability error and does not mark the session clean.

The catalog re-reads the selected file for activation and never loads cached
header bytes. Continue examines candidates in catalog order and fully
validates them until one succeeds. The first currently valid candidate is the
last successfully validated local profile. Missing, changed, corrupt, or
incompatible newer candidates remain visible with reasons and do not block an
older valid candidate from Continue.

## New Game

Opening New constructs pending setup state and requests one full-range 64-bit
seed from an injectable operating-system entropy source. Merely redrawing,
resizing, navigating fields, or leaving and returning to a confirmation does
not request entropy again. `Reroll` is an explicit second request. A failed
entropy request leaves New open with a diagnostic and no partial career.

The pending choices are exactly:

- the visible editable universe seed;
- `ASSISTED` or `PILOT`, with Assisted selected and explained by default;
- `Guided onboarding: On | Skip`, with Guided selected and explained by
  default.

Seed edits must parse the complete unsigned 64-bit range canonically before
confirmation. The pending choices do not alter a loaded profile, catalog,
preferences, or generated world. Cancel discards all of them.

Confirm allocates an unused profile ID and save sequence, constructs and fully
validates the exact displayed recipe and authoritative choices, and commits
the new profile atomically before it replaces the title state. If construction,
validation, locking, or persistence fails, the title has no new universe and
New retains the pending values for correction or retry. New never overwrites
an existing slot and is disabled when the 64-profile bound is reached.

Skip confirmation is deliberately separate from general New confirmation.
It explains that tutorial contracts cannot later be synthesized as completed
missions, rewards, discoveries, visits, or world deltas. The authoritative
Guided/Skip representation and baseline knowledge remain owned by #143; this
contract only requires them to be stored in the profile and compared during
metadata validation.

## Continue and Load

Continue is enabled only when the current catalog scan finds at least one
fully validated candidate. It names the selected profile in its explanation.
When no candidate validates, it is disabled and distinguishes an empty catalog
from missing storage, overflow, corrupt files, incompatible formats, and
unreadable candidates.

Activating Continue re-reads and validates the chosen file into temporary
state before constructing the new live session. If it changed or stopped
validating after the title scan, activation reports the failure, refreshes the
catalog, and leaves the title state intact. It does not silently fall through
to a different profile after the player activates the named candidate.

Load shows all bounded canonical candidates in stable catalog order, including
non-activatable rows with concise reasons. Selection, scrolling, resize, and
cancel do not read a candidate into live state. Activation re-reads and fully
validates the selected profile, then swaps the session only after hydration
succeeds. A failure returns to the browser at the same logical selection with
the prior live session byte-for-byte authoritative.

## Save, Save As, and dirty state

A catalog session remembers its slot ID, the exact source-byte fingerprint,
and its last successful save sequence outside authoritative world state.
`Save` atomically replaces that same slot after concurrency and full-document
validation. `Save As` allocates a new slot and never overwrites another
profile. Overwrite of a different existing profile is not part of version 1;
if later added, it requires a dialog naming both source and destination.

There is no autosave or checkpoint ring in version 1. A newly confirmed,
loaded, or successfully saved session is clean. The first authoritative tick,
command, contract transition, discovery, world delta, or other saved-state
change marks it dirty. Presentation frames, menu focus, terminal resize,
settings, benchmark results, and dismissed explanatory prompts do not.

Every currently valid docked, origin-flight, jump-spool, system-flight,
planetary-flight, return, and turned-in format-14 phase is saveable when its
complete projected document validates. Setup screens, confirmation dialogs,
and menus are not gameplay phases and are not saved. If projection or
validation fails, Save is disabled with the precise stable reason; it never
writes a partial representation.

A successful atomic write followed by successful directory sync updates the
slot fingerprint and sequence and clears dirty state. Any failure or cancel
preserves dirty state, the previous last-successful metadata, and the previous
destination file. Save errors never force a load, title transition, or exit.

## Destructive-transition truth table

| Action | Clean session | Dirty session | Failure or cancel |
| --- | --- | --- | --- |
| New from title | Open pending setup | Not reachable with a live session | Title and catalog unchanged |
| Continue from title | Validate and enter named profile | Not reachable with a live session | Remain at title and refresh reason |
| Load from title | Validate and enter selection | Not reachable with a live session | Remain in browser |
| Load from pause | Validate, then replace | Confirm discard first; then validate and replace | Prior session remains paused and authoritative |
| Save | Replace current slot | Replace current slot and clear dirty only on durable success | Session and prior file remain dirty/intact |
| Save As | Create unused slot | Create unused slot and clear dirty only on durable success | No slot is replaced or adopted |
| Settings | Open external preferences | Open external preferences | Return to invoking action; dirty state unchanged |
| Return to Title | Destroy session and rescan | Confirm discard first | Return to pause at the same selection |
| Exit from title | Exit process | Not reachable with a live session | Browsing or Escape does not exit |

Confirmation defaults to Cancel. Escape cancels the topmost dialog, then the
current nested screen; it does not accept a destructive action or exit the
process. A successful Load performs validation before replacement, regardless
of whether discard was already confirmed. Settings never satisfy or clear a
dirty authoritative session.

Ctrl-C, process kill, host loss, and power failure are external interruption
paths, not confirmed menu exits. Without autosave they may lose dirty in-memory
progress, but atomic file guarantees still protect the last durable profile.

## CLI precedence

Explicit CLI profile options select an external-path session and bypass the
local catalog for that run:

| CLI form | Behavior |
| --- | --- |
| `--new-game-seed N` | Uses exactly `N`, requests no OS entropy, and constructs the external new profile. |
| `--load PATH` | Fully validates `PATH` before terminal startup; no catalog entry becomes Continue. |
| `--save PATH` | Writes `PATH` only after a clean application exit; it does not update a catalog slot or sequence. |
| `--load A --save A` | Explicit in-place external save after successful load and clean exit. |
| `--load A --save B` | Explicit external Save As; `A` is never overwritten. |

`--load` and `--new-game-seed` remain mutually exclusive. Save-profile options
remain interactive-only and incompatible with benchmark, capture, and
acceptance modes as currently documented. During an external-path session,
catalog Save and Save As actions are disabled with a CLI-override explanation;
the clean-exit destination is the only implicit write. A failed load never
starts the terminal, and a failed clean-exit save returns failure without
altering catalog state.

Render, driver, viewport, audio, and later control CLI options override only
external preferences. They do not enter profile metadata or cause preference
files to be rewritten unless an explicit Settings Apply action later requests
that write.

## Implementation ownership

- #30 owns the separate versioned preference file.
- #143 owns authoritative Guided/Skip state and starting knowledge.
- #137 consumes this contract for New, Continue, Load, Settings, and Exit.
- #136 consumes it for Save, Load, Settings, Title, and dirty confirmations.
- #135 owns the shared Settings screen without entering deterministic saves.

None of these boundaries requires file dialogs, filesystem discovery, or save
policy in TermForge. Apsis Drift may add focused profile types after two menu
flows consume this contract; it must not extract a generic document database
or application framework in advance.

## Required validation matrix

The consuming implementation must cover empty and 64-entry catalogs, reject a
65th canonical candidate before presentation, and test the minimum and maximum
profile IDs, save sequences, seeds, ticks, paths, and metadata lengths. It must
also cover duplicate IDs/sequences, stale or missing files, symlinks, foreign
names, lock contention, concurrent replacement, interrupted atomic writes, and
directory-sync failure.

Header tests must reject duplicate keys, wrong JSON types, unknown enums,
invalid chapter/state combinations, control characters, and summary/body
mismatches. Full activation tests must retain the save-schema coverage for
unsupported formats, generator and identity mismatches, non-finite flight
state, illegal mission/travel combinations, and buffer bounds. Every failure
must leave the active session, dirty state, last-successful metadata, and prior
files unchanged before Kitty or ANSI visual smoke checks are considered.
