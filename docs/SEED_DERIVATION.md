# Seed Derivation Compatibility

Apsis Drift generation begins with an explicit 64-bit universe seed. The seed
comes from a supplied value, such as a future new-game option or a loaded save;
seed derivation does not obtain entropy, own a random generator, or consume
mutable random state.

Version 1 derives a child from three values:

- the 64-bit parent seed;
- an explicitly numbered 64-bit domain;
- a 64-bit ordinal identifying a sibling within that domain.

The public domains and their permanent version 1 identifiers are:

| Domain | Identifier |
| --- | ---: |
| `universe` | 1 |
| `system` | 2 |
| `planet` | 3 |
| `terrain` | 4 |
| `weather` | 5 |
| `settlement` | 6 |
| `encounter` | 7 |
| `star` | 8 |
| `orbit` | 9 |
| `mission` | 10 |
| `jump_alignment` | 11 |
| `navigation` | 12 |

## Version 1 algorithm

Derivation uses 64-bit FNV-1a with offset basis
`14695981039346656037` and prime `1099511628211`. Beginning at the offset
basis, feed this 44-byte record in order:

1. the 16 ASCII bytes `APSIS-DRIFT-SEED`;
2. derivation version `1` as an unsigned 32-bit integer;
3. the parent seed as an unsigned 64-bit integer;
4. the domain identifier as an unsigned 64-bit integer;
5. the ordinal as an unsigned 64-bit integer.

Every integer is fed least-significant byte first. For each byte, XOR it into
the hash and multiply the hash by the FNV prime modulo 2^64. The final hash is
the child seed; zero is a valid seed value.

For example, a caller can treat `Seed{42}` as the supplied universe seed,
derive system ordinal 0, derive planet ordinal 3 from that system, and then
derive the planet's terrain, weather, and encounter streams independently.
Deriving or consuming one stream cannot perturb any other stream.

The checked-in golden vectors in `test/test.cpp` are the executable authority
for exact results across compilers and hosts.

## Compatibility rules

The version, namespace bytes, field order, integer widths, byte order, FNV
constants, and existing domain identifiers are generated-world compatibility
data. None may change in place. A future algorithm must receive a new
derivation version, and saves must record both their universe seed and the
generator/derivation versions needed to reconstruct their world.

Adding a new explicitly numbered domain does not change results for existing
domains. A subsystem may use a deterministic PRNG after derivation, but its
algorithm and any state that must survive save/resume form a separate
compatibility contract.

The version 1 origin station uses existing domains without changing this
table: derive system ordinal `0` from the universe seed, then settlement
ordinal `0` from that origin-system seed. Its identity and onboarding boundary
are recorded in the
[Origin Station and New-Game Contract](ORIGIN_STATION.md). Adding or deriving
that child cannot perturb the planet, terrain, weather, or encounter streams.

The version 1 first intersystem route adds domains without renumbering any
existing input. It derives target system ordinal `1`, star ordinal `0`, planet
and independent orbit ordinal `0`, and mission ordinal `0` beneath the origin
station. The exact hierarchy and compatibility boundary are recorded in the
[Deterministic Intersystem Mission and Travel Contract](INTERSYSTEM_CONTRACT.md).
The target system expands those stable children into a bounded catalog using
additional named substreams documented in
[Local System Generation and Analytic Ephemeris](LOCAL_SYSTEM_GENERATION.md).

The version 1 bounded universe route adds `navigation=12`. It derives the
origin/first-target coordinate recipe independently from system ordinal one;
inspecting the route cannot perturb either local system or mutable knowledge.
The exact direction, distance, knowledge-redaction, and direct-travel boundary
are recorded in
[Discovered-Universe Navigation and Direct Travel](UNIVERSE_NAVIGATION.md).

The current v0.2 flyover continues to accept its existing 32-bit terrain seed
directly. Migrating that path is intentionally outside derivation version 1's
introduction so its acceptance scenario and historical checksums remain
unchanged.
