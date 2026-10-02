# Corrected closed-rest restraint foundation

This is the bounded source derivative registered in
[#366](https://github.com/gobha-me/apsis-drift/issues/366). It corrects the
closed starting assembly before further harness-opening work. It preserves the
original Craft09 master, operating packages and
[unchanged closed-rest export](WAYFARER_RESTRAINT_EXPORT.md).

The derivative is a separate static diagnostic snapshot. It grants no runtime
replacement, opening motion, occupied closure, service function, strength,
supported seating or boarding route. Ordinary New Game remains unchanged.

## Registered changes

Source seat-local axes are +Z up, +Y forward and +X across the seat. Only five
original objects change; two connectors are introduced:

| Object | Declared correction |
| --- | --- |
| Both shoulder ribbons | First post-terminal row Z=.93 → 1.00 m; retained root terminals |
| Shoulder port free-end row | Y=.194 m |
| Lap port free-end row | Y=.198 m |
| Lap starboard free-end row | Y=.202 m |
| Shoulder starboard free-end row | Y=.206 m |
| Service manifold | Source-local displacement (+.08, 0, +.14) m |
| Two shoulder connectors | 52 mm wide, 2 mm high; actual anchor-front to retained ribbon terminal |

Ribbon widths, thickness, materials and topology remain fixed. Free-end X/Z
coordinates remain fixed. The manifold retains its mesh, material and parent.
The seat, anchors, pads, welts, armrests, buckle, release and crotch strap remain
unchanged. Changed rest-segment lengths are disclosed; this derivative does not
inherit the length-preservation constraint of earlier failed opening studies.

The frozen source study measured these centre-path lengths before export:

| Ribbon | Original metres | Corrected metres |
| --- | ---: | ---: |
| Shoulder port | .904540161 | .962731554 |
| Shoulder starboard | .904540161 | .966751569 |
| Lap port | .522302440 | .521773862 |
| Lap starboard | .522302440 | .522845225 |

Nominal edit fields are instructions, not exact output coordinates. Blender
stores and composes rounded transforms. Receipts record actual fields,
evaluated matrices and emitted coordinates. Proofs use those coordinates
without a penetration or quantization tolerance.

## Snapshot and identity boundaries

The fixed operating tuple is `[1,1,1,0]`. Geometry is baked once into metres,
+Y up and -Z forward, with eleven flat identity render groups. There are 66
members: 64 originals and two new connectors. Seven restraint components, the
moved manifold and two connectors are separate groups; the residual seat group
retains 56 originals.

Three references serve distinct checks:

1. The published original seat node and a fresh original-rest replay must have
   the same complete triangle-attribute/material multiset.
2. A fresh original snapshot at the fixed tuple establishes a same-pose
   baseline. All 59 untouched original members must preserve their emitted
   positions, winding, normals, UVs and complete material descriptions exactly.
3. The corrected snapshot supplies matching visible/contact coordinates and
   complete source-object/prepared-face attribution. Changed ribbon normals
   are expected; original materials and UV assignments remain verified.

No comparison equates a rest mesh with a posed mesh. This diagnostic namespace
does not establish equivalence to the authoritative C++ contact catalog or
replace its rest-to-pose mapping. Identical face occurrences retain stable
source ordering; attribution does not invent a unique physical contact identity.

## Finite static assembly semantics

Each connector attaches only at its actual finite, opposed anchor-front and
ribbon-terminal face patches. Exact coplanarity and positive patch area are
required. The frozen emitted prototype has anchor patches of
104.004951/104.004713 mm² and ribbon patches of 44.996696 mm² each. These are
static geometric declarations, without fastening, strength, pressure, latch or
load qualification.

Mutual webbing engagement is permitted only inside the actual closed buckle at
this fixed state. The proof decomposes five thick ribbons into 14 closed panel
cells, checks that their oriented boundaries sum to the complete meshes and
checks all 76 cross-ribbon cell pairs. Convex hulls conservatively enclose those
finite cells. Every vertex of each nonempty hull intersection must lie strictly
within the inward kernel of every actual buckle face plane. Closed oriented
buckle topology and exact interior ray controls are checked separately. The
frozen emitted prototype has two nonempty overlap enclosures and a conservative
boundary gap of at least 3.515028825 mm.

These proofs consume decoded emitted coordinates. No whole-object pair
exemption, removed collision face or generic filled-solid interpretation
follows. Pressure shells remain surface boundaries around cabin air. All
source surfaces remain actor obstacles until a separate contact policy
qualifies their use.

## Production and remaining work

From the engine checkout, supply your retained immutable Craft09 master and a
new destination. Blender 5.2.2 is the qualified producer runtime:

```sh
blender --background --factory-startup --disable-autoexec --python-exit-code 1 \
  --python tools/export_wayfarer_corrected_rest.py -- \
  --source /path/to/hopper-craft-09.blend \
  --output-dir build-native/wayfarer-corrected-rest-01

python3 test/wayfarer_corrected_rest_checks_test.py
python3 test/wayfarer_corrected_rest_package_test.py
```

The tests use independent small fixtures and Python's standard library; they
do not require Blender or the authoring master. The exporter additionally
checks the retained source and published operating package. These tools do not
prepare a gameplay asset replacement. Atomic installation requires Linux's
no-replace directory rename; unsupported hosts refuse before source loading.

The producer must refuse changed source/reference bytes, invalid numeric or
buffer data, incomplete or duplicate attribution, altered untouched geometry,
failed finite proofs and missing inherited license bytes. It stages output and
installs into a new destination atomically after verification. It never saves
the source master. Provenance and file hashes accompany the model, contact,
attribution, proofs and assembly declarations.

The frozen prototype produces 15,940 triangles in an eleven-node, 781,536-byte
GLB. Two fresh Blender processes produced identical model and evidence bytes.
That establishes export reproducibility, without GPU or owner visual
acceptance. Final production evidence belongs to the exact producer revision.

Production replay with Blender **5.2.2 LTS** passed the independent package
validator twice. All 19 files from two fresh processes match byte-for-byte.
The final GLB has the same 15,940 faces in 781,612 bytes; its additional
diagnostic metadata distinguishes it from the earlier prototype container.
The model SHA-256 is
`8fa95658396630c8344386a6c06e5cc25fc8e5f470a8eb4ae560622d372c5999`;
the package manifest SHA-256 is
`b221e25d0c3c9394dee4ec3c4ca82cc3ce41e732cec1dd2631c813581c2024af`.
All 1,746 covered source signatures were restored. Their covered fields are
listed in each snapshot; this is separate from the unchanged source-file hash.
Thirty portable integrity/geometry tests and fifteen publication tests pass.
Six actual-package tamper controls also refuse, including rehashed attribution,
contact coordinates and an untouched object's emitted UV change. Existing
destination refusal preserves every package byte.

A separately registered opening mechanism still needs attachment continuity
and full source clearance. The [body diagnostic](ORIGIN_BOARDING_BODY.md) and
[lower-transfer investigation](LOWER_TRANSFER_FEASIBILITY.md) retain separate
self-model, support and route requirements. This foundation does not close
#362, #361, #352, #291 or #245.
