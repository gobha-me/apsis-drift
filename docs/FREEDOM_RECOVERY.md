# Standard Freedom recovery

An explicitly recorded craft loss can continue at a validated safe station with
one fresh Wayfarer starter, full baseline flight fuel and three jump charges.
The universe seed, clock, discoveries, collected-world changes and chart evidence
are retained. There is no fee, mission, insurance or wreck-retrieval requirement.
This is the approved non-permadeath baseline in
[#247](https://github.com/gobha-me/apsis-drift/issues/247).

The application owns the loss event, its craft identity, source tick, source-state
checksum and cause. Recoverable destruction reports that any wreck recovery is a
separate outcome; irrecoverable destruction has no reachable wreck. Neither case
creates a playable second ship or returns the retired craft's hardware. Current
fixtures exercise these declared outcomes after an actual neighboring-system
jump. They do not implement collision deaths, black holes or wreck generation.

Fuel exhaustion, invalid controls, a corrupt save, an asset-readiness failure and
technical exceptions do not create fictional destruction. Living-pilot fuel/tow/
evacuation needs an actual reachable capable responder; it is not this provider.
A voluntary stranded-but-alive recovery trigger remains unselected. Optional
permanent pilot loss/succession remains
[#103](https://github.com/gobha-me/apsis-drift/issues/103).

## Station and native continuation

The current bounded universe has one qualified replacement assembly: Origin
Station D1 with its real moving-port constraint, service and walker entry. A
supported remembered checkpoint selects that station. A missing or stale station/
port reference uses an explicitly reported deterministic Origin fallback. Unknown
versions, zero identities, future checkpoint clocks and an unavailable physical
constraint refuse before changing the current owner. No station is invented.

Pending loss freezes ordinary flight, walking, boarding, port, service and jump
commands. Save As preserves that pending state. Native Continue presents the
cause, safe station and retained-state explanation with an explicit continuation
button. Replacement first stages its current authored skin and terrain stream;
only a ready view can commit it. A failed asset load retains the pending loss and
allows retry. The old scene is retired when the new walker/ship scene commits.
The same mesh recipe is a skin for a new individual vessel, not recovered hardware.

Completion is idempotent. Repeating the matching loss event or completion cannot
mint another craft, rewind movement or refill resources. A later distinct loss
retires that replacement and advances its lineage once. A new jump selection
belongs to the replacement; its old ship's committed jump remains retired history.

## Persistence

Unselected origin saves retain exact format **17**, their seed-derived starter
identity and existing bytes. Selected origin lineage uses format **28**, rule
**1**, and a canonical nonzero decimal generation. Generation zero is the legacy
starter; subsequent IDs advance in a deterministic permutation of nonzero uint64
IDs rooted at that starter. The calculation excludes zero, detects exhaustion
before repeating an instance, and consumes no procedural random stream.

Recovery format **29**, state **1**, owns exactly one current format-25 knowledge/
resource/voyage or format-27 travel document. One latest immutable retired voyage
records the cause and source checksum. That snapshot cannot contain another
recovery wrapper and is never opened as another active owner or reachable wreck.
Its bounded history preserves the prior jump/crew/resource evidence without
recursively storing every previous recovery. The whole document retains the
existing 1 MiB byte and 64-level nesting limits.

Replacement uses explicit active-world ownership, preserves existing physical and
assistance recipes, clears the retired hold/attachment/crew/surface/jump operation,
and derives a fresh attached same-tick D1 body and walker. Resources use the
[existing provider](FREEDOM_RESOURCES.md), bound to the new individual craft.
Origin-only observation recipe 2 explicitly expands to the already approved
neighboring-body domain 3 without awarding observations. All existing evidence
and its original craft provenance survive. The selected knowledge recipe records
an explicit craft-lineage ceiling, so past craft evidence remains valid and future
instances cannot claim observations. New evidence after loss must belong to the
replacement. Frozen new jump requests explicitly bind that same lineage; old
requests and sampler inputs retain their original bytes and meaning.

Continue refuses a bare replacement subset without its recovery wrapper.
Malformed causes, identities, recipes, retained history, quantities, clocks,
duplicate keys, wide values and recursive/oversized wrappers fail before owner or
save-file replacement. Old valid saves do not opt into recovery on load or Save As.

## Validation

The C++ `freedom-recovery-contract` covers three universe seeds, both causes,
remembered/fallback stations, empty resources without invented death, before/
pending/completed saves, duplicate events, distinct later losses, lineage limits,
retained state, closed schemas, pending command refusal and actual replacement
walking. GCC and Clang must agree on canonical recovery traces.

With the prebuilt native extension and fixtures from
[development setup](DEVELOPMENT.md), run:

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --output-parent build-native-contracts --timeout 600 \
  --test native_recovery
```

The headless native test consumes actual C++ jump/propulsion/loss fixtures,
compares whole Save As bytes with independent continuation oracles, exercises
failed/ready asset staging and a fresh model scene, then walks, boards, departs and
commits another real jump. These checks qualify the declared recovery path; live
fatality detection, GPU appearance, manual controls, fun, equipment protection,
cargo, costs and time passage remain separate work or unselected policies.
