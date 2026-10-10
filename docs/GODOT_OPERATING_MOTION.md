# C++ source-local operating motion

Issue [#348](https://github.com/gobha-me/apsis-drift/issues/348) adds a bounded,
read-only Wayfarer/D1 transform evaluator. C++ reconstructs the selected scalar
curves, local XYZ rotations and complete source ancestry. Godot consumes those
matrices in a separately initialized inspection view. It does not interpolate
baked world deltas or run another mechanism clock.

This does not change ordinary New Game, player actions or save generation.
There is no actor climb, occupied seat, pressure transition, interlock, new
geometry or supported cabin traversal. Source transform agreement is distinct
from continuous clearance or simultaneous-mechanism safety. The existing
[operating assets](WAYFARER_OPERATING_ASSETS.md) remain byte-identical, including
their bounded corrections and the unresolved inner threshold seam.

## Recipe, sources and preparation

`assets/native/operating-motion-01/operating-motion-01.json` is a separately
versioned, **59,883-byte** purpose-specific recipe. It contains 15 craft nodes,
19 D1 nodes,13/17 stable output groups, 12 craft scalar tracks and 51 D1 tracks
(including source constant components). Every retained parent inverse is
identity, scale is unit and rotation mode is XYZ. Unexpected conventions are
refused rather than generalized into an arbitrary rig engine.

The package preserves all 17 original proposal/fixture/license files unchanged:
71 craft source poses, eight combined craft tuples with actual raw matrices,
41 D1 source poses, their scalar source data and provenance, and the inherited
five-document license roster in each original source closure. These matrices
originate in the corrected Blender objects; they are not synthesized by the new
C++ evaluator. Original source packages and operating-02 remain unchanged.

Repository-owned `tools/operating_motion_identity.py` independently pins the
recipe and archived evidence. Matching edited files to a newly written receipt
cannot authorize changed bytes. Preparation verifies the exact 19-file roster,
size/depth/finite bounds, source relationships, preserved operating dependency
hashes and producer tools before staging. Verified byte snapshots are copied
without reopening source files. Linux no-replace rename refuses an existing
destination even when another writer creates an empty directory during install.
There are no compressed payloads or new mesh imports in this sidecar.

```sh
python3 tools/prepare_operating_motion.py \
    --package assets/native/operating-motion-01 \
    --output "$NEW_MOTION_ASSET_DIR"
```

The output must be new. The prepared directory retains the recipe, source
evidence and package receipt, plus `prepared.json`. When the preparer is copied
into an isolated native runner, supply `--repository "$SOURCE_REPO"`: archived
dependency paths refer to the qualified source repository, not the copied
tool's temporary parent. Missing, linked or changed dependency roots refuse.
The runner supplies this explicitly. No host-specific checkout or display
identifier is required.

To reproduce the selected package from unchanged Station handoff directories,
use `tools/package_operating_motion.py --source "$SOURCE_SHIPS_DIR" --output
"$NEW_PACKAGE_DIR"`. That source directory contains `wayfarer-motion-proposal`
and `operating-motion-fixtures-01`. The producer preserves their raw bytes and
generates provenance; do not edit a receipt by hand after changing tools.

## Native boundary and matrix spaces

The bridge exposes `initialize_operating_motion(recipe_json)` and
`get_operating_motion_pose(roof_transfer, inner_door, seat_boarding,
station_closure)`. Initialization validates the complete source-specific recipe;
a refused replacement preserves the previously admitted recipe. Queries accept
four finite scalars in [0,1] and return three complete maps of binary64 packed
12-component transforms, ordered basis X/Y/Z followed by translation. Evaluation
is pure: no elapsed time, tick, actor motion or voyage mutation.

| Bridge map | Outputs | Application |
| --- | ---: | --- |
| `craft_world_deltas` | 13 | Apply each complete world/rest delta once to its existing flat, world-baked craft sibling mesh. |
| `station_node_local` | 17 | Assign each local transform below its unchanged imported D1 parent. |
| `station_contact_deltas` | 17 | Canonical station contact-space deltas, including the `(-.97,0,.978)` offset conjugation. These are validated and retained; they are not assigned as node-local render transforms. |

Craft computation is `C * world * inverse(rest_world) * inverse(C)`. D1 native
presentation is `C * local * inverse(C)`, whereas canonical contacts require
`K * world * inverse(reference_rest_world) * inverse(K)` with
`K = translation(-.97,0,.978) * C`. C maps Blender(x,y,z) to Godot(x,z,-y).
Each parent is composed once in parent-first order. Existing source corrections
are already baked into the selected rest geometry and recipe, so they are not
applied again.

[operating_motion_presentation.gd](../godot/scripts/ships/operating_motion_presentation.gd)
reuses the existing operating-02 source/model bindings without rewriting its
exact-knot consumer. `initialize_motion(owner,native_assets,operating_assets,
motion_assets)` takes three absolute prepared directories. `set_progress`
accepts a four-element numeric array or PackedFloat64Array in the bridge order.
It requests C++ output and qualifies all 47 matrices before assigning any of the
30 visible nodes. Wrong buffers, nonfinite/out-of-range components, nonrigid or
reflected bases, missing/extra groups and altered source hierarchy/metadata
refuse atomically. Invalid requests retain the last displayed pose/progress.
This view has no automatic processing clock or gameplay command.

## Native proof and reproduction

The headless C++ contract compares 2,421 matrices against the archived source
fixtures. The former whole-world interpolation is retained as a negative control:
at ladder progress 0.425 it misses the source by about 48.75 mm. Source-local
evaluation keeps every matrix-component comparison below `1.21e-6`
(metres for translations, dimensionless for basis components). Both
compiler builds pass all 36 CTests. The isolated native runner also passes all
43 contracts, including saved start, walking, flight, port lifecycle, audio
shutdown and the new motion consumer. Existing operating-02/starter assets,
walker implementation and save formats 16–20 remain unchanged.

The selected proof first checks invalid dimensions, types, nonfinite values,
mesh-index boundaries and native-load limits. It then compares every craft
output at all 71 independent single-channel samples and eight combined tuples,
and both D1 spaces at all 41 independent samples. Application checks require
exact single-conversion Godot transforms on every render node. Repeated
rest→pose→rest, failed recipe replacement, late malformed contact matrices,
missing D1 bindings and nested craft groups retain state on refusal.

Both GCC and Clang headless runs pass **6,212 checks with 0 failures**. The
maximum difference from the independently archived source matrix components is
`1.206396e-6`, below the declared `5e-6` tolerance. The actor/body/world snapshots
and format 20 Save As bytes remain unchanged at tick 0. Headless success qualifies
contracts and application, not rendered appearance or hardware performance.

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
    --build-dir "$QUALIFIED_GCC_BUILD" --test operating_motion
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
    --build-dir "$QUALIFIED_CLANG_BUILD" --test operating_motion
python3 test/operating_motion_package_test.py
```

The native runner stages an isolated project and all three prepared directories.
Add `--keep-work` to retain a successful stage for the optional capture below.
Its test uses exactly those three absolute arguments. For optional real-display
proof, choose a new empty capture directory and invoke its staged project:

```sh
"$GODOT_BIN" --path "$STAGED_PROJECT" --rendering-method gl_compatibility \
    --audio-driver Dummy --resolution 1280x720 \
    --script res://tests/operating_motion_test.gd -- \
    "$NATIVE_ASSETS" "$OPERATING_ASSETS" "$MOTION_ASSETS" "$EMPTY_CAPTURE_DIR"
```

Use a real display or isolated Xvfb; keep each concurrent capture's project, cache and display independent.
The optional branch follows all refusal/source checks, then records seven
off-knot/composed views, exact C++ matrices, camera poses, source/script/bridge
hashes and unchanged Save As. It contains no GDScript mechanism formulas.
Package tests also cover rehashed corrupt recipes/oracles, forged tool metadata,
symlink/FIFO/path/roster refusals, cached-copy races, concurrent empty output and
preparation using copied tools with an explicit dependency repository.

## Selected identities

| Item | SHA-256 |
| --- | --- |
| Runtime recipe | `afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298` |
| Package manifest | `dd49617f36ac6e6832f4b0f5236c14f74730137f02082c2e8db126492067d61b` |
| Producer provenance | `0a86b3b6f397f71f059408ed71bc06569a00f0d1c5a05a3cb059ce225ba54730` |
| Craft source poses | `9e0685b20363d6d93eefc8364faf764775cb29577a188269777833adae009d6f` |
| Combined craft poses | `485f9fa8d9030abd53e1db8a53dff830436e3133c726c7af1ddfc3daa37cbeab` |
| D1 source recipe/poses | `c33dfdce9613fe978a96ebeae6346af2dc60d85dfd48c3d99ec9633fe311d6db` |

The optional Compatibility/Mesa llvmpipe proofs pass **6,228 checks with 0
failures** on both compiler-specific bridges. All seven 1280×720 PNGs, all C++
matrix/camera snapshots and final Save As bytes are identical between GCC and
Clang. Independent inspection confirms coherent roof/ladder, seat
withdrawal/turn/restoration, composed craft and intermediate D1 gate views.
World tick 0/checksum `3550752582179999592` remains unchanged. These are actual
software-raster captures, not headless screenshots or a hardware-performance
claim.

| Recorded item | SHA-256 |
| --- | --- |
| Presentation script | `9540b88ec49d113a5b25fb661a5ff12c0f7cd545fa7ecdb5f6b87f65f03d98db` |
| Native proof script | `2819fe22f17cd733df97ef5d0939e442c6f1add38af2cc500cfce983bc2c2400` |
| GCC capture receipt | `b9fef88d07d67432dde605ef5392638eed5879c84185b723e186d533c8185454` |
| Clang capture receipt | `581fdf7f5d8630c01b82add94e199d17ccdf36cf6ba12fd59898be546d513002` |
| Unchanged C++ save | `69f75674909fbac75ed7cd719a62bbf2f972309e73ca595f86d59933af2b6ee1` |

Local ignored proof outputs are `build-native/motion-render-final/captures/`
and `build-native-clang/motion-render-final/captures/`. Each contains the seven
PNGs, `capture.json` and `unchanged-journey.json`; render/run logs remain beside
them. Compiler-specific headless reports remain under each build's
`motion-native-final/`. The recipe/model/source receipts and all image hashes
are retained in the capture receipt. Transform evidence does not admit a
continuous player route or remove later support, reach, seat and interlock work.
