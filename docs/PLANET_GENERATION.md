# Planet Descriptor Compatibility

A planet descriptor is the compact immutable identity shared by future
orbital, atmospheric, and local-terrain representations. Version 1 accepts an
explicit 64-bit planet seed and produces only application-owned values. It
does not render, read terminal capabilities, or consume mutable global random
state.

## Versioned fields and bounds

| Field | Representation | Version 1 bounds |
| --- | --- | ---: |
| Planet identity | original planet seed | full unsigned 64-bit range |
| Display name | generated ASCII | three fixed-table syllables |
| Radius | unsigned kilometres | 2,500–9,000 km |
| Surface gravity | unsigned milli-g | 350–1,800 milli-g |
| Atmospheric pressure | unsigned millibars | 0–2,500 mbar |
| Water coverage | unsigned basis points | 0–10,000 bp |

Atmosphere is classified as `airless`, `tenuous`, `temperate`, or `dense`.
Terrain character is classified as `oceanic`, `plains`, `rugged`, `alpine`, or
`volcanic`. A palette is selected from the `verdant`, `arid`, `glacial`,
`volcanic`, and `alien` families and contains atmosphere, deep-water,
shallow-water, lowland, highland, and peak RGB colors.

The integer units are the compatibility representation. A later simulation or
renderer may convert them to floating point for calculations, but floating
point is not part of descriptor generation or diagnostics.

## Named streams and generator

Each subsystem seed is derived from the supplied planet seed with the existing
version 1 `SeedDomain::planet` contract and one permanent ordinal:

| Stream | Ordinal |
| --- | ---: |
| `name` | 1 |
| `physical` | 2 |
| `atmosphere` | 3 |
| `terrain` | 4 |
| `hydrology` | 5 |
| `palette` | 6 |

Every stream uses SplitMix64. For each value, add
`0x9e3779b97f4a7c15` to the 64-bit state, apply the standard shifts and
multipliers `0xbf58476d1ce4e5b9` and `0x94d049bb133111eb`, then apply the final
right-shift XOR. Arithmetic wraps modulo 2^64. Bounded values use rejection
sampling before the remainder operation so mapping has no modulo bias.

Name selection consumes one value for each of the start, middle, and ending
tables. Physical generation consumes radius and then gravity. Atmosphere
consumes its class roll and, unless airless, its pressure. Terrain, hydrology,
and palette each consume one value. The checked-in golden tests are the
executable authority for exact tables, palette colors, and generated results.

## Stable diagnostics

`planet_descriptor_json()` emits schema version 1 with a fixed field order.
The planet seed is a decimal JSON string and the authoritative ID is
`planet-` followed by exactly 16 lowercase hexadecimal digits, avoiding loss
in JSON consumers that cannot represent every 64-bit integer. Measurements
remain integers, enum values use the lowercase names above, and colors use
lowercase `#rrggbb` strings.

The generator version, stream ordinals, SplitMix64 algorithm, draw order,
bounds, name tables, classification thresholds, palette definitions, and JSON
schema are generated-world compatibility data. Do not change them in place.
A future incompatible generator or diagnostic representation must receive a
new version while saves retain the version needed to reconstruct their world.

The permanent planet child-stream ordinal `7` is reserved for celestial
geometry. Local-sun generator version 1 consumes that stream independently;
adding it does not change descriptor, terrain, station, or signal draws.
