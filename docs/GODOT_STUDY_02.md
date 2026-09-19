# Godot study 02 — live C++ flight

Historical baseline; [study 03](GODOT_STUDY_03.md) adds the closed cabin, seated
head-look, structural mounts and optional versioned C++ relief experiment.

2026-09-18. The snapshot proof now has an optional real C++ GDExtension adapter.
This advances #244's evaluation; it does **not** complete that issue or select
Godot for production.

Run from the repository root:

```sh
tools/run_godot_study.sh --live=true
```

The launcher builds/stages the adapter, regenerates the seed-42 snapshot if
missing and launches the existing native study. Godot 4.5+ is required; 4.7.2 was
tested. The Linux x86-64 library mapping is currently the only platform setup.
The first live build fetches pinned `godot-cpp` 4.5 bindings (MIT, commit
`e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77`). Their license is staged beside the
experimental library. Only a small binding profile is generated; no engine fork
or extracted generic game engine is introduced.

## What is interactive

W/S thrust, A/D heading, Q/E strafe, Space/Ctrl rise/fall. C switches between
the external camera and the existing cockpit interior. V pauses, Backspace
starts a fresh experimental flight, Escape exits. Right mouse plus WASD/QE
moves the external inspection camera. Views 2/3/4 pause flight for asset/terrain
inspection; 1 returns to flight. Losing focus pauses and suppresses held flight
keys until release; returning cannot silently reapply a held throttle key.

The cockpit mesh follows the ship's C++ pose. The upper overlay reports actual
tick, speed and terrain clearance; **the mesh's instrument displays remain static
artwork**. Interior/exterior placement is a presentation fixture, not a physical
module, collision or walkable-space integration.

[Live cockpit PNG](media/freedom-live-cockpit.png) and
[live chase-view PNG](media/freedom-live-flight.png) are direct 3840×2160 Vulkan
viewport captures, with adjacent JSON reports. Native key events were injected
for smoke testing; this is not a human playtest. Focus notifications were
explicitly simulated to test policy, not the remote desktop's event delivery.
The capture is wall-time scheduled, so its final tick/checksum can vary with
startup stalls; it is not the deterministic replay fixture below.

## Ownership and safeguards

`FreedomBridge` is a small RefCounted adapter. It owns the temporary C++ planet,
tile cache, local frame, `PlanetaryFlightState` and existing `FixedStepClock`.
Godot supplies elapsed time and eight input bits; the adapter emits tick-addressed
press/release commands and calls `advance_planetary_flight` at the existing
120 Hz. Godot neither integrates velocity nor changes the generated terrain.
Catch-up remains capped at 15 steps, and discarded time is exposed in diagnostics.

Initialization regenerates the planet and frame from the versioned recipe and
checks the initial C++ state checksum against the snapshot before replacing an
existing session. Malformed initialization leaves the previous session intact.
Negative/non-finite time, elapsed values above 60 seconds, invalid control masks,
invalid dimensions/LOD and incompatible generator versions are rejected.

The visual patch is still 64 km across. Flight pauses near its edge; there is no
streaming yet. The original 16 m minimum-clearance clamp is still present—this
is **not landing or collision**. A late simulation-step error is treated as a
fatal experiment error; previously accepted ticks in that frame are not rolled
back. Save files and the production terminal application are not touched.

## Checks completed

- Both GCC and Clang build the shared adapter. Each adapter passes the actual
  Godot headless boundary test; the normal system math library is used.
- Every one of the 301 exported reference checkpoints matches live C++ state
  across 1,200 ticks. Final fixture checksum: `6185323095807381229`.
- Replaying the same input through 30, 60 and 144 FPS schedules reaches the
  same tick/checksum and presentation state. Invalid inputs and failed reset
  leave state unchanged; catch-up preserves the existing 15-step limit.
- GPU smoke checks exercise key press/release, actual turning/thrust, pause,
  focus loss/regain and switching to the pilot camera. The original first
  pause assertion ran before queued input dispatch; the corrected check allows
  dispatch, explicitly verifies pause activation, then checks frozen ticks.
- No existing golden was replaced. The separate
  [math compatibility finding](NUMERICAL_COMPATIBILITY_FINDING.md) still needs a
  production policy; local replay success does not fix cross-library arithmetic.

Run the live boundary test after building the adapter:

```sh
godot --headless --path experiments/godot-freedom \
  --script res://live_test.gd -- "$PWD/build-godot/snapshot-42.json"
```

Native smoke/capture (requires a graphics session):

```sh
godot --path experiments/godot-freedom --audio-driver Dummy \
  --script res://live_smoke.gd -- \
  --snapshot="$PWD/build-godot/snapshot-42.json" \
  --assets="$PWD/assets/visual" --live=true --render-size=3840x2160 \
  --capture="$PWD/build-godot/live-cockpit.png"
```

## Still not implemented

Streaming/local-origin changes, physical landing, walking/reboarding, station
docking, neighboring-system jumps, persistence integration, live instruments,
audio integration, controllers and target-device/HDR validation remain open.
Existing MIDI/audio assets and code are retained; the study is silent. Art and
terrain materials are still inspection-quality, not the cinematic end target.

Next: bounded terrain streaming and local-origin handling while preserving the
live tick/input boundary, alongside a deliberate portable-math decision. Do not
add landing by teleporting to a mission destination or label the clearance clamp
as a completed landing system.
