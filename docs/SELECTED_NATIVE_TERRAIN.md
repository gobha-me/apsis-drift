# Selected native terrain

The #212 integration selects physical local-detail terrain for new native games
and persists that choice through the journey. Historical saves retain their
selected terrain. C++ owns the scalar field, top triangles, contact and saved
policy; Godot presents the selected geometry rather than adding shader-only
displacement. See [local-detail sampling](TERRAIN_LOCAL_DETAIL.md) and the
[planetary scale contract](PLANETARY_SCALE_CONTRACT.md).

## Two explicit policies

| Saved flight policy | Physical height and top geometry |
| --- | --- |
| `1`, historical | Existing terrain-v1 sampling and saved contact recipe2; unchanged legacy arithmetic |
| `2`, new native game | Physical local-detail recipe2 over unchanged terrain-v1 source-LOD8 vertices, with saved contact recipe3 |

Both keep terrain tile generator1 and its original source cache/checksums.
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
policy2. Continue preserves the saved selection without silently upgrading old
terrain, relocating a craft or changing its landed anchor.

Outer starting-assembly 21, boarding 22, surface 23, resources 24, knowledge 25,
travel 27, recovery 29 and suited-walking 30 wrappers retain their existing format
numbers. Their complete nested flight/docking/journey stores the selected
31/32/33 envelope. A landed anchor's terrain policy must agree with its validated
base flight; its selected contact recipe is revalidated before continuation.

Unknown policy, missing or malformed selected field, disagreement between
format and model, or anchor/base mismatch refuses. A policy2 field cannot be
smuggled into historical 18/26/19/20, and relabeling an old envelope cannot
default it to 2. Failed decode or validation does not commit a replacement
session; rejected writes preserve existing destination bytes. See the
[save layers](FREEDOM_SAVE_FORMAT.md), [saved flight](SAVED_NATIVE_FLIGHT.md) and
[landed lifecycle](LANDED_CRAFT.md).

## Streaming and residency

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
unchanged save bytes. The surface variants use the same landing, thruster
liftoff, ground walking, reboarding and Save/Continue consumers with explicit
legacy or local-detail C++ fixtures. They share one prepared asset set in the
runner. See [runtime checks](DEVELOPMENT.md#native-runtime-contracts) for staging,
failure handling and retained evidence.

Near-ground fixtures do not represent an uninterrupted station-to-site flight;
the separate [voyage checks](PLANETARY_VOYAGE_PROTOTYPE.md) cover that composed
route. The command is a reproducible check selection, not a claim that this
revision has passed it. Headless integration checks cannot establish visible
feature size, four-pixel projection, GPU performance or controller feel.

## Remaining limits

Selection and shared ownership do not establish visual or hardware acceptance.
Current skirts, LOD transitions/popping and broader terrain presentation remain
work; this integration does not add weather. Physical controller feel, GPU
appearance and frame pacing require their own checks. Record actual feature
width/height and sample spacing before claiming recognizable relief, and use
the real camera/output projection for pixel targets. No provider timing,
shader color or software capture alone proves runtime performance.
