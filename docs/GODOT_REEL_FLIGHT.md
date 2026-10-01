# Saved-flight chapter for the Godot reel

This chapter renders the admitted Wayfarer, its actual main exhaust and the
selected C++ planet/terrain together in Godot. It starts from an **independent
legacy orbit save**. It does not demonstrate station boarding, a seated actor,
departure, atmospheric entry, landing or return. Cameras and lighting are
editorial presentation; the existing C++ world remains authoritative.

The recorded chapter contains **2,400 actual 1920×1080 PNG frames**, encoded as
100 seconds at 24 fps. Its first 80 seconds advance the public flight controller
at five ordinary 120 Hz ticks per output frame. The final 20 seconds pause C++
physics while the camera inspects the same planet. No time compression or dropped
catch-up time is used. Offline recording speed is not a runtime benchmark.

| Interval | Shot | Physical behavior |
| --- | --- | --- |
| 0–20 s | Exterior push-in | Main command ramps from .24 to .87; exhaust consumes actual applied force. |
| 20–40 s | Cockpit camera | Main command fades to zero. This camera does not claim seat ownership. |
| 40–60 s | Exterior coast | Neutral controls; actual unpowered motion. |
| 60–80 s | Attitude view | Assistance disabled; opposing small roll commands, then neutral controls. |
| 80–100 s | Planet view | Explicitly paused editorial camera; the craft is not relocated. |

`reel_flight_capture.gd` retains source/save identities, once-per-second controller
observations, camera poses, terrain reports and final tick/checksum. It writes
and reopens C++ Save As, requiring the physical state to match. It checks the
selected input save after every chapter and at completion. The fast review mode
executes the same timeline but captures only 15 images.

## Reproduction

Resolve these absolute paths locally; no particular checkout layout or desktop
is required:

```bash
REEL_REPO=/absolute/path/to/reel-checkout
ENGINE_BUILD=/absolute/path/to/qualified-native-build
GODOT_BIN=/absolute/path/to/Godot_v4.7.2-stable_linux.x86_64
NATIVE_ASSETS=/absolute/path/to/prepared/freedom-starter-01
SELECTED_SAVE=/absolute/path/to/new-wayfarer-flight.json
FLIGHT_PROJECT="$REEL_REPO/build-native/reel-flight-project"
FLIGHT_OUTPUT="$REEL_REPO/build-native/reel-flight-final"
```

Use a qualified `APSIS_DRIFT_GODOT_LIVE=ON` build. Generate the fixture through
C++, without editing a serialized save:

```bash
"$ENGINE_BUILD/experiments/godot-freedom/apsis-drift-freedom-start-fixture" \
    "$SELECTED_SAVE" 42 25 wayfarer-flight
```

Stage the native frontend and matching binary into a new isolated project using
[the native runner](../tools/test_godot_native.py)'s staging convention. Copy
`.gd`, `.gdshader`, `.godot`, `.tscn` and `.json` frontend files, the existing
`bin/freedom.gdextension`, and the qualified build's `libapsis_freedom_bridge.so`.
Preserve dependency paths; exclude `.godot` import caches. Include
[the flight capture script](../experiments/godot-freedom/reel_flight_capture.gd).
Each concurrent capture owns its project, cache and display.

```bash
"$GODOT_BIN" --headless --path "$FLIGHT_PROJECT" --editor --import --quit
"$GODOT_BIN" --headless --path "$FLIGHT_PROJECT" \
    --script res://reel_flight_capture.gd --check-only
mkdir -p "$FLIGHT_OUTPUT"
sha256sum "$SELECTED_SAVE" > "$FLIGHT_OUTPUT.source-save.sha256"
DISPLAY=:98 LIBGL_ALWAYS_SOFTWARE=1 "$GODOT_BIN" \
    --path "$FLIGHT_PROJECT" --rendering-method gl_compatibility \
    --audio-driver Dummy --script res://reel_flight_capture.gd -- \
    "--save=$SELECTED_SAVE" "--assets=$NATIVE_ASSETS" \
    "--output=$FLIGHT_OUTPUT" --review=false --render-size=1920x1080
sha256sum -c "$FLIGHT_OUTPUT.source-save.sha256"
```

Use an available display identifier, with a real display or isolated Xvfb at
1920×1080. The output directory must be entirely empty; the source hash receipt
above is deliberately stored beside it. Compatibility/Mesa llvmpipe was used for
the recorded chapter. Headless mode checks contracts and does not render images.
For review, choose another empty output and use
`--review=true --render-size=1280x720`.

Encode without deleting PNGs or the final save:

```bash
ffmpeg -hide_banner -nostdin -n -framerate 24 \
    -i "$FLIGHT_OUTPUT/frame-%06d.png" -frames:v 2400 -map 0:v:0 -an \
    -vf 'scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv444p,colorspace=ispace=bt709:iprimaries=bt709:itrc=srgb:irange=tv:all=bt709:range=tv:format=yuv420p' \
    -c:v libx264 -preset medium -crf 18 -threads 4 -g 48 -pix_fmt yuv420p \
    -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
    -movflags +faststart "$FLIGHT_OUTPUT/flight-chapter-100s.mp4"
ffprobe -v error -show_streams -show_format -of json \
    "$FLIGHT_OUTPUT/flight-chapter-100s.mp4" > "$FLIGHT_OUTPUT/ffprobe.json"
ffmpeg -v error -nostdin -i "$FLIGHT_OUTPUT/flight-chapter-100s.mp4" \
    -map 0:v:0 -f null -
```

The color conversion is Godot PNG sRGB/full RGB to BT709 transfer, primaries and
matrix with limited-range YUV420P. The chapter has no audio; the reel mixes its
separately licensed score. Preserve prepared starter license/provenance receipts.

## Recorded validation and identities

The full chapter and clean review produce **byte-identical final Save As** at
tick 9625/checksum 5617296345748690355. Initial tick is 25/checksum
8615057281278565866. Every observation reports zero dropped seconds; the input
save is unchanged. The MP4 fully decodes, has exactly 2,400 frames/100.000000 s,
and has H264/1920×1080/24 fps/BT709 limited/YUV420P video only. Representative
source frames 200/400 and an encoded frame were visually reviewed for camera
motion, actual exhaust and preserved planet appearance.

| Recorded item | SHA-256 |
| --- | --- |
| Selected input save | `aa5970ec9fc6080f1bd5b2f67f0e9cb5be3689c5ce5a2b830059be7dc9675001` |
| Capture script | `0e5980f9054a641902c5af64fbbbba51d421e408a215d55e60e26c20a9232c66` |
| Qualified bridge | `941d5f495f7a4af4e016d770de996d56b02668f3c2482547a1201151cc196eed` |
| Flight Wayfarer model | `12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8` |
| Flight Wayfarer descriptor | `17c2bc23d4f43602f85a7951dd7c8a3aaceed691e1a1b8a2ef823df703c446b7` |
| Final C++ save | `944c2a340b7cc075b8ab43620f657307cbcd1beb641529532a9fb9c6205eeb3f` |
| Render receipt | `a2df380377bb3e595c59f4819c88a5e5582667d16873ce2afd76933ae27a40cf` |
| PNG hash manifest | `a6fde967122c0e3162f5b41cd35a59386e5763575ba54e6845360d5d19345ac8` |
| Encoded chapter | `b0a50fd413f8278fa41c84d2159ef369d5caedac54d53537245e477436fd3367` |

Local ignored output is `build-native/reel-flight-final/`: PNGs, `render.json`,
`final-flight.json`, `frames.sha256`, `flight-chapter-100s.mp4`, `ffprobe.json`,
`encode-receipt.json` and full-decode log. The manifest records UTF8 filename,
space, lowercase SHA-256 and newline in numbered-frame order. Receipts preserve
the exact actual commands, frontend hashes, observations and rendering limits.

Seven meaningful CLI refusal controls pass; a normal headless parse passes.
The unchanged C++ sources also build with GCC and Clang and pass 35/35 CTests
on both. The film branch has no C++ diff from its qualified engine baseline.
This is local owner-review footage, not a release or a continuous departure test.
