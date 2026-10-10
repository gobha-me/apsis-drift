# Native information hierarchy

The mission-free Freedom view keeps the world visible. Ordinary saved flight
and suited walking use a compact text card, a nearby action when available,
and **Esc / Start · Controls**. Choose **Flight instruments & navigation**
inside the paused controls to review the complete existing telemetry, orbital
forecast, navigation, ports, surface aids, service and Save As actions.
**Back to controls** returns to the menu without resuming. Explicit Resume
still requires focus and neutral movement controls.

This implements the native part of #171 using the existing C++ projection.
There is no mandatory objective and no mission, currency or upgrade gate.
Station walking retains its existing interaction prompts; recorded pilot loss
retains the explicit recovery overlay above the scene.

| Priority / state | Persistent information | Action and review |
| --- | --- | --- |
| Recorded loss or failed state | Existing recovery choice or paused failure message | Recovery/Continue uses the existing root; UI never respawns silently |
| Suited ground walking | On foot, suit equipped, craft-centre distance, walk/look controls | Nearby return uses the original hatch handler; its actual refusal stays visible |
| Docked | Docked, known current system, reference altitude, motion, finite resources | Release is contextual; controls retain unboarding and free station service |
| Landed | Landed, known system, reference altitude, motion, resources | Liftoff is contextual; controls retain suited exit and detailed surface actions |
| Atmospheric / space flight | Modeled air distinguishes Atmosphere from Space; rotating-surface speed and signed radial rate are separate | Available port capture/approach, landing or jump uses the existing readiness and command guards |
| Landing / liftoff aid | Actual maneuver replaces the flight mode | Controls retain cancellation and readiness/refusal information |
| Spool / transit | Actual jump phase replaces mode; known selected destination and remaining charges stay visible | Cancel spool uses the existing command; committed transit offers no false cancellation |
| Resource warning | Low/empty fuel and refused propulsion use text, independently of color | Free replenishment remains available only while attached |
| Instruments | Same-tick detailed motion, orbital forecast, chart, docking, surface and saved-command messages | Paused, scrolling, keyboard/controller focus; all existing actions remain accessible |

The compact card consolidates resources and motion instead of accumulating
separate permanent rails. The existing HOME direction marker remains visible
in flight when the origin station is actually available; it is a direction
cue, not a safe route. Ground walking hides ship flight/navigation readouts.

Reference altitude is height above the C++ reference sphere, **not generated
terrain clearance**. The detailed central-body orbital forecast retains stable,
atmospheric entry, reference-surface intersection, escape and unavailable
apoapsis semantics; thrust and drag can change it. Neither a black sky nor
altitude proves orbit. Assistance is labeled when active; the compact view
does not label powered/assisted flight as coasting.

Known system and destination names come only from chart rows supplied by the
existing projection. Unknown destinations are not renamed from hidden world
truth. Historical saves without selected resources say tracking is unavailable;
no full tank, charges or equipment condition is invented. No terrain-clearance
sensor, hull-health percentage, oxygen countdown, camera protection or advanced
spectral view is added. These need their own authoritative providers and
design; the [surface conditions](CRAFT_ENVIRONMENT_ASSESSMENT.md) review retains its
existing knowledge-limited values and equipment ratings.

Command notices persist as the latest accepted/refused operation or pause/save
message. The compact card bounds long notices to 180 characters; the complete
message stays in instruments and its tooltip. There is no new event history,
clock, animation requirement or color-only warning. Review replaces the card
with the detailed panel rather than overlapping another permanent column.
The text and button layout compensates for logical/physical canvas scaling;
short windows clip the card instead of shrinking the scene or adding horizontal
scrolling. Existing detail layout stacks its forecast and scrolls at narrow
widths. The terminal path is retained independently; native pixel layout is
not an ANSI/Kitty parity requirement.

Saved pause controls use the same physical readability floor: 18-pixel body
text and 44-pixel interactive heights at the checked window sizes. The action
and remapping lists stack below 1000 physical pixels wide; each scrolls to the
focused control while the persistent status/refusal message stays visible.

Opening controls follows the existing pause behavior: it cancels active port
approach or surface assistance before review, leaves jump commitment unchanged,
and never advances simulation time. Reviewing, resizing and scrolling issue
no additional world, actuator, terrain or save mutations.

`native_status_test.gd` rejects malformed/non-finite observations and invalid
dimensions before visual checks. The public native runner additionally exercises
actual saved flight, compact controls/instrument focus and resize, full save
identity, suited walking/nearby and refused return, jump phase transitions and
recovery through the original C++ bridge. These tests preserve the existing
neutral-input, focus-loss and transactional Continue checks. Native artwork,
manual controller usability and final cockpit instrument placement remain
separate qualification work.
