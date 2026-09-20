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
