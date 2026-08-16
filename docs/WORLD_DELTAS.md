# Sparse Generated-World Journal

Apsis Drift regenerates immutable terrain and surface-signal recipes from the
saved universe recipe. Mutable state is layered onto that result through a
bounded journal; terrain tiles, signal placement data, and cache contents are
never copied into the save.

## Stable object keys

Version 1 journal application recognizes surface-signal keys in the canonical
form `signal-` followed by exactly sixteen lowercase hexadecimal digits. This
is the same stable identity emitted by the signal generator. Malformed keys
are rejected while recording or importing a journal. A canonical key that is
not present in the regenerated active-planet catalog fails application as an
unknown object rather than partially modifying the population.

## State and compaction

Each journal entry gives one generated object its current exclusive state:

- `discovered` keeps the object active and records that it is known;
- `collected`, `completed`, and `removed` are terminal states and make the
  object inactive for later interaction while preserving its generated
  identity and historical state.

At most one compact entry remains for each object key. The entry with the
greatest simulation tick wins. When two entries for the same key have the same
tick, the later source entry wins. Stale records are idempotent no-ops. Compact
output is ordered by tick and then object key, so the same input produces the
same save order. Raw input and compact state both remain subject to the save
format's 16,384-entry bound.

## Regeneration order

Loading and world reconstruction follow one transaction:

1. validate the saved recipe and regenerate the active planet;
2. regenerate its immutable surface-signal catalog through the application-owned
   terrain cache;
3. import and compact the saved journal;
4. project every known delta onto the regenerated catalog;
5. expose the complete projection only if every entry applied successfully.

Cache capacity, eviction, and regeneration cost cannot change signal identity
or journal state. Version 1 rejects unknown delta kinds in the save codec and
again at the journal boundary; supporting a new kind requires an explicit
compatibility decision rather than silently reviving or duplicating content.
