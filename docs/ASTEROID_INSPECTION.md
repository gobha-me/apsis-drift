# Asteroid close inspection

The bounded [Freedom increment #629](https://github.com/gobha-me/apsis-drift/issues/629)
adds a real Wayfarer trip to the existing origin-system minor body. C++ retains
its genuine `BodyKind::minor`, source radius, orbit, spin and tint. Godot
presents the actual craft and C++ geometry; tracking an optical annotation does
not move the ship or select a jump.

## Fly there and return

Start on Origin Station, walk to Wayfarer, board and sit. Release the port and
withdraw into space. In paused Controls, select **Select asteroid inspection jump**,
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

Mouse pitch/yaw steering, held right-mouse free look and recenter work as they
do near home. Releasing free look returns to forward view with neutral mouse
steering. Pause, focus loss and Continue clear that steering intent.

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
come from C++; Godot adds no clock, orbit or physical shape. The seed42 route
review reaches the unlit hemisphere: body and craft appear mainly as dark
silhouettes. Illuminated surface detail and night-side legibility remain visual
follow-up work. The current physical
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
silently discarded. Historical Moon flight42/walk44/local45 encodings
retain their original validators and bytes.

[Local route reuse #633](https://github.com/gobha-me/apsis-drift/issues/633)
lets the existing Moon and asteroid round trips share one save. Return home,
approach and capture the moving station, use **Replenish flight fuel and jump
charges · free**, release the port and withdraw before choosing either existing
destination. The home return leaves one reserve charge; another full round trip
requires two. Selection, Continue and changing destinations never refill fuel.
Direct Moon–asteroid transfers remain unavailable. The retained chart recipes
also refuse combining completed local routes with an intersystem voyage; the
separate [planetary-chart start](FREEDOM_PLANET_TRAVEL.md) explicitly composes
those histories while requiring a real return home between companions.

A local destination choice at home stays reversible until a local jump commits.
Cancel any active spool, then select the known neighboring system from the chart.
This replaces only empty local choices, including empty format48 route slots;
it preserves the actual ship, resources, knowledge, previous intersystem receipt
and next-attempt counter. Any committed local proof stays retained and refuses
that switch. Selecting a chart entry never moves the ship or spends a charge.

Selecting the Moon after adopting the asteroid route explicitly selects local
travel format48/version4. Reading an old save does not upgrade it. One fixed
legacy span keeps the original old-format proofs; the active and inactive routes
keep bounded first/latest checkpoints, independent of the number of trips. Each
new excursion retains its actual home departure model for its return, including
assistance changes made at home. Old45/47 saves keep their existing encoding
until this explicit adoption. Personal markers and inspection records remain
part of the same journey; choosing a destination does not grant survey facts.

The actual voyage fixture starts with station walking and boarding, then uses
real controls, charged travel and home return. Its phase saves and control
segments feed the ordinary native shell test. Invalid owners/versions/kinds,
non-finite state, mesh dimensions/buffer shortages, swept boundaries and exact
Save/Continue are checked separately. Automated and rendered checks do not
establish manual controller feel, final asteroid art or full Freedom acceptance.

## A generated minor beyond Origin

After visiting a generated non-Origin system, its first ordinary planet can
serve as the entry reference for **Select asteroid inspection jump**. The
minor remains a genuine star-orbiting catalog body. It does not become a moon
of that entry planet, and other planet-to-minor routes remain outside this
bounded outing.

The starter's three charges cover **Home → entry planet → minor → Origin
Home**, with one charge at each normal commitment. The final charge is usable
with a warning. The minor view's **Select jump to Origin Home** chooses a
direct charted return to free flight near Origin Station. Selection grants no
charge, docking or refueling. Actual endpoint distance and clearance still
apply; a route outside the drive's rated range refuses.

Use real thrusters to approach and withdraw from the existing stand-off limit.
This remains a close inspection with a bounded exclusion response. Landing,
walking and mining are unavailable. Preserve fuel for maneuvering.

Explicit foreign-minor selection adopts travel63/state9/composed3 and
minor world2/flight64. The outing retains the actual entry-planet flight
profile with its body-bound hold cleared, the paid commitments, the earlier
Moon/system/planet receipts and all existing notes. Changing an uncommitted
minor choice to a Moon keeps that selected carrier; it does not fabricate a
minor visit. Existing Origin minor worlds and formats46/55 retain their rules.
Save/Continue preserves genuine ownership and the shared clock.
