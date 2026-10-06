# Native Godot presentation

Godot is Apsis Drift’s native presentation engine. C++23 owns generated worlds,
flight, station walking, saved state and contact decisions. This project renders
that state and translates input into C++ commands. See the
[ownership decision](../docs/GODOT_ADOPTION.md).

## Run from the repository root

Install Godot 4.7.2, CMake 3.28+, Python 3.10+, Git and a C++23 compiler. Set `GODOT_BIN` to
an installed executable if `godot` is not on PATH. The launcher fetches pinned
C++ dependencies on its first build; it does not download Godot.

```sh
tools/run_godot_native.sh --new-game=42
tools/run_godot_native.sh --continue=/absolute/path/to/freedom-save.json
```

Select exactly one mode. Add `--headless-validate` to check saved-start selection
without opening a window. The Godot project’s default scene is
`scenes/native_start_shell.tscn`; the launcher supplies its explicit seed or save
selection and prepares the source-verified asset packages.

New Game starts on Origin Station. WASD or the left stick walks; right-drag or
the right stick looks. Escape opens pause controls. Save As persists through
C++; Quit does not autosave. Continue starts paused. Boarding, sitting and
leaving the station remain integration work. See [current scope](../docs/ROADMAP.md)
and [saved station walking](../docs/SAVED_STATION_WALK.md).

## Build and headless contracts

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF \
  -DAPSIS_DRIFT_GODOT_SPIKE=ON -DAPSIS_DRIFT_GODOT_LIVE=ON
cmake --build build-native --parallel 4
ctest --test-dir build-native --output-on-failure
python3 tools/test_godot_native.py --godot /path/to/godot \
  --build-dir build-native --test freedom_start --test native_shell
```

The historical CMake `GODOT_SPIKE` option name remains accepted for build
compatibility. The selected runtime is permanent; optional rendering studies
are invoked separately. CMake builds the bridge under `build-native/src/godot`
and stages its extension descriptor, shared library and binding license into
this project’s ignored `bin/` directory. Headless tests stage a private copy of
the complete resource tree and matching binaries.

The legacy project name remains the persisted `user://` namespace for control
and audio preferences, so this directory move does not relocate those settings.

## Layout

| Location | Purpose |
| --- | --- |
| `scenes/` | Saved-state native entry scene |
| `scripts/native/` | Native start, station walking and saved flight views |
| `scripts/flight/`, `scripts/world/` | Flight presentation and terrain views |
| `scripts/ui/`, `scripts/audio/` | Input, menus and playback |
| `scripts/ships/`, `scripts/characters/`, `scripts/assets/` | Presentation components |
| `shaders/`, `settings/` | Runtime shaders and fixed presentation configuration |
| `tests/` | Headless GDScript contracts |
| `studies/` | Opt-in inspection, review, capture and film tools |
| `../src/godot/` | C++ presentation bridge and native adapters |
| `../include/apsis_drift/godot/` | Native C++ interface headers |
| `../test/godot/` | C++ native contracts and fixture utilities |

## Optional studies

```sh
tools/run_godot_study.sh --stream=true --relief=true --pilot=true --flight-model=thrust
```

The study launcher explicitly selects `studies/main.tscn`. It retains its seed42
practice and inspection data separately from player saves. Read the
[study and preview guide](../docs/GODOT_STUDIES.md) for existing capture tools;
a smooth offline film is not a gameplay or frame-rate qualification.

See [development](../docs/DEVELOPMENT.md), [native starting assembly](../docs/NATIVE_STARTING_ASSEMBLY.md)
and [native asset packages](../docs/NATIVE_STARTER_ASSETS.md) for validation and asset ownership.
