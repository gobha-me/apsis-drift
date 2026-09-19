# Study 04 — bounded spherical terrain streaming

2026-09-18. First working streaming foundation for the native experiment, not
a production engine decision or finished orbit-to-ground gameplay.

Follow-up on 2026-09-18: [Godot was selected](GODOT_ADOPTION.md), and the
[first preview film](FREEDOM_PREVIEW_01.md) adds shared near-tile normals and
experimental orbital optical layers. The original study captures and timings
below remain historical evidence; they do not measure the added normal work.

## Run and inspect

```sh
tools/run_godot_study.sh --stream=true --relief=true --pilot=true
tools/run_godot_study.sh --stream=true --relief=true --view=4
```

`--stream=true` enables the live C++ extension and replaces the fixed terrain
patch with a complete, adaptive spherical cover. The original snapshot-only
and `--live=true` fixed-patch modes remain available. `--relief=true` still
selects the explicit, opt-in experimental recipe from [study 03](GODOT_STUDY_03.md);
omitting it preserves the original terrain recipe. Production saves, generation
versions, terminal presentation and existing audio are untouched.

View 1 is live surface flight; C selects the cockpit, W/S thrust, A/D turn,
Q/E strafe, Space/Ctrl climb/descend, V pause and Backspace reset. There is no
fixed-patch boundary pause in streaming mode. View 4 pauses flight and starts
with the whole planet: hold RMB and use WASD/QE to move the inspection camera.
Speed and clipping range adapt to altitude; Shift accelerates inspection.
Inspection movement is **not** orbital flight, reentry or a gameplay jump.

Actual 3840×2160 Vulkan viewport artifacts, with adjacent measurement JSONs:

- [Whole planet](media/freedom-stream-orbit-v4.png)
- [Surface approach after nine cover replacements](media/freedom-stream-approach-v4.png)
- [Seated cockpit above streamed terrain](media/freedom-stream-cockpit-v4.png)

## What is implemented

`streaming.hpp` selects a deterministic quadtree partition of all six cube
faces. Distant regions stay coarse; a generous region around the observer
refines, including behind the camera so turning around does not reveal empty
space. The complete partition has at most **384 tiles**, independent of distance
travelled. Surface coverage is never discarded simply to satisfy the budget.

One C++ worker builds missing meshes using the existing planet descriptor,
terrain generation and sampling APIs plus the selected relief recipe. Coarse
vertices coincide with canonical generator samples; fine vertices use the
same source-LOD interpolation as live flight. The default source LOD remains 8.
This is one procedural planet, not a separate decorative orbital sphere.

Each mesh has 33×33 surface vertices plus four skirts: 1,221 vertices and 7,680
indices, including double-sided skirt triangles. Skirts conceal unequal-LOD
cracks; they are not physical terrain. Meshes use tile-local coordinates.
Planet positions and anchor subtraction remain C++ doubles, then Godot receives
ship-relative transforms. The ship stays at the render origin as it travels.

The worker has a bounded 64-tile source cache and one outstanding request.
Unchanged immutable meshes are reused. Godot creates meshes/nodes on the main
thread, with at most four uploads per frame and a soft 2 ms upload budget.
It retains the previous complete cover until the replacement is ready, then
hides and frees superseded nodes. At most two covers are staged/resident:
**768 tile nodes**, not an accumulating trail around the planet.

The 384-tile C++ mesh payload is 39,459,456 bytes, about 37.6 MiB. That is a
buffer count, **not total process RAM or peak VRAM**: container overhead,
source cache, old/new overlap, boundary copies, engine meshes and driver
allocations also exist. Streaming uses a small 3×3 recipe/replay fixture instead
of requiring the previous 19 MB detailed inspection JSON. Generated terrain is
not written to a growing on-disk cache.

Material detail is presentation-only and filtered with distance. Broad mineral
staining is shared across surface/orbit views; it does not displace terrain or
create a second physical world. Atmosphere/background and sun remain review
fixtures, not a simulated atmosphere, clouds, water or orbital illumination.

## Evidence

- GCC and Clang pass snapshot and streaming C++ contract tests. The latter
  exercise 245 global/polar covers, multiple orbital altitudes, complete and
  non-overlapping coverage, cube-face shared vertices, invalid inputs, finite
  buffers, outward normals, index limits, regeneration identity, millimetre
  local positioning and actual asynchronous reuse/replacement.
- Both actual compiler-built extensions pass the headless streaming boundary
  test, including 2,400 real C++ flight ticks each crossing longitude wrap and
  a cube-face edge, polar transforms, reset during generation, malformed buffers
  and unchanged simulation checksums when only
  rendering is enabled. Test relocation sets up fixtures; it is not flight.
- Both extensions retain the existing 301 checkpoint and 30/60/144 FPS replay
  checks for original and relief terrain. No historical goldens were rewritten.
- The GPU smoke starts at 27 orbital tiles, refines through 186 and 324 to 384,
  explicitly surveys a cube edge, longitude wrap and both poles, and returns
  to the initial site. Nine replacements retire 1,528 nodes; the observed
  old/new peak is 716 nodes and the final scene contains 384 terrain children.
  Every frame checks the 384/768 caps, and each completed replacement checks
  that actual scene children match resident tiles. These are scripted camera
  stages and survey relocations, **not a full simulated circumnavigation**.
- Captures report steady-state frame intervals only. They do not characterize
  upload hitches, sustained flight, process peak memory or a gameplay benchmark.
- Native synthetic input also passes in streaming mode: thrust, turning,
  release, pause, focus handling, cockpit switching and seated head-look.
- The pre-existing broader [numerical compatibility failures](NUMERICAL_COMPATIBILITY_FINDING.md)
  remain unresolved; these targeted passes do not imply the full suite is green.

```sh
ctest --test-dir build -R 'godot-(planet-stream|snapshot)-contract' --output-on-failure
# Repeat with build-clang after configuring/building its optional study targets.

godot --headless --path experiments/godot-freedom \
  --script res://stream_test.gd -- "$PWD/build-godot/snapshot-42-stream-true.json"

# Use an already-built/staged extension; this script needs a graphics session.
godot --path experiments/godot-freedom --audio-driver Dummy \
  --script res://stream_smoke.gd -- --stream=true --view=4 \
  --snapshot="$PWD/build-godot/snapshot-42-stream-true.json" \
  --assets="$PWD/assets/visual" --render-size=3840x2160 \
  --capture="$PWD/build-godot/stream-approach.png"
```

## Next boundaries, not completion claims

The world no longer ends at the old rendered patch. Full-duration pilot
circumnavigation, including polar flight, still needs a dedicated acceptance
run. The first scheduler uses distance and a surrounding buffer, not velocity
prediction, frustum prioritization or horizon culling. Fast travel may outrun
fine-detail preparation, although the previous complete coarse cover remains.

LOD changes still pop; edge stitching/geomorphing, shared edge normals and
walking-scale detail remain work. Shader mineral coordinates still use floats
at planetary magnitudes; walking-level material precision needs a high/low
coordinate treatment. Individual main-thread uploads and boundary payload
conversion can stall despite background generation and the soft upload budget.

The 16 m flight-clearance clamp is not collision or landing. Real orbital
handoff, atmosphere/reentry physics, arbitrary landing, walking, persistence,
controller support and SteamOS/HDR validation are not delivered by this study.
Terrain and planetary art remain provisional, not AAA-complete.

## Planet-dependent weather — next visual pass

User review, 2026-09-18: orbital views should include appropriate cloud cover,
storms and airborne dust, including events that obscure the terrain. This is
a requirement for the next atmospheric presentation pass, **not implemented
weather in the screenshots above**.

- Start from the existing C++ planet descriptor and atmosphere class/pressure.
  Airless bodies have no atmospheric weather. Thin, dusty and cloud-bearing
  atmospheres need distinct profiles; not every world gets Earth-like clouds.
- Cloud layers need height, thickness, coverage and optical depth. Dust storms
  need their own opacity/color profile and may obscure broad surface regions
  when viewed from space. Their appearance must survive changes in view scale.
- A storm seen from orbit must correspond to the same location when approached:
  descending through it changes haze and visibility; emerging above it reveals
  its top. Ground-facing cloud shadows and illumination should agree with that
  cover. Weather must not mask missing terrain or change terrain identity.
- Keep authoritative weather parameters/state C++-owned, with independent
  versioned seeds and simulation time. Derive or persist phase consistently
  when returning to an unloaded region. Godot renders the result. Weather must
  not restart because a terrain tile was evicted.
- Preserve the compact-content/bounded-memory approach: procedural fields and
  quality tiers, not a stored planet-sized image sequence. Begin with coherent
  layers; richer volumetrics are an optional rendering tier. Flight hazards,
  turbulence and damage need explicit gameplay rules rather than being silently
  introduced by a visual effect.

Implementation references: Godot documents
[clockwise ArrayMesh winding](https://docs.godotengine.org/en/4.7/classes/class_arraymesh.html)
and cautions about
[rendering/thread safety](https://docs.godotengine.org/en/4.6/tutorials/performance/thread_safe_apis.html).
