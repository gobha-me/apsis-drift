# Apsis Drift

[![CI](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE.md)

A deterministic, procedurally generated spaceflight game in development.
**Godot is the primary native presentation engine.** C++23 owns the universe,
simulation, stable identities and versioned saves; Godot presents that state.
The active direction is [Freedom: exploration before progression](docs/ROADMAP.md).

## Try the native slice

Install Godot 4.7.2, CMake 3.28+, Python 3.10+, Git and a C++23 compiler. Set `GODOT_BIN` if
Godot is not on your executable search path. The launcher builds the C++ bridge
and prepares the fixed asset packages; the first build fetches pinned dependencies.
See [native setup](godot/README.md) and [development/testing](docs/DEVELOPMENT.md).

```sh
tools/run_godot_native.sh
# Or select a journey directly:
tools/run_godot_native.sh --new-game=42
tools/run_godot_native.sh --continue=/absolute/path/to/freedom-save.json
```

The [start screen](docs/NATIVE_TITLE.md) offers New Game, Continue, Flight basics
and Quit. Direct command-line selection accepts one mode. New Game accepts
unsigned 64-bit seeds, including zero.
It starts a first-person actor on Origin Station's hub floor. WASD or the left
stick walks; right-drag or the right stick looks. Escape pauses.

Walk through the workshop to D1, release movement controls and press **E**,
controller **A** or **Board Wayfarer**. A short authored boarding sequence moves
through the hatch into the pilot seat. Release the port to use the existing
flight controls. Withdraw with **Fall** and brake with **Rise**; clear the port
column before forward thrust. An optional [approach aid](docs/PORT_APPROACH_AID.md)
helps an aligned craft return using thrusters; **Capture port** remains your
action. The cyan [HOME marker](docs/HOME_NAVIGATION.md) shows the station bearing
and range during flight. **Leave seat** returns you to the station while attached at D1.
The open docking well still stops ordinary unsupported walking.
A [compact HUD](docs/NATIVE_HUD.md) leaves the scene visible; **Esc / Start · Controls**
and **Flight instruments & navigation** provide the detailed readouts and actions.
**Flight basics (paused)** explains the current controls, fuel, jumps, landing and return.

Boarding is a [playable prototype](docs/PLAYABLE_BOARDING_PROTOTYPE.md), with
animation and body-clearance refinements deferred. The [planetary voyage check](docs/PLANETARY_VOYAGE_PROTOTYPE.md) covers
station departure, atmospheric flight and return through public controls and
real saves, including an uninterrupted native-session regression. Manual/controller
qualification remains for First Flight. The [landed craft prototype](docs/LANDED_CRAFT.md)
adds deployed gear, certified touchdown, saved surface idle and thruster liftoff;
the wider Freedom journey remains separate work. New Game selects [finite flight
fuel and three jump charges](docs/FREEDOM_RESOURCES.md), with free replenishment
while attached to a supported station port. Historical saves keep their explicit
resource-unselected behavior. The [neighboring-system trip](docs/FREEDOM_NATIVE_TRAVEL.md)
supports chart selection, jumps, physical home return and station replenishment.
The [surface walking prototype](docs/FREEDOM_SURFACE_WALK.md) adds suited ground
movement, outside Save/Continue, nearby return and a station-to-site regression;
hatch transfer animation,
exposure policy and wider manual/controller qualification remain unfinished.

**Save As** writes the actor, craft, history and clock through C++. Continue
starts paused; [Load…](docs/NATIVE_LOAD.md) replaces the journey only after confirmation.
**Title…** returns to the start screen only after a discard confirmation.
Quitting does not autosave. Missing, corrupt or unsupported saves
refuse without rewriting the file. Historical saves keep their explicit format
and presentation rather than receiving invented actor or hardware state.

[Recorded ship audio](docs/NATIVE_SHIP_AUDIO.md) is optional with a pair of
prepared user-provided WAV loops and the existing saved mix controls.

For a selection check without a window:

```sh
tools/run_godot_native.sh --new-game=42 --headless-validate
```

This checks the selected C++ start, not renderer performance or a playable loop.
See [station walking](docs/SAVED_STATION_WALK.md),
[saved flight](docs/SAVED_NATIVE_FLIGHT.md) and
[the saved starting assembly](docs/NATIVE_STARTING_ASSEMBLY.md) for current contracts.

## Architecture

- **C++ application:** world and terrain generation, simulation, contact/support,
  player state, save compatibility, deterministic audio policy and headless tests.
- **Godot:** native scenes, cameras, UI, materials and input translated into C++
  commands. It consumes the same authoritative world and state.
- **TermForge:** the retained terminal presentation, structured input, capability
  detection and degradation. Terminal parity does not gate native work.

The [ownership decision](docs/GODOT_ADOPTION.md) defines this boundary.
Studies and offline films are separate from ordinary saved play; their results
are not evidence that boarding, landing or runtime performance is qualified.

## Repository map

| Location | Contents |
| --- | --- |
| `godot/scenes/` | Native presentation scenes |
| `godot/scripts/`, `godot/shaders/` | GDScript presentation and shaders |
| `godot/tests/`, `godot/studies/` | Native contracts and explicit studies |
| `src/godot/`, `include/apsis_drift/godot/` | C++ native bridge and adapters |
| `test/godot/` | C++ native adapter contracts |
| `src/`, `include/apsis_drift/`, `test/` | Application code and headless tests |
| `docs/` | Product direction, contracts and development guides |
| `assets/` | Packaged content with provenance and licenses |
| `tools/` | Build, validation, preparation and capture entry points |

## Contribute and explore

Start with [development setup and checks](docs/DEVELOPMENT.md) and
[the grouped documentation index](docs/README.md). Preserve deterministic seeds,
independent streams, versioned saves and the C++/Godot ownership split.
Native gameplay work follows the dated issue scope and [roadmap](docs/ROADMAP.md).

The [retained terminal guide](docs/TERMINAL.md) covers its separate build, entry
and benchmarks. The [historical detailed reference](docs/PROJECT_REFERENCE.md)
preserves the former README's terminal history, studies and evidence catalog.

Code uses the [BSD 3-Clause License](LICENSE.md). See
[third-party notices](THIRD_PARTY_NOTICES.md); media assets carry their own
provenance and license metadata.
