# Frozen native import comparison

This bounded [#217](https://github.com/gobha-me/apsis-drift/issues/217) capture
compares an authentic system-frame save with its independently exported
canonical planetary save through ordinary Continue. It records two actual
images at the same saved pose, tick and default chase camera. It does not
qualify the full approach/entry/landing continuum, recognizable relief,
controller operation, hardware performance or frame pacing.

The comparison exposed a presentation lifecycle defect: reopening the identical
save changed a few terrain pixels even though the complete save, cameras,
uploaded mesh arrays, transforms and uniforms matched. Retaining the same view
stayed identical, and independent cold-process imports also matched. A stable
tile material priority removes the observed allocation-dependent ordering at
neighboring terrain boundaries. Depth testing, mesh buffers, clip planes and
authoritative state retain their existing rules.

The priority uses the existing C++ cube-face/LOD/coordinate key: opposite faces
share an axis group, each of the fourteen supported LODs has separate bands, and
coordinate parity distinguishes edge/corner neighbors. This reserves 168 bands
within Godot's `-128..127` range (159 are reachable because LOD0 has one tile).
It supplies a consistent order for neighboring fragments with equal stored
depth. It is not a unique order over every resident tile or a depth-precision
repair. Godot's pinned [opaque GLES3 sort key](https://github.com/godotengine/godot/blob/ed1daf0bf/drivers/gles3/rasterizer_scene_gles3.h#L248)
places material priority before allocation-derived material/geometry keys;
the [opaque pass sorts by that key](https://github.com/godotengine/godot/blob/ed1daf0bf/drivers/gles3/rasterizer_scene_gles3.cpp#L2323).

The existing C++ import fixture supplies `system-import.json` and
`canonical-before.json`. The headless native contract checks the malformed
owner, complete projection, one real neutral tick, canonical Save As and
transactional replacement before graphical capture. No save is manufactured by
editing JSON, no camera or planet is moved for the comparison, and capture does
not advance the simulation.

## Prepare and verify

Use Linux x86_64, the qualified Godot 4.7.2 executable, Xvfb and Mesa. Build the
matching native bridge and the existing import fixture:

```sh
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DAPSIS_DRIFT_TERMINAL=OFF -DAPSIS_DRIFT_RTAUDIO=OFF \
  -DAPSIS_DRIFT_GODOT_SPIKE=ON -DAPSIS_DRIFT_GODOT_LIVE=ON
cmake --build build-native --parallel 2 --target apsis_freedom_bridge \
  apsis-drift-godot-snapshot apsis-drift-freedom-flight-import-tests
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --test native_asset_import \
  --test native_frame_import --output-parent build-native-contracts --keep-work
```

`GODOT_BIN` must name the actual installed engine. Set `IMPORT_STAGE` to the
absolute work directory printed by the successful runner. The stage contains
the frozen project, matching bridge, prepared assets and real C++ fixture saves.
Keep it until the capture finishes; an old stage does not represent changed
source. Repeat with the other compiler for publication.

## Capture on an owned display

```sh
XDG_DATA_HOME="$IMPORT_STAGE/capture-userdata" \
XDG_CONFIG_HOME="$IMPORT_STAGE/capture-config" \
XDG_CACHE_HOME="$IMPORT_STAGE/capture-cache" \
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run --auto-servernum \
  --server-args="-screen 0 800x450x24" \
  "$GODOT_BIN" --display-driver x11 --rendering-method gl_compatibility \
  --audio-driver Dummy --resolution 800x450 --path "$IMPORT_STAGE/project" \
  --script res://studies/captures/native_frame_import_capture.gd -- \
  "$IMPORT_STAGE/frame-import" "$IMPORT_STAGE/native-assets" \
  "$IMPORT_STAGE/import-capture-800" 800 450
```

The output directory must be new. Supported output sizes are 800×450,
1920×1080 and 3840×2160; change the display, resolution, output directory and
last two arguments together. This slice uses full internal resolution only.
Unsupported dimensions and headless displays are refused before staging.
Use an external process deadline appropriate to the native runner's existing
900-second limit. Each terrain-settling wait is independently bounded at
60 seconds.

The main window remains shown on this isolated display. The script does not
require Godot to hide it, does not connect to a player's display, and does not
change capture guards elsewhere. The historical failed hidden-window attempt
remains failed evidence; it is not retroactively counted as a pass.
The explicit XDG directories isolate engine logs, preferences and caches from
the player's profile.

## Read the evidence

`capture.json` records engine/display, source and bridge hashes, fixture and
prepared-asset receipt hashes, licensing, actual near/terrain cameras and clip
planes, C++ body/frame/terrain identity, lighting, complete canonical Save As
hashes and both PNG hashes. It records output and camera viewport sizes so
logical coordinates cannot silently stand in for rendered pixels.

Readback waits for an explicit camera stream request to finish, pending uploads
to drain and geometric morphing to finish. Advancing the presentation morph
clock during this wait does not advance C++ time. The source files must remain
unchanged, both saved projections and cameras must match, and both Save As files
must match the independent canonical save byte for byte. Pixel buffers must be
identical within this frozen comparison; a mismatch fails while retaining its
images for inspection. There is no new image golden or cross-renderer tolerance.

Inspect both PNGs as well as the report. Equal images prove only that these
two equivalent imports produce the same frozen appearance on the recorded
renderer. They cannot establish correct physical scale, a visible terrain
feature, broad seams or the complete visual continuum. The wider targets in
[the scale contract](PLANETARY_SCALE_CONTRACT.md) and #217 remain open.
