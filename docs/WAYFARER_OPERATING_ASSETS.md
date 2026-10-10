# Wayfarer operating asset delivery

2026-10-01, [#346](https://github.com/gobha-me/apsis-drift/issues/346).
`assets/native/wayfarer-operating-02` delivers source-bound moving Wayfarer
hardware and three small D1 station replacements for explicit mechanism
inspection. C++ supplies the actual seeded station/craft placement. The preview
changes presentation poses while preserving the actor, ship, shared clock and
save. Actual hatch/ladder entry, sitting and departure remain subsequent work
under [#245](https://github.com/gobha-me/apsis-drift/issues/245) and
[#291](https://github.com/gobha-me/apsis-drift/issues/291).

## Prepare and inspect

Python 3 with its standard `lzma` module is required. Run from the repository
root on Linux; atomic installation uses `renameat2` with no replacement.

```sh
python3 tools/prepare_operating_assets.py \
  --package assets/native/wayfarer-operating-02 --verify-only
python3 tools/prepare_operating_assets.py \
  --package assets/native/wayfarer-operating-02 \
  --output build-native/prepared-operating
```

The package preserves the archived license/source lineage of the selected
[starter delivery](NATIVE_STARTER_ASSETS.md). Preparing it needs no authoring
checkout, provider account, asset regeneration or external download. The
operating view also consumes the original station from a separately prepared
`freedom-starter-01` package.

The five-document license roster includes `LICENSE.md`, `HOPPER_MESHY_TRIAL.md`,
`STATION_KIT_01.md`, and the linked `HOPPER_GENERATED_CONCEPTS.md` and
`MESHY_QUALIFICATION_OUTPUT.md` grants.

Preparation checks the closed file roster, selected source/model/metadata
identities, licenses, receipt relationships and bounded JSON/contact buffers.
Decoded models receive finite attribute/index checks and exact source-bound
node/mesh checks before the completed directory is installed. Reusing the same
output verifies its bytes; changed or unowned output refuses replacement.
Concurrent destination creation cannot replace even an empty directory.

With a matching native bridge/exporter already built, run the read-only Godot
contract using the qualified Godot 4.7.2 executable:

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --test wayfarer_operating --keep-work
```

The runner creates an isolated project and prepares both packages. Its printed
`native-contracts-…` directory contains the staged bridge, input hashes,
preparation receipts, logs and report. The contract checks real import,
source-derived poses, invalid requests and unchanged C++ actor/body/tick state.
It does not start player boarding.

For an explicit rendered inspection, set `CONTRACT_DIR` to that absolute run
directory and `CAPTURE_DIR` to an existing, disposable absolute directory:

```sh
"$GODOT_BIN" --rendering-method gl_compatibility \
  --path "$CONTRACT_DIR/project" --audio-driver Dummy \
  --script res://studies/captures/wayfarer_operating_capture.gd -- \
"$CONTRACT_DIR/native-assets" "$CONTRACT_DIR/native-assets/operating" "$CAPTURE_DIR"
```

This opt-in script requires a rendering display and records ten 1280×720
hardware views: sealed/open roof, deployed ladder, inner door, flight/boarding
seat, and D1 open/attached-closed from two inspection directions. `capture.json`
records image/script/bridge/input hashes and actual group poses. Inspection
camera movement provides no actor traversal or target-hardware performance
acceptance.

## Immutable source and geometry

| Input | SHA-256 |
| --- | --- |
| Craft09 master | `87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677` |
| Selected station reference | `6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6` |
| Authored station closure | `6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4` |
| Qualified flight exporter | `762499e37fe13a33bf297fc58801e711590cd9d56058a6462b3467ff6b7102fe` |

`tools/operating_asset_identity.py` separately pins approved model, manifest,
contact, closure and qualification bytes through repository-owned policy;
approved identities are not inferred from a receipt's assertions. Its tool hash
is included in provenance; the six physical identity hashes exclude provenance
and the package manifest, so this creates no recursive dependency. Original masters,
original runtime exports and `freedom-starter-01` are preserved. The operating
package adds a separate derivative; ordinary sealed-flight/station assets retain
their existing delivery.

The producer reuses the exact qualified exporter visibility, UV, PBR, texture
limits, geometry reduction and seat calibration. Its roster assigns all
**1,746** included source objects uniquely, with explicit exclusions. Wayfarer
retains **728,738 triangles** in **47** mesh batches. Its operating GLB is
**40,615,884 bytes**. The D1 replacement GLB contains three meshes and **3,208
triangles** in **165,248 bytes**; the complete station is consumed from the
existing starter package.

Thirteen independent moving groups separate the roof pair, four inner leaves,
base/upper ladder, seat carriage/swivel/lift, entry arm and positive lock. They
contain complete world-baked rest geometry as flat identity siblings. Source
ancestry determines membership; a complete ancestor-composed world delta applies
once to each group. Source rig matrices remain provenance rather than another
runtime parent transform. Coordinates are metres, Godot +Y up/-Z forward, with
Blender `(x,y,z)` mapped to `(x,z,-y)`.

The corrected flight pilot eye remains `(0,1.365,-2.49)` m; the authored boarding
seat endpoint gives `(0.04,1.525,-1.7)` m. The inherited calibration moves the
seat 0.18 m up and 0.08 m forward. These anchors do not demonstrate body entry,
foot support or occupied seat reach.

## Bounded operating corrections

`derivative_corrections` records operation versions, exact source objects,
original/proposed local rest matrices, translations and channel parameters.
These adjustments preserve the selected craft's exterior silhouette, object
roster and triangle count while resolving measured component interference:

| Mechanism | Recorded derivative adjustment |
| --- | --- |
| Roof hinges | Four existing internal links move 25 mm inward in hinge-local X, clearing the pressure ceiling while preserving pin engagement and the authored hatch pivots/rotation. |
| Inner door | Four authored leaf strokes stop at 0.55 of original travel, leaving a measured 1.025 m central aperture before decorative inlays/sills reach fixed frame members. |
| Seat lock | The existing pin withdraws 70 mm along carriage-local X before swivel rotation, then restores only after 90° endpoint alignment. |
| Seat fastener | One existing mounting bolt moves 7.5° around the unchanged 0.24 m pitch circle. The source cylinder has no dedicated hole/boss; bolt geometry and all twelve fasteners remain. |

The bolt correction leaves a measured **10.532 mm** perpendicular surface
interval gap to the pin, **74.080 mm** lower-bound bounding-box separation from
its nearest bolt neighbor, and **15.000 mm** withdrawn pin/housing engagement.
The rejected source sequence crosses the lock-axis bolt during withdrawal;
reversing the roof rotation creates additional leaf/ceiling/throat collisions.
Those failures are retained as negative evidence.

The recorded `roof_transfer` channel opens the roof over progress 0–0.40, swings
the base ladder over 0.40–0.70, then extends the upper ladder over 0.70–1.
`inner_door` scales its bounded stroke independently. `seat_boarding` slides/lifts
over 0–0.55, withdraws the lock over 0.50–0.60, swivels over 0.60–0.95 and restores
the aligned lock over 0.95–1. The entry arm retains its current source pose;
old cockpit-seat studies do not qualify a different arm animation or current
standing entry. The rear ramp stays closed and gear stays at source frame100
rest throughout these channels. Gear preview retains its separate asset curve.

The manifest contains 21 samples per channel and six named poses. Craft preview
accepts the exact recorded progress values `n/20`; non-grid requests refuse
atomically, with no snapping. Producer
checks compare every included object's source world matrix with its group delta
at all 69 recorded poses; maximum absolute matrix-component discrepancy is
`4.7684e-7`. The 41 source hardware qualification samples are separate from these
21 published knots. Interpolating complete world deltas would move the actual
hinge pivots between samples; a pivot-aware curve needs separate qualification
before continuous mechanism animation or actor composition. Sampled transforms
do not establish mechanical load ratings, pressure integrity or continuous
motion separation.

## Contact evidence and remaining traversal seam

The separate contact derivative contains **253,078 indexed vertices**, **475,212
source triangles** and **32 groups** in bounded station/craft crops. Shared source
classification and rest corrections keep contact and visible geometry aligned.
Integer-micrometre rounding has at most 0.5 micrometre coordinate error.

Hardware qualification checks 41 samples per full mechanism channel and 81
pipe/closure samples, with exact named hinge, bearing, guide, gasket, retention
and lock contact pairs. Lock contact with rotating parts is allowed only while
aligned; the freely turning interval has no such crossing. Unknown contact pairs
fail. Conservative full box/triangle checks also detect contained faces; surface
intersection alone does not establish solid separation.

D1's existing equalization pipe is rerouted with its endpoints and 18 mm radius
preserved. The deck and mounting flange receive 40 mm service bores, leaving
2 mm nominal radial clearance. The replacement models have recorded source/
geometry identities. No sealing collar or pressure/service rating is certified.
Seventeen independently bound original D1 controls preserve the authored deck,
gate, isolation leaf, bolt, gasket and capture-carriage closure sequence.

The affected supported station corridor passes the full standing swept-volume
check and 121×5 actual triangle floor probes. These exploratory probes use a
centre and four 0.18 m axis offsets; the current C++ walker uses a centre and
four 0.32 m corner offsets. Runtime admission must recheck that exact support
policy against corrected contact geometry. The actual D1 shaft remains open;
no floor is added beneath it. A compact 0.60×0.40×1.95 m reservation clears the
4.357 m pure shaft descent, and the bottom standing transition and open inner
aperture have bounded volume separation. These geometric checks do not supply
an articulated climb or a supported walk into the cabin.

**Complete supported cabin traversal remains unqualified.** The existing inner
threshold lies **77.5 mm below** the nominal floor over craft back-axis
**3.6305–3.6644 m**. It fails the current five-probe support policy with 30 mm
vertical reach; 61×5 cabin-route probes retain additional missing-support
observations. Hardware qualification can pass while `floor_support_complete`
remains false: its scope is the bounded mechanism and geometric checks. The
qualification deliberately retains this failed floor evidence. Choosing a
camera, opening a door or passing the volume-clearance check does not create
support or move the saved actor.

The next composition needs actual actor entry/ladder states, a qualified floor/
threshold transition, seat reach and occupant state, interlocks and authoritative
C++ actions/save changes. This package establishes a bounded read-only hardware
preview and preserves the evidence needed for that work.

## Recorded local validation

The producer passes all 69 source pose comparisons with maximum absolute
matrix-component error `4.7684e-7`. An independent station measurement checks
all 17 local bindings at 39 union-knot and midpoint progress values against the
authored closure source; maximum error is `9.5367e-7`. The station's LINEAR
local channels retain their pivot, while craft preview accepts only its exact
21 recorded knots after the world-delta interpolation drift was identified.
These observations establish the bounded source/preview relationship; they do
not qualify a continuous craft animation or an actor route.

Both real GLBs pass offline attribute/index audits and exact flat node/source
bindings: 47 craft meshes with 13 moving identities, and three station
replacement meshes. Synthetic real-buffer tests reject nonfinite state, index
and dimension errors, nested/duplicate/unbound nodes, changed source extras,
nonidentity transforms and unsupported animation/skin/camera authority.
Operating input regressions refuse invalid requests without pose or source
mutation, and the affected exact-knot Godot checks pass.

All 35 CTest contracts and all 42 native contracts pass with GCC and Clang.
After the final malformed-provenance guard and preview-overlay cleanup, the
operating contract also passes separately with both bridges against final
package SHA-256
`9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957`.
The six approved model/metadata identities remain unchanged throughout those
checks. Twelve package test methods cover atomic installation and focused
schema, source, evidence, path and buffer refusals.

Twenty software-rendered inspection images were reviewed: ten hardware views
with each compiler bridge. Corresponding image hashes and mechanism transforms
match exactly; the authoritative world tick stays zero and its checksum remains
`3550752582179999592`. The final inside-airlock roof camera exposes the sealed
lids and open shaft. Generated port-label/ring helpers are removed only from
this explicit preview; all imported station nodes and authoritative ports are
preserved. The initial exterior roof view was obscured by the attached sleeve
and helper overlays, so that trial was retained rather than accepted as visible
roof evidence. These software captures establish presentation correspondence,
not target-hardware performance or player traversal. The player journey remains
open.
