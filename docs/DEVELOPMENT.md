# Development and testing

Run commands from the repository root. Native presentation consumes authoritative
C++ state; see [ownership](GODOT_ADOPTION.md) and [the roadmap](ROADMAP.md).
The [native project guide](../godot/README.md) describes scenes, bridge setup and
study modes. The [terminal guide](TERMINAL.md) covers the retained frontend.

## Requirements

- CMake 3.28+, Git and a C++23 compiler (GCC 13+ or Clang 19+ recommended).
- Python 3.10+ for build-time source preparation, native test runners and asset tools.
- Godot 4.7.2 for the currently qualified native runtime checks; use an installed
  executable or set `GODOT_BIN` to its executable path.
- clang-format 20, clang-tidy 20 and Clang 20 for repository quality checks.

CMake fetches pinned dependencies when compatible installed packages are absent.
The native/headless build can disable TermForge and RtAudio. Terminal audio adds
platform development headers; optional MIDI audition checks require FFmpeg.
Godot is installed separately; the local launcher does not download it.

## Native build and run

The ordinary launcher builds the bridge and prepares the selected assets:

```sh
tools/run_godot_native.sh --new-game=42
tools/run_godot_native.sh --continue=/absolute/path/to/freedom-save.json
```

Reuse an existing native compiler build to avoid another dependency/build copy:

```sh
tools/run_godot_native.sh --build-dir=build-native-gcc --new-game=42
```

Build directories are relative to the repository root, or absolute. The default
is `build-native`. Existing caches must belong to this checkout; source
directories are refused. Assets are prepared beneath the selected build. The
launcher atomically installs its selected bridge even when no relink is needed,
so a previous compiler/build cannot leave a stale library in the native project.

For an explicit native build, including the exporter and contract fixtures:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF \
  -DAPSIS_DRIFT_GODOT_SPIKE=ON -DAPSIS_DRIFT_GODOT_LIVE=ON
cmake --build build-native --parallel
ctest --test-dir build-native --output-on-failure
```

The existing `GODOT_SPIKE`/`GODOT_LIVE` option names remain the native build
switches. Keep compiler builds in distinct directories.

## Headless application checks

C++ application tests do not require Godot or terminal protocol dependencies:

```sh
cmake -S . -B build-core-gcc -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF
cmake --build build-core-gcc --parallel
ctest --test-dir build-core-gcc --output-on-failure

cmake -S . -B build-core-clang -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF
cmake --build build-core-clang --parallel
ctest --test-dir build-core-clang --output-on-failure
```

Publication changes must pass both GCC and Clang. Preserve headless simulation
and benchmarks independently of the native renderer.

## Native runtime contracts

Finish the native build before starting the Linux runtime runner:

```sh
python3 tools/test_godot_native.py \
  --godot "$GODOT_BIN" --build-dir build-native
python3 tools/test_godot_native.py \
  --godot "$GODOT_BIN" --build-dir build-native --test native_shell
python3 test/native_runner_test.py
```

Set `GODOT_BIN` to the actual executable for these explicit commands. `--test`
is repeatable and selects a subset. The runner stages an isolated project and
retains logs, reports, fixture/save outputs and copied source under `build-godot`.
After success it removes that run's generated asset/import/cache copies and
staged binaries, recording removed paths/bytes and the original binary hashes.
Use `--keep-work` when the successful stage is needed for another replay or
rendered capture. Failed or interrupted runs retain their complete stage for
diagnosis. This never cleans earlier runs, source studies or compiler caches.
The runner does not build or synchronize with concurrent builds.
A crash, timeout, script error or missing completion marker
fails even when other output looks successful.

These headless contracts exercise real C++/GDScript integration and fixtures.
They do not qualify GPU appearance, physical controllers, audio devices or
frame-rate performance. A launch with `--headless-validate` checks start selection
without importing the complete rendered view. Editor import is another path;
read the [known editor shutdown defect](GODOT_EDITOR_IMPORT.md) before using it.

## Format and lint

```sh
tools/format.sh --check
tools/format.sh --fix
tools/lint.sh
```

The formatter rejects versions other than clang-format 20. Lint uses Clang and
clang-tidy 20, configures a separate analysis build and validates suppression
policy. Suppressions must name exact checks and include an inline justification
accepted by `tools/check_nolint.sh`; blanket suppressions are refused.

Use `CLANG_FORMAT`, `CLANG_TIDY`, `RUN_CLANG_TIDY` and `CLANGXX` for explicitly
selected executables. `APSIS_DRIFT_TIDY_BUILD_DIR` and
`APSIS_DRIFT_TIDY_JOBS` select the analysis directory and parallelism.

Test invalid dimensions, nonfinite state and buffer boundaries before visual
smoke checks. Preserve deterministic streams and explicit save compatibility.
See [asset provenance](ASSET_PROVENANCE.md) for content metadata and
[the release checklist](RELEASING.md) for publication requirements.
