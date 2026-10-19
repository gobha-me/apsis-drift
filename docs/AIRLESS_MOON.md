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
The selected moon terrain uses that seed. Scalar properties and sampled terrain
are not contact certificates or a saved journey.

## Integration sequence

The explicit moon-relative inertial and moon-fixed frames retain the actual
same-tick parent-translated ephemeris. Standalone rigid state version4 encodes
full typed moon ownership; historical rigid versions1–3 retain their bytes.
`physical_moon_rotation.hpp` retains the exact epoch-turn phase and original
spin period, with the recipe1 system +Z pole. Fixed-frame handoffs include
surface spin velocity and body-resolved angular velocity exactly once.

`moon_body_dynamics.hpp` authenticates the actual moon GM and uses the existing
120Hz RK4 implementation. Gravity remains separate from gross propulsion
and its fuel accounting; rotation assistance does not cancel orbital motion.
There is no moon atmosphere, parent-planet atmosphere or extra force integrator.

`moon_terrain.hpp` owns the authenticated physical moon and terrain recipe1.
Its continuous dry field reuses the planet recipe2 smooth lattice arithmetic
with six spacing/amplitude bands, respectively in metres:
131072/3200, 32768/1000, 8192/300, 2048/32, 768/10 and 320/3.
The absolute relief bound is4545m. Each octave derives its own terrain stream
from the moon surface seed. Tint-derived rock color is quantized from height;
negative elevation remains dry bedrock, with no sea-datum clamp.

Output LOD changes mesh sample locations rather than terrain heights. Cube
projection arithmetic is shared with existing planet geometry. Typed moon tile
keys retain the real BodyId; the global planet radius and gravity admission
rules remain unchanged. Tile fills stage their complete result before writing
caller buffers. The sampler exposes the same height for surface position and
clearance. Actual contact uses the explicit top-facet provider below.

`moon_surface_contact.hpp` builds the same32-interval top triangles needed by
presentation, and certifies pads and hull through the existing bounded geometry
kernels. Authenticated negative moon relief is dry bedrock. The landed anchor
retains the supported fixed pose; idle resolution applies the actual spin at the
shared tick. Liftoff release checks actual local GM/r² and adds no impulse.
`moon_surface_walker.hpp` consumes those top facets through the same movement,
slope, step, obstruction and return-access kernel as planet walking.

Native presentation and travel still need to consume those providers in a whole
journey. A bounded
local jump branch can reuse the existing charge commitment and recovery
semantics; it must retain explicit body ownership and preserve the old
intersystem route validators. No opaque relocation or initialized landed demo
can substitute for the actual trip.

The explicit moon flight codec uses format42 and a separate optional moon world
selection on the shared flight document. It cannot coexist with a planet world
selection. Source identity is regenerated from the retained origin seed and
physical catalog2; the target is the genuine typed origin moon. Closed moon
physical/rotation/dynamics/terrain versions are distinct from historical planet
model fields. Old planet formats retain their paths and bytes. Save hydration
exposes an explicit moon result; planet atmosphere/rotation outputs are
unavailable for that selection. Moon surface format43 retains gear and an
optional genuine supported moon anchor, while the resource envelope keeps the
same craft and clock. Historical surface23 cannot reinterpret a moon anchor.
Native free-flight consumes the same RK4 and gross fuel transaction. Landing,
walking, travel and presentation dispatch still need the whole-route integration
before the moon is playable.

Suit oxygen/exposure, economy, missions, a wider fleet and multi-body gravity
are separate scopes. The existing local central-body approximation remains a
bounded simulation model. Scripted journeys and reviewed captures remain
separate from manual controller and platform qualification.
