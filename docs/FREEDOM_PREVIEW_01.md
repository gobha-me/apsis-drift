# Freedom — in-engine preview 01

2026-09-18. A **40-second, 24 FPS** scripted technical preview rendered at
3840×2160 in Godot's Vulkan Forward+ renderer. The letterboxed picture is
2.39:1 inside the 16:9 delivery. No upscaled 1080p source is used for the 4K
master. Both deliveries are SDR/BT.709, not HDR.

- [4K MP4](../assets/visual/freedom-preview-4k.mp4)
- [Smaller 1080p MP4](../assets/visual/freedom-preview-1080p.mp4)
- [PNG poster](media/freedom-preview-poster.png)
- [4K cockpit inspection still](media/freedom-preview-cockpit.png) (23 seconds into the film)
- [Frame/stream/decode verification](media/freedom-preview-verification.json)
- [Godot capture report](media/freedom-preview-render.json)
- [Attribution and licenses](licenses/FREEDOM_PREVIEW_01.md)

Godot is now the [selected native presentation engine](GODOT_ADOPTION.md).
This film is evidence of asset presentation, not completion of the Freedom
gameplay milestone or proof of sustained real-time performance.
The delivered film uses cockpit revision 3. The later
[revision-four dashboard correction](COCKPIT_LAYOUT_04.md) changes the current
master/GLBs, so rerendering with current assets intentionally produces an updated
cockpit rather than a byte-identical reproduction of this historical film.
The subsequent [shared flight-cell fit iteration](COCKPIT_FIT_05.md) updates both
cockpit and ship to revision 5 with a new human-scale eye anchor. The delivered
film remains revision-three historical evidence.

## Sequence

| Time | Shot | What it actually demonstrates |
| --- | --- | --- |
| 0–7 s | World reveal | Same C++ procedural planet, optical cloud/limb appearance study |
| 7–14 s | Station | Original hero geometry, modular structures and lighting |
| 14–20 s | Shuttle | Hero exterior, propulsion hardware, landing bays stowed |
| 20–26 s | Pilot's seat | Fixed seated eye, bounded head motion, closed cabin, static instrument art |
| 26–34 s | Terrain | Scripted camera flyover of the existing C++ relief/streamed planet |
| 34–40 s | Closing exterior | Shuttle pullback and project title |

These are editorial cuts between staged views, not a continuous simulated trip.
No docking, landing, walking, fuel use, interactive telemetry or orbital
handoff is implied. The cabin and exterior remain independently authored
assets; shared packaging/attachment coordinates need further work.

## Detail pass

- Use the original hero GLBs for offline close-ups: 219,328 cockpit, 153,970
  ship and 814,864 station triangles, without modifying or destroying masters.
  The existing near/mid/far tiers remain available for runtime budgets.
- Add a restrained procedural material finish, preserving emission and alpha
  materials. Filter subpixel grain with screen derivatives. There are no new
  baked texture downloads or shader-displaced physical surfaces.
- Replace near-tile one-sided mesh normals with a shared C++ metre-scale
  surface stencil. Adjacent tiles now agree on boundary normals; a new regression
  test checks it. Geometry heights, world identity and flight checksums do not
  change. This costs extra generation work; it is not a free performance win.
- Review start/middle/end frames for each shot. Adjust the globe and station
  framing, soften cloud density edges, and keep the cockpit eye fixed. Load and
  shader-preparation frames are excluded from the encoded sequence.
- Retain the established 384-tile budget. Prepare terrain before each relevant
  shot and retain that complete cover during its short camera move, avoiding
  worker-timing-dependent LOD changes in the film. Interactive streaming still
  updates normally in the regular viewer. This film cannot measure its hitches.

The optical shells are explicitly **appearance prototypes**. They are gated by
the descriptor's atmosphere class/pressure and tinted from its palette; airless
worlds receive none. The film uses a frame-addressed visual phase. It does not
implement persistent, C++-owned weather, storm hazards, cloud-ground shadows or
consistent inside-cloud flight. The same-world weather requirements in
[study 04](GODOT_STUDY_04.md) remain work; a pretty orbital layer is not their
completion. Volumetric clouds, dust-specific profiles, geology, surface props,
LOD edge morphing and finer material/geometry polish also remain work.
The empty space background is a staging placeholder, not a universe starfield.
The [connected-sky design direction](FREEDOM_DESIGN_PROPOSAL.md#connected-sky-design-direction-discussed-2026-09-18)
ties future individually rendered stars to chart/destination identities.

## Audio and encoding

The soundtrack preserves the existing First Light MIDI/SoundFont assets and
uses the C++ audio-pack audition tool's music-only output. The first 40 seconds
are faded in/out and loudness-normalized for this film. This is a soundtrack
mix, not proof of live in-engine audio scheduling. No new music was generated.

Every source image, including titles, letterbox and fades, comes from Godot
after `RenderingServer.frame_post_draw`. FFmpeg only encodes/color-converts
the images, mixes the existing score render, and makes the smaller delivery.
There is no AI video generation or interpolation. Capture time is independent
of presentation time; an offline render can take longer than 40 seconds.

## Reproduce and verify

Requires the normal C++ toolchain, installed Godot 4, a working graphics session,
FFmpeg/ffprobe, Python and the repository assets. No engine installation occurs.

```sh
GODOT_BIN=/path/to/godot tools/render_freedom_film.sh build-godot/new-film-directory
```

The output directory must not already exist. The script retains 960 source
PNGs, the Godot report/log, the audio-source render, both MP4s and verification
JSON. Source frames are disposable build artifacts, not shipped planet data
or required game content. Existing files are never silently overwritten.

Validation includes all 960 camera poses, malformed options, non-finite values,
dimensions, fixed seated eye, source frame numbering and PNG sizes, distinct
moving frames, contiguous shot boundaries, both videos' full decode, 24 FPS,
40-second duration, 960 decoded video frames, stereo 48 kHz AAC and hashes.
GPU error logs must be clean before encoding. These checks supplement visual
review; they do not certify that every visual detail is finished.

Delivery verification, 2026-09-18: both videos passed full decode, 960 frames,
24 FPS, 40 seconds, BT.709 limited-range color and stereo 48 kHz AAC checks.
The 4K file is 38,957,561 bytes; 1080p is 7,882,755 bytes. There are 949 distinct
source frames; the repeated black endpoints are intentional fades. Hashes are
in the linked verification report. The encoded cockpit frame was also reviewed
after color conversion. The first encode retained the source sRGB transfer tag;
delivery was regenerated with explicit sRGB-to-BT.709 conversion, not retagged.

The film camera/atmosphere contract and GPU appearance-profile smoke test pass.
Snapshot, streaming and final asset-manifest checks pass under both GCC and
Clang; the MIDI offline test passes. The broad legacy regression runs are not
claimed green: after the long onboarding matrices completed, eight legacy test
entries still failed in each compiler run (main suite and seven acceptance
matrices). These include the documented reference-state portability problems;
the entire cause of every acceptance failure has not been isolated. Initial
missing-delivery manifest failures were resolved by copying the verified files;
the initially unbuilt MIDI spike executable was built and its test rerun.

GCC and Clang run the C++ streaming/snapshot contracts; actual extensions retain
their headless streaming and live replay checks. The existing broader
[numerical portability failures](NUMERICAL_COMPATIBILITY_FINDING.md) remain.

Godot reference: [frame capture API](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html)
and [image output](https://docs.godotengine.org/en/4.7/classes/class_image.html).
