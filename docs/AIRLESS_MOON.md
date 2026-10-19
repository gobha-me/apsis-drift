# Airless moon integration

The owner selected one approachable, landable, walkable moon on 2026-10-19.
[#623](https://github.com/gobha-me/apsis-drift/issues/623) tracks the complete
Wayfarer journey. The first C++ providers below do not yet add a playable moon
or change ordinary New Game. Existing [distant contacts](DISTANT_CONTACTS.md)
retain their optical-only limits until actual flight and surface consumers land.

## Physical source

`physical_moon.hpp` extends the existing authenticated secondary catalog with
physical recipe **1**. `generate_physical_moon_descriptor` selects the actual
moon by its full `BodyId`; `validate_physical_moon_descriptor` rederives it from
its retained complete catalog. Identity, parent, radius, spin period, exact
32-bit epoch phase and tint are inherited. A minor body or a matching numeric
word of a different kind cannot become a moon or a `PlanetId`.

Recipe1 fixes zero atmosphere and water, a system +Z spin pole with zero tilt,
and an authored bulk density of 3,000 kg/m³. This density is a game recipe,
not a measured composition. The nominal gravitational constant is frozen at
6.67430 × 10⁻¹¹ m³ kg⁻¹ s⁻², consistent with the
[NIST explanation](https://www.nist.gov/how-do-you-measure-it/how-do-you-measure-strength-gravity).
Future tuning or a different pole/density requires a new recipe; changing the
constant in place must not reinterpret saved worlds.

`resolve_physical_moon_properties` authenticates first, then derives mass,
GM and reference-surface gravity from that one radius/density source. It adds
no independent force integrator. Terrain seed derivation uses the new
`moon_surface` domain17, preserving every existing stream and source catalog.
The seed is a future terrain input; a scalar property result is not generated
surface geometry, a contact certificate or a saved journey.

## Integration sequence

The next concrete boundary is a moon-relative inertial frame with the actual
same-tick parent-translated moon ephemeris. Its typed ownership and standalone
encoding must coexist with unchanged historical planet/system/station frames.
Surface rotation must retain the source's exact epoch-turn phase rather than
rounding it into the planet-only phase-tick recipe.

Then connect the C++ force, terrain, contact, landed anchor and surface walker
through that same body. Native presentation consumes their state. A bounded
local jump branch can reuse the existing charge commitment and recovery
semantics; it must retain explicit body ownership and preserve the old
intersystem route validators. No opaque relocation or initialized landed demo
can substitute for the actual trip.

Suit oxygen/exposure, economy, missions, a wider fleet and multi-body gravity
are separate scopes. The existing local central-body approximation remains a
bounded simulation model. Scripted journeys and reviewed captures remain
separate from manual controller and platform qualification.
