# Registered observation-window chapter

This optional 20-second scene composes the separately delivered observation
commons with the existing native seed42 world. The camera moves inside the
source bay while its authored shutter opens, then holds the outward view.
Actual neighboring station hardware partly occupies the sightline. The planet
is outside this framing; no celestial backdrop, decorative globe or additional
stars are inserted.

The C++ New Game remains at tick 0. Only the camera and source shutter change.
This is an editorial observation study, not a player route, pressure event,
window interaction, collision admission or departure. The bay lies outside
the currently supported walker crop. The actual player remains at the hub.
See [the storyboard](GODOT_REEL_STORYBOARD.md) and
[the integration inventory](REEL_INTEGRATION_GAPS.md).

## Source and registration

Borrow unchanged `station-observation-commons.glb`, its same-stem descriptor,
`station-commons.json` and `station-observation-checks.json` from the Station
handoff into an isolated project's `observation-source/`. The new room is a
separate derivative: source masters, old commons, procedural catalog and native
starter package remain unchanged. Original provenance explicitly registers this
derivative under `LicenseRef-Apsis-Station-Kit-Output`, with source,
documentation and runtime use, redistribution and derivatives allowed. Preserve
the handoff's provenance and `docs/licenses/STATION_KIT_01.md` beside local
outputs. Attribution is Apsis Drift contributors; original OpenAI references,
Meshy-generated pod geometry and authored Blender layout studies.

The capture imports the source GLB at 24 Hz and pauses its
`Observation shutter cycle`. It samples integer authored frames 1–100: closed
for the first second, opening over the next six seconds, then held open. This
does not replay the source closing cycle or invent actuator state.

Registration composes the inverse source-to-commons descriptor transform with
Blender-Z-up to Godot-Y-up conversion, C++ station asset offset and the actual
C++ station transform. The resulting room-to-station mapping has origin
approximately `(12.53,0,0)`, X along station−Z, Y along +Y and Z along +X.
Both native near and far cameras share the exact same transform and 64° FOV;
the room uses the existing near-geometry layer. Existing C++ terrain generation
and its native streaming view provide the world, without a second universe.

The old commons replacement binds **all 232 exported source objects**, using
Godot's node-name normalization. The descriptor also names exactly three
collection roots absent from both GLBs: `KIT05 commons area`, `.001` and `.002`.
Only those three nonexported roots are excluded. Missing exported geometry
refuses capture; no broad subtree or collision waiver is used.

The local eye eases from `(-1.30,1.37,-2.0)` to `(-2.15,1.24,-2.0)` over 12 s,
then holds. Camera targets move with its height and retain the source bay axis.
The five-frame endpoint proof checks closed/start, opening, open, final eye and
hold before the full recording. The upper-left truth label is the only baked
text; bottom 220 pixels remain available for editorial dialogue.

## Reproduction

Choose absolute paths locally for a qualified native Godot build, prepared
starter assets, source handoff and new output. Stage the native frontend and
matching bridge into a new project using
[the native runner](../tools/test_godot_native.py)'s convention: preserve script,
shader, scene and extension paths; exclude import caches. Include
[reel_observation_capture.gd](../experiments/godot-freedom/reel_observation_capture.gd).
The optional private room is loaded only after source existence/hash checks;
the ordinary native project does not require that art to parse.

```sh
mkdir "$OBSERVATION_PROJECT/observation-source"
cp "$STATION_SOURCE/assets/visual/station-observation-commons.glb" \
   "$STATION_SOURCE/assets/visual/station-observation-commons.json" \
   "$STATION_SOURCE/assets/visual/station-commons.json" \
   "$STATION_SOURCE/assets/visual/station-observation-checks.json" \
   "$OBSERVATION_PROJECT/observation-source/"
"$GODOT_BIN" --headless --path "$OBSERVATION_PROJECT" --editor --import --quit
"$GODOT_BIN" --headless --path "$OBSERVATION_PROJECT" \
    --script res://reel_observation_capture.gd -- --check-only
mkdir "$OBSERVATION_REVIEW"
"$GODOT_BIN" --path "$OBSERVATION_PROJECT" --rendering-method gl_compatibility \
    --audio-driver Dummy --resolution 1920x1080 \
    --script res://reel_observation_capture.gd -- \
    "$NATIVE_ASSETS" "$OBSERVATION_REVIEW" review
mkdir "$OBSERVATION_OUTPUT"
"$GODOT_BIN" --path "$OBSERVATION_PROJECT" --rendering-method gl_compatibility \
    --audio-driver Dummy --resolution 1920x1080 \
    --script res://reel_observation_capture.gd -- \
    "$NATIVE_ASSETS" "$OBSERVATION_OUTPUT"
```

Run raster commands on a real display or an isolated 1920×1080 Xvfb display.
Each concurrent capture owns its project, cache and display. Outputs must be empty; headless
mode checks contracts and does not render. No particular host/display identifier
is required.

Encode with the sRGB-PNG to BT709 limited YUV conversion used by
[the flight chapter](GODOT_REEL_FLIGHT.md), with `-framerate 24`,
`-frames:v 480` and a new `observation-chapter-20s.mp4` output. Preserve PNGs,
numbered-frame hashes, exact commands and C++ Save As rather than replacing
source files. Require 1920×1080/H264/YUV420P/24 fps/480 frames/20 s and a complete
decode, then compare the final Save As against the endpoint proof.

## Evidence

The sampler refuses nonfinite/out-of-range time, checks all 480 sample positions
and exact final endpoints. The first valid raster proof binds all 232 replacement
nodes, warms 144 real C++ terrain tiles and retains tick 0/checksum
`3550752582179999592`. Independently reviewed endpoints show closed shutter,
opening, source glazing and actual outside hardware without camera clipping.
Godot 4.7.2 Compatibility/Mesa llvmpipe supplies real software-raster evidence;
offline recording is not a hardware GPU or real-time performance benchmark.

| Selected source | SHA-256 |
| --- | --- |
| Observation GLB | `a983bd2130ead14beea61f64a530fbaa5c7609b16a58ad9dfe7a86592be6ab80` |
| Observation descriptor | `d87195e54c40952d16785ae5d9dc570cb3f26483abee138e25b392367d4b949f` |
| Original commons descriptor | `ca85bc97c01965266e918f609e1ed01de20cbb05cb6dbbe15c7b5adf7334852c` |
| Source checks | `8b1c4f5eecef060fe0911c2106c57ed158dd01730b3870d809a15a8bc0dc5105` |

The capture receipt records selected inputs, script identity, camera/source-frame
observations, terrain, exact room replacement, final Save As and unchanged clock.
The full chapter has **480 actual PNG frames**, is 20.000000 s at 24 fps and fully
decodes as 1920×1080/H264/BT709 limited/YUV420P, video only. Its final Save As is
byte-identical to the endpoint proof. Every frame checks the same tick/checksum
and unmoved hub actor. An encoded 10 s view was independently inspected for source
glazing, hardware, unobstructed caption area and retained framing.

| Recorded item | SHA-256 |
| --- | --- |
| Capture script | `3cb94c70e47fdf6e5e4e4a2d8f56742f2303222f7dbf7a73d051df19b0b4b218` |
| Final C++ save | `69f75674909fbac75ed7cd719a62bbf2f972309e73ca595f86d59933af2b6ee1` |
| Capture receipt | `a9122e285de71dd78cef3064a9d9fb6967d36d96b005a30fc70cc66bd20346d3` |
| Source/dependency/license receipt | `eff682d3524646ca50c86b9e4658c9ec0bf2db21fbc9f7ba3f31abb60b20d701` |
| PNG hash manifest | `092533a9e006ceef279c5afeeab03c7f1e43c9f72dad73d7ddbf2c3b32063579` |
| Encoded chapter | `30bfe153d82688fc79c65d9e897d86748525166bee837261d3a37936a7061a26` |
| Encode receipt | `4df3a1d15e8e44f23e6df76042cbfefecacd67f7c214e93327e33ec352dc7cdd` |

Local ignored output is `build-reel/observation-final/`: numbered PNGs and
`receipt.json`/`unchanged-journey.json` under `frames/`, `frames.sha256`,
`source-receipt.json`, `observation-chapter-20s.mp4`, `ffprobe.json`,
`encode-receipt.json` and full-decode log. The frame roster records UTF8 filename,
space, lowercase SHA-256 and newline in numbered order. These are source capture
identities; the final edit and soundtrack have their own assembly receipt.
