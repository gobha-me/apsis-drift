# Paused native flight basics

This is a bounded #180 increment for the existing thrust lab. Open the normal
controls menu and choose **Flight basics (paused)**. Five short pages explain
thrust/momentum, space versus orbit, assist, head-look and guidance/prototype
limits. The adjacent remapping controls remain the complete binding reference;
examples in the help read current keyboard/controller bindings and prompt style.

The orbit explanation follows the existing C++ criterion: a bound path with
periapsis strictly above `max(atmosphere_edge, 20000 metres)` relative to the
reference sphere. It does not promise collision avoidance over arbitrary relief
or turn the optional guide into autopilot. Historical assist models retain
their own qualified wording. Unimplemented landing/contact, fuel, jump and
flight-save behavior is explicitly distinguished from gear appearance, the
experimental floor guard and saved settings.

The reference stays within the existing pause. Left/right or Tab selects its
three buttons; controller up/down scrolls overflowing text. Back to controls
returns to the entry button without resuming. Existing Escape/Start/B behavior
still resumes, subject to the same neutral-input rearming requirement. Opening
the live guidance selector is a separate action that explicitly resumes flight.

The native project's logical canvas can be larger than its actual window. Help
type/layout compensates for that scaling to retain approximately 26 physical
pixels for body text in smaller windows; overflow scrolls rather than shrinking
the text. Headless tests cover keyboard/controller navigation, remapped and
PlayStation labels, 1280×720, 960×540 and 800×450 layouts, long-text scrolling,
pause/back/resume and held-thrust refusal. Actual unmapped GPU captures at 720p
and 450p were inspected separately. This is not a sofa-distance usability,
localization, hardware-controller or all-existing-menu-layout qualification.

#180 remains open for contextual teaching as additional real navigation,
landing, fuel and recovery mechanics become available; this reference does not
advertise those future systems as shipped.

2026-10-03, #384 extends this same panel to ordinary saved flight. Its explicit
saved profile describes physical thrust/torque, current orbital observations,
assistance, remapped look/view actions, Wayfarer port readiness and committed
Save As/Continue behavior. Historical saved craft receives a ports-unavailable
profile. The lab's practice, floor and no-flight-saves claims remain confined to
the lab; its additional 20 km orbit criterion is not the saved C++ STABLE policy.
Saved help introduces no NAV instrument, flight command, boarding transition,
landing, fuel or jump capability. Back returns to controls while paused and
explicit resume retains the existing current-neutral gate. Reference software
contracts cover both profiles and exact saved-owner state/save preservation;
hardware and complete First Flight remain separate qualification.

## Current Freedom reference — 2026-10-10

The saved-flight profile now has eleven pages. In addition to thrust, air/orbit,
assistance, look, Origin ports and committed saves, it explains finite flight
fuel versus three jump charges, attached free station service, chart selection
and spool/transit, manual/assisted landing and liftoff, suited ground exit/return,
and explicit recorded-loss replacement. These pages supersede the earlier
saved-profile claims that fuel, jump travel and touchdown were unimplemented.
The historical unsaved thrust lab retains its own genuine prototype limits.

Open **Esc / Start · Controls**, then **Flight basics (paused)**. Examples still
read your remapped flight bindings. Ground keys are explicitly fixed WASD; this
is distinct from those flight mappings. The reference reads a snapshot of the
existing C++ projection: most pages show a compact current mode; the resource
page retains the full current observations. Historical or malformed/absent
readouts do not gain invented fuel, charges, equipment, chart knowledge or
Wayfarer actions. **Flight instruments & navigation** is the paused path to
complete readouts and actual commands, rather than a permanent controls wall.

Landing instructions teach the local aid's limits and actual pad/hull contact,
not a site-finding autopilot. The suited page distinguishes craft-centre distance
from the aft access return radius and explains outside Save/Continue. It states
the current instant-transfer and inactive oxygen/exposure boundary. Recovery
explains an explicitly recorded loss and a different replacement craft, without
promising a tow, rescue arrival, lifeboat or automatic impact/depletion death.

Opening controls retains its existing aid cancellation and explicit neutral
resume rules. Reading/navigating the reference, remapping examples, resizing and
returning to controls issue no world, survey, movement, fuel or save mutations.
Keyboard/controller GUI tests retain readable scrolling, focus return, held
input barriers and complete saved-byte identity; actual native suited help
consumes the same outside actor before continuing the existing ground loop.
Title/catalog reference entry and future equipment, environment hazard and
rescue teaching remain separate work in #180/#136 and their providers.
