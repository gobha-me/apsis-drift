# Apsis Drift

[![CI](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml/badge.svg)](https://github.com/gobha-me/apsis-drift/actions/workflows/ci.yml)
[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE.md)

A deterministic, procedurally generated spaceflight game in development.
**Godot is the primary native presentation engine.** C++23 owns the universe,
simulation, stable identities and versioned saves. The active direction is
[Freedom: exploration before progression](docs/ROADMAP.md).

## Play the native prototype

The current native bridge and runtime checks support **Linux x86_64**. Install
Godot 4.7.2, CMake 3.28+, Python 3.11+, Git and a C++23 compiler. Set `GODOT_BIN`
if Godot is not on PATH. The launcher builds the bridge and prepares the
source-verified assets; the first build fetches pinned dependencies.

```sh
tools/run_godot_native.sh
# Or select a journey directly:
tools/run_godot_native.sh --new-game=42
tools/run_godot_native.sh --continue=/absolute/path/to/freedom-save.json
```

New Game starts on Origin Station. Walk to Wayfarer, board and sit, release the
port and use real thrusters to depart. The prototype supports atmospheric
flight, suitable dry-ground landing, suited walking, reboarding and home return.
It starts with finite flight fuel and three jump charges; station refueling is
free. A [local moon voyage](docs/AIRLESS_MOON.md) adds charged travel to one
airless moon, manual descent, walking, ascent and a jump home.
An [asteroid inspection trip](docs/ASTEROID_INSPECTION.md) adds close flight around
one existing minor body, an explicit stand-off limit and a charged return.
The title screen also offers [four charted systems](docs/FREEDOM_TOPOLOGY.md#optional-four-system-region)
for a new journey, or [two additional uncharted systems](docs/FREEDOM_TOPOLOGY.md#discoverable-systems)
to discover in flight; existing saves retain their original map.
An optional [Origin fuel service](docs/ORIGIN_FUEL_SERVICE.md) sends one actual
service Wayfarer to a supported home-Moon ship; its fuel and charges are finite.
**Esc / Start · Controls** opens paused controls and Flight basics.

[First Flight instructions](docs/FIRST_FLIGHT.md) explain the route and current
limitations. [Save, Load and Continue](docs/NATIVE_PROFILES.md) preserve C++ world
state; Quit does not autosave. Manual controller/display qualification remains
open. This is a playable prototype, not the complete MVP or beta.

## Architecture and repository

C++ owns world generation, flight, contact, player state, saves and headless
checks. Godot owns native presentation and translates input into C++ commands.
TermForge's terminal path is retained and secondary; terminal parity does not
gate native features. See [the ownership decision](docs/GODOT_ADOPTION.md) and
[terminal retention decision](docs/TERMINAL.md#retention-decision--2026-10-13).

| Location | Purpose |
| --- | --- |
| `src/`, `include/apsis_drift/`, `test/` | Authoritative application and headless contracts |
| `godot/` | Native scenes, presentation, input, shaders and runtime tests |
| `src/godot/`, `include/apsis_drift/godot/`, `test/godot/` | C++ bridge and native adapters/contracts |
| [cmake/](cmake/README.md) | Admitted data preparation and grouped test registration |
| [assets/](assets/README.md) | Source-verified packages and retained content with provenance |
| [tools/](tools/README.md) | Launch, verification, preparation and study entry points |
| [docs/](docs/README.md) | Direction, system contracts, guides and preserved research |

## Contribute

Start with [CONTRIBUTING.md](CONTRIBUTING.md),
[development setup and checks](docs/DEVELOPMENT.md) and
[the documentation index](docs/README.md). Follow dated Freedom issue amendments
and preserve deterministic seeds, independent streams and supported saves.

The [native project guide](godot/README.md) covers Godot setup. The
[terminal guide](docs/TERMINAL.md) and
[historical reference](docs/PROJECT_REFERENCE.md) preserve the other frontend
and earlier evidence. Code uses the [BSD 3-Clause License](LICENSE.md);
[third-party notices](THIRD_PARTY_NOTICES.md) and media metadata cover content.
