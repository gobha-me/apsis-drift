#!/usr/bin/env python3
"""Package the two selected starter exports from an authoring checkout.

Offline source-bound reproduction only; never changes authoring inputs. Ordinary
play consumes the published package using prepare_native_assets.py instead.
"""
import argparse
import copy
import hashlib
import json
import lzma
from pathlib import Path
import shutil
import tempfile

import prepare_native_assets as assets

SELECTED = {
    "station": ("visual/station-reference", "station-reference.glb", "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
                "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6", "station-reference-godot-checks.json"),
    "wayfarer": ("visual/hopper-wayfarer-01", "hopper-wayfarer-01.glb", "12db339e004fcfa6586f745597108a69009b6b8ebc088b5e4ff373dece656be8",
                "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677", "hopper-wayfarer-01-export-checks.json"),
}
METADATA = ["station-reference-presentation.json", "station-reference.json", "station-reference-godot-checks.json",
            "station-reference-godot-walk-checks.json", "hopper-wayfarer-01.json", "hopper-wayfarer-01-export-checks.json",
            "hopper-wayfarer-01-godot-checks.json"]


def archived(value):
    if isinstance(value, dict):
        return {key: archived(item) for key, item in value.items()}
    if isinstance(value, list):
        return [archived(item) for item in value]
    if isinstance(value, str) and value.startswith("/"):
        return "[authoring path]/" + Path(value).name
    return value


def build(source, destination, cache):
    source, destination, cache = map(assets.no_symlinks, (source, destination, cache))
    assets.require(not destination.exists(), "package destination already exists")
    registry = assets.load_json(source / "assets/provenance.json")
    by_id = {record["id"]: record for record in registry["assets"]}
    lineage = {}

    def collect(ident):
        if ident in lineage:
            return
        record = copy.deepcopy(by_id[ident])
        generated = record.get("generated")
        if generated and isinstance(generated.get("source_output"), str) and generated["source_output"].startswith("https://"):
            generated["source_output_url_sha256"] = hashlib.sha256(generated.pop("source_output").encode()).hexdigest()
        lineage[ident] = archived(record)
        for parent in record.get("derived", {}).get("parents", record.get("code_authored", {}).get("derived_inputs", [])):
            collect(parent)

    for ident, (registry_id, filename, expected, _, _) in SELECTED.items():
        license_record = by_id[registry_id]["license"]
        assets.require(license_record["expression"] == assets.MODELS[ident][1] and
                       "runtime" in license_record["permitted_uses"] and license_record["redistribution"] == "allowed", "source license does not permit this delivery")
        assets.require(assets.sha(assets.no_symlinks(source / "assets/visual" / filename)) == expected, "selected model changed")
        collect(registry_id)
    destination.parent.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".starter-package-", dir=destination.parent))
    manifest = {"schema": assets.SCHEMA, "package_id": "freedom-starter-01", "files": [], "models": [], "metadata": []}

    def record(path):
        manifest["files"].append({"path": path.relative_to(staging).as_posix(), "bytes": path.stat().st_size, "sha256": assets.sha(path)})

    try:
        (staging / "payloads").mkdir()
        (staging / "metadata").mkdir()
        (staging / "licenses").mkdir()
        for ident, (_, filename, expected, source_hash, receipt) in SELECTED.items():
            original = assets.no_symlinks(source / "assets/visual" / filename)
            compressed = assets.no_symlinks(cache / (expected + ".xz"))
            if not compressed.exists():
                with original.open("rb") as raw, lzma.open(compressed, "wb", preset=6) as packed:
                    shutil.copyfileobj(raw, packed, assets.BLOCK)
            chunks = []
            with compressed.open("rb") as packed:
                n = 0
                while data := packed.read(assets.MAX_CHUNK):
                    path = staging / "payloads" / f"{ident}-{n:02}.xz-part"
                    path.write_bytes(data)
                    record(path)
                    chunks.append(path.relative_to(staging).as_posix())
                    n += 1
            manifest["models"].append({"id": ident, "output": filename, "bytes": original.stat().st_size, "sha256": expected,
                "source_sha256": source_hash, "license": assets.MODELS[ident][1], "chunks": chunks, "receipt": "metadata/" + receipt})
        for name in METADATA:
            original = source / "assets/visual" / name
            data = archived(assets.load_json(original))
            path = staging / "metadata" / name
            path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")
            record(path)
            manifest["metadata"].append({"path": "metadata/" + name, "output": name})
        licenses = []
        for original in sorted((source / "docs/licenses").glob("*.md")):
            path = staging / "licenses" / original.name
            # The archived grant is unchanged; bind its BSD link locally.
            path.write_text(original.read_text().replace("../../LICENSE.md", "LICENSE.md"))
            record(path)
            licenses.append({"name": original.name, "authoring_sha256": assets.sha(original), "packaging_edit": "BSD link points to adjacent LICENSE.md"})
        shutil.copyfile(source / "LICENSE.md", staging / "licenses/LICENSE.md")
        record(staging / "licenses/LICENSE.md")
        path = staging / "metadata/authoring-lineage.json"
        path.write_text(json.dumps({"schema": "apsis.archived-authoring-lineage/1", "scope": "Historical source records; original file paths are evidence, not runtime dependencies",
            "registry_sha256": assets.sha(source / "assets/provenance.json"), "compression": "XZ preset6; split into ordered 32 MiB chunks; exact decoded GLB bytes",
            "licenses": licenses, "assets": [lineage[key] for key in sorted(lineage)]}, indent=2, allow_nan=False) + "\n")
        record(path)
        manifest["metadata"].append({"path": "metadata/authoring-lineage.json", "output": "authoring-lineage.json"})
        manifest["files"].sort(key=lambda record: record["path"])
        (staging / "package.json").write_text(json.dumps(manifest, indent=2) + "\n")
        assets.verify(staging)
        # Qualify cached compression and actual decoded identity before publication.
        with tempfile.TemporaryDirectory(prefix="starter-check-", dir=cache) as check:
            for model in manifest["models"]:
                assets.unpack_model(staging, model, Path(check) / model["output"])
        assets.require(not destination.exists(), "destination appeared during packaging")
        staging.rename(destination)
        print(json.dumps({"package": str(destination), "models": len(manifest["models"]), "roster_files": len(manifest["files"]), "package_sha256": assets.sha(destination / "package.json")}))
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authoring-root", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    try:
        build(args.authoring_root, args.destination, args.cache)
    except (OSError, ValueError, KeyError, EOFError, lzma.LZMAError) as error:
        parser.exit(1, "Starter packaging refused: " + str(error) + "\n")


if __name__ == "__main__":
    main()
