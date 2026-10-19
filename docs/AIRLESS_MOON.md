# Airless moon integration

The owner selected one approachable, landable, walkable moon on 2026-10-19.
[#623](https://github.com/gobha-me/apsis-drift/issues/623) tracks the complete
Wayfarer journey. The bounded implementation connects the existing station start, charged local
travel, real lunar flight, contact and suited walking. Godot consumes the same
C++ state and meshes. This does not make the other
[distant contacts](DISTANT_CONTACTS.md) landable.

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

## Local voyage and operation

New Game still starts on Origin Station. Walk to Wayfarer, board, sit, release
the port and withdraw using the existing flight controls. **Select nearby moon
jump** chooses the actual origin moon. The normal jump command spools and
commits one charge, advances the existing transit clock and arrives above that
moon. Selecting a target does not spend a charge. The starting three charges
cover outbound, home and reserve.

The moon uses manual descent: deploy gear, brake with real thrusters and settle
onto suitable ground. Flight assistance controls rotation; it does not hover or
cancel lunar gravity. The landing-aid autopilot and atmospheric controls do not
apply here. Keep flight fuel for braking and ascent. The lunar altitude readout is above
the reference radius, not radar clearance; dry terrain can lie below that datum.
The HUD and cockpit also show **Gear clearance**: the current signed minimum
and maximum terrain-normal gaps across all three deployed pad footprints.
Negative values indicate overlap within the gear stroke. This reading requires
complete existing contact certificates for every pad; stowed gear, incomplete
terrain support, landed/outside modes and active travel show **unavailable**.
It is an observation, not radar altitude, hull clearance or landing permission.
Reading it does not advance time, alter a save or change the landing envelope.
Only suitably slow,
supported three-pad contact with certified hull clearance creates a landed
anchor. The first pad may compress its existing rated gear stroke only while
all pad geometry is valid and the swept hull is clear; a hull strike or unknown
contact is not a landing.

After landing, release controls and resume, then use **E** or the selected
controller's **A/Cross** to leave through the hatch. The HUD shows the current
action; the paused Controls action remains available. Every other flight/look
demand must be neutral. The same walking controls,
slope/step limits and return-access checks apply as on the home planet. Return
to the hatch, board, release surface support and apply up thrust to lift off.
**Select jump home** and the normal jump command return to the real home body,
using the retained home model and actual station ephemeris. Local jumps are
unavailable while outside, landed or attached to a port.
Climb clear of the Moon's full terrain-and-hull envelope before selecting
home; releasing landing support alone is not enough. For the Wayfarer, about
5 km above the reference radius clears the Moon's conservative relief envelope.
This is a departure-volume rule, separate from the local signed gear gaps.
A disabled local-jump control shows its current C++ refusal reason in the
HUD and tooltip during free flight. A refused home selection reports when the
departure volume is obstructed; it does not change the selected voyage or spend
a charge. These messages explain the existing rule rather than estimating a
safe local jump from terrain samples.

Paused Controls also offer **Mark this spot**, **Replace marker**, **Clear
marker** and **Show personal marker** during lunar walks. The point records
only the actual supported foot position and its original ship time. There is
one personal marker per journey, so replacing it on another body is explicit.
MARK shows a direction and straight-line range from your feet or craft centre,
not a terrain route or landing certificate. With MARK off, RETURN points toward
the hatch; its distance remains in the HUD while MARK is shown. Continue starts
with the normal return cue. The saved lunar point survives boarding, ascent and
travel home, where it is listed as being on another body without a local
bearing. These metadata actions do not spend fuel, advance time or grant sensor
facts. The Moon name remains unresolved rather than being borrowed from a
planetary identity.

This first local branch connects the origin home body and its selected moon.
It does not add arbitrary local-body selection, landable asteroids, a new
intersystem risk policy or seamless gravity transitions. The existing
intersystem route retains its validators; switching from a retained local
voyage into that route is explicitly refused in this increment.

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
Moon walking uses explicit format44, retaining the real body-fixed actor and
heading. Local travel uses format45/version2, retaining both the original
outbound commitment and current local leg. Charges, attempt numbers, clocks,
origin ephemeris and home model survive Save/Continue; historical intersystem
format27/version1 is not reinterpreted. Native free flight consumes the same RK4
and gross fuel transaction.

Lunar personal points explicitly select nested `surface_marker` version2 with
the canonical typed body target, moon-fixed metres and recorded tick. Clearing
retains that version with a null point; replacing it on a planet selects the
existing version1 planetary point. The absent/v1 marker encoding and outer
voyage formats remain unchanged. Source validation regenerates the actual
origin Moon from its seed, including dry ground below the reference radius;
retained metadata does not claim renewed terrain support.

## Presentation and checks

The native lunar view uses the complete C++ cube-sphere cover and typed
moon-fixed anchors. The 32-interval top mesh matches the contact triangles.
Near the observer, the bounded cover refines the contact footprint to LOD13,
including cube edges and corners. Skirts close visual cracks and never provide
physics support. Uploads are staged; an old complete cover remains visible
until its replacement is complete. C++ also supplies the actual star direction,
apparent radius and background catalog. There are no clouds or ocean on this
body. Cockpit instruments, exhaust and fuel readings consume actual state.

The opt-in voyage fixture flies the continuous station-to-moon route with real
commands and no injected landing pose. It saves arrival, approach, the tick
before touchdown, landed, outside, returned, released and ascent phases.
Contact-fixture geometry provides a navigation destination, not a replacement
for flight. Focused headless contracts separately test invalid state, frame and
owner mismatches, bounded geometry, old save compatibility and exact
Save/Continue. Godot integration, raster review and manual play are separate
checks; a headless result alone does not qualify visual quality or controller
feel.

Suit oxygen/exposure, economy, missions, a wider fleet and multi-body gravity
are separate scopes. The existing local central-body approximation remains a
bounded simulation model. Scripted journeys and reviewed captures remain
separate from manual controller and platform qualification.
