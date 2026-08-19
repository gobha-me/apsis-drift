# Deterministic Signal Collection Contract

Signal collection is application-owned mutable simulation state layered over
the immutable surface-signal catalog and the derived scanner solution. It does
not modify signal generation, consume a random stream, or depend on render or
terminal cadence.

## Tick-driven states

The state machine advances once per consecutive 120 Hz simulation tick. A
selected target remains in `approach` until the scanner reports `reached`,
which uses the existing inclusive 1,000 metre radius.

| State | Consecutive in-range ticks |
| --- | ---: |
| `in_range` acquisition | 1–60 |
| `scanning` | 61–419 |
| `complete` | 420 |

The half-second acquisition and three-second scan dwell therefore complete on
the 420th consecutive in-range tick. Duplicate or skipped tick updates are
rejected, and tick overflow cannot wrap collection time.

Leaving the reached radius, losing selection, or changing targets aborts the
current attempt and resets its progress to zero. The abort is surfaced for one
tick; a selected target that returns in range begins a fresh acquisition on
the next update. Partial scan progress is intentionally not save state.

## Persistent completion

Completion records one `collected` delta using the canonical signal object key
and the completion tick. The journal and collection state commit together; a
journal validation or capacity failure leaves both unchanged. Repeated updates
after completion do not emit another delta.

When a regenerated save journal already marks the target `collected`,
`completed`, or `removed`, collection immediately projects as complete without
emitting a new entry. A `discovered` entry remains active and may be collected.
This keeps unique generated targets from being collected twice without adding
partial runtime progress to the current versioned save format.

## Cockpit presentation

The cell cockpit presents fixed-width textual cues for acquisition percentage,
scan percentage, lost scans, invalid state, and collection. The communications
region repeats the current instruction or outcome in text, so neither Kitty nor
truecolor ANSI relies on color to communicate progress.

## Deterministic acceptance

The existing `--signal-navigation-acceptance` route now continues through the
collection dwell. It reaches target `signal-945eaa623b2b8497` at tick `1072`,
releases forward thrust, completes at tick `1491`, and emits one `collected`
delta. Both supported presentations report authoritative flight checksum
`17407832030238464473`; framebuffer and timing fields remain presentation-only
diagnostics.
