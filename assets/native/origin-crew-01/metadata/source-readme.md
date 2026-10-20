# Casual male — second station figure and local reel insert

The owner simplified wardrobe to two complete outfit themes per hero: casual
and flight suit. This adds the missing male casual identity in slacks, shirt,
jacket and low shoes, alongside the existing female casual pilot. Same dark
hair/stubble identity as `flight-pair-01`; no new named NPC or faction implied.
The existing female outfit, both suits and all earlier sources are untouched.

## Delivered

- Four authored idle directions, including a true rear and a separate left view.
- Four side-walk keys held twice in eight timeline slots; symmetric clothing
  permits reflected left walking, not a mirrored front pretending to be a rear.
- Four front and four rear walk drawings. Some passing silhouettes repeat.
- Two distinct right-profile greeting poses: neutral and raised far hand.
  The original four-pose request switched arms. A repair also failed, so the
  mismatched intermediate poses are excluded, not presented as a smooth wave.
- Caller-only Godot adapter, invalid-input tests, labelled review and a
  caption-free reel derivative in an independent primitive cabin.

Five built-in `image_gen` calls total, zero Meshy credits; exact prompts and
references in `requests.json`. Three selected raw sheets plus two rejected
alternatives are preserved in `sources/`. The selected idle sheet retains its
`-draft` filename because only six of its eight cells are admitted. No Python
raster edits: read-only alpha measurements informed the crop registration.
Pixel-animation guidance informed the reduced contact/passing key selection;
an implementation subagent handled the adapter and review checks.

`frames.json` selects 18 distinct drawings in 24 timeline entries. Side keys
use [0,0,1,1,2,2,3,3]; greeting uses source [4,4,6,4]. Nonuniform portrait-sheet
spacing needs measured crops: blindly splitting at the image midpoint clips
the contact toes. One scale per sheet preserves body proportions across poses.
The 1.85 m nominal standing reference and 1.4 m cycle distance are art/review
parameters, not collision capsules or proof that painted feet remain planted.

## Run and capture

From repository root with `GODOT_BIN` set to Godot (tested 4.7.2):

```sh
"$GODOT_BIN" --headless --path experiments/pixel-pilot --editor --import --quit
"$GODOT_BIN" --headless --path experiments/pixel-pilot --script trials/casual-male-01/check.gd
"$GODOT_BIN" --headless --path experiments/pixel-pilot --script trials/casual-male-01/review.gd -- --check
"$GODOT_BIN" --path experiments/pixel-pilot --script trials/casual-male-01/review.gd
```

The actual GPU captures are under
`build-godot/studies/pixel-pilot/casual-male-01/`:

- `review/review.mp4`: labelled 20-second review.
- `clean/review.mp4`: same scene/motion, captions hidden only.

Both are 1280×720 at 30 fps, captured through a private virtual Wayland display
with Vulkan Forward+ on a hardware GPU. No user desktop configuration change.
No performance benchmark was run: offline Movie Maker timings are not sustained
game throughput. The male uses a 192×192 transparent viewport; the existing
female uses 192×320. Both retain ordinary world depth and nearest sampling.

For a fresh capture, prepare an absolute output directory, then on a real GPU:

```sh
"$GODOT_BIN" --path experiments/pixel-pilot --script trials/casual-male-01/review.gd --write-movie "$CAPTURE_DIR/review.avi" --fixed-fps 30 -- --stills="$CAPTURE_DIR"
# Add --clean after the -- separator for the caption-free version.
ffmpeg -i "$CAPTURE_DIR/review.avi" -c:v libx264 -crf 18 -pix_fmt yuv420p -an -movflags +faststart "$CAPTURE_DIR/review.mp4"
```

For the coder's reel, use **00:00–00:15** of the clean movie: idle, approach,
stop/greeting/recovery, return. The final five seconds intentionally reset the
scene to inspect front/rear views; do not pass that reset off as continuous
navigation. Full review and clean variants remain available. Add an editorial
label such as "Character presentation study — scripted motion"; caption-free
does not mean production gameplay or public distribution is certified.

## Presentation contract

```gdscript
var pilot = preload("res://trials/casual-male-01/pilot.gd").new()
add_child(pilot)
assert(pilot.configure())
pilot.present("idle", 0, "right")
pilot.present("walk_side", 2, "right")
pilot.card.billboard = BaseMaterial3D.BILLBOARD_DISABLED
pilot.present("gesture", 2, "right")
```

`idle` supports index 0/all four views; `walk_side` indices0–7/right or left;
`walk_axial` indices0–3/front or rear; `gesture` indices0–3/right or left.
The caller owns the action plane and heading relative to camera. The helper
`select_view` retains eight-degree hysteresis. The review uses authored view
choices and a fixed camera, not a new arbitrary-orbit qualification. Invalid
requests return false and retain the previous complete pose. Repeated requests
do not redraw, and configuring again invalidates the cache. Independent nodes
do not share mutable atlases. The actor has no clock, physics or root motion.

It composes the existing flight-pair renderer without modifying that source or
exposing a misleading helmet mode. This is an isolated reuse example, not a
generalized engine or approved production ABI. C++ retains simulation, collision,
eligibility, NPC AI, pressure, interaction outcomes and saves. Casual clothing
does not determine whether a planet/station is safe without a suit.

## Review, validation and limits

All eleven labelled chapter stills were inspected, plus representative clean
frames. Heads/hands remain attached in the selected poses, soles are not clipped,
the male stops before greeting, and the foreground fixture truly occludes him.
The female uses her existing authored left idle view, not mirrored side details.
Checks include 226 runtime/registration assertions and 2,000 choreography samples;
source remaps and duplicate-frame registration are explicit. Save failures exit
nonzero. The full preserved-study suite is also required; no C++ changes/build
or publication certification is claimed.

Known limits: coarse four-key gait and foot slide; some axial repetition; slight
identity/proportion drift and softness between sources; abrupt held-greeting
transition and instant view changes; planar shading/no body-shaped cast shadow;
no arbitrary-camera qualification, terrain foot IK, NPC behavior or actual
station navigation. No new seated/service/float casual-male set yet. Do not claim
both casual outfits have full flight-suit-equivalent action coverage.

Source reference and derived-art redistribution remain **uncleared**. This is
for local owner review and isolated development, not canonical asset admission.
No third-party movie/game images were imported. `receipt.json` hashes sources,
dependencies and movies; `measurements.json` records the actual capture receipts.
