# Retained terminal frontend

TermForge remains the legacy terminal presentation and input path. Native Godot
presentation is primary; terminal parity does not gate new native features.
The application continues to own simulation, world generation and saves.
This frontend's career/menu and save contracts are distinct from native Freedom.
See [ownership](GODOT_ADOPTION.md) and [native setup](DEVELOPMENT.md).

## Retention decision — 2026-10-13

[Issue #292](https://github.com/gobha-me/apsis-drift/issues/292) selects retention
as a secondary diagnostic frontend. It preserves the historical terminal
save/career checks, application raster benchmarks and live terminal capture.
These remain useful comparisons while native development proceeds; terminal
feature parity remains outside the native acceptance boundary.

The ordinary [native launcher](../tools/run_godot_native.sh) builds with
`APSIS_DRIFT_TERMINAL=OFF` and `APSIS_DRIFT_RTAUDIO=OFF`. Native New Game,
Save/Load/Continue and the [First Flight route](FIRST_FLIGHT.md) consume the C++
core. The [headless application checks](DEVELOPMENT.md#headless-application-checks)
build that core with the same disabled terminal options; the `native-core` CI
jobs exercise this dependency boundary with GCC and Clang.

The retained `apsis-drift --benchmark` still requires TermForge: it runs the
terminal framework's headless frame/driver path, including application raster
work and submission cost. The capture commands below also require that frontend.
A future retirement must preserve those measurement paths independently and
provide a recoverable source/build recipe. This decision retains the current
source, build targets and CI contracts.

## Retained source checkpoint

The immutable retained revision is
[`ab2a70f8615f43313b6704bfbc7c18e25dbe2dda`](https://github.com/gobha-me/apsis-drift/tree/ab2a70f8615f43313b6704bfbc7c18e25dbe2dda).
It contains the terminal entry point, CMake configuration, tests and the build,
benchmark and capture instructions. Its CMake fallback pins TermForge v0.57.23;
the revision's own dependency pins remain authoritative.

To rerun that checkpoint from a repository checkout containing the revision,
use a detached worktree and its own build directory:

```sh
git worktree add --detach ../apsis-drift-terminal ab2a70f8615f43313b6704bfbc7c18e25dbe2dda
cd ../apsis-drift-terminal
cmake -S . -B build-terminal -DCMAKE_BUILD_TYPE=Release \
  -DAPSIS_DRIFT_TERMINAL=ON -DAPSIS_DRIFT_RTAUDIO=OFF
cmake --build build-terminal --parallel 4
ctest --test-dir build-terminal --output-on-failure
./build-terminal/apsis-drift --benchmark 3
```

The following run/capture commands apply there with `build-terminal` in place
of `build`. RtAudio is optional for this diagnostic recipe; it can be enabled
with the platform headers described below.

## Build

Use CMake 3.28+, Python 3.10+, Git and a C++23 compiler. The default configuration includes the
terminal application. It finds a compatible TermForge package or sibling checkout,
otherwise fetches pinned TermForge v0.57.23. RtAudio 6.0.1 is enabled by default;
its platform headers are needed for device output (`libasound2-dev` on Linux).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Disable device audio explicitly when it is not needed:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DAPSIS_DRIFT_RTAUDIO=OFF
```

An explicit TermForge checkout can be selected with
`-DAPSIS_DRIFT_TERMFORGE_SOURCE_DIR=/path/to/termforge`.

## Run and save

```sh
./build/apsis-drift
./build/apsis-drift --version
./build/apsis-drift --new-game-seed 42 --save profile.json
./build/apsis-drift --load profile.json --save profile-copy.json
```

The terminal requires a supported Kitty or truecolor ANSI presentation and
semantic press/repeat/release input supplied by TermForge. Diagnostic forcing
cannot give a terminal capabilities it lacks. A no-option run opens the retained
career title menu; its onboarding/progression is not a prerequisite for native
Freedom movement. Consult [save compatibility](SAVE_FORMAT.md) and
[the menu/profile contract](MENU_AND_PROFILE_CONTRACT.md) before reusing profiles.

## Headless measurement and live capture

```sh
./build/apsis-drift --benchmark 180
./build/apsis-drift --benchmark 180 --driver ansi --report ansi.json
./build/apsis-drift --benchmark 180 --driver kitty --report kitty.json
./build/apsis-drift --benchmark 1 --snapshot landscape.ppm
./build/apsis-drift --capture-seconds 60 --report landscape-capture.json
```

The headless benchmark measures application rendering and TermForge submission.
It does not measure PTY, terminal, proxy or display performance. Live capture
forwards frames through the actual terminal path, so its cadence includes that
path. Neither is a native Godot renderer benchmark. Audio devices are not probed
by benchmark, capture or acceptance modes.

For retained acceptance schedules, controls, optional MIDI targets and dated
performance results, see the [historical detailed reference](PROJECT_REFERENCE.md)
and [Flight Deck performance envelope](PERFORMANCE_ENVELOPE_2026-08-15.md).
The [development guide](DEVELOPMENT.md) retains GCC/Clang and pinned quality checks.
