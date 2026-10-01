# Integrated workshop scene for the Godot reel

The scene places the supplied workshop, retained SD-01 instrument and the same
male casual clerk/technician together in Godot. It uses an actual source room
and the Hero action module, rather than an art-study movie. A held inspection
pose and already-held illustrative pointer meet the **instrument's screen**.
There is no modeled diagnostic jack, animated service cycle or repair outcome.

The full room and both instrument installations stay intact. Only the fitted
workshop derivative is displayed, so original station walls are not duplicated.
Its descriptor's source-to-local matrix is inverted through the documented
Blender-to-Godot axes and canonical station offset. That result is parented to
the **existing C++-registered station frame**. C++ player and craft state remain
unchanged at tick 0/checksum 3550752582179999592. This is cinematic NPC
presentation, not a second simulated universe or native fixture admission.

The chapter uses three 20-second moving-camera views: room context, hand and
fixture, then instrument detail. The male remains in one authored held service
key throughout. The final edit can use shorter slices; camera motion does not
invent a tool-operation animation.

## Ready inputs and bounded fit

The source room is `station-service-instrument-fit.glb`, a separate authored
installation of the original workshop. It includes the complete enclosing room,
bench/wall SD-01s and real retaining mounts. The instrument is disconnected and
its display is static. Its cradle stays locked in this scene.

The character is Hero checkpoint
`d932d4939d87a9d6551bcfc407141eafca5aba26`, using
`trials/casual-actions-01/pilot.gd`: `configure("male")`, `present(4,"right")`
and the finite `anchor_local("grip")`. Nominal scale remains 1.85 m; no body,
limb, fixture or root correction is used to force contact. The module supplies a
held key, not a work loop. It owns no clock, physics, AI, root motion or outcome.
Its source/derived-art redistribution remains uncleared; Hero explicitly permits
this authorized local owner review and film staging.

The first 20-image proof measures actual imported triangles in normalized
workshop Godot metres:

- The grip landmark is approximately `(0.70265144,1.11021197,0)`.
- The actual bench screen, surface 0/triangle 0, is hit near
  `(-0.94325185,1.13221204,1.10000002)`.
- Fixed foot/root is near `(-0.0106004,0.02200008,1.10000002)`, facing room −X.
  Root height comes from the actual workshop deck panel.
- Five deck rays at centre and ±0.28 m on room X/Z agree with that source floor.
  These are sampled support points, not a complete standing-body qualification.
- The prior illustrative 230 mm grip-to-tip prop places its fixed tip at the
  measured screen surface within float scene-transform tolerance. The hand and
  root do not chase the target; no IK or painted-hand work jitter is added.
- Rays above and outside the screen miss. These controls distinguish a bounded
  actual surface from an unlimited proxy plane.

Whole-room, medium and close views were inspected; another asset agent
independently reviewed the composition. Ordinary depth and lighting stay enabled.
The source card's coarse soles, fixed plane and original scale remain visible.
The other painted hand has no fitted contact anchor in this scene.

## Reproduction

Resolve inputs locally. Use a qualified native build and the source-bound Hero
runtime closure, preserving its original resource paths and provenance:

```bash
REEL_REPO=/absolute/path/to/reel-checkout
ENGINE_BUILD=/absolute/path/to/qualified-native-build
ART_ROOT=/absolute/path/to/read-only/authored-assets-checkout
HERO_CLOSURE=/absolute/path/to/verified-character-resources-d932d49
GODOT_BIN=/absolute/path/to/Godot_v4.7.2-stable_linux.x86_64
WORKSHOP_PROJECT="$REEL_REPO/build-reel/workshop-project"
WORKSHOP_OUTPUT="$REEL_REPO/build-reel/workshop-final"
```

Stage the native frontend and matching bridge using
[the native runner](../tools/test_godot_native.py)'s isolated staging convention.
Add verified Hero runtime files without replacing native `project.godot`, main
or presentation modules. Preserve `res://trials/...` paths and stage
`character-resources.json`. Include
[the workshop capture script](../experiments/godot-freedom/reel_integrated_workshop.gd).
Clone without `.godot` import caches when another capture shares the baseline.
Each process owns a separate project/cache/display. The optional script guards
its runtime Hero load, so the ordinary native project still parses without
private character assets.

```bash
"$GODOT_BIN" --headless --path "$WORKSHOP_PROJECT" --editor --import --quit
"$GODOT_BIN" --headless --path "$WORKSHOP_PROJECT" \
    --script res://reel_integrated_workshop.gd --check-only
mkdir -p "$WORKSHOP_OUTPUT"
DISPLAY=:98 LIBGL_ALWAYS_SOFTWARE=1 "$GODOT_BIN" \
    --path "$WORKSHOP_PROJECT" --rendering-method gl_compatibility \
    --audio-driver Dummy --script res://reel_integrated_workshop.gd -- \
    "$ART_ROOT" "$WORKSHOP_OUTPUT"
```

Choose an available display identifier with a real display or isolated Xvfb at
1920×1080. The output must be empty. Append `review` and choose a separate empty
output for the 20-image proof. Headless checks do not render images. Actual
recording uses Godot 4.7.2 Compatibility/Mesa llvmpipe software raster output.
Full instrument/room detail is preserved; these offline timings are not a game
performance claim or an established runtime mesh budget.

The capture pins these original GLBs/descriptors before import and checks them
again after recording:

| Source under `ART_ROOT/assets/visual/` | SHA-256 |
| --- | --- |
| `station-workshop.glb` | `e8633bbb8381a3359174175ab63c9f38e051fb3782c946395d75c2891c932f47` |
| `station-workshop.json` | `52d62249594473aca4ab5c431a7e34d96c34416c7ac96ba5c45c87f9a53e80d9` |
| `station-service-instrument-fit.glb` | `f2a724f53b973b3324c1df3e447909a039c4ace7cde90f5749f3f7bfabd521e5` |
| `station-service-instrument-fit.json` | `479c115f672e0ceac4b062919b522eefb657c2427103a2bb162e966afe9d64a4` |

Only the fit GLB is displayed; the workshop descriptor establishes the exact
source frame. Original master/export files are read-only. Preserve their station
and Meshy provenance/licenses alongside the Hero closure's separate review limit.

Encode the 1,440 actual PNGs without removing them or the C++ save:

```bash
ffmpeg -hide_banner -nostdin -n -framerate 24 \
    -i "$WORKSHOP_OUTPUT/frame-%06d.png" -frames:v 1440 -map 0:v:0 -an \
    -vf 'scale=in_range=full:out_range=tv:out_color_matrix=bt709,format=yuv444p,colorspace=ispace=bt709:iprimaries=bt709:itrc=srgb:irange=tv:all=bt709:range=tv:format=yuv420p' \
    -c:v libx264 -preset medium -crf 18 -threads 4 -g 48 -pix_fmt yuv420p \
    -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
    -movflags +faststart "$WORKSHOP_OUTPUT/workshop-chapter-60s.mp4"
ffprobe -v error -show_streams -show_format -of json \
    "$WORKSHOP_OUTPUT/workshop-chapter-60s.mp4" > "$WORKSHOP_OUTPUT/ffprobe.json"
ffmpeg -v error -nostdin -i "$WORKSHOP_OUTPUT/workshop-chapter-60s.mp4" \
    -map 0:v:0 -f null -
```

Delivery contract is H264/1920×1080/24 fps, 1,440 frames/60.000000 s,
BT709 limited YUV420P, video only. Godot PNG sRGB/full RGB is converted explicitly.
The chapter has no audio; the reel's separate score mix retains its own license.

## Integration gaps retained

The screen contact is a visual inspection/pointer fit, not electrical probing,
live diagnostics, repair success, a switch event or a tool-ownership action.
No fetch/stow/carried-walk animation, grip constraint or animated service cycle
is supplied by this male key. Painted landmarks are fitting aids; no finger
wrapping, hand forces or anatomical reach qualification is established.

Five floor samples do not qualify a full torso/arm or continuous collision
volume. Source obstacles are retained. Constrained cameras read the supplied two-sided action card; larger orbits
expose planar art. This composition views its reverse projection after turning
the technician toward room −X. That does not supply separately authored left
service art or qualify asymmetric wardrobe details from that side. There is no arbitrary-camera
qualification, terrain IK, NPC AI/collision/saves or gameplay actor transition.

The instrument remains retained. Release, off-cradle support, charging, power
isolation, interruption rules and load/zero-G behavior are unfinished. This
source-authored fit is not a procedurally admitted native room/contact package
and cannot enlarge C++ player support or interaction eligibility. Actual player
walking and the independent [saved-flight chapter](GODOT_REEL_FLIGHT.md) keep
their existing authority boundaries; this scene does not complete boarding.

## Recorded validation and identities

Full capture passes with **1,440 actual 1920×1080 PNG frames**. The video fully
decodes and has exactly 60.000000 seconds at 24 fps, H264/BT709 limited/YUV420P with
no audio. The full capture's unchanged C++ Save As is byte-identical to the
20-image review, at tick 0/checksum 3550752582179999592. Source GLBs/descriptors
are unchanged; fixed pointer/screen registration stays within float tolerance
throughout the recorded camera views. Six CLI refusal controls preserve empty
output, and the script parses in a normal native project without private art.

| Recorded item | SHA-256 |
| --- | --- |
| Capture script | `9580b86fd9f1653c5c5abf81f4ded7ad7e9c4bc151dfc2a1f2670748800f681c` |
| Capture receipt | `cff5043a716f2ba8360abfe4c2d31a32ea41410ec51c9d07cc3ac35b8f22b19e` |
| Unchanged C++ journey | `69f75674909fbac75ed7cd719a62bbf2f972309e73ca595f86d59933af2b6ee1` |
| PNG hash manifest | `01959f0301db589d557b863b3fb526c9a2457c3a596674f4f713c13afdbcbdde` |
| Encoded chapter | `6004e3549f1b20ecf87a802f75049600212b554c7ec9096339b2b79093a9e489` |
| Encode/decode receipt | `58a54681366cd696b1d7091a7680c597cd8ed692c019742d485b4781c49456ac` |

Local ignored output is `build-reel/workshop-final/`: original PNGs,
`capture.json`, `unchanged-journey.json`, `frames.sha256`,
`workshop-chapter-60s.mp4`, `ffprobe.json`, `encode-receipt.json` and full-decode
log. Receipts bind exact commands, source/projection identities, renderer,
contact/floor samples and unchanged state. The PNG manifest uses UTF8 filename,
space, lowercase SHA-256 and newline in numbered-frame order.
