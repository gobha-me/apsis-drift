# Freedom: native presentation study 01

Current cockpit/ship assets use the [shared flight cell and suited fit review 05](../../docs/COCKPIT_FIT_05.md).
The [control-clearance refinement 07](../../docs/COCKPIT_CONTROLS_07.md) lowers
the FLIGHT/JUMP pods without moving the displays or pilot eye.
Interactive live flight now has a [controller-first input foundation 06](../../docs/CONTROLLER_FOUNDATION_06.md):
analog C++ demands, remapping, saved settings and a controller-navigable pause
menu. Physical controller and SteamOS qualification remain outstanding.
Current [layout 4 / thrust flight lab](../../docs/THRUST_FLIGHT_LAB_09.md)
uses left-stick pitch/roll, right-stick yaw/vertical, bumper strafe, analog
main/retro triggers and hold-left-stick-click head-look with release-to-center.
Native practice uses [lab 2 rotational coasting](../../docs/ROTATIONAL_COASTING_LAB_15.md):
assist OFF releases spin as well as translation; active sticks retain bounded
turn-rate commands. Historical standalone model-1 fixtures remain unchanged.
`--flight-model=thrust` opts into full C++ attitude, gravity, drag and inertia;
the legacy model remains the default. Landing and collisions are not implemented.
[Presentation 10](../../docs/FLIGHT_PRESENTATION_10.md) adds live instruments,
clean/debug modes, orbit camera, seeded stars and orbit/re-entry practice starts.
[Terrain review 11](../../docs/TERRAIN_CONTINUITY_11.md) extends consistent normals
across LOD boundaries and applies a continuous distance fade to fine shading.
Launch it with `--stream=true --relief=true --pilot=true --flight-model=thrust`.
The native pilot and exterior views assemble the same cockpit with a shared
metre-scale mount and eye anchor. Seat adjustment is currently a design fixture,
not an interactive UI. Instruments are live in thrust mode; inspection/film
modes retain their historical static artwork.

Current follow-up: [study 04](../../docs/GODOT_STUDY_04.md) adds opt-in
`--stream=true` spherical terrain streaming, bounded tile residency and a whole
planet inspection view. It uses the same authoritative C++ world. Orbital flight
is now exercised in the opt-in thrust lab, but landing remains open.
[Study 03](../../docs/GODOT_STUDY_03.md) adds a structurally
supported, closed cabin, seated head-look, stowed-gear visuals and an explicitly
opt-in `--relief=true` C++ terrain experiment. Production generators/saves remain
unchanged; the default terrain is still the original study baseline.

The owner [selected Godot for native presentation](../../docs/GODOT_ADOPTION.md)
on 2026-09-18. These studies are partial evidence for
[#244](https://github.com/gobha-me/apsis-drift/issues/244), not a playable Freedom slice. This opt-in study
does not replace the terminal application, modify saves or change generators.

An optional [live-flight follow-up](../../docs/GODOT_STUDY_02.md) now connects
native input to the existing C++ simulation. The snapshot-only mode below stays
available as the simpler inspection/replay baseline.

The [40-second preview](../../docs/FREEDOM_PREVIEW_01.md) uses this scene and the
hero-tier assets through a frame-addressed Godot film rig. It is an offline
render, not an interactive frame-rate claim. Reproduce it with
`tools/render_freedom_film.sh build-godot/new-film-directory`.

## Run

From the repository root, with the normal C++ build dependencies and Godot 4:

```sh
GODOT_BIN=/path/to/godot tools/run_godot_study.sh
```

The launcher configures the existing `build` directory, builds the optional
exporter and creates `build-godot/snapshot-42.json` if absent. It uses an installed
`godot` or the isolated downloaded engine when `GODOT_BIN` is unset. It never
downloads Godot itself. For a fresh snapshot, choose another output filename with
the exporter; do not silently reuse an old fixture after changing the exporter.

For live flight and the cockpit camera:

```sh
tools/run_godot_study.sh --live=true
```

For streamed terrain and the optional relief recipe:

```sh
tools/run_godot_study.sh --stream=true --relief=true --pilot=true
```

The first **live** build fetches pinned MIT-licensed `godot-cpp` bindings through
CMake (unlike snapshot-only mode). Godot itself must already be available.
Defaults: W/S forward/reverse, A/D yaw, Q/E strafe, Space/Ctrl rise/fall,
C cockpit/chase, Alt + arrows head-look, Home recenter. Thrust mode additionally
uses I/K pitch, Z/X roll and F assist. Esc/V or Start/Options opens settings.
On a pad: RT/LT main/retro, left stick pitch/roll (thrust mode), right stick
yaw/heave, LB/RB strafe. Hold L3 with the right stick for head-look, or orbit
camera outside; release centers, then center the stick to rearm yaw/heave.
X/Square changes view; Y/Triangle toggles assist in thrust mode; View/Create
recenters. Bindings are customizable. F3 toggles diagnostics in thrust mode.
Losing focus or disconnecting the selected pad pauses; explicit resume and
neutral controls are required. In the original `--live=true` mode the
rendered patch is bounded and its edge pauses flight. `--stream=true` removes
that presentation boundary; view 4 starts at a whole-planet inspection camera.

Legacy asset inspection (`--controls=false`, also used for ordinary captures):
1 ship, 2 station, 3 cockpit, 4 terrain overview; hold right mouse
button and use WASD/QE to move the **inspection camera**, Shift for faster motion.
Exception: cockpit view 3 and live pilot view lock translation to the seat;
RMB turns the head within yaw/pitch limits instead.
R starts/stops the recorded C++ flight in ship view; Home resets the current
camera; Escape exits. These are not player flight or walking controls. Cockpit
instruments are static asset artwork, not connected telemetry.

Capture a true 4K viewport, regardless of desktop window limits:

```sh
tools/run_godot_study.sh --view=2 --render-size=3840x2160 \
  --capture="$PWD/build-godot/station.png"
```

Capture exits after 60 warmup + 120 measured frames and writes a PNG and adjacent
`.png.json` report. Only 1920x1080 and 3840x2160 fixed render sizes are accepted.
`--replay=true` exercises recorded motion during a ship capture. This is a
windowed GPU render, not a headless benchmark or proof of responsive gameplay.
The image dimensions are checked against the requested render target.

## Ownership and preservation

`snapshot.hpp` calls the existing `generate_planet_descriptor`,
`TerrainSurfaceSampler`, coordinate conversion and `advance_planetary_flight`
APIs. It does not copy their algorithms. Planet seed 42 is a fixture, not a
newly generated universe or the mission-selected home planet.

- The snapshot carries generator versions, a planet descriptor, local frame,
  terrain colors/vertices and 301 poses spanning 1,200 fixed 120 Hz C++ ticks.
- 64-bit seeds, identities and checksums remain strings across JSON parsing.
- C++ projects the sampled surface into a double-precision local tangent frame
  before Godot receives metre-sized coordinates: east/up/minus-north → X/Y/Z.
  Planet curvature is retained; there is no height exaggeration.
- Godot creates the mesh and interpolates the recorded poses for display.
  It owns **no authoritative flight state**. Replay wrapping and inspection
  movement are presentation-only. In live mode the optional C++ extension owns
  the temporary session and calls the original flight/terrain APIs each tick.
- Existing `hero-*-near.glb` files are loaded without modifying their masters.
  Station/cockpit views are isolated exhibits, not placed world entities.
  Lighting and sky are review fixtures, not the simulated atmosphere or sun.
- Existing MIDI, TinySoundFont, RtAudio, procedural audio and music files remain
  intact. The read-only viewer remains silent. The opt-in native thrust study
  now has [recording-based audio and saved mix controls](../../docs/RECORDED_SHIP_AUDIO_28.md);
  local prepared WAVs require their own source/license receipts and are not
  distributed with this code. `--ship-audio=true` retains the historical,
  subsequently rejected synthesis experiment; it is not the current sound
  direction or an automatic fallback for missing recordings.
- There is no water surface, collision, landing, walking, docking,
  jump control, save/load bridge, HDR or SteamOS validation. Controller mapping
  exists as an initial tested-with-synthetic-input implementation, not hardware qualification.
  Streaming is experimental and opt-in; see study 04 for its measured limits.

The exporter rejects non-finite coordinates, samples outside 2..513, spans
outside 64..262144 metres and LOD above 10 (to bound pinned tile memory).
The consumer checks schema, dimensions, buffers, finite values, replay ticks and
string identities before allocation. Files above 96 MiB are rejected.

## Verify

For the native GDScript contracts, first finish a build with both
`APSIS_DRIFT_GODOT_SPIKE=ON` and `APSIS_DRIFT_GODOT_LIVE=ON`, then run:

```sh
python3 tools/test_godot_native.py --godot /path/to/godot --build-dir build
```

This Linux runner stages the selected build's exporter and bridge, copies the
test project, generates atmospheric/airless fixtures, and runs 26 explicitly
listed contracts headlessly with Dummy audio. Each run gets a fresh retained
directory under `build-godot`, isolated preferences/cache, per-test logs and a
JSON report with source/binary hashes. It does not build, download content,
import the editor, launch a visible window or use private recordings. Finish
building before starting it; it does not synchronize with concurrent builds.

Nonzero/crash exits, deadlines, engine/script errors, leaked resources and
missing completion markers fail the run. A zero process exit alone is not a
pass. The shutdown contract's deliberately exercised drain-deadline warning
remains expected. `--test NAME` selects a subset (repeatable); `--timeout` sets
the per-process deadline within 1–600 seconds. Reports identify the selected
subset, not a full-suite pass. `python3 test/native_runner_test.py` tests runner
failure handling without Godot and runs in hosted CI.

These checks cover generated/synthetic fixtures and the actual native bridge,
not GPU appearance, imported-art fit, physical controllers, device hotplug,
speaker listening or frame-rate performance. The full native run remains an
explicit local check; the hosted runner unit test is not equivalent to it.

```sh
cmake -S . -B build -DAPSIS_DRIFT_GODOT_SPIKE=ON
cmake --build build --target apsis-drift-godot-snapshot apsis-drift-godot-snapshot-tests
ctest --test-dir build -R godot-snapshot-contract --output-on-failure
build/experiments/godot-freedom/apsis-drift-godot-snapshot \
  build-godot/new-snapshot.json 42 129 64000
godot --headless --path experiments/godot-freedom \
  --script res://validate_test.gd -- "$PWD/build-godot/new-snapshot.json"
```

The exporter refuses an existing output filename. Configure a second build with
`-DCMAKE_CXX_COMPILER=clang++` to repeat the C++ checks. See the
[evidence and continuation notes](../../docs/GODOT_STUDY_01.md) for actual results.

Godot API references: [SurfaceTool](https://docs.godotengine.org/en/stable/classes/class_surfacetool.html)
for clockwise triangle winding/normals and
[GLTFDocument](https://docs.godotengine.org/en/stable/classes/class_gltfdocument.html)
for runtime loading of existing assets.
