# Thrust flight lab 1 — handling before visual polish

Follow-up: [presentation 10](FLIGHT_PRESENTATION_10.md) adds live instruments,
clean/debug modes, stars, orbit camera, orbital telemetry and practice starts,
with continuous ascent/coast/atmospheric return test evidence.

2026-09-18. Opt-in, unsaved six-DOF flight experiment. This supersedes the
interim layout in [control direction 08](FLIGHT_CONTROL_DIRECTION_08.md), not
the existing terminal simulation, saves, golden checksums or captured films.

```sh
tools/run_godot_study.sh --stream=true --relief=true --pilot=true \
  --flight-model=thrust --start-paused=true
```

Omit `--flight-model=thrust` for the unchanged legacy physics. The lab requires
interactive controls and live/streaming mode. It is not the completed Freedom
milestone and does not implement landing, collisions, fuel, damage or saving.

Native startup/reset now uses the [repeatable surface practice survey](SAFE_SURFACE_START.md)
to begin above nearby ridges. It preserves the original seed and reference
coordinates; historical standalone lab fixtures remain unchanged.

## Controller layout 4

The agreed follow-up removes roll's modifier. Mapping stays the same in
atmosphere and space; environmental forces, not control meanings, change.

| Action | Xbox / PlayStation | Keyboard |
| --- | --- | --- |
| Main acceleration | RT / R2, analog | W |
| Weaker retro acceleration | LT / L2, analog | S |
| Roll / pitch | Left stick; pull back pitches up | Z/X, I/K |
| Yaw / vertical translation | Right stick | A/D, Space/Ctrl |
| Lateral translation | LB / L1 left, RB / R1 right | Q/E |
| Head-look | Hold left stick click / L3, right stick | Hold Alt, arrows |
| Translation/gravity assist | Y / Triangle | F |
| Cockpit / chase | X / Square | C |
| Recenter head | View / Create | Home |
| Pause / controls | Start / Options | Esc |

Head-look release snaps to the ship centerline; it does not rotate the ship.
Head-look takes priority over yaw and vertical translation, including their
keyboard equivalents. Center those controls after leaving head-look to prevent
unintended yaw/thrust. Pitch, direct analog roll, bumper strafe and both engines
remain available while looking. Left-stick-click is a remappable hold, not a
toggle. There is no roll modifier and no automatic atmospheric control swap.
All bindings remain editable; triggers use a linear response after the chosen
deadzone, sticks use the adjustable response curve. Both engines can be
demanded independently. New settings use `freedom-controls-v4.json`; v1/v2/v3
profiles remain untouched. Legacy physics also uses layout 4, but cannot
consume pitch/roll/assist actions. Old screenshots show historical mappings.

## What changes physically

`experiments/godot-freedom/thrust_flight.hpp` owns the fixed 120 Hz model:
planet-centered double-precision position/velocity, full orthonormal attitude,
bounded angular-rate control, engine spool, body-relative thrust, inverse-square
gravity and density-dependent drag. Godot consumes the resulting full pose.
The lab has its own `lab1:` checksum identity and refuses mixed legacy input.

The old low-altitude model asks for a target velocity, capped at 120 m/s.
The lab asks for thrust. Main acceleration is provisionally 45 m/s², retro
9 m/s²; sideways jets 14 m/s², dorsal/ventral translation 26/18 m/s².
These are game tuning values for an 8-tonne fictional shuttle, not a validated
engine design or human g-tolerance model. Coasting is real; main thrust is not
automatically applied to stop forward drift. Retro opposes forward motion but
continues into reverse if held. Turning around and burning the main engine is
more effective than the weaker retro jets. There is no instant brake.

Assist counters gravity and damps lateral/vertical slip **within available
thruster authority**. It can saturate when tilted or in stronger gravity.
Gravity compensation can request longitudinal thrust too; the HUD distinguishes
commanded engine levels from actual combined body-axis thrust acceleration.
Turning translation/gravity assist off leaves these corrections to the pilot.
Attitude-rate damping remains on in both modes: this is not unassisted torque
flight. Engine/jet actuation is currently telemetry, not animated exhaust or
moving engine geometry. Retro is modeled as opposing thrusters, not a magically
reversed main nozzle. Fuel and actuator allocation are future work.

Atmospheric density uses a pressure-scaled exponential with an authored 8.5 km
scale height. Drag follows the [NASA drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/drag-equation/)
with different authored effective areas along the body axes. Dynamic pressure
continuously reduces maximum attitude rate; there is no arbitrary atmospheric
ban on roll. This is a powered, wingless shuttle approximation: no wing lift,
stall, wind, rotating atmosphere, shock heating or Mach-dependent coefficients.
One body's gravity is not a solar-system/n-body dynamics implementation.

A conspicuous **16 m floor guard** still prevents ground penetration; this is
a test aid, not believable terrain collision or landing. It can hide mistakes
near the surface. Fly clear of terrain when judging gravity and braking.
The existing fixed-clock catch-up budget remains: slow rendering can drop
simulation time, reported by the bridge, not silently improve benchmark claims.

## Evidence and remaining playtest

GCC and Clang tests both measured 349.982 m/s (about 1,260 km/h) after 20 seconds
of full thrust in the seed-42 dense-atmosphere fixture at an initial 2 km
altitude. At 100 km, two-second main/half/retro tests reached respectively
82.6874 / 43.2187 / 17.475 m/s. These are reproducible isolated test conditions,
not guarantees for every attitude, planet or streamed terrain encounter.

Tests cover all seven actuator channels, invalid/nonfinite input/state, buffer
lengths, fixed-step rejection, atomic failure, vacuum coasting, altitude-dependent
drag, weak-retro versus flip/main braking, all six motion axes, 10,000 ticks of
attitude normalization, floor guard and identical 30/60/144 Hz constant-command
traces. Real extension tests additionally cover old/new model isolation.
Native synthetic controller tests exercise the rendered cockpit pose, analog
propulsion, pitch/roll, head-look, assist, remapping, menus and safety gates.
Synthetic tests do not establish physical-controller feel or SteamOS support.

Next: pilot playtesting of acceleration, turning, braking distances and assist;
then collision/landing and actuator/propellant limitations. No economy or visual
upgrade is a prerequisite. The pre-existing numerical compatibility findings
in [this report](NUMERICAL_COMPATIBILITY_FINDING.md) remain separate; no historical
golden has been rewritten to approve the lab.
