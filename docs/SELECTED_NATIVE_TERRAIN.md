# Selected native terrain

The #212 integration selects physical local-detail terrain for new native games
and persists that choice through the journey. Historical saves retain their
selected terrain. C++ owns the scalar field, top triangles, contact and saved
policy; Godot presents the selected geometry rather than adding shader-only
displacement. See [local-detail sampling](TERRAIN_LOCAL_DETAIL.md) and the
[planetary scale contract](PLANETARY_SCALE_CONTRACT.md).

## Explicit terrain policies

| Saved flight policy | Physical height and top geometry |
| --- | --- |
| `1`, historical | Existing terrain-v1 sampling and saved contact recipe2; unchanged legacy arithmetic |
| `2`, selected local detail | Physical local-detail recipe2 over unchanged terrain-v1 source-LOD8 vertices, with saved contact recipe3 |
| `3`, new native game | Recipe2 heights raised to the sea datum when submerged, raw wetness retained, saved contact recipe4 |

All keep terrain tile generator1 and its original source cache/checksums.
Policy2 adds continuous macro interpolation and independently seeded local
relief; it does not reinterpret the legacy integer tiles or generator-version
tuple. The complete physical catalog/body owner distinguishes authored Origin
from a procedural planet with the same numeric identity.

The selected finest top triangulation remains mesh LOD13, 32 intervals per
tile and the existing anti-diagonal: `(00,10,01)` and `(10,11,01)`. Recipe3
samples its vertices from the physical local-detail field. Landing, hull
clearance and ground walking use those selected top triangles; native streaming
uses the same selected C++ field. Skirts and material shading are presentation
details and do not supply contact surfaces. The provider's finite-scale tangent
slope estimate is not a facet normal or a support certificate.

## Save compatibility

`FreedomFlightModel.terrain_policy` defaults to 1 for historical documents.
Existing flight formats 18/26 keep their exact eight-field model. Policy2 adds
a mandatory ninth model field, `terrain_policy: 2`, in these explicit formats:

| Layer | Historical format | Selected terrain format |
| --- | ---: | ---: |
| Physical flight | 18 or 26 | 31 |
| Port docking | 19 | 32 |
| Station actor journey | 20 | 33 |

Docking/journey flatten the flight envelope, so their decoders restore the
matching selected lower layer. Formats 31/32/33 can retain the explicit active
world owner; they do not invent another universe or clock. New Game selects
policy3 in the explicit optics1 envelopes below. Continue preserves the saved selection without silently upgrading old
terrain, relocating a craft or changing its landed anchor.

Outer starting-assembly 21, boarding 22, surface 23, resources 24, knowledge 25,
travel 27, recovery 29 and suited-walking 30 wrappers retain their existing format
numbers. Their complete nested flight/docking/journey stores its matching
selected envelope: 31/32/33 for local detail, or 39/40/41 with optics1. A landed anchor's terrain policy must agree with its validated
base flight; its selected contact recipe is revalidated before continuation.

Unknown policy, missing or malformed selected field, disagreement between
format and model, or anchor/base mismatch refuses. A policy2 field cannot be
smuggled into historical 18/26/19/20, and relabeling an old envelope cannot
default it to 2. Failed decode or validation does not commit a replacement
session; rejected writes preserve existing destination bytes. See the
[save layers](FREEDOM_SAVE_FORMAT.md), [saved flight](SAVED_NATIVE_FLIGHT.md) and
[landed lifecycle](LANDED_CRAFT.md).

## Triangulated ocean top

Policy3 is available only in explicit optics1 flight/docking/journey envelopes
39/40/41. New Game selects it; historical optical policies1/2 and all earlier
formats retain their exact arithmetic and accepted policies. Recovery, travel,
walking and nested wrappers preserve the selected owner and policy.

The underlying terrain remains recipe2. A shared C++ operation raises raw
sub-datum TOP vertices to zero and retains their raw wetness when the C++
descriptor has nonzero water coverage. Zero-water bodies retain negative relief
as dry geometry/material; the existing conservative support checks still apply. Streamed meshes,
contact recipe4 and hull clearance use those vertices and the existing finest
topology. Fully dry triangles keep their original vertices and normals. Mixed
shoreline triangles change when submerged vertices rise; preserving their dry
portions exactly would require additional clipping topology.

These are actual triangulated chords, with interiors slightly below the datum,
not an exact spherical shell. Radial first-surface queries intersect the actual
facet. Reported reference altitude remains radius-relative; it is not a terrain
clearance. No radius inflation or tolerance relaxation conceals chord sag.

Raw triangle wetness classifies all-dry, all-wet and mixed facets. Wet facets
refuse landing, and mixed facets remain unsupported for landing/walking. Existing
dry support/slope checks remain required. Ordinary flight still has no terrain
floor guard; the optional impact prototype retains its explicit refusal of
unsupported wet/shore intersections. There is no supported water-entry response,
swimming, buoyancy, waves or phase simulation.

C++ supplies wetness with the terrain buffers; native shading blends bounded
water albedo/roughness/specular response. Coarse wetness interpolates through the
same morph correspondence as geometry. Invalid base attributes and morph weights
refuse before upload. Material blending across a mixed facet grants no support
or survey knowledge, and pale terrain remains styling rather than physical ice.

The bounded software comparison holds one existing wet patch at an initialized
external viewpoint, with the same pose, clock and actual daylight. The sea top
rises about 301 m above the prior basin there, and both full saves round-trip
exactly. The volcanic palette makes the sea almost uniformly brown: it does not
yet read convincingly as an ocean. This verifies geometry/material integration,
not coastline, specular quality, final art, flown arrival or hardware acceptance.
The retained HOME marker overlaps the diagnostic caption.

## Streaming and residency

The planetary material uses the C++ mesh's height-blended vertex colors as its
albedo base. Neutral mineral/strata detail modulates brightness without replacing
the palette with a common brown surface. Without a material-profile packet its
scalar factor stays between 0.2925 and 0.495 and fades to 0.45 at orbital distance.
The same coarse/fine color endpoints participate in LOD morphs.
Normals, terrain shape, star lighting and optical cover retain their existing
owners. Nonplanet study materials retain their previous shading.

These are visual palettes. Pale terrain does not certify ice, green does not
identify vegetation, and blue does not introduce a liquid surface or new contact
rules. Palette presentation grants no survey or hazard knowledge.

### Surface finishes

C++ material profile1 derives four bounded artist coefficients from the existing
descriptor's palette family. It changes albedo scale, detail contrast, roughness
and specular response on the actual terrain, with existing footprint/distance
filters. Glacial styling is brighter, smoother and less granular; arid styling
is dusty, while volcanic rock retains stronger mineral contrast. These are
cosmetic finishes, independent of the generated temperature/pressure envelopes.

| Palette art | Albedo scale | Detail contrast | Roughness | Specular |
| --- | ---: | ---: | ---: | ---: |
| Verdant | 0.45 | 0.85 | 0.88 | 0.35 |
| Arid | 0.55 | 0.65 | 0.98 | 0.18 |
| Glacial | 0.72 | 0.30 | 0.38 | 0.65 |
| Volcanic | 0.43 | 1.25 | 0.85 | 0.40 |
| Alien | 0.50 | 0.90 | 0.82 | 0.45 |

The current-body packet supplies only version/disclosure1, authenticated body
IDs and integer coefficients. It grants no knowledge, material composition or
phase measurement. Unknown versions, wrong owners, missing fields and values
outside the declared coefficient bounds refuse before material mutation.
Unchanged profiles do not request a full material update. New tiles receive the
accepted parameters before display, including during LOD replacement. Missing
historical packets use the neutral shader fallback. Current presentation applies
the cosmetic profile without changing saved recipes or full save bytes.

Terrain heights, physical normals and contact remain unchanged. This does not
certify load-bearing ice or add slippery surfaces. Historical policies1/2 retain basin geometry and sub-datum landing/walking
refusal. Policy3 selects the triangulated sea top described above; material
finishes themselves do not alter that physical selection.

The bounded software comparison uses authored, fixed material planes and one
retained actual ground view. It shows brighter glacial art and warmer arid art;
the cockpit ground pair has limited visible terrain. Flat planes mainly check
albedo differences and do not establish convincing ice, grazing-angle specular
response, new relief, oceans or hardware quality. Those limits do not change the
physical surface or block continued work.

Ordinary bridge streams retain the session's complete physical owner, current
planet BodyId and saved policy. The descriptor-only constructor remains for
explicit studies. A completed batch carries its stream kind, generation and
source identity: owned batches identify system/body/policy and physical catalog;
study batches identify their experimental source.

C++ clears exported tile payload keys when it replaces a stream. Before
uploading a batch, Godot validates its identity against the current stream and
retires resident and pending covers when owner, policy or generation changes.
Matching cube-face tile keys cannot authorize reuse of another selection's
geometry. Malformed or mismatched batches refuse before stale upload. Ordinary
clock advancement within the selected world does not choose another recipe.

Within one world, incoming meshes remain hidden until their complete placement
batch validates. A failed surface upload or invalid placement preserves the
accepted cover's visibility, transforms and committed counters, and latches a
presentation error until explicit reset. World-owner changes still retire the
old world's geometry rather than retaining an unrelated cover.

LOD replacements use a quarter-second geometric transition. C++ derives coarse
endpoints from the actual triangles of the previous or incoming cover, without
another terrain query. Refinement draws incoming fine topology; coarsening keeps
outgoing fine topology until it has collapsed onto the incoming coarse surface.
Positions, normals, colors and elevations share one presentation clock. Pausing
freezes that clock while pending mesh uploads can finish; simulation time and
save state do not change with presentation progress.

The temporary draw cover contains only existing old/new keys and stays within
their combined limit of 768 tiles. No third cover is requested during a morph.
Uploads retain the four-mesh/two-millisecond soft budget; swept mesh bounds feed
both visibility culling and camera depth. Malformed transition packets preserve
the accepted base cover. Extra attributes on accepted fine meshes remain
inactive until those meshes retire; the reported attribute payload is not a GPU
allocation or performance measurement.

This interpolates the top surface. Skirts keep each fine tile's existing inward
offset; they do not reproduce the old coarse skirt triangulation or weld cube
faces. A smooth top-surface handoff alone does not certify every seam, lighting
transition or screen pixel.

For policy2, tile planning subtracts the selected surface elevation from the
radial observer distance before scoring the existing datum grid. A low camera
over elevated ground therefore requests local detail using its radial surface
clearance. This adjustment affects refinement only; camera pose, terrain
vertices and physical state retain their actual positions. The historical
planner is unchanged.

## Focused checks

With a finished matching native build and an explicit Godot executable:

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" --build-dir build-native \
  --test native_selected_terrain \
  --test native_surface --test native_surface_detail \
  --test native_surface_walk --test native_surface_walk_detail
```

The selected-stream contract exercises real New Game, metadata refusals,
legacy/selected Continue, pending-payload retirement, mesh residency and complete
unchanged save bytes. It also checks transition direction, paused progress,
retirement, reset, swept bounds and malformed endpoint buffers. The C++ stream
contract independently checks actual coarse triangles, shared-edge endpoints,
both directions and bounded payloads. The surface variants use the same landing,
thruster liftoff, ground walking, reboarding and Save/Continue consumers with explicit
legacy or local-detail C++ fixtures. They share one prepared asset set in the
runner. See [runtime checks](DEVELOPMENT.md#native-runtime-contracts) for staging,
failure handling and retained evidence.

Near-ground fixtures do not represent an uninterrupted station-to-site flight;
the separate [voyage checks](PLANETARY_VOYAGE_PROTOTYPE.md) cover that composed
route. The command is a reproducible check selection, not a claim that this
revision has passed it. Headless integration checks cannot establish visible
feature size, four-pixel projection, GPU performance or controller feel.

## One measured low-flight feature

The opt-in `--terrain-feature` fixture initializes one viewpoint above a measured
ridge in universe 42's existing first target, body `7198692597331823481`. It samples
the actual catalog 2, terrain 2 owner and chooses one daylight tick from its real
rotation. This is an initialized measurement view, not a flown arrival or the
ordinary voyage. The saved craft sits 50 m above the left trough; the cockpit eye
keeps its authored offset and looks toward the crest.

With a matching native build, retain one staged project for the capture:

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --test native_asset_import \
  --test native_selected_terrain --keep-work
```

Set `FEATURE_STAGE` to the work directory printed by that command. Use an empty
fixture directory and a new capture output directory. The fixture refuses mixed
voyage options and existing output content.

```sh
mkdir "$FEATURE_STAGE/feature"
build-native/src/godot/apsis-drift-freedom-planetary-fixture \
  "$FEATURE_STAGE/feature" --terrain-feature
"$GODOT_BIN" --path "$FEATURE_STAGE/project" \
  --rendering-method gl_compatibility --resolution 1280x720 \
  --script res://studies/captures/native_terrain_feature_capture.gd -- \
  "$FEATURE_STAGE/feature" "$FEATURE_STAGE/native-assets" \
  "$FEATURE_STAGE/feature-output"
```

The capture retains an untouched image, actual camera projection of the three
physical points, nearest streamed top-grid vertices and their incident surface edge lengths,
stream identity and source/save hashes. Pixel coordinates use the reported
camera viewport dimensions. Save/Continue must retain the complete initialized
state. Physical sampling spacing, streamed edge lengths and visible feature size
are separate measurements; a nearest vertex is not a contact or visibility
certificate. Inspect the image before drawing a conclusion about readability.

The initial measured ridge has 28.18 m prominence over 3.04 km. At the crest, the
retained top grid has approximately 54 m edges; the nearest vertex is 21.5 m away
horizontally and differs by about 0.07 m in local height. The actual 1280×720
Compatibility cockpit image still looks subdued. This establishes a concrete
sampling/projection baseline, not recognizable-relief acceptance or evidence
that the renderer erased the feature. Wider composition, transition and weather
work remains open under #212.

## Remaining limits

Selection and shared ownership do not establish visual or hardware acceptance.
Skirt seams, broader LOD/lighting continuity and terrain composition remain
work; geometric morphing does not add weather. Physical controller feel, GPU
appearance and frame pacing require their own checks. Record actual feature
width/height and sample spacing before claiming recognizable relief, and use
the real camera/output projection for pixel targets. No provider timing,
shader color or software capture alone proves runtime performance.
