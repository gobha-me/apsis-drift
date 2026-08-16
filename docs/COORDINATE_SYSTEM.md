# Coordinate and Terrain LOD Contract

This contract gives orbital presentation, atmospheric approach, local flight,
and generated terrain tiles one application-owned spatial vocabulary. Version
1 uses double-precision metres for positions and radians for angles. These
runtime floating-point values are not save or generator serialization formats;
versioned saves must encode them according to their own explicit schema.

## Coordinate frames

All frames are right-handed:

- **System inertial:** origin at the system barycenter, `+z` toward the
  system's angular-momentum north, and `+x` along the epoch reference ray.
  Positions are metres. Planet ephemeris state will own the time-dependent
  rigid transform into a planet frame; this contract does not invent orbital
  or rotation state before those systems exist.
- **Planet fixed:** origin at the planet center, `+z` through spin north, `+x`
  through latitude 0 and longitude 0 at the reference epoch, and `+y` through
  latitude 0 and longitude +90 degrees east. The reference surface is the
  sphere described by `PlanetDescriptor::radius`.
- **Local flight:** an east/north/up tangent frame rooted at an explicit
  geodetic point. Local `+x` is east, `+y` is north, and `+z` is outward. This
  refines the existing landscape convention of two terrain-plane axes and
  positive `z` up without changing the current flyover state in this session.

Geodetic latitude is in `[-pi/2, pi/2]`. Longitude is east-positive and
canonicalized to `[-pi, pi)`, so `+pi` and `-pi` identify the same antimeridian.
Inverse conversion assigns longitude zero at either exact pole. Altitude is
radial distance from the spherical reference surface; the planet center and
altitudes at or below `-radius` are invalid.

For radius `R`, latitude `lat`, canonical longitude `lon`, and altitude `h`:

```text
x = (R + h) cos(lat) cos(lon)
y = (R + h) cos(lat) sin(lon)
z = (R + h) sin(lat)
```

Examples on planet seed 42 (`R = 5,499,000 m`) are:

| Geodetic position | Planet-fixed position |
| --- | --- |
| `lat=0, lon=0, h=0` | `(5,499,000, 0, 0) m` |
| `lat=0, lon=pi/2, h=1,000` | `(0, 5,500,000, 0) m` within floating error |
| `lat=pi/2, lon=any, h=0` | `(0, 0, 5,499,000) m` within floating error |
| `lat=0, lon=+pi, h=0` | `(-5,499,000, 0, 0) m`, canonical longitude `-pi` |

Away from pole longitude singularities, executable round trips must remain
within `1e-12` radians and `1e-6` metres. Local ENU round trips use the same
metre tolerance. Callers must rebase local frames before accumulated tangent
plane error becomes meaningful to their gameplay system.

## Cube-sphere terrain addresses

Terrain uses a six-face cube projected onto the sphere. This makes the poles
ordinary face centers and prevents the geographic antimeridian from becoming
a special terrain seam. A tile key contains the planet ID, cube face, LOD, and
unsigned `x/y` indices. An address adds `u/v` coordinates in `[0,1]` within the
tile.

Each face maps `normalize(N + face_u U + face_v V)` with face coordinates in
`[-1,1]`:

| Face | Normal `N` | `U` | `V` |
| --- | --- | --- | --- |
| `+x` | `+x` | `+y` | `+z` |
| `-x` | `-x` | `-y` | `+z` |
| `+y` | `+y` | `-x` | `+z` |
| `-y` | `-y` | `+x` | `+z` |
| `+z` | `+z` | `+y` | `-x` |
| `-z` | `-z` | `+y` | `+x` |

The largest absolute planet-fixed component selects the face. Equal
magnitudes resolve in `x`, then `y`, then `z` order; the component sign selects
the positive or negative face. This gives every seam and corner one canonical
forward address. Noncanonical adjacent-face edge addresses remain valid and
inverse-map to the same direction.

LOD `L` has `2^L` tiles per face axis. Tile intervals are half-open: an exact
internal boundary belongs to the higher index. A face coordinate of exactly
one belongs to the final tile and retains within-tile coordinate `1`, allowing
shared outer edges to be sampled exactly. LOD is limited to 0 through 16, so
indices and subdivision counts remain bounded.

## Altitude-driven LOD bands

The nominal great-circle span of one tile is:

```text
span(L) = pi R / (2 * 2^L)
```

For non-negative altitude `h`, select the smallest LOD whose span is no more
than eight times `max(h, 32 m)`, clamped to LOD 16. Equivalently, descend to a
finer level when altitude drops below `span(L) / 8`. Exact thresholds remain
on the coarser level. The 32 metre floor prevents unbounded refinement during
terrain flight, while the planet-radius term gives every generated world
comparable angular coverage.

This policy chooses deterministic data coverage, not renderer quality. A
renderer may blend already-selected levels or choose a presentation cadence,
but those choices cannot change tile identity or generated samples.

## Invalid and boundary behavior

Conversions return a typed error for non-finite values, invalid descriptor
radii, latitude or altitude outside the contract, the planet center, malformed
local frames, unknown faces, LODs above 16, out-of-range tile indices, and
within-tile coordinates outside `[0,1]`. No conversion mutates caller state.

Executable tests cover both poles, antimeridian aliases, all face centers,
canonical seams and corners, exact internal tile boundaries, every LOD
threshold, minimum and maximum planet radii, and invalid inputs.
