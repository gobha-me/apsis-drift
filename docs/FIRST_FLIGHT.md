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

The current native path is Linux x86_64. Connect the controller before launch.
New Game starts a first-person actor on Origin Station's hub floor. WASD or the
left stick walks; right-drag or the right stick looks. **Esc / Start · Controls**
opens paused controls. The start screen also offers control Settings and
[Flight basics](NATIVE_TITLE.md).

Walk through the workshop to D1. Release movement controls, then use **E**,
controller **A**, or **Board Wayfarer**. The authored boarding sequence takes
you through the hatch to the pilot seat. The open docking well stops ordinary
unsupported walking; approach the craft through the supported station route.

## Depart, explore and return

Open paused controls and choose **Release attached port**. Neutralize the controls,
then resume explicitly. Withdraw with **Fall** and brake with **Rise**; clear
the port column before forward thrust. Thruster exhaust follows actual applied
propulsion. Flight basics explains the current keyboard/controller mappings;
[the flight reference](NATIVE_FLIGHT_REFERENCE.md) explains handling.

Flight has finite fuel. Atmospheric drag, gravity, braking and assistance use
the same C++ flight owner. Watch your fuel while exploring terrain and returning
to space. The cyan [HOME marker](HOME_NAVIGATION.md) gives the station bearing
and range. Use [the approach aid](PORT_APPROACH_AID.md) for an aligned return;
it applies thrusters and consumes fuel. **Capture port** remains your action.
While attached at D1, **Unboard to station** restores station walking and
[free station replenishment](FREEDOM_RESOURCES.md) can restore both resources.

The [neighboring-system trip](FREEDOM_NATIVE_TRAVEL.md) supports chart selection,
jump commitment and home return. Three initial jump charges leave an outbound,
return and reserve charge. [Landings](LANDED_CRAFT.md) use deployed gear and
certified dry terrain; [suited walking](FREEDOM_SURFACE_WALK.md) permits nearby
exploration, return through the hatch and thruster liftoff. Flight basics gives
the supported actions and conditions for each mode.

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
