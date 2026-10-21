# Playable boarding prototype

Owner direction, 2026-10-09: document imperfections, deliver the interaction and
return to refinements later. Full anatomical clearance became an inappropriate
prerequisite for a game using pixel characters. The retained body, balance,
joint and connected-region studies are research, not gates for this prototype.
Their observations remain intact; they do not certify the new gameplay route.

## Selected implementation

Use the existing Origin station walk and selected Wayfarer assets. A nearby
player action starts a restricted, authored kinematic route through D1 access,
the ladder and cabin to the seat. The camera moves continuously; seat and hatch
presentation follows the existing operating recipe. A seated player then uses
the existing flight controller, port release and applied exhaust. Disembarking
at D1 reverses the interaction and returns to the actual station approach.

C++ owns actor phases, transition progress, equipment state and the shared
120-Hz voyage clock. Godot presents that state and sends commands. Boarding
does not create a new universe, relocate the craft or grant flight input while
the actor is walking or traversing. Ordinary station collision and the open
well remain enforced outside the controlled interaction. The authored route
uses a simple gameplay body proxy; it does not solve every joint, contact force
or anatomical self-intersection. Free cabin walking is deferred.

Format22 records the actual transition or occupied state explicitly. Existing
formats16–21 retain their decoding and meanings; an old flight save does not
acquire an invented actor journey. Save As and refused commands stay
transactional. Continue retains progress and starts paused.

## Delivery checks and deferred detail

Check nearby/far, attached/detached, wrong-port, non-finite input, illegal phase,
paused state, save/resume, uninterrupted camera movement and the shared clock.
Exercise boarding, sitting, ordinary departure and disembarking through public
commands with both compilers and Godot. Inspect the visible sequence before
requesting an owner playtest. Keep atmospheric/surface flight regressions.

Document imperfect animation, body-proxy fit, restraint contact and visual
polish for later refinement. Full biological balance/SELF certification,
occupied restraint mechanics and exhaustive hardware/body collision proofs
remain deferred. They must not restart an automatic diagnostic issue chain.
The owner handoff and First Flight remain open until their composed playable
journey works; implementation and test progress are reported separately.

The initial visual review used ten 1280×720 Godot captures from an actual
New Game 42 station walk, boarding and reverse disembarking, retaining the same
session and ship model. Rendering waits did not advance the voyage clock. The
capture script is `godot/studies/captures/native_boarding_capture.gd`; it accepts
an absolute prepared-assets directory and output directory on a rendering
display, and writes images plus source hashes and asset license records.

Observed refinements: the hatch traversal faces a plain shaft wall, cabin
framing shows rough mesh joins, docked cockpit glazing is very dark, and the
debug HUD occupies substantial screen space. Reverse travel currently keeps
the same view direction. These are recorded presentation improvements, not
new anatomical proof prerequisites. The initial C++ run passed all 104 tests
with both GCC and Clang, including atmospheric/surface flight regressions.

The subsequent 15-image round-trip capture adds a real port release, downward
withdrawal, brief main burn, thruster-assisted return and explicit capture before
reversing the same boarding route. The same C++ session and Wayfarer model are
retained throughout; the craft is not reset to its pre-departure state. See
[close-range approach](PORT_APPROACH_AID.md) for the bounded aid and historical
orbit compatibility. The low-thrust main plume now survives the transparent
camera composite. The small withdrawal nozzles are difficult to read at chase
distance; nozzle/camera readability remains a presentation refinement.

Owner keyboard/mouse review, 2026-10-17: the boarding animation reads well, but
the 90-degree parked seat and tight approach deserve another ingress pass.
That swivel is explicitly authored in the operating asset; the suggested
180-degree entry needs a coordinated operating-recipe/asset change, rather
than a presentation-only axis correction. This feedback does not reopen the
deferred anatomical studies. Contextual ladder actions and ordinary mouse look
are addressed separately from changing that authored route.

Mouse, configured look keys and the selected right stick let the player look
around during either direction of the authored interaction. Looking only offsets
the camera; the C++ eye position, movement, heading and saved route stay unchanged.
The view gently recentres during the final second of the twelve-second route so
seating and ordinary station walking resume without a camera snap. Pause, focus
loss and dialogs release the pointer and suppress look input; Continue starts
paused with a centred view. This does not widen the route or guarantee
clipping-free views inside the provisional mesh.

Active station walking and boarding now show a small status/help card beside the
existing contextual ladder prompt. The full scrollable actions return on Pause;
settings, recorded observations and save dialogs retain their existing ownership
and neutral-resume rules. This reduces the sidebar covering the boarding view,
without changing the authored eye, route, physical state or saves. Text still
wraps in small windows; cramped ingress and rough mesh joins remain unfinished.

## First cockpit control

The bounded continuation under #277 adds one provisional GEAR control to the
existing console. Its placement derives from the admitted current SYSTEMS
screen, rather than an earlier cockpit layout. The target is presentation,
not an authenticated mechanical switch or proof of the pilot's reach.

Activation immediately requests the existing C++ gear change. Assistance does
not turn this command into an automatic landing. The visible switch follows
accepted gear state; refused commands leave it unchanged. Existing paused
controls remain available. Camera changes, pause, focus loss and Continue
clear transient interaction, and holding an activation does not repeat it.

The admitted pilot assets do not include a discrete hand reach/press/return
clip. That animation and final seated visual quality remain open under #277;
they do not block this usable control or restart the deferred anatomy studies.
