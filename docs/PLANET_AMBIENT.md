# Immutable planetary ambient truth

Provider version 1 adds environmental design envelopes to the existing C++
physical catalog. It does not change a planet, orbit, terrain, atmosphere,
rotation, discovery or flight recipe. Both physical catalog/ephemeris pairs
1/1 and 2/2 are supported and generate the same environmental values for the
same body. Exact catalog ownership remains part of the recipe.

These are bounded fictional gameplay inputs, not a climate simulation or a
claim of astrophysical consistency with the existing star/orbit descriptors.
In particular, temperature is not inferred from the present star radius,
orbital distance or terrain palette. Existing water coverage is a terrain
classification; this provider does not infer liquid phase or habitability.
Radiation is an unshielded surface exposure envelope; equipment margins remain
a consumer decision.

## Fields and boundaries

| Field | Integer unit and inclusive range | Meaning |
| --- | --- | --- |
| Gravity | existing 350–1,800 milli-g | Existing descriptor authority; no new acceleration model |
| Pressure | existing 0–2,500 millibars | Surface reference, not an instantaneous altitude reading |
| Temperature minimum/maximum | 30,000–1,400,000 millikelvin, minimum≤maximum | Surface exposure extremes; not a day/night clock |
| Radiation | 0–1,000,000,000 nanosieverts/hour | Unshielded surface dose-rate envelope |
| Electrical field | 0–100,000 volts/metre | Atmospheric electrical exposure envelope; no discharge event |
| Gust | 0–200,000 millimetres/second | Atmospheric turbulent/wind envelope; no applied force |
| Ground acceleration | 0–20,000 millimetres/second² | Geologic structural exposure envelope; no earthquake event |
| Visibility | airless, clear, cloud or dust | Immutable atmospheric appearance potential |
| Maximum obscuration | 0–10,000 basis points | Potential obscuration; not current cloud cover or storm intensity |

Atmosphere classifications retain their existing pressure intervals: airless 0,
tenuous 1–249, temperate 250–1,499 and dense 1,500–2,500 millibars. Boundary values
are accepted; adjacent values in the wrong class refuse. Airless environments
require zero electrical field, gust and obscuration, and the airless visibility
kind. Radiation and geologic motion can remain nonzero without an atmosphere.
Clear profiles have zero obscuration. Cloud/dust profiles have positive
obscuration; a tenuous atmosphere can support dust but not this recipe's cloud
profile. Unknown enums and out-of-range values refuse.

The authored origin home is identified through the validated origin catalog,
never by a procedural planet with a matching seed or a generic safe-looking
descriptor. Its minimum temperature is 273–283K, maximum 293–313K, radiation
50–250nSv/hour, field 0–100V/m, gust 0–10m/s and ground acceleration 0–0.05m/s².
Cloud potential is bounded at 25% obscuration. This retains a mild starting
reference without making a suit/ship capability decision or asserting breathable
air. Other planets use the ordinary bounded hazard mapping.

## Seeds and exact mapping

Append-only `PlanetDescriptorStream` children 9–14 are respectively
`ambient_temperature`, `ambient_radiation`, `ambient_electrical`,
`ambient_turbulence`, `ambient_ground_motion` and `ambient_visibility`.
Existing children 1–8 are unchanged. Each value independently derives
`derive_seed(derive_planet_stream_seed(planet_seed, stream), planet, ordinal)`,
then applies the fixed SplitMix64 finalizer (no mutable state or increment).
Mapping is `low + word % (high-low+1)` with unsigned 64-bit arithmetic. Modulo
bias is part of this explicit game recipe, not an observational distribution.

Ordinary temperature child ordinal 1 selects three bands by modulo 3. Ordinal 2
selects a minimum and ordinal 3 selects a positive spread:

| Band | Minimum, millikelvin | Spread, millikelvin |
| --- | --- | --- |
| Cold | 30,000–179,000 | 1,000–120,000 |
| Moderate | 180,000–330,000 | 5,000–160,000 |
| Hot | 400,000–1,200,000 | 25,000–200,000 |

Radiation ordinal 1 selects by modulo 3; ordinal 2 selects 0–1,000,
1,001–1,000,000 or 1,000,001–1,000,000,000nSv/hour. Electrical field, gust and
ground motion each use their own ordinal 1 over their full declared interval.
The authored home instead uses ordinals 1/2 for its two temperature bounds and
ordinal 1 for each mild hazard interval.

Visibility ordinal 1 modulo 3 selects clear when zero. Otherwise tenuous
atmospheres or existing water coverage below 1,000 basis points select dust;
other atmospheres select cloud. Ordinal 2 selects obscuration 1–10,000 (1–2,500
at home). The airless override sets atmospheric fields and obscuration to zero.
Adding/changing a hazard stream cannot consume another stream's state.

Version 1 locks seed derivation 1, planet descriptor 1, source local catalog 1
and authored origin-home 1. Their future upgrade requires an explicit
compatibility decision. Generation and validation use integer arithmetic only.
They introduce no further host math-library dependency.

## Ownership, persistence and consumers

`generate_planet_ambient_recipe` validates the complete physical context and
existing PlanetId. `resolve_planet_ambient_environment` rederives and compares
all recipe owner fields before returning facts. Altered context, unknown body,
forged universe/system, procedural origin substitution and unsupported/mixed
versions refuse. `validate_planet_ambient_environment` only checks bounds and
coherence for explicitly authored test/reference profiles; it does not certify
that a fabricated profile belongs to a generated world.

The closed JSON recipe record contains provider name/version, selected physical
catalog/ephemeris, SystemId/system seed, catalog kind, optional origin universe
seed and PlanetId. Encoding and decoding validate against the actual world.
It contains no generated environment catalog, observations or time state.
The document is bounded to 1,024 bytes and two container levels before parsing;
duplicate/extra/missing keys, wrong types, narrowing/overflow, non-finite JSON
and invalid ownership refuse. IDs/seeds are exact unsigned 64-bit integers.

Existing flight/save formats retain their original bytes and semantics: none
has selected this provider yet. A future capability or walking consumer must
explicitly carry this recipe/version in its saved schema. Loading an old save
does not silently grant new hazard truth or reroll an environmental selection.
The recipe codec qualifies that persistence boundary without an unrelated
flight-save migration.

Outdoor and landing exposure projections mark geologic motion applicable;
atmospheric transit does not. All retain the same immutable surface envelopes.
This is a conservative reference input, not altitude-resolved pressure, terrain
impact, automatic damage or a force added to the flight integrator. Actual
gravity/atmospheric dynamics retain their existing providers. Completed #93
retains speed/descent-driven heating and thermal integration.

Raw facts are available to C++ simulation consumers only. #175 must redact
undiscovered facts and retain observation provenance before any new cockpit,
chart or renderer presentation. #102 owns equipment-specific margins; #199
owns suit/surface walking integration; #248 owns jump interference design.
Time-varying storms, day/night thermal evolution, shield attenuation, hazard
damage and detailed minor-body/stellar physics remain separate work. No mutable
weather state or competing rotation/light clock is introduced here.
