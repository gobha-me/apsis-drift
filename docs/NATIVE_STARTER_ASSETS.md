# Native starter asset delivery

2026-09-30, #333. The selected Wayfarer and station-owned reference exports are
in `assets/native/freedom-starter-01`. This package preserves their **exact GLB
bytes and full geometry**, with a closed hash roster, archived source lineage,
license/attribution records and passing authoring/import receipts. It does not
change spacecraft physics or implement station walking, release or flight.
Ordinary play still needs the separate presentation and transition consumers.

## Prepare and verify

Python 3 with its standard `lzma` module is required. Run from the repository:

```sh
python3 tools/prepare_native_assets.py \
  --package assets/native/freedom-starter-01 --verify-only
python3 tools/prepare_native_assets.py \
  --package assets/native/freedom-starter-01 \
  --output build-native/prepared-starter
```

Preparation uses an ignored build directory. It checks the complete package
before decoding, verifies decoded sizes/hashes and GLB headers, checks staged
metadata and current input identity, then publishes the completed directory.
Reusing an unchanged prepared directory verifies its model/metadata bytes.
An output belonging to another package, containing unowned files or changed
bytes refuses replacement. Remove only your disposable generated output when
rebuilding it; preparation never changes a source checkout or asset master.

The command does not import an editor, open a viewer, generate assets or execute
a game. No authoring checkout, provider account, private machine path or external
asset download is required for ordinary preparation.

## Recorded source and packaging

| Model | Decoded bytes | SHA-256 |
| --- | ---: | --- |
| Station | 513,069,840 | `79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80` |
| Wayfarer | 40,586,888 | `12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8` |

The station source is the closed occupied reference. Its exported GLB already
omits visiting spacecraft and authoring-only fixtures; their omission list is
retained in its presentation metadata. The Wayfarer export derives from Craft09.
The [station geometry contract](ORIGIN_STATION_GEOMETRY.md) records their measured
collar coordinates separately from mass/thrust and gameplay capture tolerances.

XZ preset6 compresses these exports losslessly. Ordered 32 MiB chunks avoid
GitHub's single-file limit. Their combined compressed payload is **347,391,384
bytes**; the station has ten chunks and the Wayfarer one. Package1 contains 29
rostered files, including metadata and licenses. The manifest SHA-256 is
`6ab1085d30a376fc3620d3821027be31828b98dc2f7c015f9a65b4fc29094fe4`.
No mesh reduction, image alteration, resizing or paid generation occurred.

`metadata/authoring-lineage.json` retains the 78 recorded source/derivative
ancestors and the source registry digest. Historical source filenames in that
archive are evidence pointers, not missing runtime dependencies or active old
ship designs. Private authoring paths are anonymized; any signed source-output
URL is represented by its digest. Archived license grants retain their text;
their BSD link points to the adjacent packaged `LICENSE.md`. The package keeps
the source hashes of those license documents and records that link adjustment.

For offline reproduction from an authoring checkout containing the selected
exports, use an explicit unused destination and disposable compression cache:

```sh
python3 tools/package_native_starter.py \
  --authoring-root /path/to/authoring-checkout \
  --destination build-native/repacked-starter \
  --cache build-native/asset-package-cache
```

That bounded builder verifies the selected model/source receipts and recorded
runtime/redistribution licenses, preserves masters, and validates decoded cached
compression before publishing its new package. It is not a generic asset catalog.

## Qualification and limits

Eight real filesystem tests cover exact install/reuse, existing-output protection,
path traversal, symlink inputs/outputs/parents, duplicate/unrostered/missing
files, unknown licenses, failed receipts, byte budgets, malformed/nonfinite JSON,
truncated/trailing compressed data, excessive expansion, decoded mismatch and
invalid GLB headers. Refusals leave no installed partial output or staging tree.

The native runner's `native_asset_import` contract prepares the actual package
in isolated output and fingerprints its preparer/package receipt. Before importing
real bytes it tests short buffers, invalid indices/counts, nonfinite vertices/UVs
and short normal arrays. Godot checks both known GLB hashes, import success,
finite transforms/bounds/attributes, index limits and selected geometry totals:

- Wayfarer: 34 meshes, 119 surfaces, 728,738 triangles.
- Station: 5,009 meshes, 5,056 surfaces, 13,687,924 triangles and 9,510,065 vertices.

Run with a prebuilt matching bridge/exporter:

```sh
python3 tools/test_godot_native.py --godot "$GODOT_BIN" \
  --build-dir build-native --test native_asset_import
```

Hosted CI includes package refusal tests and actual Godot asset import. Hash and
headless import evidence do not establish GPU cost, appearance, character fit,
runtime collisions/interlocks or a playable station-to-flight journey. Those
remain separate native integration checks under #219/#220/#245.

The optional Game Development Studio vendoring CLI was unavailable in this
environment. No claim is made that its canonical admission command ran; this
delivery uses the repository's source/license/hash/import evidence.
