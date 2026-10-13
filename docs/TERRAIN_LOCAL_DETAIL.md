# Explicit physical local-detail terrain

Issue [#211](https://github.com/gobha-me/apsis-drift/issues/211) adds an explicitly
selected C++ point and tile sampler. It does not select a new recipe for saved
worlds, native streaming or landing/contact. Those consumers retain their
existing terrain owner and policy until a separate versioned integration.
The [planetary scale contract](PLANETARY_SCALE_CONTRACT.md) defines physical
fidelity targets; [#212](https://github.com/gobha-me/apsis-drift/issues/212)
owns recognizable terrain presentation.

## Selection and ownership

`TerrainLocalDetailSampler::create()` accepts a complete `PhysicalLocalSystem`,
a `BodyId`, an explicit `TerrainLocalDetailRecipe` and the caller's bounded
`TerrainTileCache`. Construction authenticates the selected physical catalog
and body, then retains an immutable owner copy. A matching numeric PlanetId or
seed alone is insufficient: the authored Origin home and its procedural
namesake are distinct descriptor owners. See [physical catalogs](PHYSICAL_LOCAL_SYSTEM.md)
and [body identity](BODY_EPHEMERIS.md).

Unknown recipe versions, unsupported or forged owners, and unsupported or
nonplanet body identities refuse with typed errors. Recipe selection never
rewrites generator defaults, saves, source tiles or descriptor metadata.

| Recipe | Height and color | Slope output |
| --- | --- | --- |
| `1`, `kLegacyPhysicalTerrainRecipe` | Unchanged legacy integer/quantized surface sample at the requested output LOD; zero local offset | Absent |
| `2`, `kLocalDetailTerrainRecipe` | Continuous binary64 interpolation of generator1 source-LOD8 heights plus independent physical local relief; unchanged legacy quantized color sampling at source LOD8 | Finite-scale tangent slope estimate |

The sample separates `macro_elevation_metres`, `local_offset_metres` and their
sum, `elevation_metres`. Elevation is relative to the descriptor's spherical
datum; it is not camera altitude, ground clearance or a contact certificate.
Recipe2 output LOD selects query locations, never a different underlying field.

## Recipe2 field

The macro field bilinearly interpolates the four existing generator1 source-LOD8
integer vertices in binary64. It does not round the physical interpolant back
to integer metres. Legacy source samples, palettes, cache keys and checksums
remain unchanged. Color retains the existing quantized palette sampling and
does not add a biome or material-generation rule.

Local relief is evaluated in planet-fixed Cartesian metres on the reference
sphere. It uses three quintic-interpolated value-noise fields with cell spacings
of **2048, 768 and 320 metres**. These are lattice spacings, not a guarantee
that every generated feature has that width or periodic wavelength.

Normalize the direction by its largest absolute component, then its Euclidean
length; canonicalize zero components to positive zero. Multiply the unit
components by the descriptor radius in metres. Divide each Cartesian component
by the selected spacing, use `floor` for the cell index and retain the fractional
part. The selected interpolation expressions are:

```text
lerp(a, b, t) = a + (b - a) * t
quintic(t)   = t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
macro       = lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), ty)
```

Noise interpolates the eight Cartesian lattice values in X, then Y, then Z,
using the quintic fractions. Lattice values use fixed unsigned 64-bit mixing,
signed-coordinate conversion modulo 2^64 and axis-specific constants/rotations.
The final hash's upper 53 bits are divided by `4503599627370496.0`, then reduced
by 1 to give a value in `[-1, 1)`. The exact mixing constants and operation order
are retained in [the recipe implementation](../src/terrain_local_detail.cpp).

| Terrain character | 2048 m field amplitude | 768 m field amplitude | 320 m field amplitude |
| --- | ---: | ---: | ---: |
| Oceanic | 4 m | 1 m | 0.25 m |
| Plains | 8 m | 2 m | 0.5 m |
| Rugged | 32 m | 10 m | 3 m |
| Alpine | 48 m | 16 m | 5 m |
| Volcanic | 40 m | 12 m | 4 m |

The permanent terrain-generation stream `local_relief = 3` is independent of
the existing `shape = 1` and `detail = 2`. Derive its seed with the existing
[version 1 seed derivation](SEED_DERIVATION.md), then derive permanent octave
children in `SeedDomain::terrain` with ordinals 1, 2 and 3. Queries consume no
mutable random state. Tile face, query order, output LOD, observer and cache
residency do not select another relief stream.

Accumulate `amplitude * noise` in listed spacing order, starting from zero;
add the final local offset to the continuous macro height without quantization.

The selected binary64 expression order belongs to recipe2. Source-specific
floating-point contraction is disabled for this implementation; legacy terrain
compilation and integer generation remain unchanged. Paired compiler checks
do not establish universal cross-host/libm replay; the existing numerical
portability limitation remains separate.

## Slope and tile queries

Recipe2 uses six height evaluations for a centered global Cartesian stencil:
offset the reference-sphere point by plus/minus **32 metres** along each of its
three axes, resolve each offset direction through the same scalar height field,
then project the resulting gradient onto the reference sphere's tangent plane.
`tangent_slope_estimate` is a finite-scale slope input. It is neither an analytic
derivative nor a selected mesh facet/contact normal; it cannot certify support
or replace the owning landing triangles. Recipe1 supplies no slope estimate.

`fill_tile()` uses the existing dyadic cube-sphere addresses and a fixed
**65 by 65** row-major sample layout: 64 intervals plus shared positive-edge
samples. See [terrain tiles](TERRAIN_TILES.md) and [coordinates](COORDINATE_SYSTEM.md).
The caller provides at least 4225 `TerrainLocalDetailSample` entries. Unsupported
dimensions, malformed tile identity/face/LOD/indices, short output spans,
nonfinite or degenerate directions, invalid macro inputs and unsafe arithmetic
refuse. Results are staged before publication: any refusal leaves the entire
caller buffer unchanged, including a trailing unused region; success writes
only the fixed tile region. This output guarantee does not promise rollback
of ordinary cache lookups or recency updates.

The caller's cache retains generator1 macro tiles only. Its entry count stays
bounded by its existing capacity; local-detail outputs do not enter it. The
sampler does not pin all macro tiles for an entire output tile. Query-local
references can finish a lookup without creating an unbounded retained tile set.
The cache must outlive its sampler. Staged output storage has a fixed sample
count rather than growing with world size.

## Verification and consumer limits

Provider qualification checks malformed ownership and numerical boundaries
before any visual consumer. The required evidence includes unchanged legacy
stream/height/color/checksum vectors, selected-recipe point/tile equivalence,
shared cube edges/corners, aligned cross-LOD locations, seam-limit continuity
and an independent finite-scale slope oracle. Exact selected-recipe fixtures
and measured results belong to executable tests and their recorded reports;
the amplitude table alone is not a measured crest-to-trough result.

For a chosen physical feature, record its width, actual surface sample spacing
and crest-to-trough relief. The scale contract's initial targets are at least
four samples across that width and four output pixels across its projection.
Point-provider measurements can establish physical sampling; projected size
requires a real consumer's camera, geometry and output frame. Neither a
shader-only ridge nor a color checksum proves geometric relief.

Native presentation, saved recipe selection, contact geometry, facet normals
and landing/support adoption remain separate work. This provider does not
promote the experimental Godot relief study, certify GPU appearance, establish
frame rate or imply a whole-game memory ceiling. Report bounded provider work,
storage and CPU timings separately from renderer, display and asset costs.

## Provider checkpoint

The paired GCC/Clang fixture selects a real alpine body from procedural system
seed `3`: BodyId `14720432346406072953`, reference radius 6,771,000 m. Three fixed
513-point profiles use approximately 32 m physical spacing. The selected crest
is bracketed by troughs 3,968 m apart: 125 samples / 124 intervals, with 64.8387 m
prominence above the higher trough. Its local-detail offsets are 54.0253 m at
the crest, -41.4953 m at the left trough and -7.4187 m at the right trough. This
is measured physical relief, rather than a width inferred from noise spacing.
The four-sample target passes; projected-pixel and rendered-view qualification
remain #212.

The sample occupies 64 bytes in both checked builds. Fixed tile staging and the
caller tile each occupy 270,400 bytes. The measured LOD14 tile uses six cached
macro entries of a twelve-entry capacity; its reused-cache CPU times were
approximately 9.7 ms (GCC) and 7.6 ms (Clang). These are measurements of that
provider case, not thresholds, cold-generation bounds or renderer frame times.
Run `apsis-drift-terrain-local-detail-tests --measure` from a completed build
to record the current result and machine-dependent CPU time.

Each recipe2 point makes seven scalar-height evaluations and at most fourteen
legacy-cache lookups. A miss can generate a complete 65x65 macro tile. Coarse
output grids may touch many distinct source tiles and cost substantially more
CPU than the measured near-surface tile; cache entry count and staging storage
stay bounded. Consumer scheduling and reuse must account for those misses.
