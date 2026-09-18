# Controller-first foundation 06

Superseding control layout: [thrust flight lab / layout 4](THRUST_FLIGHT_LAB_09.md).
The mapping and PNG below document the first iteration. Current defaults use
analog main/retro triggers, left-stick roll/pitch, right-stick yaw/vertical,
bumper strafe and left-stick-click head-look. Full attitude requires the opt-in lab.

2026-09-18. First functional input increment, not a completed Freedom slice or
physical Xbox / DualSense / SteamOS qualification. Godot presents the controls;
the existing C++ application still owns flight, terrain and world identity.

[Native controls-screen PNG](media/freedom-controller-menu-v6.png) — captured
during the synthetic controller test with spherical terrain streaming enabled.
Both settings columns scroll; the safety/remapping message stays in a fixed footer.

## Try it

From the repository root:

```sh
tools/run_godot_study.sh --stream=true --relief=true --pilot=true
```

Add `--start-paused=true` to start directly in the controls screen while
connecting a controller. Flight will wait for explicit Resume.

Press **Start / Options**, **Esc**, or **V** for the controls/pause screen.
D-pad navigates, A / Cross activates, B / Circle goes back to flight.
The menu includes fullscreen, saved settings and bindings, and an explicit
reset of this temporary experimental flight session. No game saves are changed.
Native menu controls remain reserved so remapping cannot remove the escape path.

| Default control | Current action | Keyboard equivalent |
| --- | --- | --- |
| Left stick horizontal | Heading left / right | A / D |
| Left stick vertical | Rise / fall | Space / Ctrl |
| Right / left trigger | Forward / reverse demand | W / S |
| Left / right bumper | Strafe left / right | Q / E |
| Right stick | Seated head-look | Arrow keys; RMB + mouse also retained |
| X / Square | Cockpit / chase | C |
| Y / Triangle | Recenter head | Home |
| Start / Options | Pause and settings | Esc / V |

These are **assisted surface-flight controls**, not pitch / roll controls.
The existing C++ model has heading and three translation axes, not full
six-degree-of-freedom attitude physics. Fractional demands feed its existing
target-velocity and turn-rate rules; they are not a new Newtonian thrust model.
No landing, collision, gear operation, walking, flight-assist toggle, fuel UI,
jump controls or functioning cockpit instruments are claimed by this change.

## Player choices and safety

- Remap each flight/head-look/camera action separately for keyboard and pad.
  Replacing one family retains the other. Conflicts and reserved buttons are
  rejected visibly; Esc / Start cancels capture. Mouse button remapping is
  deferred; RMB remains the existing head-look gesture.
- Adjust dead zone, response curve, head-look speed and vertical look inversion.
  Head-look uses a radial dead zone. Flight axes are shaped independently.
- Automatic Xbox / PlayStation text labels, with a manual family override for
  virtual devices and misidentified controllers. These are text prompts, not
  licensed platform icon artwork.
- One active controller owns flight and controller menu actions. A second
  connected pad cannot silently steer. Initial selection is the first enumerated
  controller; explicit device selection is a later improvement.
- Losing window focus or disconnecting the selected pad pauses flight. Neither
  focus regain nor reconnection resumes automatically. On explicit resume,
  release flight/head-look controls before input can arm. Opposing held triggers
  cannot defeat that gate by cancelling each other numerically.
- Pausing suspends the last authoritative tick; on resume a neutral command is
  submitted until controls are released. Existing orbital coasting is preserved:
  neutral is not an emergency brake.
- Settings use a size-bounded, validated, versioned JSON file in Godot's
  `user://freedom-controls-v1.json`. Invalid documents fall back to defaults;
  saves use a temporary file and rename. This is separate from game persistence.
- Legacy inspection shortcuts cannot reset or change exhibits underneath live
  player bindings. `--controls=false` retains the old inspection/input path;
  ordinary `--capture` runs stay noninteractive for reproducible captures.

Hold/toggle choices for future ship systems, alternate flight presets, rumble,
gyro, trigger effects, full device selection and six-axis flight are not yet
implemented. No special controller feature is required by the baseline.

## Simulation contract

`PlanetaryAnalogInput` supplies four finite values in [-1, 1] per fixed tick.
It is an optional manual-input path through `advance_planetary_flight`, not
Godot physics. Mixed digital-command and analog input on one tick is rejected
rather than silently choosing an owner. The legacy digital path is unchanged.

The native extension validates array length, every axis and elapsed time before
scheduling ticks. It retains the existing 120 Hz clock and catch-up budget.
Old save layouts, generator versions and state checksum layouts are unchanged.
Analog input is **per-tick command data**, not a persistent throttle setting;
an eventual analog replay recorder must store those tick-addressed values.
Constant-command tests across render cadences do not prove unrecorded physical
input events arrive on identical ticks at different frame rates.

## Verification

- C++ contract: invalid / nonfinite axes, rejection without state mutation,
  all 81 combinations of negative / neutral / positive endpoint inputs agree
  exactly with digital state and checksums; fractional input is repeatable and
  differs from full input. Built and tested with GCC and Clang.
- Existing snapshot and spherical-stream contracts pass under both compilers.
- Godot input contracts: schema/JSON round trip, drift, analog values, reserved
  controls, conflicting bindings, alternate prompt labels, device isolation,
  focus suppression and reconnect gating, using synthetic events.
- Live extension checks: invalid buffers/time, constant analog traces at
  30 / 60 / 144 FPS, digital replay equivalence and analog-to-digital release.
- Native GPU smoke exercises analog movement, head-look, pause, controller GUI
  navigation/activation, settings, remap capture, focus and hotplug handling;
  passed with both bounded-patch and spherical-stream terrain presentation.

Physical-controller feel, actual USB/Bluetooth unplugging, Steam Input behavior,
SteamOS packaging and television-distance usability still require hands-on
testing. This build does not claim those are qualified.

The full existing `apsis-drift-tests` executable was rerun under both compilers.
Both report the same eight failed assertions as the preceding film baseline,
with identical failure messages and planetary replay checksums. Those existing
[numerical compatibility failures](NUMERICAL_COMPATIBILITY_FINDING.md) remain
open; reference values were not changed to make this increment appear green.

Run headless input checks:

```sh
"$GODOT_BIN" --headless --path experiments/godot-freedom --script res://input_test.gd
"$GODOT_BIN" --headless --path experiments/godot-freedom --script res://live_test.gd -- "$SNAPSHOT"
ctest --test-dir build -R 'godot-(analog-input|snapshot|planet-stream)-contract' --output-on-failure
```

Native integration smoke (use an existing snapshot, absolute asset path and a
new output PNG path; synthetic settings are not saved):

```sh
"$GODOT_BIN" --path experiments/godot-freedom --audio-driver Dummy \
  --script res://controller_smoke.gd -- \
  --snapshot="$SNAPSHOT" --assets="$PWD/assets/visual" \
  --live=true --pilot=true --controls-persist=false --render-size=1920x1080 \
  --controller-capture="$PWD/build-godot/controller-review.png"
```

Implementation reference: Godot's official
[controller input guidance](https://docs.godotengine.org/en/stable/tutorials/inputs/controllers_gamepads_joysticks.html)
and [Input API](https://docs.godotengine.org/en/stable/classes/class_input.html).
No external UI art, controller logos, audio, or generated textures were added.

## Next gate

Playtest this control surface on physical Xbox and PlayStation controllers;
then connect real cockpit telemetry and tackle suitable-terrain landing.
Sensors/shutters and broader progression remain separate bounded increments.
