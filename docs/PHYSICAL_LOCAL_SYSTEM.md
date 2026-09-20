# Explicit physical circular catalogs

The `physical_circular` recipe family, version **1**, is an opt-in C++ world
context for new native universes. It is not legacy local-system generator v2.
`generate_local_system`, `generate_origin_system`, their global version
constants, legacy save16 generation and the current native demonstration retain
their original behavior. No native bootstrap, save migration, rotating-frame
adapter or automatic time acceleration is enabled by this provider.

## Identity and generation

`PhysicalLocalSystem` wraps an existing-shaped catalog with physical periods,
its physical-family version, source catalog generator and analytic ephemeris
versions, stellar mass and GM. An origin-home context additionally retains its
universe seed. The public physical generation, validation, lookup and ephemeris
APIs require this distinct type. The embedded catalog fails legacy validation:
its first physical period is greater than 30 hours, while legacy ordinal zero
has a 6–8-hour period. It cannot be unwrapped and accepted by existing rotation,
contact or rigid-state providers as a legacy world.

The dependencies of physical-family v1 are seed derivation1, local catalog1,
planet descriptor1, authored origin-home1, origin-station2 (the origin system's
seed/ownership derivation) and analytic ephemeris1. Validation
regenerates the complete physical context from its selected seed and kind, and
compares every field. Unknown versions, forged owners, altered GM/periods/star
or planet metadata, missing planets and substituted procedural home descriptors
are refused. Merely matching SystemId, PlanetId or seed does not grant ownership.

The source catalog supplies unchanged IDs, names, planet descriptors, radii,
epoch phases and orbit-plane orientation. The origin variant retains the exact
authored home descriptor, not its same-ID procedural base. Star radius,
temperature and color retain their old fields. They are **not** used to infer
mass and are not a stellar-evolution or thermal-consistency model.

Stellar mass uses an independent child of the existing star seed:
`derive_seed(star_seed, SeedDomain::star, 3)`. Streams1 (name) and2 (legacy
physical) remain untouched. One SplitMix64 step (increment then finalizer) is
mapped by `80 + value % 1521` to integer milli-solar masses, inclusive
**0.08–1.6 nominal solar masses**. This bounded technical recipe is not an
observational stellar-population distribution.

## Physical period and numerical contract

The nominal solar mass parameter is exactly
`132712440000 km³/s²`, from
[IAU 2015 Resolution B3](https://iauarchive.eso.org/static/resolutions/IAU2015_English.pdf).
Use of nominal GM avoids introducing a separately rounded gravitational
constant and kilogram mass. `GM = 132712440 * mass_millisolar` is an exact
bounded integer. A planet is treated as a negligible-mass test particle on its
existing circular orbit around the single star at the system origin. No
planet–planet perturbations, eccentricity or relativistic corrections are
implied.

For semi-major radius `a` in kilometres, the versioned binary64 operation order
is:

```
cube    = (a * a) * a
seconds = (2 * pi_binary64) * sqrt(cube / double(GM))
ticks   = round(120 * seconds)
```

Positive half-tick ties round upward. Integer arithmetic is never used to cube
the radius. The radius domain is 4–68 million km; the conservative accepted
period bounds are 13,000,000–4,200,000,000 ticks. All integer periods and reduced
cycle ticks are therefore exactly representable in binary64. The domain's
extreme ideal periods are approximately 1.263–395.757 days. Period rounding
introduces at most half a simulation tick apart from the qualified numerical
allowance; generated examples are checked with a higher-precision independent
oracle. This does not change orbital radii to habitable-zone distances or
promise an Earth-like year.

The stored integer period drives **both** phase and analytic velocity. Time is
the existing 120 Hz simulation clock, with `tick % period` before conversion.
Queries accept finite sub-tick fractions in `[0,1)` for presentation; simulation
queries use zero. Tick `UINT64_MAX` is refused; maximum minus one is tested.
There is no renderer-owned clock or independent lighting acceleration.

The actual position/velocity resolver shares the extracted existing circular
kernel: node/inclination orientation, phase arithmetic, nearest-metre positions
and 0.001 m/s velocities retain their expression order and quantization. Legacy
callers retain their original validation/error behavior. Only the new period
generation source disables floating-point contraction explicitly; the legacy
kernel's compilation policy is unchanged. Same-host GCC/Clang fixtures are
qualification evidence, not universal cross-libm determinism; #255 remains.

## Integration limits and next consumer

Native bootstrap must explicitly retain this family/version and universe owner
with the active session, derive descriptor metadata from that context, and
create fresh context-scoped terrain caches. It must not accept a physical
catalog via legacy snapshot-v1 or a legacy save16 tuple. The current rotation
and contact wrappers need explicit physical-context ownership before use with
this provider; seed-only reinterpretation is not a bridge.

Origin-station relative orbits remain their separately authored legacy
90–120-minute recipe. This provider has no physical station overload. A later
station recipe can derive its period from host gravity/radius without silently
rewriting legacy station identity. Mission-free starter resources, chart
grants, station spawning and saved new-universe workflows also remain their
existing issue owners.

Tests cover every wrapper version, ownership and physical fields; authored
home preservation; both mass endpoints; independent long-double period and
orbit-basis oracles; circular radius/tangency/GM residuals with quantization
budgets; period wrap and near-maximum ticks; malformed inputs; legacy rejection
and stream noninterference. Existing catalog, station, rotation and frame
handoff goldens remain separate unchanged regression gates.
