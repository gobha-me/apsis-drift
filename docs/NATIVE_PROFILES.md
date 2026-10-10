# Native Freedom save catalog

New Game starts an unsaved Freedom journey. Save As creates its first slot;
missing or full storage does not prevent station movement or starter flight.

The paused root offers **Resume, Save, Save As, Load, Settings, Title**.
Save As asks before creating a new slot; cancel changes nothing. After a durable
write, that slot becomes active and the current world becomes clean. Save
replaces only the active slot. The former slot survives Save As. Export save
file remains a separate explicit-path workflow.

Load lists local slots. Invalid or legacy Guided/Skip careers remain visible
with an unavailable reason; Freedom does not reinterpret them. An empty catalog
has no Continue target. Title Continue chooses the greatest valid save sequence,
not a timestamp or the current seed. Load revalidates the selected snapshot and
stages the complete C++ world and presentation before replacing anything.

Load and Title ask before discarding dirty progress. Cancel retains the current
paused view and focus. A clean catalog replacement does not need a discard warning; explicit file
workflows retain their existing confirmation.
Catalog browsing, save confirmation and Settings suspend gameplay; leaving them
never automatically resumes movement. Resume still requires focus and neutral
controls. Quitting does not autosave.

## Storage and compatibility

The native path shares the directory, canonical sixteen-digit filenames,
64-candidate bound, identity allocation and write lock defined in
[the menu/profile contract](MENU_AND_PROFILE_CONTRACT.md). Both native and legacy
files consume slots and participate in durable save sequence allocation. More
than 64 canonical regular files disables the catalog rather than exposing an
arbitrary subset. Reads do not create storage; directory and file symlinks are
unavailable. Owned directories and files use user-only permissions.

A native profile adds a version 1 root `profile` header with `kind: freedom`, a
positive decimal-string ID and save sequence, and a bounded summary: full
64-bit seed/tick, native save format and phase. Its header must match its
canonical filename and the validated authoritative document. Unknown fields,
wrong types, duplicate keys, overflow, inconsistent summaries and incompatible
versions fail closed. Existing save formats and generation versions do not
change. The header never enters world state, random streams or checksums.

The historical docked shell and pending-recovery overlay show explicit
unavailable reasons for ordinary catalog actions; their existing workflows
remain separate. Typed provider saves still preserve these records exactly.

The supported phase summaries are historical station, station walking,
boarding, attached craft, flight, landed craft, suited surface walking, jump
spool/transit and recorded loss. These are listing hints; the unchanged typed
C++ save document owns the actual phase, resources, actor and recovery state.

Session metadata records the active slot and last successful canonical world
bytes outside the simulation. Failed writes adopt nothing. Save and Load refuse
if a selected source changed since scanning/adoption. Writers hold the catalog
lock and use the existing temporary-file, sync, atomic replacement and directory
sync policy. As documented in the shared contract, a directory-sync failure
*after* replacement can leave new bytes visible without confirming durability;
the session preserves its prior dirty state and does not adopt the proposed metadata.

Explicit command-line or file-open sessions do not silently enter the catalog.
Their catalog Save/Save As actions are disabled with a reason; their existing
file export and Continue workflow remains available. No autosave, overwrite of
another slot, deletion browser or cloud synchronization is introduced.

## Validation scope

`native-profile-contract` covers header/body identity, malformed input, mixed
catalog ordering, stale sources, locks, symlinks, full/overflow catalogs and
transactional session metadata. The native `native_profile` group exercises the
real C++ provider, staged station views and isolated storage. Compiler and
headless results are distinct from GPU, controller hardware and manual visual
qualification.
