# Asteroid close inspection

The bounded [Freedom increment #629](https://github.com/gobha-me/apsis-drift/issues/629)
adds a real Wayfarer trip to the existing origin-system minor body. C++ retains
its genuine `BodyKind::minor`, source radius, orbit, spin and tint. Godot
presents the actual craft and C++ geometry; tracking an optical annotation does
not move the ship or select a jump.

## Fly there and return

Start on Origin Station, walk to Wayfarer, board and sit. Release the port and
withdraw into space. In paused Controls, select **Asteroid inspection jump**,
then **Begin local jump** and explicitly resume. Selecting a target is free;
the ordinary three-second spool commits one jump charge, followed by the
existing two-second transit. Cancel during spool spends no charge.

Arrival leaves the same occupied craft near the actual minor body. Use normal
thrusters to approach, change attitude, fly around or withdraw. Assistance
stabilizes rotation; it does not cancel local gravity. The HUD and cockpit show
clearance above the spherical source proxy, radial speed, fuel, charges and the
remaining gap to the inspection stand-off boundary. Negative radial speed means
closing. There is no atmosphere, atmospheric drag, gear deployment, surface
exit, mining or landing action here.

**Select jump home**, begin the normal spool and resume to return to the real
home body and moving station. Each committed leg spends one charge; three
starting charges cover the trip and a reserve. Flight thrust spends its existing
separate fuel. No automatic refill is added. Dock and use the existing free
station service before another round trip if necessary.

Save As and Continue retain the actual selected/spooling/committed/arrived
journey, pilot, craft, clock and resources. Continue opens paused. Reading an
instrument, moving the camera or selecting a target does not advance time or
grant unrelated survey facts. A retained personal surface marker remains on its
original planet or Moon; it does not become an asteroid point or local bearing.

## Explicit inspection limit

The catalog's sphere is a provisional shape, not asteroid terrain or a landing
surface. The C++ inspection recipe sets an exclusion radius outside that proxy:
source radius plus 1,000 metres plus the actual craft's conservative hull bound.
The complete hull is checked along each tick's motion segment.

When a known crossing reaches that limit, the prototype keeps the last clear
position and removes inward radial velocity. Steering, simulation time and
actual applied-thrust fuel costs continue. This lets the pilot turn and retreat,
instead of becoming trapped by a refused inward-coasting tick. The HUD explains
when the guard intervenes. It is a disclosed stand-off constraint, not collision
damage, safe landing certification, an autopilot or proof of a detailed surface.
Invalid or unresolved geometry refuses without committing that tick.

The sphere's true source radius is used throughout. The bounded C++ mesh has
32 latitude by 64 longitude intervals and is uploaded once. Its actual
same-tick body-fixed transform, star lighting and camera-relative placement
come from C++; Godot adds no clock, orbit or physical shape. The current physical
minor is not also drawn as a distant optical contact. Cockpit and world use the
existing separate depth ranges to preserve close-craft precision.

## Versioned ownership and compatibility

Physical minor recipe1 uses the original secondary catalog and explicitly
selects a fixed density of 3,000 kg/m³, the existing nominal SI gravitational
constant and a system +Z spin pole. Air and water are zero. These are authored
simulation assumptions, not measured composition. Gravity uses the existing
120Hz RK4 kernel and gross-propulsion fuel accounting. Parent/tidal/N-body forces
are outside the current central-body approximation.

Minor-relative rigid version5 authenticates its actual minor owner; Moon
rigid-v4 and historical planet encodings remain separate. Flight format46
selects the minor's physical/rotation/dynamics recipes without a fake PlanetId
or Moon descriptor. Local travel format47/version3 retains the original home
model and existing commitment history. A completed home–Moon journey can be
retained when adopting the inspection route from home; no proof or bill is
silently discarded. Direct Moon–minor and arbitrary intersystem combinations
are outside this increment. Historical Moon flight42/walk44/local45 encodings
retain their original validators and bytes.

The actual voyage fixture starts with station walking and boarding, then uses
real controls, charged travel and home return. Its phase saves and control
segments feed the ordinary native shell test. Invalid owners/versions/kinds,
non-finite state, mesh dimensions/buffer shortages, swept boundaries and exact
Save/Continue are checked separately. Automated and rendered checks do not
establish manual controller feel, final asteroid art or full Freedom acceptance.
