# Local System Generation and Analytic Ephemeris

Version 1 turns one target-system seed into an application-owned star, an
ordered planet catalog, and circular analytic orbits. It is deliberately a
bounded first-loop generator, not an arbitrary galaxy, an N-body simulation,
or a generic celestial engine.

## Stable catalog

The catalog contains between three and six planets. The count comes from the
named `system/catalog` child stream. Star identity uses `star/0`; planet and
orbit identity at catalog ordinal `n` use the independent `planet/n` and
`orbit/n` children. Planet ordinal zero is therefore the mission planet already
reserved by the intersystem contract. Catalog indices are useful for ordered
display but missions and lookups retain `SystemId` plus `PlanetId`.

Every planet embeds the existing immutable `PlanetDescriptor` generated from
its unchanged planet seed. Generating the catalog, looking up a body, or adding
a later ordinal cannot consume mutable random state or alter existing planet,
terrain, signal, station, or local-sun identity.

The star descriptor records its stable seed and ID, bounded ASCII name,
spectral class, temperature in kelvin, radius in kilometres, and RGB
presentation color. Version 1 generates temperatures from 2,800 through 7,500
kelvin and radii from 350,000 through 1,400,000 kilometres. Spectral class and
color are derived from temperature rather than consuming another random draw.

## Orbit parameters

Version 1 orbits are circular and system-centered. Each orbit records only
fixed-width compatibility fields:

- orbit seed, planet ID, and ordinal;
- radius in integer kilometres;
- period in authoritative 120 Hz ticks;
- epoch phase and ascending node as unsigned 32-bit fractions of one turn;
- inclination in signed microdegrees, bounded to plus or minus ten degrees.

Ordinal bands keep radii and periods strictly increasing. The innermost orbit
uses a radius of 4–8 million kilometres and a period of 6–8 hours. Each later
ordinal advances the radius band by 12 million kilometres and the period band
by 8 hours. These are readable game-system scales, not a claim of
astrophysical fidelity.

The catalog count, star name/physical fields, and each orbit's radius, period,
phase, and orientation use separate permanent child-stream ordinals. All
bounded selection uses the same rejection-sampled SplitMix64 convention as
planet generation. The checked-in golden catalog is the executable authority
for exact tables, stream ordinals, draw order, and values.

## Ephemeris query

`resolve_planet_ephemeris()` accepts a stable planet ID and an
`EphemerisQueryTime`. Authoritative simulation passes an integer universe tick
with a zero fraction. Rendering may supply a finite fraction in `[0, 1)` to
sample between ticks; that fraction is presentation-only and is never saved or
fed back into simulation.

The resolver first reduces the tick modulo the orbit period, then applies the
epoch phase, ascending node, and inclination to resolve system-inertial center
position and velocity. Positions are quantized to integer metres and
velocities to millimetres per second. Modulo reduction keeps maximum 64-bit
ticks finite and prevents elapsed-time precision loss. Equal query times repeat
exactly, and adding one complete period reproduces the same result.

Unknown bodies, non-finite or out-of-range sub-tick fractions, mismatched
system/star/planet identities, out-of-bounds catalogs, and any orbit that does
not reproduce from its stable seed are rejected before an ephemeris is
returned.

## Diagnostics and persistence

`local_system_diagnostic_json()` emits schema version 1 with generator and
ephemeris versions, canonical fixed-width IDs, star fields, ordered planet
identities, and integer orbit parameters. It is compact enough for acceptance
reports and defines the data that a later save format may project.

Save format version 3 records this generator and ephemeris version alongside
the stable mission-selected system, star, and planet identities. It regenerates
rather than serializing mutable ephemeris or random state. Rendering, craft
travel, and planet-fixed orientation remain separate systems.
