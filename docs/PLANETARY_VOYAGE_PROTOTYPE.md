# Planetary voyage prototype

The opt-in fixture starts New Game 42 on the station, walks to D1, boards and
seats the pilot, releases the port, descends into atmosphere, cruises across
terrain, climbs back into space, intercepts the moving home station, captures
D1 and disembarks to walk again. Every movement uses public C++ commands;
the fixture does not assign craft positions, velocities or discoveries.

This is a regression check for the planetary leg of [#245](https://github.com/gobha-me/apsis-drift/issues/245).
It does not complete First Flight or the broader Freedom journey. Landing,
surface walking, reboarding, neighboring-system jumps, resources and recovery
remain separate work. Manual/controller play and continuous native presentation
still require qualification.

## Run the check

Configure the existing native build with `APSIS_DRIFT_GODOT_SPIKE=ON` and
`APSIS_DRIFT_GODOT_LIVE=ON`, then build:

```sh
cmake --build build --target apsis_freedom_bridge apsis-drift-godot-snapshot \
  apsis-drift-freedom-planetary-fixture
python3 tools/test_godot_native.py --godot /absolute/path/to/godot \
  --build-dir build --timeout 600 --test native_planetary
```

The runner creates one isolated presentation project, prepares the existing
licensed station and Wayfarer assets, and runs the matching compiler's fixture.
Keep compact logs, JSON saves and reports after review; the copied project and
prepared assets can then be removed. Do not retain another full asset closure
for each trial.

Eleven actual Save As/Continue checkpoints span the voyage. At each checkpoint,
Continue must preserve the complete actor, craft, assembly and history. An
independent C++ clone exports the next neutral tick. The Godot check loads the
real checkpoint, verifies paused presentation cannot advance authoritative time,
resumes through the view, advances one public bridge tick and compares the
entire saved file with that C++ reference. The reference clone does not change
the voyage. Non-finite metadata, invalid shapes, unsupported versions and path
traversal are refused before staging; invalid CLI arguments refuse before world
construction.

For an opt-in rendered review, run
`res://studies/captures/native_planetary_capture.gd` on a rendering display,
passing three absolute directories after `--`: generated planetary checkpoints,
prepared assets and an image output directory. It freezes simulation while
waiting for C++ terrain generation and GPU uploads, then records image hashes,
source hashes and license references. These are renders of actual voyage
checkpoints, not evidence of uninterrupted native flight or frame pacing.

## Limits and observations

The deterministic test pilot translates through the twelve existing signed
thruster channels and rotates through their real torque limits. Its conservative
route takes roughly 84 minutes of simulated flight time; this is not a proposed
gameplay duration or a performance result. Planet-scale station interception
follows a planetary arc before closing locally, avoiding a shortcut through
the planet. Final closure uses the existing bounded approach aid and explicit
capture.

The surface cruise covers about 5.97 kilometres. Terrain heights are recorded at checkpoints, and clearance is checked every
60 game seconds with source LOD8 and relief version0, matching saved native terrain rather
than experimental relief. Minimum checked clearance is recorded in the trace. This high-altitude check does not
certify continuous terrain collision or low-level landing clearance. C++ owns
both those terrain samples and Godot's streamed world. Existing boarding and
exhaust presentation refinements remain documented in
[the boarding prototype](PLAYABLE_BOARDING_PROTOTYPE.md).

## Uninterrupted native session

`--test native_voyage` additionally generates a bounded command stream and runs
one native session from ordinary New Game, through the shell's boarding and
return view handoffs. The Wayfarer model must retain its instance identity;
no phase reload or pose assignment is used. Complete saved files must match
the independently generated C++ reference at every checkpoint.

```sh
python3 tools/test_godot_native.py --godot /absolute/path/to/godot \
  --build-dir build --timeout 600 --test native_voyage
```

The exporter takes an optional `--commands` after its absolute empty output
directory. Only this mode holds test-pilot demands for fifteen ticks (0.125
seconds); the default fixture and its existing checkpoints remain unchanged.
Commands carry exact IEEE754 binary64 values as little-endian hexadecimal bytes,
avoiding decimal-parser rounding between C++ and Godot. The file is bounded to
32 MiB, 100,000 rows and fifteen ticks per batch. Closed command names, finite
control buffers, checkpoint order and tick totals are validated before staging.

The flight check feeds recorded semantic input through the real view process,
actuator mapping, C++ clock, terrain and applied-force exhaust. It substitutes
only the operating-system input sampler; controller mapping, focus and neutral
latches retain their separate tests and manual qualification. Automatic node
processing is disabled during explicit playback, so render waits and deferred
view handoffs cannot change authoritative time. This is accelerated integration
playback, not a gameplay autopilot or a frame-pacing measurement.

For optional GPU review, `res://studies/captures/native_voyage_capture.gd` accepts
the same three absolute directories as the checkpoint review. It renders the
actual uninterrupted session at each phase, waiting for streamed terrain with
simulation frozen, and writes `voyage.json` with source/input/image hashes,
engine/renderer information and the existing asset license location. Preserve
those compact records and selected licensed captures, then remove temporary
copied asset packages and projects.

The opt-in [surface walking loop](FREEDOM_SURFACE_WALK.md#checks) adds a physical
landing, ground excursion and thruster liftoff to a station-to-station voyage.
It uses a distinct schema and `native_surface_loop` runner contract; the original
eleven-phase high-altitude voyage remains available unchanged.
