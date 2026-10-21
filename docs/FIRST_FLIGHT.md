# First Flight prototype

This is the current station-to-flight route. The
[Freedom roadmap](ROADMAP.md) defines the active scope; manual controller and
hardware qualification is still open. The review bookmark
[`first-flight-checkpoint-2026-10-13`](https://github.com/gobha-me/apsis-drift/tree/first-flight-checkpoint-2026-10-13)
preserves the earlier tested revision while development continues.

## Start on the station

Follow [native setup](../godot/README.md). From the repository root:

```sh
tools/run_godot_native.sh --new-game=42
```

The current native path is Linux x86_64. Keyboard/mouse work directly; a controller is optional.
New Game starts a first-person actor on Origin Station's hub floor. WASD by default or the
left stick walks; ordinary mouse movement, arrow keys or the right stick look. **Esc / Start · Controls**
opens paused controls. **Settings / Controls** lets you change each walking key
for both station and surface movement. Choices stay pending until Apply; Cancel
keeps the previous keys. The walking hint shows the current choices. Release
held movement keys before resuming, including keys held while changing them.
Flight-key and controller remapping takes effect after the preferences save.
A save error keeps the previous binding and leaves capture open; follow the
status message or press **Esc / Start** to cancel.
The start screen also offers control Settings and
[Flight basics](NATIVE_TITLE.md). Look speed and vertical inversion apply to
on-foot keys and the right stick as well as flight look. Walking keeps its
default stick rate; the speed setting scales it. Mouse look uses the same speed
and vertical inversion preferences. Walking and flight capture the pointer; pause,
focus loss and dialogs release it. Resume explicitly after releasing controls.
In flight, move the mouse from the centre to request pitch and yaw; the ring shows
your steering demand. Bring it back to centre or use the displayed recenter binding
to neutralize steering. Hold right mouse for free look; releasing it faces forward
and starts steering from neutral. These requests use the same physical thrusters
and fuel as keyboard/controller steering.

Resizing the window while movement, flight or look controls are held pauses
input. Release those controls, then resume explicitly; the craft retains its
committed pose and momentum. A resize with neutral controls keeps the current
running or paused state. Initial layout does not add a pause.

An optional [bay attendant](STATION_CREW.md) stands beyond the arrival guard.
Face the attendant and release movement to use the displayed Talk action
(**E**, left click or controller **A**). If you press just as you stop, the request
waits for the next ordinary walking step. Moving again cancels it. Nearby guidance
shows a refusal when you need to get closer, face the attendant or stop.
Conversations return to paused controls; resume explicitly to continue walking.

The arrivals and side branches remain outside the current supported walk route;
visible access rails mark that limit at the junction. Follow D1 toward the
workshop. Near the ladder, a contextual **Climb aboard** prompt names the action.
Release movement controls, then use **E**, left click while the prompt is shown,
controller **A**, or **Climb aboard
Wayfarer**. The authored boarding sequence takes
you through the hatch to the pilot seat. The open docking well stops ordinary
unsupported walking; approach the craft through the supported station route.
Walking and boarding show a compact status/help card. Pause restores the full
scrollable controls. You can look around during boarding; the view settles back
toward the authored route near its end before handing over to the cockpit.

## Depart, explore and return

Open paused controls and choose **Release attached port**. Neutralize the controls,
then resume explicitly. Withdraw with **Fall** and brake with **Rise**; clear
the port column before forward thrust. Thruster exhaust follows actual applied
propulsion. Flight basics explains the current keyboard/controller mappings;
[the flight reference](NATIVE_FLIGHT_REFERENCE.md) explains handling.

Flight has finite fuel. Atmospheric drag, gravity, braking and assistance use
the same C++ flight owner. Watch your fuel while exploring terrain and returning
to space. The cyan [HOME marker](HOME_NAVIGATION.md) gives the station bearing
and range. Within one kilometre of the station centre, the cue becomes **D1** or
**D2** and points at the selected collar; the range still measures the station
centre. Near the selected port, choose **Dock with thrusters** to align, close
the gap, brake and latch automatically. It uses flight fuel and accepts only the
[bounded approach corridor](PORT_APPROACH_AID.md#smooth-docking). Manual flight
input or pause cancels it. The older approach aid and manual capture remain
available in paused controls.
During live keyboard/mouse flight, the lower action button names **left click**:
click with the pointer captured to invoke that current action. Paused Controls
remain available through Esc / Start.
While attached at D1, **Unboard to station** restores station walking and
[free station replenishment](FREEDOM_RESOURCES.md) can restore both resources.

For a deliberate observation, establish a stable orbit and choose
**Begin orbital terrain survey** in Controls. [Orbital surveys](FREEDOM_ORBITAL_SURVEY.md)
acquire the current known planet's coarse terrain model in 30 seconds of
qualifying ship time; leaving the band holds progress.

The three cockpit screens show native NAV, FLIGHT and SYSTEMS readings,
including fuel, jump charges and applied propulsion. Reference altitude is
height above the body's reference sphere, not ground clearance. Use paused
Controls for commands; the screens themselves are read-only instruments.
The seeded background stars are visual reference points, not selectable chart
destinations or newly granted observations.

The [neighboring-system trip](FREEDOM_NATIVE_TRAVEL.md) supports chart selection,
jump commitment and home return. Three initial jump charges leave an outbound,
return and reserve charge. [Landings](LANDED_CRAFT.md) use deployed gear and
certified dry terrain; [suited walking](FREEDOM_SURFACE_WALK.md) permits nearby
exploration, return through the hatch and thruster liftoff. After a supported
planetary landing, release flight and look controls, then use the displayed
**Leave Wayfarer · suit** action, **E**, captured **left click**, or selected-pad
**A/Cross**. Leaving pauses outside; resume explicitly to walk. **Liftoff** stays
in paused Controls. Flight basics gives the supported actions and conditions
for each mode.
For the [airless Moon trip](AIRLESS_MOON.md#local-voyage-and-operation), choose
**Select nearby moon jump** after departing the station. **Deploy gear · manual
descent** deploys the gear; brake and descend with your thrusters. **Release
support · use up thrusters** releases a landed craft so you can thrust upward.
Keep fuel for ascent and clear the Moon's departure envelope before jumping home.
On foot, release walking and look controls and use **E**, captured **left click**
or selected-pad **A/Cross** for the nearby hatch or geological inspection. The
hatch takes priority when within reach; inspection opens a paused reader.
Back in paused Controls, **Collect hand specimen** carries one sample from an
inspected exposure. **Sample pouch** shows its source and retained field notes;
Discard frees one of four slots. See [hand specimens](FREEDOM_SURFACE_LANDMARKS.md#hand-specimens)
for persistence and replacement rules.

## Save and report problems

[Save / Save As, Load and Continue](NATIVE_PROFILES.md) persist the actor, craft,
history and clock through C++. Continue opens paused. Load and Title ask before
discarding dirty progress. Quit does not autosave. Missing, corrupt or unsupported
saves refuse without rewriting the source file.

For a problem report, give the first failed step, seed, input device, platform,
reproduction steps and whether a save is needed. Include relevant logs or a
small capture when useful; remove private paths/details before posting. A short
successful path report is useful too. See [contributing](../CONTRIBUTING.md).

## Current limits

Boarding, materials and character presentation remain prototypes. Anatomical
refinements are [deferred research](PLAYABLE_BOARDING_PROTOTYPE.md), and surface
hatch transfer animation and exposure/oxygen policy remain unfinished. Terrain
transitions, broader planetary presentation, jump stress and impact consequences
have open work. Scripted journey checks and software renders do not establish
physical controller feel, GPU appearance, audio-device behavior or frame pacing.
Missions, paid economy and populated stations are outside this first route.

## Optional asteroid inspection trip

After station departure, paused Controls offer **Select asteroid inspection jump**.
Release controls, select the actual origin minor, begin the normal local spool
and explicitly resume. A committed outbound and home leg each spend one jump
charge. Use real thrust to inspect or withdraw; the cockpit/HUD show proxy
clearance, radial speed and the disclosed stand-off gap. Landing is unavailable.
Select home and spool normally to return to the real moving station.
[The inspection contract](ASTEROID_INSPECTION.md) explains the prototype guard,
Save/Continue, fuel accounting and spherical-shape limit. The existing Moon trip
remains a separate route; direct Moon–minor hops are not supported.

For optional read-only frame and fixed-clock readings during a playtest, see
[frame diagnostics](NATIVE_FRAME_DIAGNOSTICS.md). The paused toggle does not
change the journey or tune graphics automatically.
