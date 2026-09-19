# Native rotational coasting — lab 2

2026-09-19. Implements the agreed control direction: turning flight assistance
off also releases rotational stabilization. This is an unsaved native practice
model, not the completed planetary dynamics, damage or persistence milestone.

Later native lab-3 amendment: [orbit-preserving assist](ORBIT_ASSIST_18.md)
supersedes automatic translation assistance in space. The angular behavior
below remains unchanged; lab 2 remains available as a historical test model.

## Pilot contract

The existing controller layout and saved bindings remain unchanged. Y/Triangle
(keyboard F) toggles flight assistance; L3 head-look still only moves the camera.

- **ON:** released rotation controls request zero angular velocity. Available
  thruster torque slows a spin over time; switching ON cannot snap it to zero.
  Existing bounded gravity compensation and lateral/vertical drift correction
  remain active. Neither mode holds forward speed.
- **OFF:** a released rotation axis commands exactly zero torque. Releasing all
  rotation controls lets angular momentum coast. Countersteer, or turn
  assistance back ON, to slow the spin.
- A deflected stick still requests a bounded turn rate in both modes. OFF is
  not an unlimited raw-torque stick mode: the active-axis controller can apply
  bounded torque to approach the requested rate, including compensating
  gyroscopic coupling on that active axis. Released axes receive no such
  correction. This preserves the familiar held-stick action.

The shuttle has unequal principal moments of inertia. With mixed-axis spin,
body-axis angular velocity need not stay constant even with all thrusters off;
the conserved quantities in vacuum are world angular momentum and rotational
energy. This can produce tumbling, not hidden stabilizer activity.

The cockpit reports `OFF / COAST` and distinguishes coasting from stabilized
spin. The debug view and pause help explain the selected model. No remapping,
automatic atmosphere/space layout swap, or new button is introduced.

## State and integration boundary

The native surface-practice entry point selects `thrust-lab-2`, with a `lab2:`
checksum namespace. It stores an authoritative canonical quaternion and body
angular velocity. Presentation axes are derived from that quaternion, never
fed back from Godot. The initial authored basis is converted once at tick zero.

The shared [vacuum attitude kernel](VACUUM_DYNAMICS.md) integrates asymmetric
inertia and actual bounded torque at 120 Hz. It accepts a caller-owned
nonrotating frame without inventing a system identity for the old practice
planet. The full vacuum provider and this attitude-only helper share the same
RK4 kernel; the former's deterministic trace goldens remain unchanged.

Existing lab translation, inverse-square gravity, engine spool, atmospheric
drag and the conspicuous 16 m floor guard remain experimental and unchanged.
Dynamic pressure reduces requested turn rates as before; it is **not** an
aerodynamic moment model. Released axes can therefore coast in atmosphere too
until a real aerodynamic torque provider is implemented.

The old angular-speed safety guard (10 rad/s vector magnitude) remains a
transactional refusal boundary, not an artificial momentum clamp. These
handling tests do not establish safe or accurate behavior for every arbitrary
high-energy state. The core provider has its own explicitly documented
numerical domain.

`enable_thrust_flight()` and standalone historical lab fixtures still select
model 1 and retain their original checksums and always-damped rotation.
The version-1 safe-start survey is unchanged; its candidate is promoted to
model 2 before the native bridge commits it. Practice/survey relocation retains
the chosen model, and an explicit reset reproduces its starting state.

## Verification and remaining gate

The C++ contracts cover malformed/nonfinite state and intent, transactional
failure, canonical orientation, old model-1 goldens, principal-axis coasting,
mixed-axis conservation, countersteering, bounded assisted recovery, sustained
commands and fixed-step render-cadence independence. The Godot bridge contract
checks version identity, buffers, telemetry, practice/reset and 30/60/144 Hz
equivalence. The synthetic controller smoke checks that released spin reaches
the rendered ship and assistance arrests it.

Automated correctness is not handling approval. A pilot must still judge
whether OFF spin, countersteering and ON recovery feel controllable on the
physical controller. No fuel use, thruster animation, impact consequences or
completed SteamOS qualification is claimed here.

Local evidence: full GCC and Clang builds and all ten focused craft/state,
vacuum and Godot C++ contracts pass. The four original full-provider trace
goldens and four pinned model-1 diagnostic traces are unchanged. Both compiled
bridges pass the new headless coasting contract with the same cadence checksum
`lab2:11576286749180550467`. The model-2 C++ cadence fixture is
`2410862457160760375`. Native Vulkan controller and presentation smoke checks
pass, including rendered rotation, camera return, live panels and clean/debug
views. This is local synthetic evidence, not hosted CI or physical playtesting.

Partially active OFF-axis stress cases run for 600 simulated seconds. The
largest observed angular speed is 8.03856 rad/s when holding pitch and yaw
after an existing mixed-axis spin; released roll receives exactly zero torque.
A separate 3,600-second diagnostic observed the same peak. This demonstrates
the tested trajectory, not a mathematical guarantee for every possible input.
Commanded rate limits are **not** limits on uncommanded spin.

Reproduce the headless bridge contract after preparing the native build:

```sh
tools/run_godot_study.sh --stream=true --relief=true --prepare-only=true
godot --headless --path experiments/godot-freedom \
  --script res://rotation_coast_test.gd -- \
  "$PWD/build-godot/snapshot-42-stream-true.json"
ctest --test-dir build --output-on-failure \
  -R 'craft-frame|rigid-body|vacuum-dynamics|godot-.*contract'
```
