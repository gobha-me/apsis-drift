# Deterministic Terrain Tile Compatibility

Terrain tile generator version 1 turns a `PlanetDescriptor` and the existing
cube-sphere `TerrainTileKey` into local-flight height and color samples. It is
application-owned, performs no terminal work, and does not replace the current
v0.2 flyover terrain until the later Planetfall presentation handoff.

## Tile identity and samples

A key contains the planet ID, cube face, LOD, and unsigned tile coordinates
defined by the [coordinate contract](COORDINATE_SYSTEM.md). Every tile contains
65 by 65 samples: 64 intervals plus the shared sample on each positive edge.
Elevations are signed integer metres relative to the descriptor's spherical
reference surface; zero is generated sea level. Colors are the descriptor's
application-owned RGB palette rather than TermForge pixels.

The generator rejects malformed planet descriptors, another planet's key,
unknown cube faces, LODs above 16, out-of-range tile indices, and out-of-range
sample coordinates with typed errors.

## Version 1 generation

The descriptor's permanent `terrain` stream is the parent of two permanent
terrain-generation streams:

| Stream | Ordinal |
| --- | ---: |
| `shape` | 1 |
| `detail` | 2 |

Both children use the existing version 1 `SeedDomain::terrain` derivation.
Generation uses fixed-width integer hashing and fixed-point interpolation; it
does not consume mutable random state or use standard-library distributions.
Terrain character selects fixed amplitude and octave weights. Water coverage
sets sea level, and elevation selects colors from the planet palette.

Each address maps to an integer coordinate on one canonical cube lattice at
maximum LOD. Adjacent face bases map a physical edge or corner to the same
signed coordinate triple. Coarser samples are exact subsets of that lattice,
so same-LOD neighbors, cube-face neighbors, and aligned samples across LODs
produce identical height and color values without copying border data.

Generator version, tile dimensions, stream ordinals, cube mapping, hash and
fixed-point algorithms, octave constants, terrain parameters, sea-level
mapping, palette interpolation, sample order, and checksum encoding are
generated-world compatibility data. An incompatible change requires a new
generator version.

## Cache contract

`TerrainTileCache` is an application-owned deterministic LRU cache. Its default
capacity is 64 tiles; zero capacity is rejected. A hit returns the same shared
immutable tile and refreshes recency. A key already resident for a conflicting
descriptor is rejected rather than returning stale terrain. A miss generates
before changing cache state, then inserts at the most-recently-used end and
evicts at most one least recently used entry. External shared references may
keep an evicted immutable tile alive, but the cache itself never retains more
entries than its capacity.
