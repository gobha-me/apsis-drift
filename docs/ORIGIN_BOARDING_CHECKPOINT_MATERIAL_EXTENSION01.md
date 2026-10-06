# Checkpoint material extension01 — registered two-source completion

Selected for #456 on2026-10-06, after #455 merged at `d653d79e08c5be6f2c29eddd9f2e33ccd40f7d68`. This registration precedes new source capture, constructor queries and WORLD02 observations. WORLD01 already proves preparation[0,0.25] but stops on missing material relations for the cabin emergency pressure frame and retained nose joint backing. This work completes those sources and consumes their relations over the same Self02 cover. Body sizes, controls, clock, original geometry/removals/hardware and existing InitialMaterial01/WORLD01 observations remain unchanged. No actor, seat, save or First Flight authority.

## Fixed input and capture

| Source | Current object | Full vertices/triangles | Existing native crop |
| --- | --- | --- | --- |
|1436|WF02 \| CABIN emergency pressure frame|256/512|craft_fixed object650, start188765,296 triangles|
|1574|WF02 \| retained nose joint backing|3152/5187|craft_fixed object733, start341614,2898 triangles|

Both are fixed original MESH objects with no parent/motion group, not replacements or new geometry. The current19-mesh package omits both full meshes. Crop faces cannot restore their missing full geometry or material semantics. Current master identity is `87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677`; inventory identity `a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8`.

Frame whole ordered raw/quantized triangle fingerprints: `0047c631d9b53d9f7d6d0955f99a546b9d2562f128748eecc12be818280ba9aa` / `789f5d12d72f9b9185a40e33bdcdb888df0a15f89083de3b1ddcb86738a75507`. Nose: `983a916affe2927c3fc9f7b96e6d2102cb26bdcac80d7c27568fc57714801c5f` / `9731912e8929e9476f7a0aef3ae7badbd6c193489ac5493606fb542f6756d7ef`. These authenticate whole ordered streams, not an invented per-face lookup table. Full evaluated ordinals remain distinct from native crop keys.

One narrow read-only capture opens the unchanged master with the same Blender5.2.2LTS convention and explicitly extracts only these two evaluated meshes. Collect the frame's current base32 vertices/32 quad faces, full modifier fields and affine frame, plus both available current properties. Verify master before/after, save count0, expected counts/fingerprints and existing crop attribution. Do not invoke the previous broad6+165+13 capture, save a source, remodel or generate assets. Freeze the new capture helper and its exact hash before running it.

Existing helper identities: material_inventory.py `df430228aef4e1527076353e20e03bda518a3f63abaa131d7a2f51e8953fdf22`; material_source_completion.py `8fbb8a8bed5abcca57a2e8626966466d6cd4f5e8b89c0f2c0ee122dfc60eaceb`; prepare_material_source.py `13a64f8a8fe440db369622df8093fdb7c4b487f9c43a7c9e5fb229b5c4d51987`. Reuse checked source/quantization/hash helpers. Public build inputs are the normalized additive package, not a private master path or asset-history checkout.

## Selected material rules

**FrameAnnulus01.** Historical retained constructor hopper_lifeboat02.py (SHA `1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1`) builds an annular extrusion of explicit outer/inner eight-point profiles at authoredY1.035±.028,32 vertices/32 quads, BEVEL width.006/segments3 and WEIGHTED_NORMAL. Bind the actual current base topology/profiles/modifiers/affine frame to that named source; history does not replace current binding.

Select the union of eight actual convex profile-band prisms with six outward planes each as a sufficient containing material relation, preserving the aperture. Validate profile orientation, convexity, extrusion and all base vertices. Use actual stored normals and outward support maxima over the base prism vertices; do not assume exact unit normals or unrounded plane coefficients. Every raw evaluated triangle must have all three vertices in one registered convex sector. The guard may refuse a bevel or sector seam; its outcome is unknown. An uncontained raw source remains unavailable, with its actual triangle/sector/predicate retained. No post-result profile/radius fitting, enlarged encloser or filled frame hull.

Quantized containment is separate. Freeze a world-axis cube allowance of one micrometre: nearest-micrometre rounding contributes at most half a micrometre per coordinate, and stored-grid decoding at absolute coordinates≤8m is well within the remaining half. Verify that derivation and reject outside-workspace input. Shift plane support maxima outward by this fixed allowance times the stored normal's L1 norm using outward arithmetic. This allowance covers quantization, never a raw/bevel containment failure. Raw and quantized guards remain separately counted.

**RetainedCutSkin02.** Select the pinned current nose descendant under inherited ShellSheet semantics: its exact full5187 triangles represent the cut skin, with open cuts and no introduced caps, thickness or filled interior. Historical clip_copy preserves source UV/matrix and cuts atY1.12/X±1.4, with no fill or SOLIDIFY; the current object has no modifiers. This is explicit game material policy, not volume permission inferred from clear surfaces. Bind current name/master/whole fingerprints and available properties. The ancestor was deleted and clip_copy did not retain ancestor/cut-plane custom fields: do not require or claim nonexistent lineage. Clearly distinguish historical recipe evidence from authenticated current geometry.

## Package, issuer and consumer

Create additive `assets/native/wayfarer-checkpoint-material-extension-01` with provenance/license metadata, two complete mesh packets and the frame constructor packet. The existing package remains unchanged. Additional geometry binary is48*3408+12*5699=231,972B. Conceptual combined geometry has21 meshes/191943 vertices/354786 triangles; existing decoded source is retained, not copied.

A purpose-specific immutable extension issuer binds the old material capability, native source/removal identities and OperatingProgress{1,1,1,0}. Only original indices1436/1574 may acquire these genuine relations. Caller enums/flags/raw solids do not enroll material. Constructor admission may refuse; preserve actual evidence before strengthening a test.

WORLD02 creates one fresh original Self02 child, retains one original cover and source handles, and uses the existing full-frame WORLD proxy and immutable union→retained-cell arithmetic. No second DFS, graph/body copy, controls or per-pair subdivision. Frame exclusion requires separation from every authenticated prism; neither new object receives a sole/floor exemption. Nose streams all original full triangles under the selected sheet policy. Other unknown/stowed sources retain named refusal. Preserve the old WORLD01/InitialMaterial01 results fieldwise; their source1 API must not acquire the new policy implicitly.

## Registered resources

Extension decoded source≤256KiB, including prepared vertex/index arrays, descriptors, constructor/base/plane data and retained handles. No binary+decoded duplication, per-face SHA string copies, duplicated crop tables or runtime JSON. Base14,049,188B+extension cap262,144B gives14,311,332B within the complete-material32MiB bound; existing original/HALO sources retain their separate budgets. Reconcile actual layouts before admission. Read-only capture/decode extra scratch≤64MiB, excluding unchanged master/Blender host storage.

Freeze base/topology/metadata constructor guards≤4096, full raw+quantized vertex coordinate guards≤20448 (3408*3*2), full face index guards≤17097 (5699*3), and frame triangle-in-one-sector plane-vertex checks≤147456 (512*8*3*6*2). Count actual attempted operations; allowances are not executed counters. New constructor numeric stage≤8KiB; WORLD stage≤16KiB; co-live original child phase≤48KiB; owned output≤16MiB. Include new controller/return/source-proof temporaries, do not relabel scratch as source. Actual sizeof/assertions and GCC/pinnedClang20 O3 stack receipts precede FIRST.

Inherit unchanged child/clock/depth/node/leaf caps; material roster1759/effective1751; union envelopes26265; domain15+15360; refined envelopes1048576; refined triangles1048576; shared axes16777216; eager direction entries20217856; sole vertex guards192. New source-derived caps are base enclosures360, refined enclosures368640, total proxies2496512, full-material triangles354786, combined HALO triangles362886 and triangle-union comparisons5443290. Six-plane frame primitives prepare no direction list. Lowered/overcap controls must reach actual stages and stop before the next operation. No cap increase following a refusal.

## Outcome discipline

Freeze code, capture receipts, actual source/live/output accounting and registered recipe before first constructor and WORLD02 observations. Keep both compiler outcomes before making an observed success/refusal mandatory. Meaningful controls include annular cavity vs material-band crossing; clear sheets inside a containing volume; malformed base/modifier/hash/mapping; seam/raw-containment refusal; full ordinal vs crop key; old named WORLD01 refusals; invalid/moved/stale bindings, nonfinite/unsafe state; and actual zero/exact/one-less source/proxy/face/plane/preparation work. Raw arithmetic carries no source/body/WORLD/actor authority.

A new refusal identifies the next concrete blocker. It does not authorize body/primitive/control tuning or completion of #361. The full transfer and actual seated endpoint remain separate acceptance requirements.
